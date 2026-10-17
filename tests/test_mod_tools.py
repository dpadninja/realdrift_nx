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
def test_patches_without_a_dump_warn(tmp_path):
    """Without a dump the patches cannot be checked: a warning, not a failure."""
    mod = write_mod(tmp_path, "p", "id = p\napi = 1\n", "[Assembly-CSharp:X]\na = 1\n")
    rep = mod_check.check_mod(mod, None, None)
    assert not rep.errors and "no dump" in rep.warnings[0]


def test_parse_checker_output():
    """The checker's output lines become info, patch lines (values may contain spaces) and
    errors.
    """
    info, patches, errors = mod_ini.parse_checker_output(
        "info id=a\npatch 3 Assembly-CSharp Ns.Cls f a b\nerror bad thing\n"
    )
    assert info == {"id": "a"} and errors == ["bad thing"]
    assert patches == [mod_ini.PatchLine(3, "Assembly-CSharp", "Ns.Cls", "f", "a b")]


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
    mods = [EXAMPLES / n for n in ("hello", "balance", "retexture", "fov")]
    assert mod_check.main([str(m) for m in mods]) == 0
    out = capsys.readouterr().out
    assert all(f"ok {n}" in out for n in ("hello", "balance", "retexture", "fov"))


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
