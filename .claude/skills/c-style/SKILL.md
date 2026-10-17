---
name: c-style
description: Rules for writing or editing C in realdrift_nx (source/, tests/c/, tools/*.c) - modern C11, module and function comments, named values, pure/glue split, warnings as errors. Use before adding or changing any C code in this repository.
---

# Writing C in realdrift_nx

The full rules are in `docs/CODING_STYLE.md`; read it once per session before editing C. The
checklist below is what reviews catch most often.

## Before you write

1. Find the layer the code belongs to (`docs/ARCHITECTURE.md`, or the `port-architecture` skill).
2. Look for an existing helper first: `rd_il2cpp.h` (il2cpp API, fields, arrays, strings),
   `rd_icall.h` (icall overrides and wrappers), `rd_sig.h` / `rd_patchsites.h` (instruction
   patterns), `rd_a64.h` (encodings), `rd_overlay.h` (drawing on top of the frame),
   `rd_platform.h` (`rd_now_ns`, frame cadences).
3. Decide what is pure logic (host-testable, no libnx/GL/il2cpp) and what is glue.

## While you write

- File header comment: purpose, callers, thread, the facts a reader needs, and the license
  (`MIT (see LICENSE).` for ours). Forked code: origin author + repository URL + commit +
  upstream copyright and license. Every file of code: C, Python, Makefile, scripts, CI.
- A comment above every function declared in a header (what / arguments / return / ownership /
  thread). Comment `static` functions whose name does not say it all. Explain *why*.
- `static` unless another file needs it; then a prototype in the module header.
- `(void)` for no parameters, `const` where nothing is modified, `uint32_t`-style types for ABI data.
- No literal that already has a name: game identity `rd_game.h`, C#/icall names
  `rd_game_symbols.h`, timing/sizes `rd_platform.h`, device `rd_device.h`, config keys
  `rd_settings.c` table, log tags `rd_log.h`.
- No per-build offsets: instruction patterns (`rd_sig.c`) or names through il2cpp.
- No `#if 0`, no commented-out code.
- Log with `debugPrintf("[tag] ...")`, once per event.

## Before you finish

- New pure module → `tests/c/test_<module>.c` with `/* sources: ... */` on line 1; a comment
  above every test function: the case it checks and why it matters.
- `make test` (host, ASan/UBSan, `-Werror`), the devkitPro build (`-Werror`), `make coverage` above
  the threshold, `uv run python -m tools.doc_check` clean.
- A new `config.txt` key goes into the `rd_settings.c` table; run `make config-example`.
