# Architecture

realdrift_nx runs the original Android build of Real Drift (Unity 2018.4, IL2CPP, arm64) on the
Switch. It does not emulate a phone: the game's own `libmain.so`, `libunity.so` and
`libil2cpp.so` are loaded into a libnx process, and everything they expect from Android is
provided by our code.

## Layers

```
 mods      the mod host: mods/<id>/ folders, the mod API (sdk/rd_mod.h)   source/mods/
   │
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
   `android/unity_jni.c`); load the mods (`mods/rd_mods.c`: their archive files, patch.ini, cars.ini,
   mod.so and icall overrides must be in place before Unity starts); call `JNI_OnLoad` of
   libunity and libil2cpp.
6. Recover the `UnityPlayer` natives by name from libunity's `RegisterNatives` table and drive
   the lifecycle the Java side would: `initJni` → surface → resume → focus.
7. Main loop: one `nativeRender` per frame, paced to 60 Hz.

## A frame

```
 HID (pad, touch, mouse) ── platform/nx_pointer ──┬─► engine/unity_input_hook (UnityEngine.Input icalls)
                                                  └─► game/rd_drive_hook (car controls)
 main loop ── mods/rd_mods_frame (game ready, scene changes, patch.ini, mods' on_frame)
           ── Unity nativeRender ── game code (C#, IL2CPP) ── our hooks (race HUD, smoke)
                     │
                     └─ GLES via mesa ── android/egl: eglSwapBuffers wrapper
                                            ├─ platform/rd_overlay: FPS counter, cursor, mods' text
                                            └─ timing counters for the [perf] log
```

## Files and saves

- Game data is read straight from `base.apk` and the OBB: Unity's own zip reader through the
  bionic `fopen`/`read` shims, and the fake `AssetManager` (`android/unity_jni.c`) over
  `android/rd_zip.c`. Nothing is extracted to the SD card except what IL2CPP writes itself.
- With mods that replace archive files, both readers see a view of the archive with those
  files put in (`bionic/rd_zipview.c` builds it, `bionic/rd_vfile.c` serves it as virtual
  descriptors and FILEs); the archives on the SD card are never changed.
- `SharedPreferences` (the game's saves) are stored in `prefs.kv` (`game/rd_prefs.c`).

## Threads

| Thread | Created by | Runs |
|---|---|---|
| main | libnx | boot, the main loop, Unity's main thread (all C# and GL calls) |
| engine threads | Unity through `pthread_create` (bionic shim) | job workers, loading, FMOD mixer |
| SDL audio | SDL2 | OpenSL ES buffer-queue callbacks (`android/opensles.c`) |
| clock | `engine/rd_patches.c` | CPU boost while loading, `debug.log` flushes |
| watchdog | `loader/diag.c` | dumps every thread's state when frames stop |

## Where new code goes

- A missing libc/bionic function → `bionic/`. A missing Java method or NDK call → `android/`.
- Something that understands Unity or IL2CPP but not Real Drift → `engine/`.
- Anything that knows a Real Drift class, field or behaviour → `game/`; names go into
  `game/rd_game_symbols.h`.
- A Switch service used by more than one layer → `platform/`.
- Something mods need from the port → a function in the `rd_mod_api` table (`sdk/rd_mod.h`,
  filled in `mods/rd_mod_api.c`).
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

### bionic

| File | Purpose |
|---|---|
| `imports.{c,h}` | the main import table (libc, libm, pthread, EGL/GLES, NDK), the one-line stubs it points at, and import resolution |
| `rd_imports.c` | imports of this game build the inherited tables did not have |
| `libc_shim.h` | declarations of every bionic function below, for the import tables |
| `shim_libc.c` | fortify wrappers, system properties, futex/syscall, sysconf, locale, network/process stubs |
| `shim_fs.{c,h}` | open/read/write/close/stat/rename/readdir; `shim_fs.h` is the internal interface of the three fs files |
| `shim_fs_path.c` | path rewrites: APK asset root, case-sensitivity probe, `mkdir -p`, synthetic `/proc` and `/sys` |
| `shim_fs_cache.c` | per-fd state: read-ahead windows, synthetic inodes, open-fd cache for `.so` files, SD commit after writes |
| `shim_stdio.c` | `fopen` and the fake `stdin`/`stdout`/`stderr` streams |
| `shim_mmap.{c,h}` | `mmap`/`munmap`/`mprotect`/`madvise`, memalign fallback, read-only file-map cache; `shim_mmap.h` is internal |
| `shim_mmap_arena.c` | the page arena on the newlib heap |
| `shim_mmap_oc.c` | the stack-region overcommit window for Unity's huge `PROT_NONE` reservations |
| `shim_dl.c` | `dlopen`/`dlsym` over the loaded modules (and the icall override hand-over) |
| `shim_pthread.c` | pthread on newlib: lazily backed mutexes/conditions, thread start, TLS keys, rwlocks, semaphores |
| `fakefd.{c,h}` | in-process pipes for the engine's pipe()-based thread signalling |
| `rd_zipfmt.h` | ZIP record layout shared by `rd_zip.c` and `rd_zipview.c` |
| `rd_zipview.{c,h}` | an archive with entries replaced or added, as a view (pure) |
| `rd_vfile.{c,h}` | that view served as virtual descriptors / FILEs to Unity and the AssetManager |
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
| `opensles_internal.h` | OpenSL ES types, vtables and the shim's objects (internal) |
| `egl.{c,h}` | EGL/GLES wrappers: surface size and lifetime, `eglSwapBuffers` with overlays, GL wait timing |
| `android_log.{c,h}` | liblog and `__assert2` into `debug.log` |
| `rd_zip.{c,h}` | read single entries of `base.apk` (stored or deflated) for the AssetManager |
| `rd_meta.{c,h}` | `ApplicationInfo.metaData` values from the manifest |
| `rd_features.{c,h}` | `PackageManager.hasSystemFeature()` answers |
| `rd_apkpath.{c,h}` | what `Context.getPackageCodePath()` returns |

### engine

| File | Purpose |
|---|---|
| `rd_il2cpp.{c,h}` | the il2cpp runtime API, resolved once, plus helpers (classes, fields, arrays, strings) |
| `rd_icall.{c,h}` | replace or wrap engine icalls by name at registration (plus the mods' table) |
| `rd_hook.{c,h}`, `rd_hook_install.c` | inline hooks: prologue relocation into a trampoline (pure) and the code patch |
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
| `rd_fps.{c,h}` | the debug FPS counter (counting, layout through `rd_text`) |
| `rd_text.{c,h}` | the built-in 3x5 pixel font as rectangles (FPS counter, mods' text) |
| `rd_gpuwait.{c,h}` | time mesa waits for the display and the GPU; real clock rates in the log |
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
| `rd_prefs.{c,h}` | `SharedPreferences` semantics and the `prefs.kv` save file |

`main.c` and the import tables (`imports.c`, `unity_imports.c`, `rd_imports.c`) are composition
points: they may include headers of every layer to wire implementations to the engine.

### mods

| File | What |
|---|---|
| `rd_mods.{c,h}` | boot (scan `mods/`, files, patch.ini, mod.so), per-frame work, the mods' text overlay |
| `rd_mod_api.c` | the functions of the `rd_mod_api` table handed to each mod |
| `rd_mods_internal.h` | state shared by the two files above (internal) |
| `rd_mod_ini.{c,h}` | mod.ini / patch.ini parsing and the mod.ini key table |
| `rd_mod_patch.{c,h}` | patch.ini values parsed for their field type and written |
| `rd_mod_cars.{c,h}` | cars.ini: its key table (`rd_car_props`) and parsing, pure |
| `rd_mods_patches.c` | patch.ini and cars.ini values written into live objects (a car = every model carrying a `Drivetrain`), each when the game reads it: every second, per scene, or as the car's originals in prefix hooks on `MenuManager.LoadOriginalValues` / `RaceManager.UpdateVehicle` |
| `rd_modhost.{c,h}` | load order, file/icall claims, the per-frame text queue |
| `../../sdk/rd_mod.h`, `../../sdk/mod.mk` | the public mod API and the mod build rules |
