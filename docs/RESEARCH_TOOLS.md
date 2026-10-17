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

Needs docker (for Il2CppDumper), git, `llvm-objdump` and `original/base.apk`.

```sh
uv run python -m tools.il2cpp_dump      # -> analysis/dump508/ (dump.cs, script.json, ...)
uv run python -m tools.unity_ref fetch  # -> analysis/ref/UnityCsReference at the game's Unity tag
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
```

- `class` prints every field with its offset in the object (what a hook reads through
  `rd_il2cpp` once it has the field by name) and every method with its RVA in `libil2cpp.so`.
  Names may carry the namespace (`UnityEngine.ParticleSystem`) or not, nested types use a dot
  (`CarDynamics.SurfaceType`), generic types match without their parameters (`List`).
- `method` accepts `Type.Method`, `Type::Method` and `Type$$Method` and lists all overloads.
  A generic method has no code of its own: its shared instances are listed with their RVAs.
- Each result names its line in `dump.cs`, to read the surrounding declarations.

## il2cpp_disasm: a method body you can read

```sh
uv run python -m tools.il2cpp_disasm Wheel.CalcSkidSmoke
uv run python -m tools.il2cpp_disasm 0x95961c
```

The method's code runs from its RVA to the next code start in `script.json`. Every line
gets a note when the tool can name what the instruction touches:

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

- **Calls:** the C# method called. A branch inside the method is shown as `<+offset>`. Code
  right after another method's `ret` is a small function the metadata does not list
  (`sub_168535c (unnamed, after ...)`). The struct property setters `Emit` uses are emitted
  like that.
- **`this`:** loads, stores and addresses relative to `this` (x0 on entry and the registers it
  is copied to) get the field name, including fields of base classes and offsets inside struct
  fields (`smokePosition+0x4`). Static methods and shared generic code have no `this` fields.
- **Data:** `adrp` + `ldr`/`add` pairs show type and method metadata, string literals, C
  strings and float/double constants.
- **il2cpp runtime calls** are named from the instructions in front of them:
  - metadata initialisation (`ldr w0, [x]`);
  - the static-constructor check (`ldrb [x0, #...]` + `tbz`);
  - NullReferenceException (`mov x0, xzr`);
  - icall resolution (an icall name in x0).

  These are pattern guesses; anything else is `il2cpp runtime`.

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

## Not used: Cpp2IL

[Cpp2IL](https://github.com/SamboyCoding/Cpp2IL) 2022.1.0-pre-release.21 reads this game
(metadata 24.1, arm64), but it gives only an ISIL listing with nothing more than
`il2cpp_disasm` shows:
- `FCMP` is not implemented;
- data addresses are file-based, not virtual;
- there are no field names.

There is no C# pseudocode for arm64 at metadata 24.
