/* main.c -- Real Drift (realdrift_nx) Nintendo Switch loader: entry point and render loop.
 *
 * Target: rd_game.h (Real Drift 5.0.8, Unity 2018.4.14f1 / IL2CPP, arm64-v8a). Loads libmain +
 * libunity + libil2cpp from the game folder, resolves their imports against Switch
 * implementations (imports.c, unity_imports.c, rd_imports.c), and drives the lifecycle the Java
 * UnityPlayer normally runs (JNI_OnLoad -> initJni -> surface -> resume/focus -> render loop).
 * The UnityPlayer natives are recovered BY NAME from libunity's RegisterNatives table
 * (jni_fake.c); engine patches find their sites by instruction pattern (rd_patches.c).
 * Game data is read straight from base.apk and the OBB (unity_jni.c, rd_zip.c).
 *
 * main() is a list of boot steps (boot_* below); the memory layout set up before it runs is in
 * mem_layout.c.
 *
 * Forked from badpiggies_nx by ChanseyIsTheBest (https://github.com/ChanseyIsTheBest/badpiggies_nx,
 * commit 7726be3), from the colorsheep_nx / laytonbmr_nx so-loader lineage. MIT (see LICENSE). */
#include <dirent.h>
#include <SDL2/SDL.h>

#include "config.h"
#include "rd_il2cpp.h"
#include "rd_platform.h"
#include "rd_patches.h"
#include "libc_shim.h"
#include "nx_fonts.h"
#include "util.h"
#include "error.h"
#include "imports.h"
#include "jni_fake.h"
#include "android_native_unity.h"
#include "opensles.h"
#include "unity_entrypoints.h"
#include "unity_input_hook.h"
#include "rd_mods.h"
#include "rd_gpuwait.h"

volatile uint64_t g_rd_frame_start_ns = 0;   /* Read by rd_patches.c clock thread */
volatile int      g_rd_in_frame = 0;

#include "diag.h"
#include "nx_crash_handler.h"
#include "unity_jni.h"
#include "rd_perfstat.h"
#include "rd_gcbridge.h"
#include "mem_layout.h"

so_module main_mod, unity_mod, il2cpp_mod;

/* Strong override of nx_crash_handler.c's weak stub: name addresses that fall inside
 * our loaded .so images (creport can't, since they aren't real modules). */
int crash_resolve_module(uintptr_t addr, char *name_out, size_t name_cap, uintptr_t *base_out) {
  const struct { const char *n; so_module *m; } mods[] = {
    { RD_LIB_MAIN, &main_mod }, { RD_LIB_UNITY, &unity_mod }, { RD_LIB_IL2CPP, &il2cpp_mod },
  };
  for (unsigned i = 0; i < sizeof(mods)/sizeof(*mods); i++) {
    uintptr_t b = (uintptr_t)mods[i].m->load_virtbase;
    if (b && addr >= b && addr < b + mods[i].m->load_size) {
      snprintf(name_out, name_cap, "%s", mods[i].n);
      *base_out = b;
      return 1;
    }
  }
  return 0;
}


/* audio warmup gate for opensles.c (frames since boot) */
static volatile uint32_t g_frame_count = 0;
uint32_t port_frame_count(void) { return g_frame_count; }

/* rd_il2cpp_load lookup: an export of libil2cpp (mod is its so_module), or NULL. */
static void *il2cpp_sym(void *mod, const char *name) {
  return (void *)so_try_find_addr_rx((so_module *)mod, name);
}
/* Logs an il2cpp API function libil2cpp does not export (the hooks needing it stay off). */
static void il2cpp_sym_missing(const char *name) {
  debugPrintf("[boot] WARNING: %s not exported by " RD_LIB_IL2CPP "\n", name);
}

/* Fatal unless the process may map code memory and change its permissions (needs the right
 * launch mode / NPDM). */
static void check_syscalls(void) {
  if (!envIsSyscallHinted(0x77)) fatal_error("svcMapProcessCodeMemory is unavailable.");
  if (!envIsSyscallHinted(0x78)) fatal_error("svcUnmapProcessCodeMemory is unavailable.");
  if (!envIsSyscallHinted(0x73)) fatal_error("svcSetProcessMemoryPermission is unavailable.");
  if (envGetOwnProcessHandle() == INVALID_HANDLE) fatal_error("Own process handle is unavailable.");
}

/* ==========================================================================
 * Game data layout and module loading.
 * ========================================================================== */

/* The game data is read straight from the original files: Unity opens
 * base.apk (getPackageCodePath, rd_apkpath.h) and the OBB (getObbDir()/main.<versionCode>.
 * <package>.obb, getObbDir() == RD_GAME_HOME) as ZIPs, and the fake AssetManager serves
 * assets/ from base.apk (rd_zip.c). Nothing has to be extracted onto the SD card. */
static void check_data(void) {
  const char *files[] = {
    RD_LIB_MAIN, RD_LIB_UNITY, RD_LIB_IL2CPP,
    RD_BASE_APK_NAME,
    RD_GAME_OBB_NAME,
  };
  char path[RD_PATH_MAX];
  struct stat st;
  for (unsigned i = 0; i < sizeof(files)/sizeof(*files); i++) {
    snprintf(path, sizeof path, "%s/%s", RD_GAME_HOME, files[i]);
    if (stat(path, &st) < 0)
      fatal_error("Missing data file:\n%s\nCheck your SD card layout (see README.md).\n"
                  "Run tools/stage_sd.py to assemble it from your own APK and OBB.", files[i]);
  }
}

/* Loads a game library from the game folder into the next part of the .so region and resolves
 * its imports; 0 or -1. */
static int load_module(so_module *mod, const char *name) {
  char path[RD_PATH_MAX];
  snprintf(path, sizeof path, "%s/%s", RD_GAME_HOME, name);
  if (mem_so_load(mod, path) < 0)
    return -1;
  nx_resolve_imports(mod);
  return 0;
}


/* engine entry points (resolved by name from RegisterNatives, post-JNI_OnLoad) */
static fn_initJni  Unity_initJni;
static fn_gfxstate Unity_nativeRecreateGfxState;
static fn_v        Unity_nativeSendSurfaceChanged;
static fn_z        Unity_nativeRender;
static fn_v        Unity_nativeResume;
static fn_vz       Unity_nativeFocusChanged;
static fn_v        Unity_nativeDone;           /* ()V in 2018.4 */

/* Save persistence: commit pending SD writes (prefs.kv) when the app loses focus or exits,
 * besides the periodic flush in the render loop. */
static AppletHookCookie g_applet_cookie;
static void nx_applet_hook(AppletHookType hook, void *param) {
  (void)param;
  if (hook == AppletHookType_OnFocusState || hook == AppletHookType_OnExitRequest)
    nx_sd_flush();
}

/* The engine-facing objects built by the fake JNI (jni_fake.c, unity_glue.c). */
extern void *fake_env, *fake_unityplayer_thiz, *fake_context_obj, *fake_surface_obj;
extern void *fake_vm;

/* The main thread runs the init arrays and the engine lifecycle with this bionic TLS block. */
static uint8_t main_tls[BIONIC_TLS_SIZE] __attribute__((aligned(16)));

/* ---- boot steps, in the order main() runs them ----------------------------------------- */

/* Title override leaves the cwd at the .nro folder or the SD root; Unity reads many files by
 * relative path, so chdir into RD_GAME_HOME. */
static void boot_enter_game_folder(void) {
  char cwd[256] = {0};
  getcwd(cwd, sizeof cwd);
  int rc = chdir(RD_GAME_HOME);
  struct stat st;
  int reach_apk = stat(RD_BASE_APK_NAME, &st) == 0;
  int reach_obb = stat(RD_GAME_OBB_NAME, &st) == 0;
  debugPrintf("[boot] cwd '%s' -> chdir(%s)=%d; reachable(rel): %s=%d %s=%d\n",
              cwd, RD_GAME_HOME, rc, RD_BASE_APK_NAME, reach_apk, RD_GAME_OBB_NAME, reach_obb);
}

/* config.txt: read it, or write it with defaults on first run / after a schema change. */
static void boot_read_config(void) {
  const char *cfg = RD_GAME_HOME "/" RD_CONFIG_FILE;
  if (read_config(cfg) != 0) write_config(cfg);
}

/* Render resolution (config.txt render_resolution, rd_perf.h): auto = 720p handheld /
 * 1080p docked, chosen once at boot. Touch follows it (nx_pointer scales the panel). */
static void boot_pick_resolution(void) {
  rd_pick_resolution(config.render_res,
                     appletGetOperationMode() == AppletOperationMode_Console,
                     &screen_width, &screen_height);
  debugPrintf("[gfx] boot mode=%s render=%dx%d (render_resolution); touch x%.2f/%.2f\n",
              appletGetOperationMode() == AppletOperationMode_Console ? "DOCKED" : "HANDHELD",
              screen_width, screen_height,
              (double)screen_width / RD_TOUCH_PANEL_W, (double)screen_height / RD_TOUCH_PANEL_H);
}

/* Sweep Unity's case-sensitivity probe files: CASESENSITIVETEST<guid> strays from older
 * builds, plus the single hidden scratch the probe is now redirected to (shim_fs_path.c
 * casetest_redirect). Prevents junk accumulating on the SD card each launch. */
static void boot_sweep_case_probes(void) {
  DIR *dd = opendir(RD_GAME_HOME);
  int swept = 0;
  if (dd) {
    struct dirent *de;
    while ((de = readdir(dd))) {
      if (strncasecmp(de->d_name, RD_CASETEST_PREFIX, sizeof RD_CASETEST_PREFIX - 1) == 0 ||
          strcmp(de->d_name, RD_CASETEST_SCRATCH) == 0) {
        char pth[RD_PATH_MAX]; snprintf(pth, sizeof pth, RD_GAME_HOME "/%s", de->d_name);
        if (unlink(pth) == 0) swept++;
      }
    }
    closedir(dd);
  }
  if (swept) debugPrintf("[boot] swept %d case-sensitivity probe file(s)\n", swept);
}

/* Loads libmain, libunity and libil2cpp into the .so region, maps them as code, starts the GC
 * bridge and applies the engine patches. */
static void boot_load_modules(void) {
  debugPrintf("[boot] loading modules...\n");
  if (load_module(&main_mod,   RD_LIB_MAIN)   < 0) fatal_error("Could not load %s", RD_LIB_MAIN);
  if (load_module(&unity_mod,  RD_LIB_UNITY)  < 0) fatal_error("Could not load %s", RD_LIB_UNITY);
  if (load_module(&il2cpp_mod, RD_LIB_IL2CPP) < 0) fatal_error("Could not load %s", RD_LIB_IL2CPP);
  g_il2cpp_base = (uintptr_t)il2cpp_mod.load_virtbase;
  g_il2cpp_size = il2cpp_mod.load_size;
  debugPrintf("[boot] libmain=%p libunity=%p libil2cpp=%p\n",
              main_mod.load_virtbase, unity_mod.load_virtbase, il2cpp_mod.load_virtbase);

  so_finalize(&main_mod);   so_flush_caches(&main_mod);
  so_finalize(&unity_mod);  so_flush_caches(&unity_mod);
  so_finalize(&il2cpp_mod); so_flush_caches(&il2cpp_mod);
  /* Reads libil2cpp code, so only after so_finalize() mapped it at
   * load_virtbase (run 09 faulted calling this right after so_load). */
  rd_gc_bridge_init(&il2cpp_mod);
  debugPrintf("[boot] modules finalized + flushed\n");

  /* Engine patches (rd_patches.c), found by instruction pattern. */
  {
    int total = 0;
    int n = rd_install_engine_patches((uintptr_t)unity_mod.load_virtbase, &total);
    debugPrintf("[boot] engine patches applied: %d/%d\n", n, total);
  }
}

/* Runs the libraries' constructors (with the main thread's bionic TLS installed). */
static void boot_run_init_arrays(void) {
  install_bionic_tls(main_tls);
  debugPrintf("[boot] running init arrays...\n");
  so_execute_init_array(&main_mod);
  so_execute_init_array(&unity_mod);
  so_execute_init_array(&il2cpp_mod);
  so_free_temp(&main_mod); so_free_temp(&unity_mod); so_free_temp(&il2cpp_mod);
  debugPrintf("[boot] init arrays done\n");
}

/* The fake JNI world, the engine-facing objects, the window size and HID input. */
static void boot_jni_and_input(void) {
  jni_init();
  unity_environment_init(RD_GAME_HOME);
  android_native_update_mode();
  android_native_input_init();
  debugPrintf("[boot] jni + env + hid ready\n");
}

/* JNI_OnLoad of libunity (registers the UnityPlayer natives) and of libil2cpp. */
static void boot_jni_onload(void) {
  /* Call libunity's real JNI_OnLoad(fake_vm) FIRST (exported symbol): runs
   * jni::Initialize() (caches the JavaVM) AND registers the UnityPlayer natives,
   * which our fake RegisterNatives captures for by-name resolution below. */
  {
    fn_jnionload Unity_JNI_OnLoad =
      (fn_jnionload)so_try_find_addr_rx(&unity_mod, "JNI_OnLoad");
    if (!Unity_JNI_OnLoad) fatal_error("libunity JNI_OnLoad not found");
    debugPrintf("[boot] calling libunity JNI_OnLoad(fake_vm)...\n");
    int jver = Unity_JNI_OnLoad(fake_vm, NULL);
    debugPrintf("[boot] JNI_OnLoad returned 0x%x\n", jver);
  }

  /* Register the JavaVM with the il2cpp runtime by calling libil2cpp's exported JNI_OnLoad by
   * name (version-proof, unlike hard-coding the RVAs of its two VM globals). */
  {
    fn_jnionload Il2cpp_JNI_OnLoad =
      (fn_jnionload)so_try_find_addr_rx(&il2cpp_mod, "JNI_OnLoad");
    if (Il2cpp_JNI_OnLoad) {
      debugPrintf("[boot] calling libil2cpp JNI_OnLoad(fake_vm)...\n");
      Il2cpp_JNI_OnLoad(fake_vm, NULL);
    } else {
      debugPrintf("[boot] WARNING: libil2cpp JNI_OnLoad not found (managed JNI may fail)\n");
    }
  }
}

/* The UnityPlayer natives the lifecycle needs, by name. */
static void boot_resolve_natives(void) {
  /* Resolve the UnityPlayer natives BY NAME from the captured RegisterNatives table. */
  #define RESOLVE_NATIVE(var, cast, name) do {                         \
      var = (cast)jni_lookup_unity_native(name);                       \
      if (!(var)) fatal_error("UnityPlayer native not captured: %s", name); \
    } while (0)
  RESOLVE_NATIVE(Unity_initJni,                  fn_initJni,  RD_NATIVE_initJni);
  RESOLVE_NATIVE(Unity_nativeRecreateGfxState,   fn_gfxstate, RD_NATIVE_nativeRecreateGfxState);
  RESOLVE_NATIVE(Unity_nativeSendSurfaceChanged, fn_v,        RD_NATIVE_nativeSendSurfaceChanged);
  RESOLVE_NATIVE(Unity_nativeRender,             fn_z,        RD_NATIVE_nativeRender);
  RESOLVE_NATIVE(Unity_nativeResume,             fn_v,        RD_NATIVE_nativeResume);
  RESOLVE_NATIVE(Unity_nativeFocusChanged,       fn_vz,       RD_NATIVE_nativeFocusChanged);
  RESOLVE_NATIVE(Unity_nativeDone,               fn_v,        RD_NATIVE_nativeDone);
  #undef RESOLVE_NATIVE
  debugPrintf("[boot] natives resolved (initJni=%p render=%p)\n",
              (void *)Unity_initJni, (void *)Unity_nativeRender);
}

/* What the Java UnityPlayer does on start: initJni, surface, resume, focus. */
static void boot_start_unity(void) {
  /* initJni is 3-ARG (env,thiz,Context) in 2018.4 (unity_entrypoints.h). */
  debugPrintf("[boot] initJni(ctx)...\n");
  Unity_initJni(fake_env, fake_unityplayer_thiz, fake_context_obj);
  debugPrintf("[boot] nativeRecreateGfxState...\n");
  Unity_nativeRecreateGfxState(fake_env, fake_unityplayer_thiz, 0, fake_surface_obj);
  Unity_nativeSendSurfaceChanged(fake_env, fake_unityplayer_thiz);
  debugPrintf("[boot] surface changed; resume + focus\n");

  Unity_nativeResume(fake_env, fake_unityplayer_thiz);
  Unity_nativeFocusChanged(fake_env, fake_unityplayer_thiz, 1 /* hasFocus */);
  debugPrintf("[boot] resumed + focus=true\n");
}

/* Before the first nativeRender: GC off (its stop-the-world needs signals Horizon lacks). */
static void boot_disable_gc(void) {
  /* CRITICAL ORDER: disable the Boehm GC + install the clock BEFORE the first
   * nativeRender. First-frame managed allocs can trigger a GC whose stop-the-world
   * uses POSIX signals Switch never delivers -> nativeRender would never return. */
  {
    typedef void (*fn_set_mode)(int);
    typedef void (*fn_void)(void);
    fn_set_mode il2cpp_gc_set_mode = (fn_set_mode)so_try_find_addr_rx(&il2cpp_mod, "il2cpp_gc_set_mode");
    fn_void     il2cpp_gc_disable  = (fn_void)    so_try_find_addr_rx(&il2cpp_mod, "il2cpp_gc_disable");
    if (il2cpp_gc_set_mode) { il2cpp_gc_set_mode(1); debugPrintf("[boot] il2cpp_gc_set_mode(DISABLED)\n"); }
    else debugPrintf("[boot] WARNING: il2cpp_gc_set_mode not found\n");
    if (il2cpp_gc_disable)  { il2cpp_gc_disable();   debugPrintf("[boot] il2cpp_gc_disable() -> GC OFF\n"); }
    else debugPrintf("[boot] WARNING: il2cpp_gc_disable not found\n");
  }
}

/* One nativeRender per frame, paced to RD_TARGET_HZ, until the engine or the applet quits.
 * Slow frames log a [perf] line with what happened during them. */
static void run_main_loop(void) {
  int frame = 0;
  uint64_t next_frame_ns = 0;
  next_frame_ns = rd_now_ns();
  while (appletMainLoop() && !jni_quit_requested) {
    diag_frame(frame);
    g_frame_count++;
    android_native_update_mode();
    android_native_feed_hid();
    rd_mods_frame((uint32_t)frame);

    /* Time the engine's frame. When one blows the budget, print what happened DURING it --
     * the per-frame delta of the shim counters -- plus whether a finger was down. A tap that
     * hitches will show its own cause here (sd=1 -> an SD commit, mmap=N -> heap growth or a
     * file map, gc=N -> a stop-the-world, open=N -> a burst of file opens). Costs one
     * clock_gettime pair per frame and prints nothing on a healthy frame. */
    const uint64_t t0 = rd_now_ns();
    unsigned _o0 = nx_stat_open, _c0 = nx_stat_commit, _m0 = nx_stat_mmap, _g0 = nx_stat_gcstop;

    g_rd_frame_start_ns = t0;
    g_rd_in_frame = 1;                                     /* Boost_loading */
    unsigned long long _rb0 = nx_stat_read_bytes, _rn0 = nx_stat_read_ns, _sw0 = nx_stat_swap_ns,
                       _gw0 = nx_stat_glwait_ns, _dq0 = nx_stat_dequeue_ns, _fn0 = nx_stat_fence_ns;
    int _alive = Unity_nativeRender(fake_env, fake_unityplayer_thiz);
    g_rd_in_frame = 0;
    if (!_alive) break;

    {
      long _ms = (long)((rd_now_ns() - t0) / RD_NS_PER_MS);
      if (_ms >= RD_SLOW_FRAME_MS) {
        debugPrintf("[perf] frame %d took %ldms  open=%u sd=%u mmap=%u gc=%u  touch=%d  "
                    "read=%lluKB/%llums swap=%llums gl=%llums deq=%llums fence=%llums\n",
                    frame, _ms,
                    nx_stat_open - _o0, nx_stat_commit - _c0,
                    nx_stat_mmap - _m0, nx_stat_gcstop - _g0,
                    nx_hook_touch_count(),
                    (nx_stat_read_bytes - _rb0) >> 10, (nx_stat_read_ns - _rn0) / RD_NS_PER_MS,
                    (nx_stat_swap_ns - _sw0) / RD_NS_PER_MS,
                    (nx_stat_glwait_ns - _gw0) / RD_NS_PER_MS,
                    (nx_stat_dequeue_ns - _dq0) / RD_NS_PER_MS,
                    (nx_stat_fence_ns - _fn0) / RD_NS_PER_MS);
      }
    }
    if (frame < RD_LOG_FIRST_FRAMES || (frame % RD_LOG_FRAME_EVERY) == 0) debugPrintf("[boot] frame %d rendered\n", frame);
    if (frame == 1) debugPrintf("[input] UnityEngine.Input icalls replaced: %d\n", rd_icall_applied());
    if (frame == 1 || (frame > 0 && (frame % RD_CLOCK_LOG_FRAMES) == 0)) rd_log_clocks(frame == 1 ? "start" : "running");
    if ((frame % RD_DRIVE_RETRY_FRAMES) == 0) rd_drive_try_install();
    frame++;
    if ((frame % RD_SD_FLUSH_FRAMES) == 0) nx_sd_flush();   /* commit pending saves */
    /* FRAME LIMITER (~60fps). No real display vsync -> pace to 16.6ms so we don't
     * flood the compositor (an unthrottled present flood crashes vi). */
    next_frame_ns += RD_FRAME_NS;
    uint64_t now = rd_now_ns();
    if ((int64_t)(next_frame_ns - now) > 0) svcSleepThread(next_frame_ns - now);
    else next_frame_ns = now;
  }
}

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  socketInitializeDefault();
  debugPrintf(RD_LOG_TAG_BANNER " realdrift_nx start (Real Drift " RD_GAME_VERSION_NAME ", Unity " RD_UNITY_VERSION ") ===\n");

  boot_enter_game_folder();
  boot_read_config();
  check_syscalls();
  debugPrintf("[boot] syscalls ok\n");
  mem_log_boot();
  mem_arm_overcommit();
  boot_pick_resolution();

  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0)
    debugPrintf("SDL_Init failed: %s\n", SDL_GetError());

  check_data();
  boot_sweep_case_probes();

  debugPrintf(RD_LOG_TAG_BANNER "== realdrift_nx build %s %s =====\n", __DATE__, __TIME__);
  debugPrintf("[boot] fixes: ftt-stub gc-offsets asset-redirect touch-hooks 1080p rename-posix "
              "map-dedup fd-cache(.so-only) synth-proc-cache cjk-fonts+opendir\n");
  /* Extract the console's shared fonts and expose them where Unity's dynamic font fallback
   * looks (/system/fonts + fonts.xml). MUST run before the modules load: libunity reads the
   * font config while building its fallback list, and an empty list is exactly why the
   * Simplified Chinese banner text rendered blank while the Latin UI was fine. */
  nx_fonts_init(RD_GAME_HOME);

  boot_load_modules();
  boot_run_init_arrays();
  boot_jni_and_input();
  rd_mods_boot(RD_GAME_HOME);   /* before Unity opens the archives and registers icalls */
  install_bionic_tls(main_tls);
  boot_jni_onload();
  boot_resolve_natives();
  install_bionic_tls(main_tls);
  boot_start_unity();

  appletHook(&g_applet_cookie, nx_applet_hook, NULL);
  boot_disable_gc();

  /* The exported il2cpp API used by the input/driving/race hooks, resolved once. */
  int il2cpp_missing = rd_il2cpp_load(&g_il2cpp, il2cpp_sym, &il2cpp_mod, il2cpp_sym_missing);
  debugPrintf("[boot] il2cpp API: %d function(s) missing\n", il2cpp_missing);

  /* Controller driving + race UI (rd_drive_hook.c) are installed lazily in the loop. */
  rd_start_clock_thread();
  debugPrintf("[boot] GC off + clock thread started; entering render loop\n");

  diag_thread_register(NULL, 0);
  diag_set_name(NULL, "NX_UIMain");
  diag_watchdog_start();

  run_main_loop();

  Unity_nativeDone(fake_env, fake_unityplayer_thiz);

  opensles_shutdown();
  SDL_Quit();
  socketExit();

  debug_log_flush();   /* Buffered debug.log */
  extern void NX_NORETURN __libnx_exit(int rc);
  __libnx_exit(0);
  return 0;
}
