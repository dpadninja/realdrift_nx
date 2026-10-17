---
name: port-architecture
description: Layer map of the realdrift_nx so-loader (loader / bionic / android / engine / game / platform) and recipes for adding a libc shim, a JNI or NDK handler, an engine patch, an icall override or a Real Drift hook. Use when deciding where new code goes or when changing how the port is structured.
---

# realdrift_nx architecture

Read `docs/ARCHITECTURE.md` for the layers, boot sequence, frame flow and threads. Summary:

| Layer | Directory | Knows about |
|---|---|---|
| loader | `source/loader/` | ELF, memory layout, process start, logging, crashes |
| bionic | `source/bionic/` | the bionic C ABI on top of newlib, import tables |
| android | `source/android/` | JNI + Java classes, NDK, EGL/GLES, OpenSL ES, APK assets |
| engine | `source/engine/` | Unity / IL2CPP internals, not the game |
| game | `source/game/` | Real Drift classes, fields, behaviour, `config.txt` |
| platform | `source/platform/` | Switch services shared by several layers |

A layer includes only headers from its own layer, lower layers and `platform`.

## Recipes

- **Missing import** (the loader logs an unresolved symbol, `tools/import_coverage.py` lists it):
  implement it in `bionic/` (libc), `android/` (NDK, EGL, OpenSL) or `engine/unity_imports.c`
  (Unity-specific), then add the row to the matching import table.
- **Java method the engine calls** (`[jni] call:` lines): handle it in `android/unity_jni.c`
  (real behaviour) or `android/jni_fake.c` (generic object model); keep Java class names in one
  place.
- **Engine patch**: an instruction pattern in `engine/rd_patchsites.c` that matches exactly once
  in both supported releases (`make tools` + `build-host/find_patch_sites`,
  `tests/test_patch_sites.py`), applied in `engine/rd_patches.c`. Never a fixed offset.
- **Replace or wrap an engine icall**: an `rd_icall_override` row (`engine/rd_icall.h`); set
  `orig` to receive the engine's function when wrapping. Only icalls the game's code uses are
  registered: check with `uv run python -m tools.unity_ref icalls <Type>` (status `game`)
  before relying on one (`docs/RESEARCH_TOOLS.md`).
- **Game hook**: names in `game/rd_game_symbols.h`, offsets and methods resolved through
  `engine/rd_il2cpp.h`, pure decisions in `game/rd_<feature>.c` (host test), the glue in
  `game/rd_<feature>_hook.c`. Find the class, fields and the method's behaviour first with
  `tools/il2cpp_find.py` and `tools/il2cpp_disasm.py`. Hook per-frame work into the race HUD update (`rd_race_hook.c`)
  instead of adding new frame callbacks.
- **Drawing on screen**: `platform/rd_overlay.h` between `rd_overlay_begin` / `rd_overlay_end`
  from the eglSwapBuffers wrapper.
- **New setting**: one row in the `game/rd_settings.c` table, then `make config-example`.

Research findings and their commands go into `analysis/research/*.md` (local, not published).
