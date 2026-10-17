---
name: port-review
description: Review checklist for changes to realdrift_nx - layering, comments, named values, no per-build offsets, tests and coverage, config table, attribution, research notes. Use when reviewing a diff or before declaring a change in this repository finished.
---

# Reviewing a realdrift_nx change

Go through the diff with these questions. Report each problem with file:line and the rule it breaks.

## Structure
- Is each new or changed file in the right layer (`port-architecture` skill)? Does any file
  include a header from a layer above it?
- Did a file grow past ~600 lines or gain a second concern? Suggest the split.
- Is pure logic separated from libnx / GL / il2cpp glue, and is the pure part host-tested?

## Readability (`docs/CODING_STYLE.md`)
- File header comment present, still true after the change, and stating the license
  (forked code: upstream copyright, author, repository)?
- Every function declared in a header has a comment (`uv run python tools/doc_check.py`)?
- Comments explain why; no stale comments describing old behaviour; no `#if 0` or dead code.
- Names follow `module_verb_object`; `static` where possible; `(void)`, `const`.

## Correctness traps of this port
- Literals that already have a name (`rd_game.h`, `rd_game_symbols.h`, `rd_platform.h`,
  `rd_device.h`, `rd_log.h`, `rd_a64.h`, the settings table)?
- Fixed offsets into libunity/libil2cpp or game objects instead of patterns / il2cpp names?
- Code called from engine threads touching state of the main thread without care?
- Newlib file calls from engine threads go through the locked wrappers?
- Icall wrappers keep the engine's calling convention (managed arguments only, `this` first)?

## Verification
- `make test` and the devkitPro build pass with `-Werror`; `make coverage` stays above the
  threshold; `make config-example` leaves `config.example.txt` unchanged unless a key changed.
- Borrowed code names its authors and repository; LICENSE / README credits updated if needed.
- Significant findings recorded in `analysis/research/*.md` with the command that reproduces them.
- Behaviour changes visible on the console were checked there, or the change says it was not.
