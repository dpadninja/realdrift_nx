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
| SDL audio | SDL2 | OpenSL ES buffer-queue callbacks (`android/opensles.c`) |
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

Filled in by the layout move; see the directory listing of each layer and each file's header comment.
