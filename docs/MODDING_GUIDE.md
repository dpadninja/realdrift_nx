# Modding guide

How to change Real Drift with mods: what each file of a mod is for, how far it reaches, and how
to find what to change when you do not know the name of the thing yet. `docs/MODDING.md` is the
short reference; `sdk/rd_mod.h` is the code API.

MIT (see LICENSE).

## 1. How the game is put together

Knowing this makes everything below make sense.

- **The game's logic** is C# code, compiled ahead of time to native ARM code (Unity's IL2CPP)
  in `libil2cpp.so`. The C# classes still exist at run time with their names: `Drivetrain`,
  `Wheel`, `CarDynamics`... Each class has **fields** (values of an object: `limiterRPM`,
  `gear`) and **methods** (functions: `FixedUpdate`, `CalcValues`). You cannot add C# code,
  but you can change field values and replace methods.
- **Objects in a scene.** A scene (the menu, a track) is made of GameObjects carrying
  components. A component is an object of a class derived from `MonoBehaviour`: the car you
  drive has a `Drivetrain`, a `CarDynamics`, four `Wheel`s. Other classes are plain C# objects
  that only live inside those (`CarValue`, lists, save data).
- **The game's assets** (textures, texts, sounds, meshes, and the stored values of components)
  are in **Unity files** inside two zip archives: `base.apk` (menus, UI: the big
  `sharedassets0.assets`, stored cut into 41 parts) and the OBB (tracks, cars: ~1770 files
  named by GUIDs). A Unity file holds many objects; each object has a name and a type.
- **The port** loads your mods at boot, shows Unity the archives with the mods' files in them,
  sets patch.ini values on live objects, and runs mod code.

## 2. What each file of a mod changes

| File | Changes | Reaches | Needs code |
|---|---|---|---|
| `mod.ini` | nothing in the game: the mod's id, API version, load priority, own settings | -- | no |
| `cars.ini` | **a car's characteristics** by car name: engine curve, power, drive, gears, differential, weight, suspension, tires, brakes, assists | the car named, as the game reads each value | no |
| `patch.ini` | **numbers and switches** of the game's components: fields of MonoBehaviour objects | live objects of the named class, re-applied every second | no |
| `assets.ini` | **images and texts**: Texture2D and TextAsset objects, by name, in the APK or the OBB | every place the game draws that texture or reads that text | no |
| `files/` | whole archive members, byte for byte (advanced; see 5.4) | anything stored in a Unity file | no |
| `mod.so` (`src/*.c`) | **behaviour**: replace or wrap methods and engine functions, call the game's methods, draw text | anything the C# code or the engine does | yes (C) |

Pick the first row that fits: something about a car → cars.ini, another number → patch.ini, a picture or text → assets.ini, anything
else (logic, new behaviour, values patch.ini cannot reach) → code.

## 3. mod.ini

Every mod has one. The keys the port reads are in `docs/MODDING.md`; any other key is the
mod's own setting, read by its code with `api->ini(api, "key")` (see `mods/examples/fov`:
`scale = 1.2`). `priority` decides who wins when two mods change the same thing.

## 4. patch.ini: values of the game's components

### What it does

```ini
[Assembly-CSharp:Drivetrain]
limiterRPM = 9500
```

After every scene change, and again every second, the port finds every live object of class
`Drivetrain` (`UnityEngine.Object.FindObjectsOfType`) and writes 9500 into its `limiterRPM`
field. `Assembly-CSharp` is the game's own code; Unity's classes are in other assemblies
(`UnityEngine.CoreModule:UnityEngine.Camera`).

### Where it stops

- **Only MonoBehaviour classes** (components) and other `UnityEngine.Object`s. `il2cpp_find
  class X` shows the base class: `class Drivetrain : MonoBehaviour` works; `class CarValue`
  (no base) is a plain C# object and is never found -- change it with code.
- **Every object of the class** gets the value -- every car in the scene, every copy of a menu
  panel -- unless the section names objects: `[Assembly-CSharp:CarController @ Skyline*]`
  writes only into objects whose name matches (`*` any run of characters, `?` one, case
  matters). A component's name is the name of its GameObject, usually the prefab's name plus
  `(Clone)` for spawned ones; section 7.4 shows how to see the real names.
- **Only fields of type** bool, int, uint, long, float, double. Not enums, strings, vectors,
  colours, references or arrays.
- **Only instance fields** (not `static`), including fields declared in a base class.
- **The value must matter when it is read later.** If the game reads the field every frame
  (physics, input), the change works at once. If the game reads it once in `Awake`/`Start` and
  computes other values from it, the change comes too late and does nothing -- patch the
  computed field instead, or call the method that recomputes it from code.
- **The game may write it back.** If one of the class's methods stores into the field every
  frame, your value is overwritten until the next second. Section 7 shows how to see that.
- **Timing**: an object created in the middle of a scene runs with the original value for up
  to one second.

### cars.ini: one car, by its name

A car's characteristics the way a player thinks of them -- which car, which value. Every key
is one of the game's own values (a field, an enum or a method of the car's components), in
the game's units and names; derived values are computed by the game's own methods.

```ini
[E30M]
power = 197          # hp, stock peak
torque_curve = 0:0 2000:190 4500:240 6500:200 7500:0
weight = 1200
rear_diff = two_way_LSD
rear_tire_pressure = 2.0
gear_1 = 3.6
```

- **Section** = a car name, or `[*]` for every car (a car's own section wins over `[*]`). The
  names (`240Z`, `E30M`, `LadaVTS`, `Skyline`, ...): `uv run python -m tools.mod_assets cars`.
  `mod_check` refuses a name the game does not have.
- **Which objects**: every model of the car -- the race car `E30M(Clone)`, the garage model
  `E30M_Garage(Clone)`, the menu model `E30M_cover` -- and the components under it (its
  wheels). So the garage specs show the values too.
- **The engine curve**: the cars take their torque from a curve, rpm -> Nm
  (`Drivetrain.torqueCurve`). `uv run python -m tools.mod_assets curves E30M` prints the car's
  own curve as a `torque_curve =` line to start from. The curve's shape is `torque_curve`, its
  height `power`. Neither `rev_limit` nor `max_rpm` changes the curve:
  - `rev_limit` (`limiterRPM`) is where the revs stop: at it the game cuts the throttle for
    the car's limiter time (a bouncing limiter) or, with no limiter time, holds the revs there;
  - `max_rpm` (`maxRPM`) is not a cut: the engine's internal friction grows with rpm / maxRPM
    (a lower `max_rpm` brakes the engine more at the same revs), and the game looks for the
    peak power and torque it shows only between `idle_rpm` and `max_rpm`.

#### When a value is written

The port writes each key when the game reads it:

- **build** -- the car's originals. The garage reads them from the car model each time a car
  is picked (`MenuManager.LoadOriginalValues`), the race when it builds the car
  (`RaceManager.UpdateVehicle`); the port writes them into the model just before. The game
  then applies the player's tuning on top (weight reduction, shift time, suspension height,
  camber, steering angle, weight distribution, tires, gear ratios), as for a stock car.
- **once** -- written when a car is built and on each scene change; the game never rewrites them.
- **live** -- read while driving; written every second when it differs. Axle values are
  passed to the wheels by the game's `SetWheelsParams` / `SetBrakes` / `SetTiresType`.

#### cars.ini keys

| Key | Value | When | The game's value |
|---|---|---|---|
| `power` | hp at the curve's peak, stock (the player's engine tuning adds on top): scales the whole curve | once | `Drivetrain.powerMultiplier`, from `CalcMaxPowerMaxTorque` |
| `torque_curve` | 2 to 32 `rpm:Nm` pairs, rpm rising (`curves` prints the car's own) | once | `Drivetrain.torqueCurve` via `AnimationCurve.SetKeys` + `SmoothTangents` |
| `turbo` | 0 / 1 | live | `Drivetrain.turboCharged` |
| `turbo_bar` | bar | live | `Drivetrain.maxTurboPressure` (+ the limit `Awake` derives) |
| `rev_limit` | rpm: where the revs stop | live | `Drivetrain.limiterRPM` |
| `max_rpm` | rpm: the scale of engine friction, the end of the range the specs' peak is looked for in | live | `Drivetrain.maxRPM` |
| `idle_rpm` | rpm | live | `Drivetrain.minRPM` |
| `drive` | `RWD`, `FWD`, `AWD`, `XWD` | live | `Drivetrain.transmission` + `SetTransmission` |
| `shift_time` | s | build | `Drivetrain.shiftTime` |
| `final_ratio` | ratio | build | `Drivetrain.finalDriveRatio` |
| `gear_1` `gear_2` `gear_3` `gear_4` `gear_5` `gear_6` | ratio | build | `Drivetrain.gearRatios` |
| `front_diff`, `rear_diff` | `open`, `viscous`, `locked`, `one_way_LSD`, `two_way_LSD`, `one_point_five_way_LSD` | live | `Axle.differential` |
| `front_diff_preload`, `rear_diff_preload` | Nm | live | `Axle.preLoad` |
| `front_diff_power`, `rear_diff_power` | LSD ratio on power | live | `Axle.powerRatio` |
| `front_diff_coast`, `rear_diff_coast` | LSD ratio off power | live | `Axle.coastRatio` |
| `weight` | kg | build | the car's `Rigidbody.mass` |
| `weight_distr` | centre of mass, front-rear | build | `CarDynamics.zlocalPosition` |
| `susp_height` | suspension travel, both axles | build | `Axle.suspensionTravel` |
| `front_camber`, `rear_camber` | degrees | build | `Axle.camber` |
| `steering_angle` | degrees | build | front `Axle.maxSteeringAngle` |
| `front_spring`, `rear_spring` | spring rate | live | `Axle.suspensionRate` |
| `front_bump`, `rear_bump`, `front_rebound`, `rear_rebound` | damper rates | live | `Axle.bumpRate`, `reboundRate` |
| `front_antiroll`, `rear_antiroll` | anti-roll bar rate | live | `Axle.antiRollBarRate` |
| `front_grip`, `front_side_grip`, `rear_grip`, `rear_side_grip` | factor | build | `Axle.forwardGripFactor`, `sidewaysGripFactor` |
| `front_tires`, `rear_tires` | a `CarDynamics.Tires` name: `sport_front`, `competition_rear`, `supersport_front`, `touring_rear`, ... (`uv run python -m tools.mod_ini keys` lists all) | live | `Axle.tires` + `SetTiresType` |
| `tire_pressure` | 0 / 1: the game's tire pressure physics | live | `CarDynamics.tirePressureEnabled` |
| `front_tire_pressure`, `rear_tire_pressure` | bar | live | `Axle.tiresPressure` |
| `front_brake_torque`, `rear_brake_torque`, `front_handbrake_torque`, `rear_handbrake_torque` | Nm | live | `Axle.brakeFrictionTorque`, `handbrakeFrictionTorque` + `SetBrakes` |
| `brake_balance`, `handbrake_balance` | front share | live | `CarDynamics.frontRearBrakeBalance`, `frontRearHandBrakeBalance` |
| `tcs`, `esp`, `abs` | 0 / 1 | live | `CarController.TCS`, `ESP`, `ABS` |
| `tcs_slip`, `esp_strength`, `abs_slip` | the assist's setting | live | `TCSAllowedSlip`, `ESPStrength`, `ABSAllowedSlip` |

**Anything else** -- any field of the car's components works as a key too: a field of
`CarController`, `Drivetrain`, `CarDynamics` or `CarSpecs` by its name (`maxPowerRPM`), any
other component of the car as `Class.field` (`Wheel.gripMaterial`), and a name two of them
share as `Class.field` (`mod_check` says when). These are written every second and follow
"Where it stops" above. Look at the current values with `inspect` (section 7.4) first.

#### What is not reachable

- **The number of gears**: the length of the car's `gearRatios` array; the game's gear screen
  has six.
- **Gear ratios in the race**: `RaceManager.UpdateVehicle` takes them from the player's gear
  tuning saved per car (`Vehicle.customizations`), not from the originals; so `gear_N` and
  `final_ratio` reach the garage and the specs, and the race only through that tuning (not
  checked on the console yet).
- **The tire model's coefficients** (`Wheel.a1`, `b1`, `c`: arrays), **wheel size** (the
  game's tuning has none; `Wheel.radius` and `width` are read once at start-up) and
  **aerodynamics** (only as `Class.field` of its components).

### Find the class and the field

1. Guess words and search the names: `uv run python -m tools.il2cpp_find grep 'rpm|limiter'`
   prints every class, field and method whose name matches, with its line in the dump.
2. Look at the class: `il2cpp_find class Drivetrain` shows each field with its offset and C#
   type (`0xc4  public float limiterRPM`) and each method. Check the first line for the base
   class.
3. Check who reads and writes the field (section 7).
4. Check the current value on the console with the `inspect` example (section 7.4).
5. Write patch.ini; `uv run python -m tools.mod_check mods/<id>` checks class, field and type.

## 5. assets.ini: images and texts

### What it does

```ini
MainMenu_Options = art/options.png       # an image, same size as the original
GSIlogo          = art/logo.png          # this one is in base.apk -- it does not matter
SomeConfig       = text/config.txt       # a text asset, any bytes
```

Each line puts a file of your mod into the game object with that name. You do not need to know
whether it lives in the APK or the OBB, which Unity file holds it, or that the file is cut into
parts: the tools find all of that by the name. When you stage the mods
(`stage_sd.py --mods`), the Unity files are rebuilt from your own `base.apk` / OBB with every
replacement of every mod in them and put into the SD copy. The mod folder keeps only the list
and your art.

### Find the name

```sh
uv run python -m tools.mod_assets list --name 'Menu' --type Texture2D
```
```
MainMenu_Chrono = <file>    # apk sharedassets0.assets 136 Texture2D MainMenu_Chrono 230x96 RGB24
MainMenu_Options = <file>    # obb e3dd51a5... 1 Texture2D MainMenu_Options 230x96 RGB24
# apk sharedassets0.assets 622 Sprite MainMenu_Chrono   (not editable)
```

Copy a line, replace `<file>`. The comment says where it is (apk/obb), its size and format. A
name used by more than one texture is printed as `name@<file>` -- keep the `@` part. Then get
the original to paint over: `uv run python -m tools.mod_assets export MainMenu_Chrono chrono.png`.

### Where it stops

- **Texture2D and TextAsset** only. A **Sprite** (a cut-out of a texture) changes when you
  change its texture; replace the texture and keep the layout of the atlas. Meshes, sounds,
  materials and scene objects cannot be replaced by assets.ini.
- **Same size** as the original. The format is kept (RGB24, ETC2, ...): your PNG is encoded
  into it.
- Names come from the game; several textures with one name need `@<file>`.
- Two mods may change different textures of one Unity file (they are merged); the same texture
  in two mods: the higher priority wins.
- Building needs the realdrift_nx tools and your own game files on the PC (`uv run ...`).

### 5.4 files/: whole archive members (advanced)

`files/apk/<member path>` and `files/obb/<member path>` replace (or add) whole members of the
archives, byte for byte -- for things assets.ini cannot express. Such a file contains the
game's data: make it from your own game files and never publish it. assets.ini and `files/`
cannot both change the same member.

## 6. mod.so: behaviour

When the change is logic: what happens when, new behaviour, values patch.ini cannot reach. You
write C against `sdk/rd_mod.h`, copy the closest example and change it. The flow:

- `rd_mod_entry` runs at boot: register callbacks, and `icall_override` for engine functions.
- `on_game_ready`: look up classes, fields and methods by name; install hooks.
- `on_scene` / `on_frame`: work on live objects (`find_objects`, fields at `obj + offset`).

Three kinds of hooks (details in `docs/MODDING.md`):
- `icall_override` replaces an **engine** function C# calls (`UnityEngine.Camera::set_fieldOfView`);
- `method_swap` replaces a **Unity message** (`Update`, `Start`) or anything Unity calls by
  MethodInfo;
- `inline_hook` replaces **any compiled method** for all callers (example `fov`).

## 7. When you do not know where something is

The tools below read your copy of the game on the PC. Run `uv run python -m tools.il2cpp_dump`
once first (and `uv run python -m tools.unity_ref fetch` for section 7.3).

### 7.1 Find candidates by name

```sh
uv run python -m tools.il2cpp_find grep 'smoke|skid'       # classes, fields, methods
uv run python -m tools.il2cpp_find class Wheel             # one class: fields + methods
uv run python -m tools.mod_assets list --name 'Logo'       # textures and texts
```

Names are the developers' own, often English, sometimes abbreviated: try synonyms (`rpm`,
`rev`, `limiter`; `grip`, `friction`, `slip`).

### 7.2 See what a method does

```sh
uv run python -m tools.il2cpp_disasm Drivetrain.DoRevlimiter
```
```
96e064:  ldr    s0, [x19, #0x1d8]     ; this.rpm
96e068:  ldr    s1, [x19, #0xc4]      ; this.limiterRPM
96e074:  ldrb   w8, [x19, #0xc8]      ; this.revLimiter
```

You do not need to read ARM assembly; read the notes after `;`:
- `ldr ... ; this.X` reads field X; `str ... ; this.X` **writes** it;
- `bl ... ; Class$$Method` calls a method; `= 0.4` is a constant the code compares with;
- `"..."` is a text the code uses.

To decide whether a patch.ini value will stick, run the class's frame methods (`Update`,
`FixedUpdate`, `LateUpdate`) and the setup ones (`Start`, `Awake`, `Calc...`) and look for
the field: read every frame and never written there = patch.ini works. For `limiterRPM`:
`DoRevlimiter` and `FixedUpdate` read it, `CalcValues` writes it -- so a patch holds except
right after `CalcValues` runs, and the next second puts it back.

Who calls a method:
```sh
uv run python -m tools.il2cpp_disasm Wheel.CalcSkidSmoke --callers
```
```
Wheel$$CalcSkidSmoke: 1 direct callers
    95928c  Wheel$$Update+0xec
```
Zero direct callers (as for `Drivetrain.CalcValues`) means the method is called indirectly --
by Unity as a message, through a delegate or a UI event -- or never.

### 7.3 Engine functions

```sh
uv run python -m tools.unity_ref find Camera.fieldOfView    # Unity's C# source of it
uv run python -m tools.unity_ref icalls Camera              # which engine functions exist
```
Only icalls marked `game` can be overridden: the game uses them, so the engine registered
them. `engine` ones exist but are not reachable in this game.

### 7.4 See the values on the console

The `inspect` example logs fields of live objects to `debug.log` on every scene change and
every 10 seconds:

```ini
class = Assembly-CSharp:Drivetrain
fields = limiterRPM:float maxRPM:float gear:int
objects = 2
```
```
[mod:inspect] <scene name>: <how many> objects
[mod:inspect]   #0 "<object name>"
[mod:inspect]   #0 limiterRPM = <value>
```

Each object is logged with its name (`#0 "Skyline(Clone)"`) -- the name a patch.ini
`@ filter` matches. Use it to learn the names and the original values before patching, and
to check that a patch arrived.

### 7.5 Deeper

For the logic of a long method, load `libil2cpp.so` into Ghidra with Il2CppDumper's
`ghidra_with_struct.py` (in `analysis/tools/il2cppdumper/`): it shows C-like code with field
names. `docs/RESEARCH_TOOLS.md` describes every tool.

## 8. On the console

`debug.log` (next to `config.txt`) has the mod lines also with `debug_log = 0`:

| Line | Means |
|---|---|
| `[mods] loaded <id> 1.0 (api 1): mod.so, 1 patch values, 0 apk + 1 obb files` | the mod is loaded, with what |
| `[mods] <id>: ..., skipped` | mod.ini problem, wrong API version, or a failing mod.so: the reason is on the line |
| `[mods] <id>: patch.ini:3: no field ...` | a patch value could not be resolved (class, field, type) |
| `[mods] main.76...obb: 1 files replaced, 0 added` | Unity sees the archive with the mod's files |
| `[mods] scene <name>` | a scene change (patches applied, on_scene called) |
| `[mod:<id>] ...` | the mod's own log |
| `[hook] ... -> ...` | an inline hook was installed |

Nothing at all: is `mods = 1` in `config.txt`, and is the folder `mods/<id>/mod.ini` next to it?
