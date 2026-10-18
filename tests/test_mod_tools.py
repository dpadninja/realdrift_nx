"""tools/mod_new.py, mod_build.py, mod_check.py and stage_sd.py --mods: the mod workflow.

MIT (see LICENSE).
"""

import shutil
import subprocess
import zipfile
from pathlib import Path

import pytest

from tools import hostc, mod_build, mod_check, mod_ini, mod_new, stage_sd
from tools.game import ROOT, target_release, user_archives
from tools.il2cpp_find import parse_dump

EXAMPLES = ROOT / "mods" / "examples"
needs_cc = pytest.mark.skipif(hostc.CC is None, reason="no host C compiler")


# ---- mod_new ------------------------------------------------------------------------------


def test_ini_keys_come_from_the_c_table():
    """mod.ini keys are read from the port's RD_MOD_INI_KEYS table, so the template and docs
    cannot drift from what the console parses.
    """
    keys = mod_new.ini_keys()
    assert [k for k, _, _ in keys] == ["id", "name", "version", "api", "enabled", "priority"]
    assert {k for k, req, _ in keys if req} == {"id", "api"}
    assert mod_new.api_version() >= 1


def test_create_writes_ini_and_code(tmp_path):
    """A new mod gets every key with its help comment and the hello template as code."""
    mod = mod_new.create("mymod", tmp_path, code=True)
    ini = (mod / "mod.ini").read_text()
    assert "id = mymod" in ini and f"api = {mod_new.api_version()}" in ini
    assert "# priority:" in ini
    assert (mod / "src" / "main.c").read_text().startswith("/* mymod --")
    data = mod_new.create("data", tmp_path, code=False)
    assert not (data / "src").exists()


@pytest.mark.parametrize("bad", ["Upper", "has space", "x" * 33, ""])
def test_create_refuses_bad_ids_and_existing(tmp_path, bad):
    """Ids the port would refuse are refused here, and an existing folder is never overwritten."""
    with pytest.raises(ValueError):
        mod_new.create(bad, tmp_path, code=False)
    mod_new.create("ok", tmp_path, code=False)
    with pytest.raises(ValueError, match="exists"):
        mod_new.create("ok", tmp_path, code=False)


def test_mod_new_main(tmp_path, capsys):
    """The command line prints the new folder, or one error line."""
    assert mod_new.main(["x1", "--dir", str(tmp_path)]) == 0
    assert capsys.readouterr().out.strip() == str(tmp_path / "x1")
    assert mod_new.main(["x1", "--dir", str(tmp_path)]) == 1
    assert "exists" in capsys.readouterr().err


def test_api_version_needs_the_define(tmp_path):
    """A header without RD_MOD_API_VERSION is an error, not version 0."""
    h = tmp_path / "rd_mod.h"
    h.write_text("/* nothing */\n")
    with pytest.raises(ValueError):
        mod_new.api_version(h)


# ---- mod_build ----------------------------------------------------------------------------


def test_build_command_mounts_mod_and_sdk(tmp_path):
    """The container sees the mod at /mod and the SDK read-only at /sdk and runs sdk/mod.mk as
    the caller's user.
    """
    cmd = mod_build.docker_command(tmp_path, 1000, 100)
    assert cmd[:5] == ["docker", "run", "--rm", "-u", "1000:100"]
    assert f"{tmp_path.resolve()}:/mod" in cmd and f"{mod_build.SDK}:/sdk:ro" in cmd
    assert cmd[-3:] == ["make", "-f", "/sdk/mod.mk"]


def test_build_main(tmp_path, monkeypatch, capsys):
    """A folder without src/ is refused; a failing docker build is reported."""
    assert mod_build.main([str(tmp_path)]) == 1
    (tmp_path / "src").mkdir()
    monkeypatch.setattr(
        mod_build.subprocess, "run", lambda cmd, check: subprocess.CompletedProcess(cmd, 2)
    )
    assert mod_build.main([str(tmp_path)]) == 1
    monkeypatch.setattr(
        mod_build.subprocess, "run", lambda cmd, check: subprocess.CompletedProcess(cmd, 0)
    )
    assert mod_build.main([str(tmp_path)]) == 0
    assert capsys.readouterr().out.strip().endswith("mod.so")


# ---- mod_check ----------------------------------------------------------------------------

DUMP = """\
// Namespace:
public class Drivetrain : MonoBehaviour // TypeDefIndex: 1
{
\t// Fields
\tpublic float limiterRPM; // 0xC4
\tpublic Drivetrain.EngineTypes engineType; // 0x18
\tpublic static int count; // 0x0
}
"""


def write_mod(root: Path, folder: str, ini: str, patch: str | None = None) -> Path:
    """A mod folder with the given mod.ini (and patch.ini)."""
    mod = root / folder
    mod.mkdir()
    (mod / "mod.ini").write_text(ini)
    if patch is not None:
        (mod / "patch.ini").write_text(patch)
    return mod


@needs_cc
def test_ini_checked_by_the_ports_parser(tmp_path):
    """mod.ini errors come from the C parser the console runs (here: id differs from folder)."""
    rep = mod_check.Report()
    mod = write_mod(tmp_path, "a", "id = b\napi = 1\n")
    mod_check.check_ini(mod, rep)
    assert rep.errors and "differs from the folder" in rep.errors[0]


@needs_cc
def test_patches_against_the_dump(tmp_path):
    """Patch values are checked against the dump: a missing field, a static field, an enum
    field patch.ini cannot set and a missing class fail; a float field passes.
    """
    mod = write_mod(
        tmp_path,
        "p",
        "id = p\napi = 1\n",
        "[Assembly-CSharp:Drivetrain]\nlimiterRPM = 9500\nnope = 1\ncount = 2\nengineType = 1\n"
        "[Assembly-CSharp:Missing]\nx = 1\n",
    )
    rep = mod_check.check_mod(mod, parse_dump(DUMP), None)
    text = "\n".join(rep.errors)
    assert "no instance field nope" in text and "no instance field count" in text
    assert "engineType is Drivetrain.EngineTypes" in text and "no class Missing" in text
    assert len(rep.errors) == 4 and "5 patch values" in rep.notes


@needs_cc
def test_filtered_section(tmp_path):
    """A section limited to objects by name ("@ pattern", spaces allowed) is read by the port's
    parser with its filter and checked against the dump like any other.
    """
    mod = write_mod(
        tmp_path,
        "f",
        "id = f\napi = 1\n",
        "[Assembly-CSharp:Drivetrain @ Lada VTS*]\nlimiterRPM = 9000\n",
    )
    patches = mod_check.check_ini(mod, mod_check.Report()).patches
    assert [(p.cls, p.filter, p.field) for p in patches] == [
        ("Drivetrain", "Lada VTS*", "limiterRPM")
    ]
    assert not mod_check.check_mod(mod, parse_dump(DUMP), None).errors
    bad = write_mod(tmp_path, "g", "id = g\napi = 1\n", "[Assembly-CSharp:Drivetrain @ ]\nx = 1\n")
    rep = mod_check.check_mod(bad, None, None)
    assert any("Assembly:Class @ filter" in e for e in rep.errors)


CAR_DUMP = (
    DUMP
    + """\
public class CarController : MonoBehaviour // TypeDefIndex: 2
{
\t// Fields
\tpublic bool TCS; // 0x110
\tpublic float ESPStrength; // 0x130
\tpublic float throttle; // 0x50
}
public class MobileCarController : CarController // TypeDefIndex: 3
{
}
public class Wheel : MonoBehaviour // TypeDefIndex: 4
{
\t// Fields
\tpublic float gripMaterial; // 0x200
\tpublic string tag; // 0x208
}
"""
)


def write_cars(root: Path, folder: str, cars: str) -> Path:
    """A mod folder with mod.ini and the given cars.ini."""
    mod = write_mod(root, folder, f"id = {folder}\napi = 1\n")
    (mod / "cars.ini").write_text(cars)
    return mod


@needs_cc
def test_cars_against_dump_and_car_names(tmp_path):
    """cars.ini keys find their one car component (or the named class) in the dump; an unknown
    car, a field of no component, a field two components share, a Class.field that does not
    exist and a string field fail. [*] needs no name check.
    """
    good = write_cars(
        tmp_path,
        "c",
        "[*]\nTCS = 0\n[Skyline]\nESPStrength = 0.3\nlimiterRPM = 9000\nWheel.gripMaterial = 1.1\n"
        "Drivetrain.limiterRPM = 8000\n",
    )
    rep = mod_check.check_mod(good, parse_dump(CAR_DUMP), None, None, ["Skyline"])
    assert not rep.errors and "5 car values" in rep.notes
    bad = write_cars(
        tmp_path,
        "d",
        "[Skylne]\nnope = 1\n[Skyline]\nthrottle = 1\nWheel.nope = 1\nMissing.x = 1\n"
        "Wheel.tag = 1\n",
    )
    text = "\n".join(mod_check.check_mod(bad, parse_dump(CAR_DUMP), None, None, ["Skyline"]).errors)
    assert "no car [Skylne]" in text and "nope is no field of CarController" in text
    assert "throttle is in" not in text  # Drivetrain has no throttle in this dump
    assert "Wheel has no instance field nope" in text and "no class Missing" in text
    assert "Wheel.tag is string" in text


@needs_cc
def test_cars_ambiguous_and_unchecked(tmp_path, monkeypatch):
    """A field in two car components must be written Class.field; without a dump or the
    archives the check warns instead of failing. A key outside a [car] section is refused by
    the port's parser.
    """
    dump = CAR_DUMP.replace("limiterRPM; // 0xC4", "limiterRPM; // 0xC4\n\tpublic float throttle;")
    mod = write_cars(tmp_path, "a", "[Skyline]\nthrottle = 1\nCarController.throttle = 1\n")
    errors = mod_check.check_mod(mod, parse_dump(dump), None, None, ["Skyline"]).errors
    assert errors == [
        "cars.ini:2: throttle is in CarController, Drivetrain; write it as Class.throttle"
    ]
    rep = mod_check.check_mod(mod, None, None, None, None)
    assert not rep.errors and len(rep.warnings) == 2
    orphan = write_cars(tmp_path, "o", "TCS = 0\n")
    assert any(
        "not under a [car] section" in e for e in mod_check.check_mod(orphan, None, None).errors
    )
    monkeypatch.setattr(mod_check, "user_archives", lambda: {"obb": tmp_path / "none"})
    assert mod_check.load_cars([mod]) is None and mod_check.load_cars([tmp_path / "x"]) == []


@needs_cc
def test_car_keys(tmp_path):
    """cars.ini's own keys come from the port's table with kind, timing, axle and enum names;
    their values are checked as numbers (names and curves by the parser), and the dump is not
    asked about them. Negative values pass where the game allows them (camber).
    """
    keys = mod_ini.car_keys()
    assert keys["power"].unit == "hp, stock peak" and keys["drive"].names == (
        "RWD",
        "FWD",
        "AWD",
        "XWD",
    )
    assert keys["weight"].when == "build" and keys["front_spring"].when == "live"
    assert keys["rear_diff"] == mod_ini.CarKey(
        keys["rear_diff"].unit,
        "axle",
        "live",
        "Axle",
        "differential",
        "rear",
        keys["rear_diff"].names,
    )
    assert "two_way_LSD" in keys["rear_diff"].names and "sport_front" in keys["front_tires"].names
    assert keys["susp_height"].axle == "both" and keys["torque_curve"].when == "once"
    mod = write_cars(
        tmp_path,
        "k",
        "[Skyline]\npower = 420\ndrive = AWD\nweight = 1250\nfront_camber = -2\n"
        "rear_diff = two_way_LSD\ntorque_curve = 0:0 5000:240 7500:0\n"
        "gears = 3.6 2.1 1.5 1.1 0.9\n",
    )
    rep = mod_check.check_mod(mod, parse_dump(CAR_DUMP), None, None, ["Skyline"])
    assert not rep.errors and "7 car values" in rep.notes
    bad = write_cars(tmp_path, "b", "[Skyline]\npower = lots\nweight = 0\nfinal_ratio = -1\n")
    errors = mod_check.check_mod(bad, None, None, None, ["Skyline"]).errors
    assert errors == [
        "cars.ini:2: power = 'lots' is not a number",
        "cars.ini:3: weight = 0 must be above 0",
        "cars.ini:4: final_ratio = -1 must be above 0",
    ]
    for i, (line, why) in enumerate(
        [
            ("drive = 4WD", "RWD FWD AWD XWD"),
            ("rear_diff = lsd", "two_way_LSD"),
            ("torque_curve = 1:1", "torque_curve"),
            ("gears = 3.6 0", "gears needs"),
        ]
    ):
        mod = write_cars(tmp_path, f"e{i}", f"[Skyline]\n{line}\n")
        assert any(why in e for e in mod_check.check_mod(mod, None, None).errors), line


@needs_cc
def test_mod_ini_keys_command(capsys):
    """`mod_ini keys` lists every cars.ini key with its timing and unit, or its enum names."""
    assert mod_ini.main(["keys"]) == 0
    lines = capsys.readouterr().out.splitlines()
    assert len(lines) == len(mod_ini.car_keys())
    assert any(
        line.split()[:2] == ["rear_diff", "live"] and "two_way_LSD" in line for line in lines
    )
    assert mod_ini.main([]) == 2


@needs_cc
def test_car_keys_exist_in_the_game():
    """Every class and field a cars.ini key writes exists in the real dump with the type the
    port writes it as: plain fields of a supported type, axle fields on Axle (enums as the
    game's enum with exactly the names of the table), special keys' floats, gears, curve and
    body; the helper fields, methods and the managers' car fields too.
    """
    rel = target_release()
    if not (rel.dump / "dump.cs").is_file():
        pytest.skip("needs the il2cpp dump")
    from tools.il2cpp_find import find_types, load

    types = load(rel.dump)

    def ftype(cls: str, fld: str) -> str | None:
        return mod_check.field_type(types, find_types(types, cls)[0], fld)

    def enum_names(enum: str) -> tuple[str, ...]:
        t = find_types(types, enum)[0]
        return tuple(
            f.decl.split("=")[0].split()[-1] for f in t.fields if "const" in f.decl.split()
        )

    enums = {"Axles.Differentials", "CarDynamics.Tires", "Drivetrain.Transmissions"}
    for key, k in mod_ini.car_keys().items():
        t = ftype(k.cls, k.field)
        if k.names:
            assert t in enums and enum_names(t) == k.names, (key, t)
        elif k.kind in ("field", "axle"):
            assert t in mod_check.PATCH_TYPES, (key, t)
        else:
            assert t in {"float", "float[]", "AnimationCurve", "Rigidbody"}, (key, t)
    for helper in [
        "maxPower",
        "maxTorque",
        "maxPowerRPM",
        "externalTurboPressure",
        "actualMaxTurboPressure",
    ]:
        assert ftype("Drivetrain", helper) == "float"
    assert ftype("Drivetrain", "first") == "int"
    assert ftype("CarDynamics", "axles") == "Axles"
    assert ftype("Axles", "frontAxle") == "Axle" and ftype("Axles", "rearAxle") == "Axle"
    for manager in ["MenuManager", "RaceManager"]:
        assert ftype(manager, "drivetrain") == "Drivetrain"
        assert ftype(manager, "carDynamics") == "CarDynamics"
    assert ftype("MenuManager", "rigidbody") == "Rigidbody"
    owners = ("CarDynamics", "Drivetrain", "MenuManager", "RaceManager")
    methods = {m.name for t in owners for m in find_types(types, t)[0].methods}
    assert {
        "SetWheelsParams",
        "SetBrakes",
        "SetTiresType",
        "CalcMaxPowerMaxTorque",
        "SetTransmission",
        "LoadOriginalValues",
        "UpdateVehicle",
    } <= methods


def test_car_components_from_the_header(tmp_path):
    """The components come from RD_CAR_COMPONENTS in rd_game_symbols.h, in order; a header
    without it is an error.
    """
    assert mod_check.car_components()[:2] == ["CarController", "Drivetrain"]
    h = tmp_path / "h.h"
    h.write_text('/* RD_CAR_COMPONENTS { "No" } */\n')
    with pytest.raises(RuntimeError):
        mod_check.car_components(h)


@needs_cc
def test_patches_without_a_dump_warn(tmp_path):
    """Without a dump the patches cannot be checked: a warning, not a failure."""
    mod = write_mod(tmp_path, "p", "id = p\napi = 1\n", "[Assembly-CSharp:X]\na = 1\n")
    rep = mod_check.check_mod(mod, None, None)
    assert not rep.errors and "no dump" in rep.warnings[0]


def test_parse_cars():
    """cars.ini lines from the checker: car, class (empty for a car component), field, value."""
    out = (
        "info id=a\ncar\t2\tSkyline\t\tTCS\t0\t\ncar\t3\t*\tWheel\tgripMaterial\t1.1\t\n"
        "car\t4\tGTR\tDrivetrain\tpowerMultiplier\t600\tpower\n"
    )
    assert mod_ini.parse_cars(out) == [
        mod_ini.CarLine(2, "Skyline", "", "TCS", "0"),
        mod_ini.CarLine(3, "*", "Wheel", "gripMaterial", "1.1"),
        mod_ini.CarLine(4, "GTR", "Drivetrain", "powerMultiplier", "600", "power"),
    ]


def test_parse_checker_output():
    """The checker's output lines become info, patch lines (values and object-name filters may
    contain spaces, so fields are tab-separated) and errors.
    """
    info, patches, errors = mod_ini.parse_checker_output(
        "info id=a\npatch\t3\tAssembly-CSharp\tNs.Cls\t\tf\ta b\n"
        "patch\t4\tAssembly-CSharp\tCarController\tLada VTS*\tESP\t0\nerror bad thing\n"
    )
    assert info == {"id": "a"} and errors == ["bad thing"]
    assert patches == [
        mod_ini.PatchLine(3, "Assembly-CSharp", "Ns.Cls", "f", "a b"),
        mod_ini.PatchLine(4, "Assembly-CSharp", "CarController", "ESP", "0", "Lada VTS*"),
    ]


def test_no_mod_ini():
    """A folder without mod.ini is not a mod."""
    rep = mod_check.check_mod(Path("/nonexistent"), None, None)
    assert rep.errors == ["no mod.ini"]


def test_so_checks(tmp_path):
    """A file that is not an aarch64 library fails cleanly instead of crashing the checker."""
    so = tmp_path / "mod.so"
    so.write_bytes(b"not an elf")
    rep = mod_check.Report()
    mod_check.check_so(so, rep, None)
    assert rep.errors and "mod.so" in rep.errors[0]
    assert mod_check.describe_reloc(1027) == "R_AARCH64_RELATIVE"
    assert mod_check.describe_reloc(7) == "type 7"


def test_files_against_the_archives(tmp_path, monkeypatch):
    """A file under files/obb that is no member of the OBB is flagged (it would be added, not
    replaced -- usually a typo); members that exist pass; a missing archive is a warning.
    """
    obb = tmp_path / "game.obb"
    with zipfile.ZipFile(obb, "w") as z:
        z.writestr("assets/bin/Data/abc", "x")
    monkeypatch.setattr(mod_check, "user_archives", lambda: {"obb": obb, "apk": tmp_path / "no"})
    mod = tmp_path / "m"
    (mod / "files" / "obb" / "assets" / "bin" / "Data").mkdir(parents=True)
    (mod / "files" / "obb" / "assets" / "bin" / "Data" / "abc").write_text("y")
    (mod / "files" / "obb" / "assets" / "typo").write_text("y")
    (mod / "files" / "apk").mkdir()
    rep = mod_check.Report()
    mod_check.check_files(mod, rep)
    assert len(rep.warnings) == 2
    assert any("typo is not in game.obb" in w for w in rep.warnings)
    assert "files/apk: no not found, members not checked" in rep.warnings


@needs_cc
def test_examples_pass(capsys):
    """The shipped examples pass the check (built mod.so and files/ are checked when present)."""
    mods = [EXAMPLES / n for n in ("hello", "balance", "retexture", "fov", "inspect")]
    assert mod_check.main([str(m) for m in mods]) == 0
    out = capsys.readouterr().out
    assert all(f"ok {n}" in out for n in ("hello", "balance", "retexture", "fov", "inspect"))


@needs_cc
def test_main_reports_failure(tmp_path, capsys):
    """A failing mod prints its errors and FAILED, and the exit code is 1."""
    mod = write_mod(tmp_path, "bad", "id = bad\n")
    assert mod_check.main([str(mod)]) == 1
    assert "FAILED bad" in capsys.readouterr().out


def test_real_hello_so():
    """The built hello example is a valid mod.so: entry exported, imports and relocations the
    loader handles.
    """
    so = EXAMPLES / "hello" / "mod.so"
    if not so.is_file():
        pytest.skip("build it: python -m tools.mod_build mods/examples/hello")
    rep = mod_check.Report()
    mod_check.check_so(so, rep, None)
    assert not rep.errors, rep.errors


def test_user_archives_names():
    """Mods address the archives as files/apk and files/obb."""
    a = user_archives()
    assert set(a) == {"apk", "obb"} and a["apk"] == target_release().apk


# ---- stage_sd --mods ------------------------------------------------------------------------


def test_copy_mods_without_sources(tmp_path):
    """Mods are copied without src/, an older copy is replaced (removed files do not linger),
    and a folder without mod.ini is refused.
    """
    src = tmp_path / "hello"
    shutil.copytree(EXAMPLES / "hello", src, ignore=shutil.ignore_patterns("mod.so"))
    out = tmp_path / "sd"
    (out / "mods" / "hello").mkdir(parents=True)
    (out / "mods" / "hello" / "stale.txt").write_text("old")
    assert stage_sd.copy_mods([src], out) == ["hello"]
    assert (out / "mods" / "hello" / "mod.ini").is_file()
    assert not (out / "mods" / "hello" / "src").exists()
    assert not (out / "mods" / "hello" / "stale.txt").exists()
    with pytest.raises(ValueError, match="not a mod folder"):
        stage_sd.copy_mods([tmp_path], out)


def test_stage_mods_only(tmp_path, capsys):
    """--mods-only updates just the mods folder; without it the APK and OBB are required."""
    out = tmp_path / "sd"
    assert stage_sd.main(["--mods-only", str(EXAMPLES / "balance"), "--out", str(out)]) == 0
    assert (out / "mods" / "balance" / "patch.ini").is_file()
    with pytest.raises(SystemExit):
        stage_sd.main(["--out", str(out)])
    assert stage_sd.main(["--mods-only", str(tmp_path), "--out", str(out)]) == 1


# ---- assets.ini in mod_check and stage_sd -------------------------------------------------


def test_check_assets(tmp_path):
    """assets.ini is checked like the build would: names against the game's objects, files,
    and a texture PNG of the wrong size; without the archives only a warning.
    """
    from PIL import Image

    from tools.mod_assets import Asset

    mod = write_mod(tmp_path, "a", "id = a\napi = 1\n")
    (mod / "art").mkdir()
    Image.new("RGBA", (4, 4)).save(mod / "art" / "logo.png")
    (mod / "assets.ini").write_text("Logo = art/logo.png\nNope = art/logo.png\nbad\n")
    assets = [Asset("obb", "assets/bin/Data/aaa", 1, "Texture2D", "Logo", "4x2 RGB24")]
    rep = mod_check.Report()
    mod_check.check_assets(mod, rep, assets)
    text = "\n".join(rep.errors)
    assert 'not "Name = file"' in text and "0 editable objects named 'Nope'" in text
    assert "logo.png is 4x4, Logo is 4x2" in text
    rep = mod_check.Report()
    mod_check.check_assets(mod, rep, None)
    assert "names not checked" in rep.warnings[0]


def test_stage_builds_assets_and_skips_art(tmp_path, monkeypatch):
    """Staging a mod with assets.ini runs the asset build on the staged copy and leaves art/
    and assets.ini on the PC.
    """
    import tools.mod_assets

    calls = []
    monkeypatch.setattr(tools.mod_assets, "build", lambda mods, out: calls.append(out) or ([], []))
    out = tmp_path / "sd"
    assert stage_sd.copy_mods([EXAMPLES / "retexture"], out) == ["retexture"]
    assert calls == [out / "mods"]
    staged = out / "mods" / "retexture"
    assert (staged / "mod.ini").is_file()
    assert not (staged / "art").exists() and not (staged / "assets.ini").exists()


def test_stage_without_the_tools(tmp_path, monkeypatch):
    """The standalone stage_sd.py (no repository tools) says how to build asset mods instead
    of failing with a traceback.
    """
    import builtins

    real_import = builtins.__import__

    def no_tools(name, *a, **kw):
        if name == "tools.mod_assets":
            raise ImportError("no module")
        return real_import(name, *a, **kw)

    monkeypatch.setattr(builtins, "__import__", no_tools)
    with pytest.raises(ValueError, match="need the realdrift_nx tools"):
        stage_sd.build_assets([EXAMPLES / "retexture"], tmp_path)
