/* rd_patches.c -- runtime patches of libunity.so + the background service thread.
 *
 * Every patch site is found by instruction pattern in the loaded image (rd_patchsites.c,
 * analysis/research/10-patch-sites.md) and is skipped -- never partially written -- if the
 * pattern is missing or ambiguous. so_patch_code(): reference so-loader (MIT).
 *
 * (The Unity 2020.3 patches inherited from badpiggies_nx -- Choreographer::Get free-run,
 * TimeManager clock detour, FrameTimeTracker / physmem / big.LITTLE stubs, forced OpenSL
 * output -- were keyed to Bad Piggies offsets, never applied to Real Drift and were removed.) */
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <switch.h>
#include "so_util.h"
#include "rd_log.h"
#include "util.h"          /* install_bionic_tls, BIONIC_TLS_SIZE, cpu_boost */
#include "config.h"
#include "rd_platform.h"
#include "rd_patches.h"
#include "rd_perf.h"
#include "rd_a64.h"
#include "rd_modimage.h"
#include "rd_patchsites.h"
#include "rd_game.h"
#include "rd_il2cppcopy.h"

extern volatile int jni_quit_requested;   /* jni_fake.c */
extern so_module unity_mod;               /* main.c */

/* ---- UnityChoreographer SDK gate (Unity 2018.4 frame-2 hang) ----------------
 * Rewrites the `b.lt` after `cmp w0,#16` (Android API level) into an unconditional `b` to
 * the same target, so VSYNC via the Java Choreographer is never enabled: no Looper thread
 * exists under the fake JNI, and the engine would wait for it forever on frame 2. */
static int install_choreographer_sdk_gate(uintptr_t ub) {
  rd_image im;
  uint64_t gate = 0, skip = 0;
  int r = rd_image_from_module(&unity_mod, &im) == 0 ? rd_find_choreo_gate(&im, &gate, &skip)
                                                     : RD_SITE_NOT_FOUND;
  if (r != RD_SITE_OK) {
    debugPrintf("[patch] Choreographer SDK gate: pattern not found (result %d)\n", r);
    return 0;
  }
  uint32_t b = rd_a64_b(gate + 4, skip);              /* b.lt -> b, same target */
  so_patch_code((void *)(ub + gate + 4), &b, sizeof b);
  debugPrintf("[patch] Choreographer SDK gate -> always skip @libunity+0x%llx (%08x)\n",
              (unsigned long long)(gate + 4), b);
  return 1;
}

/* ---- Android blit type "Never" ------------------------------------------------
 * The game ships PlayerSettings.androidBlitType = Always: Unity renders every frame into its own
 * back-buffer FBO and then copies that to the window (~0.45 ms GPU at 720p, every scene). With
 * Never it renders straight into the window. GetPlayerSettings() is replaced by an absolute jump
 * to get_settings_never, which returns the same object with the field set to Never -- so every
 * reader, whenever it reads, sees Never (analysis/research/16-graphics-enhancements.md). */
#define BLIT_NEVER 1   /* Unity's AndroidBlitType: 0 Always, 1 Never, 2 Auto */
static void *(*g_get_manager)(int context);
static uint32_t g_blit_field;
static uint32_t g_splash_field;   /* fast_boot: m_ShowUnitySplashScreen, 0 = left alone */

/* GetPlayerSettings() with blit type Never and, with fast_boot, the Unity splash off. The
 * first call comes in frame 0, before the splash starts (frame 1). */
static void *get_settings_never(void) {
  void *ps = g_get_manager(0);
  int32_t *blit = ps ? (int32_t *)((char *)ps + g_blit_field) : NULL;
  if (blit && *blit != BLIT_NEVER) {
    debugPrintf("[patch] androidBlitType %d -> %d (render straight to the window)\n", *blit, BLIT_NEVER);
    *blit = BLIT_NEVER;
  }
  uint8_t *splash = ps && g_splash_field ? (uint8_t *)ps + g_splash_field : NULL;
  if (splash && *splash == 1) {     /* a bool: anything else means the offset is wrong */
    debugPrintf("[patch] m_ShowUnitySplashScreen 1 -> 0 (no blank splash)\n");
    *splash = 0;
  }
  return ps;
}

static int install_blit_type_never(uintptr_t ub) {
  rd_image im;
  uint64_t gs = 0, gm = 0;
  int r = rd_image_from_module(&unity_mod, &im) == 0 ? rd_find_blit_type(&im, &gs, &gm, &g_blit_field)
                                                     : RD_SITE_NOT_FOUND;
  if (r != RD_SITE_OK) {
    debugPrintf("[patch] blit type: pattern not found (result %d)\n", r);
    return 0;
  }
  g_get_manager = (void *(*)(int))(ub + gm);
  uint32_t jump[RD_A64_ABS_JUMP_WORDS];
  rd_a64_abs_jump(jump, (uint64_t)(uintptr_t)&get_settings_never);
  if (so_patch_code((void *)(ub + gs), jump, sizeof jump)) return 0;
  debugPrintf("[patch] GetPlayerSettings @libunity+0x%llx -> blit type Never (field +0x%x)\n",
              (unsigned long long)gs, g_blit_field);
  return 1;
}

/* ---- fast_boot: Unity splash off ---------------------------------------------
 * The game ships a Unity splash with no logo for 2 s: a blank screen while the first scene
 * loads. Only the field is found here; get_settings_never clears it. */
static int install_splash_off(uintptr_t ub) {
  (void)ub;
  if (!config.fast_boot) return 0;
  rd_image im;
  int r = rd_image_from_module(&unity_mod, &im) == 0 ? rd_find_splash_flag(&im, &g_splash_field)
                                                     : RD_SITE_NOT_FOUND;
  if (r != RD_SITE_OK) {
    g_splash_field = 0;
    debugPrintf("[patch] splash flag: pattern not found (result %d)\n", r);
    return 0;
  }
  debugPrintf("[patch] Unity splash off (PlayerSettings +0x%x)\n", g_splash_field);
  return 1;
}

/* ---- fast_boot: il2cpp data copied once (rd_il2cppcopy.h) -------------------------
 * The call to CopyDirectory(source, destination, replace) is redirected through a veneer in
 * the padding after libunity's code (a bl reaches it; it jumps on to il2cpp_copy_once). */
static int (*g_copy_dir)(void *src, void *dst, int replace);

/* The stamp file's contents into buf (n bytes); returns the length, 0 when there is none. */
static size_t read_stamp(const char *path, char *buf, size_t n) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  const size_t k = fread(buf, 1, n, f);
  fclose(f);
  return k;
}

/* CopyDirectory's replacement. Unity's FileSystemEntry starts with its path. */
static int il2cpp_copy_once(void *src, void *dst, int replace) {
  const char *to = dst;
  if (!rd_il2cppcopy_is_data_dir(to)) return g_copy_dir(src, dst, replace);
  char stamp[RD_IL2CPPCOPY_STAMP_MAX], file[RD_IL2CPPCOPY_STAMP_MAX], path[RD_PATH_MAX];
  struct stat st;
  int len = -1;
  if (stat(RD_GAME_HOME "/" RD_BASE_APK_NAME, &st) == 0)
    len = rd_il2cppcopy_stamp(stamp, sizeof stamp, st.st_size, st.st_mtime);
  if (len > 0) {
    snprintf(path, sizeof path, "%s/%s", to, RD_IL2CPP_METADATA);
    const int64_t meta = stat(path, &st) == 0 ? st.st_size : 0;
    snprintf(path, sizeof path, "%s/%s", to, RD_IL2CPP_STAMP_FILE);
    const size_t got = read_stamp(path, file, sizeof file);
    if (rd_il2cppcopy_current(file, got, stamp, meta)) {
      debugPrintf("[patch] il2cpp data in %s is current: copy skipped\n", to);
      return 1;
    }
    unlink(path);                     /* an interrupted copy must not look complete */
  }
  const uint64_t t0 = rd_now_ns();
  const int ok = g_copy_dir(src, dst, replace);
  debugPrintf("[patch] il2cpp data copied to %s in %llu ms (ok %d)\n", to,
              (unsigned long long)((rd_now_ns() - t0) / RD_NS_PER_MS), ok & 1);
  if ((ok & 1) && len > 0) {
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(stamp, 1, (size_t)len, f); fclose(f); }
  }
  return ok;
}

static int install_il2cpp_copy_once(uintptr_t ub) {
  if (!config.fast_boot) return 0;
  rd_image im;
  uint64_t call = 0, copy = 0, veneer = 0;
  int r = rd_image_from_module(&unity_mod, &im) == 0
            ? rd_find_il2cpp_copy(&im, &call, &copy, &veneer) : RD_SITE_NOT_FOUND;
  if (r != RD_SITE_OK) {
    debugPrintf("[patch] il2cpp copy: pattern not found (result %d)\n", r);
    return 0;
  }
  const uint32_t *pad = (const uint32_t *)(ub + veneer);
  for (int i = 0; i < RD_A64_ABS_JUMP_WORDS; i++)
    if (pad[i]) { debugPrintf("[patch] il2cpp copy: padding at +0x%llx in use\n",
                              (unsigned long long)veneer); return 0; }
  const uint32_t bl = rd_a64_bl(call, veneer);
  if (!bl) return 0;
  g_copy_dir = (int (*)(void *, void *, int))(ub + copy);
  uint32_t jump[RD_A64_ABS_JUMP_WORDS];
  rd_a64_abs_jump(jump, (uint64_t)(uintptr_t)&il2cpp_copy_once);
  if (so_patch_code((void *)(ub + veneer), jump, sizeof jump)) return 0;
  if (so_patch_code((void *)(ub + call), &bl, sizeof bl)) return 0;
  debugPrintf("[patch] il2cpp copy @libunity+0x%llx -> copy once (veneer +0x%llx)\n",
              (unsigned long long)call, (unsigned long long)veneer);
  return 1;
}

static const struct { const char *name; int (*install)(uintptr_t unity_base); } ENGINE_PATCHES[] = {
  { "Choreographer SDK gate", install_choreographer_sdk_gate },
  { "Blit type Never", install_blit_type_never },
  { "Unity splash off", install_splash_off },
  { "il2cpp data copied once", install_il2cpp_copy_once },
};

int rd_install_engine_patches(uintptr_t unity_base, int *total) {
  int n = 0;
  const int count = (int)(sizeof ENGINE_PATCHES / sizeof ENGINE_PATCHES[0]);
  for (int i = 0; i < count; i++) {
    int ok = ENGINE_PATCHES[i].install(unity_base);
    debugPrintf("[patch] %-28s %s\n", ENGINE_PATCHES[i].name, ok ? "applied" : "skipped");
    n += ok;
  }
  if (total) *total = count;
  return n;
}

/* ---- background service thread -----------------------------------------------
 * Flushes the buffered debug.log about once a second and switches the CPU boost while the
 * engine is stuck in a long (loading) frame. */
#define CLOCK_POLL_NS        (4 * RD_NS_PER_MS)
#define CLOCK_LOG_FLUSH_NS   RD_NS_PER_S
#define CLOCK_THREAD_STACK   0x8000
#define CLOCK_THREAD_PRIO    0x2C          /* Horizon: lower value = higher priority */
#define CLOCK_THREAD_CPU     -2            /* libnx: the default core */
static Thread g_clock_thr;
/* Background service thread (own bionic TLS): every CLOCK_POLL_NS flushes debug.log about once
 * a second and switches the CPU boost on long loading frames (boost_loading). arg != NULL: the
 * boost is already on since boot. */
static void rd_clock_thread(void *arg) {
  static uint8_t clk_tls[BIONIC_TLS_SIZE] __attribute__((aligned(16)));
  install_bionic_tls(clk_tls);
  uint64_t last_flush = 0;
  rd_boost boost = {0};
  if (arg) rd_boost_start(&boost, rd_now_ns(), RD_BOOST_TAIL_NS);   /* on since boot */
  while (!jni_quit_requested) {
    svcSleepThread(CLOCK_POLL_NS);
    uint64_t now = rd_now_ns();
    if (now - last_flush > CLOCK_LOG_FLUSH_NS) { last_flush = now; debug_log_flush(); }
    if (config.boost_loading) {
      int was = boost.on;
      rd_boost_update(&boost, now, g_rd_in_frame, g_rd_frame_start_ns,
                      RD_BOOST_THRESHOLD_NS, RD_BOOST_TAIL_NS);
      if (boost.on != was) {
        cpu_boost(boost.on);   /* every switch logged: it also lowers the GPU clock */
        debugPrintf("[perf] CPU boost %s\n", boost.on ? "ON (loading)" : "off");
      }
    }
  }
}

void rd_start_clock_thread(int boosted) {
  if (R_SUCCEEDED(threadCreate(&g_clock_thr, rd_clock_thread, boosted ? &g_clock_thr : NULL, NULL,
                               CLOCK_THREAD_STACK, CLOCK_THREAD_PRIO, CLOCK_THREAD_CPU)))
    threadStart(&g_clock_thr);
}
