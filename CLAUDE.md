# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Porting the Android game **Real Drift** (`com.realdrift.sipon`, Unity 2018.4.14f1, IL2CPP) to Nintendo Switch homebrew.

**It uses so-loader approach:** load the original arm64 `libunity.so` / `libil2cpp.so` / `libmain.so` from the APK into a libnx process and emulate the Android environment with shims (bionic → newlib, NDK, EGL/GLES via switch-mesa, OpenSL ES, fake JNI). The loader is a fork of [badpiggies_nx](https://github.com/ChanseyIsTheBest/badpiggies_nx) (Unity 2020.3 port, MIT lineage).

## Rules

- **Research findings go into `analysis/research/*.md`** (index in `analysis/research/README.md`; gitignored, not published -- do not mention them in README.md). Record every significant result as you go, together with the command or script that reproduces it.
- **All our own code must have unit tests**: Python tools in `tests/` (pytest); C code from the loader/shims needs host-side tests that run without the Switch. Forked upstream code does not need retroactive tests, but our changes to it do.
- Never commit game files: `original/` (APK/OBB) and `analysis/` (extracted libs, dumps, reference clones) are gitignored.

## Layout

- `source/`, `Makefile`: the Switch loader (`realdrift_nx`), forked from badpiggies_nx commit `7726be3` and since cleaned of everything not used by Real Drift (`analysis/research/11-cleanup.md`; upstream parity is no longer a goal). Our own modules use the `rd_` prefix.
  - **Named values live in one place** -- never repeat a literal that one of these defines:
    - `rd_game.h`: game identity (package, versionCode, unity.build-id, Unity version) and file names (libs, `base.apk`, OBB, `config.txt`, `prefs.kv`, `debug.log`, game folder). `tools/game.py` reads it for the Python side.
    - `rd_game_symbols.h`: C# assembly/class/method/field names the hooks resolve by name.
    - `rd_platform.h`: frame timing, main-loop cadences, touch panel size, buffer sizes, `rd_now_ns()`, the frame clock.
    - `rd_device.h`: the fake Android device (SDK, fingerprint, ABI, cores, RAM) reported via JNI, properties, sysconf, /proc.
    - `rd_settings.h/.c`: every `config.txt` key as one table row (name, type, field, help text); parsing, writing and `config.example.txt` come from it.
    - `rd_log.h`: log tags (`RD_LOG_TAG_*`) shared by producers and the debug.log filter.
    - `rd_a64.h`: AArch64 encodings; `rd_il2cpp.h`: the il2cpp API (`g_il2cpp`, resolved once in main.c).
- **No per-build offsets**: locate libunity/libil2cpp patch sites at runtime by masked instruction patterns (`source/rd_sig.c`, `source/rd_patchsites.c`; a pattern must match exactly once and be cross-checked), and C# fields/methods/icalls by name through the il2cpp API. Check new patterns against both releases with `make tools` + `build-host/find_patch_sites` / `tests/test_patch_sites.py` (release -> analysis paths: `RELEASES` in `tools/game.py`).
- `original/`: user-supplied game files: `base.apk` = full Real Drift **5.0.8** (`com.realdrift.sipon`, versionCode 76, `unity.build-id a716b65c…`, arm64-v8a) and its `main.76.com.realdrift.sipon.obb`. The loader targets this pair (`source/rd_game.h`); `tools/stage_sd.py` refuses any other APK (e.g. Real Drift Lite, `com.realdriftportnew.sipon`). The previously used 5.0.7 APK is kept as `analysis/realdrift-5.0.7-build74.apk`.
- `analysis/`: regenerable artifacts: `lib64/` + `data507/` (5.0.7), `lib64_508/` + `data508/` (5.0.8), `lib32/`, `dump507/` (Il2CppDumper), `*.imports.txt`, `logs/` (console runs), `ref/` (clones of badpiggies_nx and cloverpit_nx).
- `sd/`: staged SD-card folder produced by `tools/stage_sd.py` (gitignored; kept out of `build/` because `make clean` deletes `build/`): the `.nro`, the three `.so`, `base.apk` and the OBB only. Game data is read directly from `base.apk`/OBB (Unity's zip reader + the fake `AssetManager` over `source/rd_zip.c`); nothing is extracted.
- `tools/`: Python 3.13 helpers (uv project): `game.py` (game identity from `rd_game.h`, analysis paths per release), `stage_sd.py` (stdlib only: it ships in the release, where `tools/release.py` inlines the game identity), `release.py`, `import_coverage.py`, `jni_natives.py`, `il2cpp_symbolize.py` (addresses in logs -> C# methods), `hostc.py` (builds host C programs), and `find_patch_sites.c`.
- `tests/`: pytest. `tests/c/test_*.c` are host-side C unit tests: the first line `/* sources: ... */` lists the loader files to link (same header for `tools/find_patch_sites.c` and `tests/c/gen_config_example.c`); `tools/hostc.py` builds them, with ASan/UBSan for tests. Tests that need files from `analysis/` skip when those files are missing.
- `analysis/research/`: research notes (local only).
- `.github/workflows/release.yml`: a `vX.Y.Z` tag builds the `.nro` in the devkitPro container, runs the tests and publishes `realdrift_nx-vX.Y.Z.zip` (`.nro`, standalone `stage_sd.py`, `config.example.txt`).
- `config.example.txt`: every `config.txt` key at its default, generated from the settings table (`make config-example`); `tests/test_config_example.py` fails if it goes stale. New keys go into the table in `source/rd_settings.c`, never by hand here.

## Commands

```sh
make test                                       # all tests (Python + host C) = uv run pytest -q
make tools                                      # build-host/find_patch_sites
make config-example                             # regenerate config.example.txt
uv run pytest tests/test_c_units.py -k rd_iov   # one C test file
uv run pytest tests/test_jni_natives.py::test_read_cstr

docker run --rm -u $(id -u):$(id -g) -v ./:/project -w /project devkitpro/devkita64:latest make   # -> realdrift_nx.nro

unzip -j -o original/base.apk 'lib/arm64-v8a/*' -d analysis/lib64_508
uv run python tools/import_coverage.py analysis/lib64_508/*.so     # imports not provided by source/ tables
uv run python tools/jni_natives.py analysis/lib64_508/libunity.so  # UnityPlayer natives + signatures
uv run python tools/stage_sd.py original/base.apk original/main.76.com.realdrift.sipon.obb \
    --nro realdrift_nx.nro --out sd/switch/realdrift_nx      # -> SD folder
uv run python -m tools.release realdrift_nx.nro dist/test   # what CI publishes
```

The devkitPro image already contains every portlib the loader needs (mesa, libdrm_nouveau, SDL2, freetype, libpng, zlib, bzip2). Run it with `-u` so build outputs are not owned by root.
