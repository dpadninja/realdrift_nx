# Modding

A mod is a folder in `mods/` next to `config.txt` on the SD card. The port loads every enabled
mod at boot; `mods = 0` in `config.txt` starts the original game. A broken mod is skipped with
a log line, never the game. The API is `sdk/rd_mod.h`: read it, it is short.
**New to this? Read `docs/MODDING_GUIDE.md`** -- what each file changes, its limits, and how
to find classes, fields and textures with the reverse tools. This page is the reference.

MIT (see LICENSE).

## A mod folder

```
mods/<id>/
  mod.ini      required: id, api, and the mod's own keys
  cars.ini     optional: car characteristics by car name
  patch.ini    optional: values for fields of any of the game's objects
  assets.ini   optional: game textures / texts replaced by the mod's files, by name
  art/         the files assets.ini names (stays on the PC, like src/)
  files/       advanced: whole archive files (files/apk/<member>, files/obb/<member>)
  mod.so       optional: native code, built from src/*.c
  src/         the mod's C sources (stays on the PC)
```

## Workflow

```sh
uv run python -m tools.mod_new mymod                     # mods/mymod/ from the hello example
uv run python -m tools.il2cpp_find class Drivetrain      # find classes, fields, methods
uv run python -m tools.unity_ref icalls Camera           # engine icalls, registered or not
uv run python -m tools.mod_build mods/mymod              # src/*.c -> mod.so (docker)
uv run python -m tools.mod_check mods/mymod              # everything the console would refuse
uv run python tools/stage_sd.py --mods-only mods/mymod --out sd/switch/realdrift_nx
```

On the console, `debug.log` has `[mods]` lines from the host and `[mod:<id>]` lines from the
mods (kept with `debug_log = 0` too):
`[mods] loaded mymod 0.1 (api 1): mod.so, 1 patch values, 0 apk + 1 obb files`.

## mod.ini

`key = value` lines, `#` comments (on their own line, or after a value: `x = 1  # note`). Keys the port reads (any other key is the mod's own, read
with `api->ini`):

| Key | Required | Meaning |
|---|---|---|
| `id` | yes | the folder name: `a-z 0-9 _ -`, up to 32 |
| `name` | no | shown in the log; defaults to `id` |
| `version` | no | the mod's own version, free text |
| `api` | yes | `RD_MOD_API_VERSION` the mod was built for; a newer one is refused |
| `enabled` | no | `0` skips the mod (default `1`) |
| `priority` | no | higher wins when two mods replace the same file or icall (default `0`) |

Mods load in priority order (highest first, then by id); the first to claim a file or an icall
keeps it and the other is logged.

## cars.ini

A car's characteristics by its name -- no classes, no patterns:

```ini
[*]                 # every car (a car's own section wins)
tcs = 0
[E30M]              # names: uv run python -m tools.mod_assets cars
power = 197         # hp; also torque_curve, drive, gear_1, rear_diff, weight, ... (the guide)
Wheel.gripMaterial = 1.1
```

- Keys: the table in `docs/MODDING_GUIDE.md` ("cars.ini keys"; `uv run python -m tools.mod_ini keys`
  prints it). Each is one of the game's own values, written when the game reads it: the car's
  originals right before the game builds the car (garage, race start; the player's tuning
  applies on top), the engine once per car, the rest every second.
- Any other key is a field of `CarController`, `Drivetrain`, `CarDynamics` or `CarSpecs`, or
  `Class.field` of any component of the car or its children.
- Every model of the car is changed (race car, garage and menu models): the specs screen
  shows the values. `mod_assets curves <car>` prints a car's torque curve.

## patch.ini

One section per class: `[Assembly:Namespace.Class]` (the game's classes have no namespace,
nested classes are `Outer/Inner`), then `field = value`:

```ini
[Assembly-CSharp:Drivetrain]
limiterRPM = 9500

[Assembly-CSharp:CarController @ Skyline*]   # only objects named like this
ESPStrength = 0.3
```

- `@ pattern` limits a section to objects whose name matches (`*` any run, `?` one
  character, case-sensitive). A component's name is its GameObject's (`Skyline(Clone)`);
  the `inspect` example logs them. Without `@`, every object of the class.
- Field types: `bool` (0/1/true/false), `int`, `uint`, `long`, `float`, `double`. Enums,
  strings and references are refused.
- Values go into every live object of the class after each scene change and again once a
  second (objects created later). A value the game computes once in `Awake`/`Start` from
  other fields does not change behaviour; patch what the game reads every frame
  (`il2cpp_disasm` shows what a method reads).
- `mod_check` checks classes, fields and types against the dump.

## Replacing assets

`assets.ini` names what to replace; the mod ships only its own files:

```ini
MainMenu_Options = art/options.png     # Texture2D: a PNG of the same size
SomeConfig       = text/config.txt     # TextAsset: any bytes
Logo@e3dd51a5... = art/logo.png        # name@member when the name is not unique
```

```sh
uv run python -m tools.mod_assets list --name 'MainMenu_' --type Texture2D   # ready lines
uv run python -m tools.mod_assets export MainMenu_Options options.png         # the original
```

`stage_sd.py --mods` builds the Unity files from your own base.apk / OBB into the SD copy
(`mods/<id>/files/...`); nothing derived from the game is kept in the mod. Replacements of all
staged mods in one Unity file are merged into one file; only the same object in two mods is a
conflict (the higher priority wins, the other is warned). Split members (`*.splitN`) cannot
be changed. `mod_check` checks names, files and texture sizes.

`files/` holds whole archive members for what assets.ini cannot express; such a file
contains the game's data, so build it from your own files and never publish it.

## Code (mod.so)

`src/*.c` built with `sdk/mod.mk` (devkitA64, `-nostdlib`): libc functions are resolved from
the port at load time, everything else comes through the `rd_mod_api` table. The entry point:

```c
#include "rd_mod.h"
RD_MOD_ENTRY {                  /* int rd_mod_entry(const rd_mod_api *m, void **user) */
  m->on_game_ready(m, ready);   /* register callbacks and icall overrides here */
  return 0;                     /* anything else: the mod is skipped */
}
```

Lifecycle: `rd_mod_entry` runs at boot before Unity starts (register `on_game_ready`,
`on_scene`, `on_frame`, `icall_override`); `on_game_ready` once the game's C# classes resolve;
`on_scene` after each scene change; `on_frame` every frame. All on the main thread.

Finding things: `find_class`, `find_nested`, `field`, `method`, `method_code`, `invoke`, `new_object`,
`new_string`, `string_eq`, `find_objects`, `object_name`, `icall`. Logging and settings: `log`, `ini`,
`id`, `dir`. Screen: `draw_text` (3x5 font, 720p coordinates). Versioning: `version`, `size`.

### Which hook

| You want to change | Use | Reaches |
|---|---|---|
| an engine function C# calls (`UnityEngine.X::Y`) | `icall_override` (in `rd_mod_entry`) | every call; check the icall is `game` in `unity_ref icalls` |
| a Unity message or anything run by MethodInfo (`Update`, `Start`, coroutines, `invoke`) | `method_swap` | calls made through the MethodInfo only |
| any compiled method, including direct calls between C# methods | `inline_hook` on `method_code(method)` | every caller |

A swapped or hooked C# method has its C signature plus a trailing `MethodInfo*`
(`void fn(void *self, float value, void *mi)`); `orig` calls the original. `inline_hook`
refuses tiny and tail-calling functions (about a quarter of the game's methods are such
stubs): hook the function they jump to.

## Examples (`mods/examples/`)

| Mod | Shows |
|---|---|
| `hello` | the template: `ini`, `log`, `on_scene`, `on_frame`, `draw_text` |
| `cars` | a data-only mod: `cars.ini` sets assists and rev limiters per car |
| `balance` | a data-only mod: `patch.ini` raises the rev limiter |
| `retexture` | an asset mod: `assets.ini` gives the Options button new art |
| `fov` | `inline_hook` on `UnityEngine.Camera.set_fieldOfView`, scaled by a mod.ini key |
| `inspect` | a research aid: logs the values of chosen fields of live objects |

## Limits

- No new C# code: IL2CPP has no JIT. New behaviour is C calling the game through il2cpp.
- No new Unity content: AssetBundle is stripped from the game. New content means changing or
  cloning existing objects (`find_objects`, `invoke`) and replacing assets.
- A native mod runs in the game's process: a bug in it can crash the game. `mod_check` catches
  what can be caught before the console.
- `inline_hook` patches code while the game runs; hook in `on_game_ready` (the main thread,
  before the method is busy). Up to 256 hooks in all.
- Two mods cannot replace the same object, raw file or icall (the higher priority wins).
- Up to 32 mods, 512 replaced files, 256 patch and car values, 64 icall overrides, 32 text lines a frame.
