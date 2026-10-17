# Research tools

How to find out what the game and the engine do without running them on the console. The
tools are in `tools/`, run on the PC through uv, and work on the files in `analysis/`
(gitignored, extracted from your own APK). They default to the release the loader targets
(`rd_game.h`); `--release 5.0.7` picks the other one (`RELEASES` in `tools/game.py`).

| Question | Tool |
|---|---|
| Which classes, fields, field offsets and methods does the game have? | `il2cpp_find` |
| What does a C# method of the game do, instruction by instruction? | `il2cpp_disasm` |
| Which C# method is at this address of a crash log? | `il2cpp_find addr`, `il2cpp_symbolize --log` |
| What does a UnityEngine API do before it reaches native code? | `unity_ref find` |
| Which icall backs it, and can an `rd_icall` override reach it in this game? | `unity_ref icalls` |
| Does an instruction pattern match in both releases? | `make tools`, `build-host/find_patch_sites` |

MIT (see LICENSE).

## Setup (once)

Needs docker (for Il2CppDumper), git, `llvm-mc` (LLVM) and `original/base.apk`.

```sh
uv run python -m tools.il2cpp_dump      # -> analysis/dump508/ (dump.cs, script.json, ...)
uv run python -m tools.unity_ref fetch  # -> analysis/ref/UnityCsReference at the game's Unity tag
uv run python -m tools.il2cpp_find check  # the parsers agree with the raw files (see Self-checks)
uv run python -m tools.unity_ref check
```

`il2cpp_dump` extracts `libil2cpp.so` and `global-metadata.dat` from the APK if they are
missing, downloads [Il2CppDumper](https://github.com/Perfare/Il2CppDumper) (Perfare, MIT) into
`analysis/tools/il2cppdumper` with a pinned checksum, and runs it in the
`mcr.microsoft.com/dotnet/runtime:6.0` image. A complete dump is reused; `--force` dumps
again. `unity_ref fetch` makes a shallow clone of
[UnityCsReference](https://github.com/Unity-Technologies/UnityCsReference). That source is
under Unity's reference-only license: it stays in `analysis/` and is never copied into the
repository.

## il2cpp_find: the game's types

```sh
uv run python -m tools.il2cpp_find class Wheel            # fields + offsets, methods + RVAs
uv run python -m tools.il2cpp_find method Wheel.CalcSkidSmoke
uv run python -m tools.il2cpp_find grep 'skidSmoke|Smoke$'
uv run python -m tools.il2cpp_find addr 0x959700          # Wheel$$CalcSkidSmoke+0xe4
uv run python -m tools.il2cpp_find method System.Linq.Enumerable.Select   # generic: instances
uv run python -m tools.il2cpp_find method 'List.MoveNext'  # explicit interface implementation
```

- `class` prints every field with its offset in the object (what a hook reads through
  `rd_il2cpp` once it has the field by name) and every method with its RVA in `libil2cpp.so`.
  Names may carry the namespace (`UnityEngine.ParticleSystem`) or not, nested types use a dot
  (`CarDynamics.SurfaceType`), generic types match without their parameters (`List`).
- `method` accepts `Type.Method`, `Type::Method` and `Type$$Method` and lists all overloads.
  Generic parameters may be left out (`Select` finds `Select<TSource, TResult>`). A generic
  method has no code of its own: its shared instances are listed with their RVAs. An explicit
  interface implementation (`System.Collections.IEnumerator.MoveNext`) is found by its full
  name or its last part.
- Each result names its line in `dump.cs`, to read the surrounding declarations.

## il2cpp_disasm: a method body you can read

```sh
uv run python -m tools.il2cpp_disasm Wheel.CalcSkidSmoke
uv run python -m tools.il2cpp_disasm System.Linq.Enumerable.Select     # its generic instances
uv run python -m tools.il2cpp_disasm 0x95961c
uv run python -m tools.il2cpp_disasm Wheel.CalcSkidSmoke --callers     # who calls it (direct)
```

The method's code runs from its RVA to the next code start in `script.json` and is decoded
with `llvm-mc` (about 0.5 s a call; the first call on a new dump takes about a second longer
to build the helper table, cached as `helpers.json` in the dump folder). A generic method is
shown through its instances, at most 8; pass an address for another. Every line gets a note
when the tool can name what the instruction touches:

```
959680:  ldrb   w8, [x19, #0x3d0]                    ; this.isSkidSmoke
959684:  cbz    w8, 0x959da8 <+0x78c>
959690:  mov    x0, xzr
959694:  bl     0x72a97c                             ; il2cpp: throw NullReferenceException
9596a0:  bl     0x13af448                            ; UnityEngine.Transform$$get_position
9596a8:  ldr    x23, [x23, #0x758]                   ; UnityEngine.Vector3_TypeInfo
9598b0:  ldr    s8, [x8, #0x7dc]                     ; = 0.05
9597fc:  add    x20, x19, #0x40c                     ; &this.particle
959aac:  bl     0x6072a0                             ; memcpy@plt
```

- **Calls:** the C# method called, an exported function, or a PLT stub (`memcpy@plt`). A
  branch inside the method is shown as `<+offset>`. Code
  right after another method's `ret` is a small function the metadata does not list
  (`sub_168535c (unnamed, after ...)`). The struct property setters `Emit` uses are emitted
  like that.
- **`this`:** loads, stores and addresses relative to `this` get the field name, including
  fields of base classes and offsets inside struct fields (`smokePosition+0x4`). `this` is x0
  on entry and every register it is copied to. The tool follows branches and keeps a register
  at a join point only if every path into it agrees. Calls (`bl`, `blr`) clobber x0-x18.
  Static methods and shared generic code have no `this` fields.
- **Data:** `adrp` + `ldr`/`add` pairs show type and method metadata, string literals, C
  strings and float/double constants.
- **il2cpp runtime calls** are named from the instructions in front of their calls, counted
  over the whole library:
  - metadata initialisation (`ldr w0, [x]`);
  - the static-constructor check (`ldrb [x0, #...]` + `tbz`);
  - NullReferenceException (`mov x0, xzr`);
  - icall resolution (an icall name in x0).

  A function is named only when at least 3 of its call sites show one pattern and they are the
  majority, so it gets the same name in every listing. Anything else is `il2cpp runtime`.

An icall wrapper shows the exact name the engine registers:

```sh
uv run python -m tools.il2cpp_disasm ParticleSystem.Internal_EmitOld
# add  x0, x0, #0xc66  ; "UnityEngine.ParticleSystem::Internal_EmitOld(UnityEngine.ParticleSystem/Particle&)"
# bl   0x760570        ; il2cpp: resolve the icall named in x0 (engine function pointer)
```

## unity_ref: the engine's C# side and its icalls

```sh
uv run python -m tools.unity_ref find ParticleSystem.Emit
uv run python -m tools.unity_ref find 'UnityEngine.ParticleSystem::Internal_EmitOld'
uv run python -m tools.unity_ref icalls ParticleSystemRenderer
```

- `find` prints `file:line` and the declaration of every member with that name: managed code
  to read, or an `extern` (an icall). It also accepts an icall name from a log or from
  `rd_game_symbols.h`; `get_`/`set_` and `_Injected` are stripped to find the member.
- `icalls` lists a type's `extern` members with the name the engine registers them under
  (`Namespace.Class/Nested::Member`, `get_`/`set_` for properties, `_Injected` where the
  bindings generator passes structs by reference) and their status in this game:

| Status | Meaning |
|---|---|
| `game` | `libil2cpp.so` names it: the game's code calls it, the engine registers it, an `rd_icall` override can reach it |
| `engine` | only `libunity.so` has it: the engine has the function, but it is **not** registered in this game |
| `-` | neither library names it |

```
Runtime/ParticleSystem/ScriptBindings/ParticleSystemRenderer.bindings.cs:31: extern public float maxParticleSize
    engine UnityEngine.ParticleSystemRenderer::get_maxParticleSize
    engine UnityEngine.ParticleSystemRenderer::set_maxParticleSize
artifacts/generated/bindings_old/common/ParticleSystem/ParticleSystemBindings.gen.cs:3265: extern private void Internal_EmitOld (ref ParticleSystem.Particle particle)
    game UnityEngine.ParticleSystem::Internal_EmitOld
```

This is the check the first smoke attempt lacked: `maxParticleSize` exists in the engine, but
the game never calls it, so it is not registered and an override finds nothing.
`Internal_EmitOld` is the game's, so the smoke hook wraps it.

Limits:
- **`UnityEngine.AndroidJNI`:** the reference has only the non-Android stub (no externs), so
  its 146 icalls are not found. Every other icall the game uses is.
- **Editor code:** sources under `Editor/` are skipped, since that code never runs in the player.

## Self-checks

The parsers read text formats they do not own: Il2CppDumper's `dump.cs` and the C# of the Unity
reference. A format change would make things silently vanish from queries rather than fail.
So each has a check that compares the parse with counts taken straight from the raw files.
Run them after changing a parser, after updating a tool, or on a new game or Unity version:

```
$ uv run python -m tools.il2cpp_find check
ok       types                               5928 / 5928
ok       fields                             27411 / 27411
ok       methods                            40409 / 40409
ok       method names                       40409 / 40409
ok       code addresses in script.json      57350 / 57350
$ uv run python -m tools.unity_ref check
ok       files with unbalanced braces           0 / 0
ok       game icalls found (or stub)         1207 / 1207
         (146 icalls only as non-extern stubs in the reference)
```

- `il2cpp_find check` compares parsed types, fields and methods with `// TypeDefIndex`, field
  and `// RVA` lines of `dump.cs`. It also checks that every method name is a well-formed
  identifier and that every code address is named in `script.json`, which `il2cpp_disasm`
  names calls by. It exits 1 on any mismatch.
- `unity_ref check` lists player sources whose braces do not balance after blanking (the
  scanner gets lost there) and looks up every `UnityEngine.*` icall the game's libil2cpp names.
  An icall the reference has only as a plain member is a stub; one it does not have at all is
  printed as missing and fails the check.

Both checks pass on 5.0.8 and 5.0.7.

## Limits

What the tools can get wrong or cannot see, and how it shows:

| Tool | Limit | Effect |
|---|---|---|
| all | Checked on one game: Unity 2018.4, metadata 24.1 (both Real Drift releases) | Run the self-checks first on anything else |
| `il2cpp_dump` | Il2CppDumper 6.7.46 (its latest release) supports Unity 5.3 to 2022.2. It fails on encrypted or obfuscated metadata | No dump; nothing else works |
| `il2cpp_find` | Reads Il2CppDumper's text output, not the metadata | A changed format loses entries; `check` shows it |
| `il2cpp_find` | One level of nested generic parameters in method names (`Select<T, Func<T, bool>>`) | Deeper nesting: the method is missed; `check` counts it |
| `il2cpp_disasm` | arm64 only (`llvm-mc -triple=aarch64`) | Games that ship only `armeabi-v7a` are not supported |
| `il2cpp_disasm` | A method ends at the next known code start; code the metadata does not list (small accessors, compiler outlining) is folded into the method before it, unless it follows a `ret`/`b` | A listing can include such a tail |
| `il2cpp_disasm` | `this` and adrp tracking are intraprocedural and register-only: values spilled to the stack and reloaded are lost, and a `br` jump table ends the path | Fewer notes, not wrong ones |
| `il2cpp_disasm` | Field names come from the method's own type and its bases | In shared generic code `this` has no single type, so there are no field notes |
| `il2cpp_disasm` | Runtime helpers are recognised by the code shape of Unity 2018-2019 il2cpp (Unity 2020.2+ initialises metadata differently) | Unrecognised helpers stay `il2cpp runtime`; about 40 % of direct calls in 5.0.8 are such internals (exception throw, casts, array checks) |
| `il2cpp_disasm` | No pseudocode: an annotated listing, not a decompiler | For deep logic use Ghidra (Alternatives) |
| `unity_ref` | The reference is the managed half only; what the native side does is not in it | It ends at the `extern` |
| `unity_ref` | `UnityEngine.AndroidJNI` is published only as a non-Android stub | Its 146 icalls show as stubs |
| `unity_ref` | Sources under `Editor/` are skipped (never in the player); `#if` branches are all scanned | A member under a platform `#if` may appear twice |
| `unity_ref` | Status = string presence: `game` means libil2cpp names the icall (the game calls it, so it is registered). This matched every case tried on the console (8), not a proof | Treat `engine` as "not reachable"; a new hook still logs when `il2cpp_resolve_icall` finds nothing, as `rd_smoke_hook.c` does |
| `unity_ref` | The C# scanner is checked on the 2018.4 reference only | Other Unity versions: run `unity_ref check` |

## Recipes

**What does C# method X do, and where does it go next?**
1. `il2cpp_find method X`: overloads and RVAs.
2. `il2cpp_disasm X`: the calls it makes, the fields it reads, the constants it compares with.
3. For each `UnityEngine.*` call, `unity_ref find Type.Method` shows the engine's managed side
   down to its icall.

**Can we change engine behaviour Y with an icall override?**
1. `unity_ref find Type.Member` shows the C# code down to the `extern`.
2. `unity_ref icalls Type` shows the icall names and their status. Only `game` works with
   `rd_icall_override`.
3. If it says `engine`, look one level up: a `game` icall the same code path goes through
   (`il2cpp_disasm` on the caller).
4. Put the name into `source/game/rd_game_symbols.h`.

**What is this address in a log?**
- `il2cpp_find addr 0x...` for one RVA.
- `uv run python tools/il2cpp_symbolize.py analysis/dump508/script.json --log debug.log`
  annotates every `libil2cpp.so+0x...` of a log.
- Then `il2cpp_disasm` on the method.

**Does a patch pattern hold in both releases?** `make tools`, then
`build-host/find_patch_sites` and `tests/test_patch_sites.py`. The `RELEASES` table in
`tools/game.py` maps each release to its files.

## Alternatives

Most of this exists in some form; the tools here are the small, scriptable part that answers
quick questions from a terminal without a GUI project.

- **[Il2CppDumper](https://github.com/Perfare/Il2CppDumper) + Ghidra or IDA.** Il2CppDumper
  ships `ghidra_with_struct.py` / `ida_with_struct_py3.py` (in `analysis/tools/il2cppdumper`).
  They name every method, string and metadata slot and apply the `il2cpp.h` structs, so the
  decompiler shows C pseudocode with field names (`this->fields.squeal`). This is the strongest
  view of a method body. The cost is a Ghidra project: its first analysis of libil2cpp takes a
  long time and a lot of disk, and then each function is a click away (or a `analyzeHeadless`
  script call). Use it when the logic of a method matters; `il2cpp_disasm` is for "what does
  it call, which fields, which constants".
- **[Il2CppInspector](https://github.com/djkaty/Il2CppInspector)**: an alternative to
  Il2CppDumper (C++ scaffolding, IDA/Ghidra scripts). No commits since 2022.
- **[Cpp2IL](https://github.com/SamboyCoding/Cpp2IL)** 2022.1.0-pre-release.21 reads this game
  (metadata 24.1, arm64), but gives only an ISIL listing that shows less than `il2cpp_disasm`:
  `FCMP` is not implemented, data addresses are file-based, there are no field names. No C#
  pseudocode for arm64 at metadata 24.
- **[frida-il2cpp-bridge](https://github.com/vfsfitvnm/frida-il2cpp-bridge)**: tracing
  il2cpp methods live on an Android device. It needs the game running under Frida, not the
  Switch.
- **The icall status** (`unity_ref icalls`): no existing tool we know of says which engine
  icalls a game registers. That is the check the first smoke attempt lacked.
