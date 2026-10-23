# Architecture

realdrift_nx runs the original Android build of Real Drift (Unity 2018.4, IL2CPP, arm64) on the
Switch. It does not emulate a phone: the game's own `libmain.so`, `libunity.so` and
`libil2cpp.so` are loaded into a libnx process, and everything they expect from Android is
provided by our code.

## Layers

```
 game      Real Drift specifics: controls, race HUD, smoke, settings      source/game/
   │
 engine    Unity / IL2CPP: il2cpp API, icall overrides, engine patches,   source/engine/
   │       GC bridge, UnityEngine.Input
   │
 android   what Android offers an app: JNI + Java classes, NDK            source/android/
   │       (ANativeWindow, AInput, ALooper), EGL/GLES glue, OpenSL ES, APK assets
   │
 bionic    the C library the .so files were linked against: bionic ABI    source/bionic/
   │       on top of newlib, the import tables, fake fds
   │
 loader    ELF loading and relocation, process start, memory layout,      source/loader/
           logging, crash reports, stall watchdog
 platform  Switch services used across layers: input, overlay, fonts,     source/platform/
           clocks, timing constants, the fake device description
```

A layer may use the layers below it and `platform`; it never includes headers from a layer above.
The few upward calls the loader needs (for example `main.c` driving the engine lifecycle) go
through the documented entry points of the upper layer's headers.

## Boot (`loader/main.c`)

1. Heap and memory layout: newlib heap, the mmap arena and the stack-region overcommit window.
2. `config.txt` (`game/rd_settings.c`), render resolution (`platform/rd_perf.c`).
3. Shared fonts for Unity's font fallback (`platform/nx_fonts.c`).
4. Load the three `.so` files (`loader/so_util.c`); resolve their imports against our tables
   (`bionic/imports.c`, `engine/unity_imports.c`, `bionic/rd_imports.c`); locate and apply
   engine patches by instruction pattern (`engine/rd_patches.c`, `engine/rd_patchsites.c`).
5. Run the libraries' init arrays, build the fake JNI world (`android/jni_fake.c`,
   `android/unity_jni.c`), call `JNI_OnLoad` of libunity and libil2cpp.
6. Recover the `UnityPlayer` natives by name from libunity's `RegisterNatives` table and drive
   the lifecycle the Java side would: `initJni` → surface → resume → focus.
7. Main loop: one `nativeRender` per frame, paced to 60 Hz.

## A frame

```
 HID (pad, touch, mouse) ── platform/nx_pointer ──┬─► engine/unity_input_hook (UnityEngine.Input icalls)
                                                  └─► game/rd_drive_hook (car controls)
 main loop ── Unity nativeRender ── game code (C#, IL2CPP) ── our hooks (race HUD, smoke)
                     │
                     └─ GLES via mesa ── android/egl: eglSwapBuffers wrapper
                                            ├─ platform/rd_overlay: FPS counter, cursor
                                            └─ timing counters for the [perf] log
```

## Files and saves

- Game data is read straight from `base.apk` and the OBB: Unity's own zip reader through the
  bionic `fopen`/`read` shims, and the fake `AssetManager` (`android/unity_jni.c`) over
  `android/rd_zip.c`. Nothing is extracted to the SD card except what IL2CPP writes itself.
- `SharedPreferences` (the game's saves) are stored in `prefs.kv` (`game/rd_prefs.c`).

## Threads

| Thread | Created by | Runs |
|---|---|---|
| main | libnx | boot, the main loop, Unity's main thread (all C# and GL calls) |
| engine threads | Unity through `pthread_create` (bionic shim) | job workers, loading, FMOD mixer |
| audio output | `platform/rd_audioout.c` (core 2) | the OpenSL ES mixer and buffer-queue callbacks (`android/opensles_mixer.c`) |
| clock | `engine/rd_patches.c` | CPU boost while loading, `debug.log` flushes |
| watchdog | `loader/diag.c` | dumps every thread's state when frames stop |

## Where new code goes

- A missing libc/bionic function → `bionic/`. A missing Java method or NDK call → `android/`.
- Something that understands Unity or IL2CPP but not Real Drift → `engine/`.
- Anything that knows a Real Drift class, field or behaviour → `game/`; names go into
  `game/rd_game_symbols.h`.
- A Switch service used by more than one layer → `platform/`.
- Decide by the most specific knowledge the code needs; split pure logic from glue
  (`docs/CODING_STYLE.md`).

## File map

One line per module; each file's header comment has the details.

### loader

| File | Purpose |
|---|---|
| `main.c` | entry point: `main()` as a list of boot steps (load libraries, JNI, Unity lifecycle) and the main loop |
| `mem_layout.{c,h}` | memory layout before `main()`: newlib heap, `.so` region, mmap arena; arming the overcommit window |
| `so_util.{c,h}` | load an arm64 ELF shared object into the reserved region, relocate it, find symbols |
| `rd_modimage.{c,h}` | a loaded module as an `rd_image` for the pattern search |
| `rd_codeaddr.{c,h}` | is a stack word a plausible return address into a loaded module (backtraces) |
| `util.{c,h}` | `debugPrintf`, the buffered `debug.log` |
| `rd_perfstat.{c,h}` | frame-spike counters for the `[perf]` line (opens, commits, mmaps, GPU waits, ...) |
| `rd_log.{c,h}` | which log lines `debug_log=0` keeps and which must reach the SD card at once |
| `error.{c,h}` | fatal error screen |
| `nx_crash_handler.{c,h}` | CPU exception handler: registers and backtrace into the log |
| `diag.{c,h}` | thread registry and stall watchdog: dumps every thread when frames stop |
| `rd_loadprof_hook.{c,h}` | debug_load: samples file counters and thread CPU around long frames |

### bionic

| File | Purpose |
|---|---|
| `imports.{c,h}` | the main import table (libc, libm, pthread, EGL/GLES, NDK), the one-line stubs it points at, and import resolution |
| `rd_imports.c` | imports of this game build the inherited tables did not have |
| `libc_shim.h` | declarations of every bionic function below, for the import tables |
| `shim_libc.c` | fortify wrappers, system properties, futex/syscall, sysconf, locale, network/process stubs |
| `shim_fs.{c,h}` | open/read/write/close/stat/rename/readdir; `shim_fs.h` is the internal interface of the three fs files |
| `shim_fs_path.c` | path rewrites: APK asset root, case-sensitivity probe, `mkdir -p`, synthetic `/proc` and `/sys` |
| `shim_fs_cache.c` | per-fd state: the archive block cache for `base.apk` / OBB fds, read-ahead windows, synthetic inodes, open-fd cache for `.so` files, SD commit after writes |
| `rd_sdcommit.{c,h}` | which written files commit the SD card on close (not Unity's per-launch il2cpp copy) |
| `rd_archcache.{c,h}` | archive_cache: the block pool (LRU) shared by every open of `base.apk` / the OBB |
| `shim_stdio.c` | `fopen` (archive FILEs read through the block cache) and the fake `stdin`/`stdout`/`stderr` streams |
| `shim_mmap.{c,h}` | `mmap`/`munmap`/`mprotect`/`madvise`, memalign fallback, read-only file-map cache; `shim_mmap.h` is internal |
| `shim_mmap_arena.c` | the page arena on the newlib heap |
| `shim_mmap_oc.c` | the stack-region overcommit window for Unity's huge `PROT_NONE` reservations |
| `shim_dl.c` | `dlopen`/`dlsym` over the loaded modules (and the icall override hand-over) |
| `shim_pthread.c` | pthread on newlib: lazily backed mutexes/conditions, thread start, TLS keys, rwlocks, semaphores |
| `fakefd.{c,h}` | in-process pipes for the engine's pipe()-based thread signalling |
| `rd_iov.{c,h}` | `writev` on top of `write` |
| `rd_synth.{c,h}` | stdio access to the synthetic `/proc` and `/sys` files |

### android

| File | Purpose |
|---|---|
| `jni_fake.{c,h}` | the fake `JNIEnv` / `JavaVM` function tables and call dispatch |
| `jni_objects.c` | the fake Java object model: local references, objects, strings, arrays, classes, IDs |
| `jni_framework.c` | what framework methods and fields answer for classes without real behaviour |
| `jni_internal.h` | shared types and helpers of the three JNI files |
| `jni_unimpl.h` | generated stubs that log any JNI function slot nobody implemented |
| `unity_jni.{c,h}` | real behaviour for the Java classes Unity needs (Context, AssetManager, prefs, ...) |
| `android_native_unity.{c,h}` | NDK: `ANativeWindow` over the Switch window, `ALooper`, sensors, input |
| `opensles.{c,h}` | the OpenSL ES subset FMOD uses: interfaces, objects, engine, `slCreateEngine` |
| `opensles_mixer.c` | the SDL2 audio device, the player registry and the software mixer |
| `rd_audiowatch.{c,h}` | debug_audio: notices silences of the output and their end |
| `opensles_internal.h` | OpenSL ES types, vtables and the shim's objects (internal) |
| `egl.{c,h}` | EGL/GLES wrappers: surface size and lifetime, `eglSwapBuffers` with overlays, GL wait timing |
| `rd_etc2bc.{c,h}` | ETC1/ETC2 blocks re-encoded as S3TC (BC1 / BC3), the only user of the vendored codecs |
| `rd_slvolume.{c,h}` | OpenSL volume level and mute as the mixer's gain |
| `rd_etc2bc_gl.{c,h}` | texture_transcode: ETC2 texture storage and uploads turned into S3TC |
| `third_party/etcdec.h`, `third_party/stb_dxt.h` | vendored ETC/EAC decoder (iOrange/etcdec) and DXT encoder (nothings/stb) |
| `rd_memwatch.{c,h}` | debug_mem: `[mem]` lines and `glGetError` after GL calls that allocate |
| `android_log.{c,h}` | liblog and `__assert2` into `debug.log` |
| `rd_zip.{c,h}` | read single entries of `base.apk` (stored or deflated) for the AssetManager |
| `rd_meta.{c,h}` | `ApplicationInfo.metaData` values from the manifest |
| `rd_features.{c,h}` | `PackageManager.hasSystemFeature()` answers |
| `rd_apkpath.{c,h}` | what `Context.getPackageCodePath()` returns |

### engine

| File | Purpose |
|---|---|
| `rd_il2cpp.{c,h}` | the il2cpp runtime API, resolved once, plus helpers (classes, fields, arrays, strings) |
| `rd_icall.{c,h}` | replace or wrap engine icalls by name at registration |
| `rd_uinav.{c,h}` | focus navigation geometry (pure): direction moves, auto-repeat, refocus |
| `rd_ugui.{c,h}` | the clickable uGUI elements on screen and their rectangles, for menu navigation |
| `rd_sig.{c,h}` | masked AArch64 instruction-pattern search |
| `rd_patchsites.{c,h}` | the patterns that locate every engine patch site and GC variable |
| `rd_patches.{c,h}` | apply the libunity patches; the background clock thread (CPU boost, log flush) |
| `rd_a64.{c,h}` | AArch64 instruction encode/decode helpers |
| `rd_gcbridge.{c,h}`, `rd_gcbridge_hook.c` | Boehm GC stop-the-world without POSIX signals: register publishing (pure) and `pthread_kill` for the collector |
| `unity_imports.{c,h}` | imports specific to the Unity/IL2CPP engine |
| `unity_glue.c` | the fake UnityPlayer / Context / Surface objects the engine is driven with |
| `unity_input_hook.{c,h}` | `UnityEngine.Input` icalls fed from the Switch touch screen and cursor |
| `unity_entrypoints.h` | signatures of the `UnityPlayer` natives |

### platform

| File | Purpose |
|---|---|
| `rd_platform.h` | timing constants, main-loop cadences, buffer sizes, `rd_now_ns()`, the frame clock |
| `rd_device.h` | the Android device the port pretends to be (SDK level, ABI, cores, RAM) |
| `nx_pointer.{c,h}` | pad, touch screen, USB mouse, gyro, cursor movement and its settings file |
| `nx_pointer_draw.c` | the cursor image (`cursor.png` or the built-in arrow), drawn through `rd_overlay` |
| `nx_pointer_internal.h` | state shared by the two nx_pointer files (internal) |
| `nx_fonts.{c,h}` | the console's shared fonts as Android system fonts for Unity's fallback |
| `rd_overlay.{c,h}`, `rd_overlay_gl.c` | 2D drawing on top of the finished frame (cursor, FPS counter) |
| `rd_fps.{c,h}` | the debug FPS counter: counting and the built-in pixel font |
| `rd_gpuwait.{c,h}` | time mesa waits for the display and the GPU; real clock rates in the log |
| `rd_sdstat.c` | debug_load: FS file opens and reads under newlib (what reaches the SD card) |
| `rd_loadprof.{c,h}` | debug_load: the `[load]` lines (file traffic, CPU per thread) |
| `rd_jobs.{c,h}` | worker threads on cores 1 and 2, which the engine leaves idle |
| `rd_audioout.{c,h}` | the sound output: libnx audout fed by our thread (in place of SDL's Switch audio, which could hang) |
| `rd_memstat.{c,h}` | the debug_mem report: memory snapshot, its log line, when to take one |
| `rd_perf.{c,h}` | render resolution choice and CPU boost while loading |

### game

| File | Purpose |
|---|---|
| `rd_game.h` | identity of the supported game build and its file names |
| `rd_game_symbols.h` | C# classes, methods, fields and icalls the hooks use, by name |
| `rd_settings.{c,h}`, `config.{c,h}` | `config.txt`: the settings table, parsing, writing, defaults |
| `rd_drive.{c,h}`, `rd_drive_hook.c` | controller driving: button mapping (pure) and the car-controller hook |
| `rd_race.{c,h}`, `rd_race_hook.c` | race HUD: hide touch controls, pause button |
| `rd_smoke.{c,h}`, `rd_smoke_hook.c` | lighter tyre smoke through the particle Emit icall |
| `rd_menu.{c,h}`, `rd_menu_hook.c` | menus on the controller: focus frame, A taps, B goes back (synthetic touch) |
| `rd_prefs.{c,h}` | `SharedPreferences` semantics and the `prefs.kv` save file |

`main.c` and the import tables (`imports.c`, `unity_imports.c`, `rd_imports.c`) are composition
points: they may include headers of every layer to wire implementations to the engine.
