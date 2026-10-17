# Coding style

The loader is C11 (`-std=gnu11`) built with devkitA64 for the Switch, and the same sources are
built on the host for the unit tests. These rules apply to all C code in `source/`, `tests/c/` and
`tools/*.c`; the header, license and comment rules apply to every file of code, Python included.
Code forked from other projects follows them where we touch it.

## Files and modules

- One module = one concern. A `.c` file over ~600 lines usually holds more than one; split it.
- Our modules use the `rd_` prefix (`rd_smoke.c`). Inherited modules keep their names
  (`so_util.c`, `jni_fake.c`, `nx_pointer.c`).
- Every file starts with a header comment:
  ```c
  /* rd_smoke.h -- optional lighter tyre smoke (config.txt smoke_size, smoke_amount).
   *
   * What the module does and why it exists, who calls it, which thread it runs on, and the
   * one or two facts a reader needs before the code makes sense.
   * Pure logic here (host-tested, tests/c/test_rd_smoke.c); the il2cpp glue in rd_smoke_hook.c. */
  ```
  A `.c` file whose header documents everything may start with `/* rd_x.c -- see rd_x.h. MIT (see LICENSE). */`.
- **Every file of code states its license** in its header comment -- C sources and headers,
  tests, Python tools, the Makefile, scripts, CI workflows. Our files say `MIT (see LICENSE).`
  (in Python: `# MIT (see LICENSE).` in the module docstring or header comment); forked files keep
  their upstream copyright notice and name the origin: author, repository URL and commit, license
  (see `nx_pointer.c`). `tools/doc_check.py` enforces it.
- **Pure logic and glue live apart.** Decisions, parsing, maths and tables go into a file that
  builds on the host without libnx (`rd_drive.c`, `rd_smoke.c`); the code that touches libnx, GL,
  il2cpp or the game goes into a thin `*_hook.c` / `*_gl.c` file next to it. The pure file gets a
  host test in `tests/c/`.

## Comments

- **Every function declared in a header has a comment** right above its declaration: what it
  does, what the arguments and return value mean, ownership of pointers, and the thread it may be
  called from when that matters. `tools/doc_check.py` enforces this.
- A `static` function gets a comment when its name and signature do not tell the whole story.
- Comment the *why*: the constraint, the Horizon/bionic/Unity quirk, the log line that proved it.
  Do not narrate what the next line obviously does.
- Group long files into sections with `/* ---- name ---- */` lines.

## Names and values

- Functions: `module_verb_object` (`rd_smoke_set_add`, `rd_fps_format`). Types: `rd_thing`
  (lower case, no `_t`). Macros and enum constants: `RD_UPPER_CASE`.
- **A named value lives in one place** and is never repeated as a literal:
  `rd_game.h` (game identity, file names), `rd_game_symbols.h` (C# and icall names),
  `rd_platform.h` (timing, cadences, sizes), `rd_device.h` (the fake Android device),
  `rd_settings.c` (config keys), `rd_log.h` (log tags), `rd_a64.h` (AArch64 encodings).
  A module's own limits go into its header (`RD_SMOKE_PERCENT_MIN`).
- No per-build offsets: patch sites are found by instruction pattern (`rd_sig.c`), C# members by
  name through the il2cpp API (`rd_il2cpp.h`).

## Language

- `static` for everything not used by another file; prototypes for everything that is, in the
  module's header (`-Wmissing-prototypes`).
- `(void)` for empty parameter lists; `const` for data a function does not modify; fixed-width
  integers (`uint32_t`) for sizes on the wire and in structures shared with the game's ABI.
- `bool`-like results are `int` 0/1 (the existing convention); errors are negative return values
  or `NULL`, explained in the function comment.
- No `#if 0` and no commented-out code: git keeps the history.
- Warnings are errors on both builds (`-Wall -Wextra -Wshadow -Wstrict-prototypes
  -Wmissing-prototypes -Werror`).
- Formatting: `.clang-format` (2 spaces, 100 columns). Align related initializers and table rows
  when it helps reading.

## Logging

- `debugPrintf("[tag] ...\n")`; a tag the `debug_log=0` filter needs to recognise is a
  `RD_LOG_TAG_*` in `rd_log.h`. Log once per event, not per frame, unless it is a `[perf]` line
  for a slow frame.

## Tests and coverage

- Every new pure module gets `tests/c/test_<module>.c`; its first line lists the sources it links:
  `/* sources: source/game/rd_smoke.c */`. Python tools get pytest tests in `tests/`.
- `make test` runs everything (with ASan/UBSan for C), `make lint` checks the Python; `make coverage` reports line coverage of the
  host-testable sources and of `tools/`, and CI fails below the recorded threshold.
