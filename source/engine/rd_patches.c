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

static const struct { const char *name; int (*install)(uintptr_t unity_base); } ENGINE_PATCHES[] = {
  { "Choreographer SDK gate", install_choreographer_sdk_gate },
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
#define BOOST_LOG_MAX        6             /* log only the first CPU boost switches */
static Thread g_clock_thr;
static void rd_clock_thread(void *arg) {
  (void)arg;
  static uint8_t clk_tls[BIONIC_TLS_SIZE] __attribute__((aligned(16)));
  install_bionic_tls(clk_tls);
  uint64_t last_flush = 0;
  rd_boost boost = {0};
  unsigned boost_logged = 0;
  while (!jni_quit_requested) {
    svcSleepThread(CLOCK_POLL_NS);
    uint64_t now = rd_now_ns();
    if (now - last_flush > CLOCK_LOG_FLUSH_NS) { last_flush = now; debug_log_flush(); }
    if (config.boost_loading) {
      int was = boost.on;
      rd_boost_update(&boost, now, g_rd_in_frame, g_rd_frame_start_ns,
                      RD_BOOST_THRESHOLD_NS, RD_BOOST_TAIL_NS);
      if (boost.on != was) {
        cpu_boost(boost.on);
        if (boost_logged++ < BOOST_LOG_MAX)
          debugPrintf("[perf] CPU boost %s\n", boost.on ? "ON (loading)" : "off");
      }
    }
  }
}

void rd_start_clock_thread(void) {
  if (R_SUCCEEDED(threadCreate(&g_clock_thr, rd_clock_thread, NULL, NULL, CLOCK_THREAD_STACK,
                               CLOCK_THREAD_PRIO, CLOCK_THREAD_CPU)))
    threadStart(&g_clock_thr);
}
