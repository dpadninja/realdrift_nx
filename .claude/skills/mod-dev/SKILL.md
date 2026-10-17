---
name: mod-dev
description: Writing, fixing or reviewing a Real Drift mod (mods/<id>/ with mod.ini, patch.ini, assets.ini, src/*.c -> mod.so). Use for any mod work instead of reading the port's sources.
---

# Writing a realdrift_nx mod

Read only these, in this order -- the port's sources are not needed:
1. `sdk/rd_mod.h` -- the whole API (every function commented).
2. `docs/MODDING.md` -- folder layout, mod.ini / patch.ini, which hook to use, limits.
   Unsure what a file can reach or how to find a name: `docs/MODDING_GUIDE.md` sections 4-7.
3. The example closest to the task in `mods/examples/` (hello: template, balance: patch.ini,
   retexture: assets, fov: inline hook, inspect: log live field values).

## Loop

```sh
uv run python -m tools.mod_new <id>                       # or copy the closest example
uv run python -m tools.il2cpp_find class <Class>          # fields + offsets, methods
uv run python -m tools.il2cpp_disasm <Class.Method>       # what a method reads and calls
uv run python -m tools.unity_ref icalls <UnityType>       # engine icalls: only "game" works
uv run python -m tools.mod_build mods/<id>                # -> mod.so
uv run python -m tools.mod_check mods/<id>                # must print "ok <id>"
uv run python tools/stage_sd.py --mods-only mods/<id> --out sd/switch/realdrift_nx
```

## Rules

- Data first: a field the game reads every frame -> `patch.ini`, no code.
- Art and texts -> `assets.ini` (`mod_assets list` prints ready lines); never commit `files/`
  built from the game.
- Engine function -> `icall_override` (only in `rd_mod_entry`); Unity message (Update, ...) ->
  `method_swap`; any other C# method -> `inline_hook(method_code(...))`.
- Class lookups only from `on_game_ready` on; keep pointers in statics.
- Hooked C# methods: C signature + trailing `MethodInfo*`; call `orig` to keep the game's work.
- Check every return value; log failures with `api->log` (one line, not per frame).
- C style of the port (`c-style` skill), MIT header in every file.
- Unsure whether something reaches the console? `mod_check`, then ask the user to test and
  send `debug.log` (`[mods]` and `[mod:<id>]` lines).
