"""tools/mod_assets.py: listing, exporting and replacing game assets (UnityPy faked, plus one
real-file test that skips without the user's OBB).

MIT (see LICENSE).
"""

import zipfile
from types import SimpleNamespace

import pytest
from PIL import Image

from tools import hostc, mod_assets
from tools.game import ROOT, user_archives

RED = (200, 30, 30, 255)


class FakeTexture:
    """Texture2D data as UnityPy exposes it."""

    def __init__(self, name, size=(4, 2)):
        """A solid red texture."""
        self.m_Name, (self.m_Width, self.m_Height), self.m_TextureFormat = name, size, 3
        self.image = Image.new("RGBA", size, RED)

    def save(self):
        """Nothing to do: the fake environment saves."""


class FakeText:
    """TextAsset data."""

    def __init__(self, name, text):
        """A text asset."""
        self.m_Name, self.m_Script = name, text

    def save(self):
        """Nothing to do."""


class FakeObj:
    """An object reader."""

    def __init__(self, path_id, kind, data):
        """Wraps data as object path_id of type kind."""
        self.path_id, self.type, self.data = path_id, SimpleNamespace(name=kind), data

    def read(self):
        """The object's data."""
        return self.data


class FakeEnv:
    """A loaded serialized file; save() registers what it wrote so it can be loaded again."""

    def __init__(self, objects, registry):
        """objects, and the registry load() reads from."""
        self.objects, self.registry = objects, registry
        self.file = SimpleNamespace(save=self.save)

    def save(self):
        """Serialized bytes of the current objects."""
        out = b"SAVED%d" % len(self.registry)
        self.registry[out] = self.objects
        return out


@pytest.fixture
def game(tmp_path, monkeypatch):
    """An OBB with a texture member, a text member, a split member and a .resS, and UnityPy
    replaced by the fakes above.
    """
    registry = {
        b"TEX": [
            FakeObj(1, "Texture2D", FakeTexture("Logo")),
            FakeObj(2, "Sprite", FakeText("Logo", "")),
        ],
        b"TXT": [FakeObj(5, "TextAsset", FakeText("Config", "a=1"))],
        b"SPLIT": [FakeObj(1, "Texture2D", FakeTexture("Split"))],
        b"TWO": [
            FakeObj(1, "Texture2D", FakeTexture("A")),
            FakeObj(2, "Texture2D", FakeTexture("B")),
        ],
        b"DUP": [FakeObj(1, "Texture2D", FakeTexture("A"))],
    }

    def load(data):
        if data not in registry:
            raise ValueError("not a Unity file")
        return FakeEnv(registry[data], registry)

    monkeypatch.setattr(mod_assets.UnityPy, "load", load)
    obb = tmp_path / "game.obb"
    with zipfile.ZipFile(obb, "w") as z:
        z.writestr("assets/bin/Data/aaa", b"TEX")
        z.writestr("assets/bin/Data/bbb", b"TXT")
        z.writestr("assets/bin/Data/ccc.split0", b"SPLIT")
        z.writestr("assets/bin/Data/aaa.resS", b"raw")
        z.writestr("assets/bin/Data/junk", b"junk")
        z.writestr("assets/bin/Data/ddd", b"TWO")
        z.writestr("assets/bin/Data/eee", b"DUP")
        z.writestr("other/x", b"TEX")
    archives = {"obb": obb, "apk": tmp_path / "missing.apk"}
    monkeypatch.setattr(mod_assets, "user_archives", lambda: archives)
    return archives


def test_scan_lists_named_objects(game):
    """Every named object under assets/bin/Data is listed with archive, member, path id, type
    and texture size; .resS data, non-Unity members and paths outside Data are skipped.
    """
    lines = [a.line() for a in mod_assets.scan(game)]
    assert "obb aaa 1 Texture2D Logo 4x2 RGB24" in lines
    assert "obb bbb 5 TextAsset Config" in lines
    assert not any("junk" in line or "other" in line for line in lines)


def test_select_by_name_and_member(game):
    """A name picks objects across members; --member narrows it (GUID or full path)."""
    assets = list(mod_assets.scan(game, mod_assets.EDITABLE))
    assert len(mod_assets.select(iter(assets), "Logo", None)) == 1  # the Sprite is not editable
    assert mod_assets.select(iter(assets), "Logo", "assets/bin/Data/aaa")
    assert not mod_assets.select(iter(assets), "Logo", "bbb")


def test_export(game, tmp_path):
    """A texture exports as PNG, a TextAsset as its bytes, anything else is refused."""
    tex = mod_assets.select(mod_assets.scan(game), "Logo", None)
    png = tmp_path / "logo.png"
    mod_assets.export(game, next(a for a in tex if a.type == "Texture2D"), png)
    assert Image.open(png).getpixel((0, 0)) == RED
    (txt,) = mod_assets.select(mod_assets.scan(game), "Config", None)
    mod_assets.export(game, txt, tmp_path / "c.txt")
    assert (tmp_path / "c.txt").read_text() == "a=1"
    sprite = next(a for a in tex if a.type == "Sprite")
    with pytest.raises(ValueError, match="cannot be exported"):
        mod_assets.export(game, sprite, tmp_path / "s")


def png(path, size=(4, 2), color=(10, 200, 10, 255)):
    """Writes a solid PNG and returns its path."""
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.new("RGBA", size, color).save(path)
    return path


def make_mod(root, mod_id, assets_ini, art=()):
    """A mod folder with an assets.ini and the given PNGs (relative paths)."""
    mod = root / mod_id
    mod.mkdir(parents=True)
    (mod / "mod.ini").write_text(f"id = {mod_id}\napi = 1\n")
    (mod / "assets.ini").write_text(assets_ini)
    for a in art:
        png(mod / a)
    return mod


def test_parse_assets_ini(tmp_path):
    """Lines are "Name = file" with an optional @member, comments and blank lines are skipped,
    and a bad line is reported with its number.
    """
    mod = tmp_path / "m"
    mod.mkdir()
    (mod / "assets.ini").write_text(
        "# comment\n; also\n\nLogo = art/logo.png   # trailing\nA@ddd = art/a.png\nbroken\n"
    )
    wanted, errors = mod_assets.parse_assets_ini(mod)
    assert [(w.name, w.member, w.file, w.line) for w in wanted] == [
        ("Logo", None, mod / "art/logo.png", 4),
        ("A", "ddd", mod / "art/a.png", 5),
    ]
    assert errors == ['assets.ini:6: not "Name = file"']
    assert mod_assets.parse_assets_ini(tmp_path / "none") == ([], [])


def test_plan_merges_one_member_and_keeps_priority(game, tmp_path):
    """Two mods changing different objects of one member build ONE file owned by the first mod
    in load order -- no conflict; the same object in both is kept by the first, with a warning.
    """
    hi = make_mod(tmp_path, "hi", "A@ddd = a.png\n", ["a.png"])
    lo = make_mod(tmp_path, "lo", "B = b.png\nA@ddd = a2.png\n", ["b.png", "a2.png"])
    wanted = {m.name: mod_assets.parse_assets_ini(m)[0] for m in (hi, lo)}
    plan = mod_assets.plan_build(
        ["hi", "lo"], wanted, list(mod_assets.scan(game, mod_assets.EDITABLE))
    )
    assert not plan.errors
    key = ("obb", "assets/bin/Data/ddd")
    assert plan.owner == {key: "hi"}
    assert [(r.wanted.mod, r.asset.name) for r in plan.members[key]] == [("hi", "A"), ("lo", "B")]
    assert plan.warnings == ["lo: assets.ini:2: A is also replaced by hi, which wins"]


def test_plan_errors(game, tmp_path):
    """An ambiguous name lists its candidates as name@member, an unknown name, a missing file
    and a split member are errors.
    """
    mod = make_mod(
        tmp_path, "m", "A = a.png\nNope = a.png\nLogo = missing.png\nSplit = a.png\n", ["a.png"]
    )
    plan = mod_assets.plan_build(
        ["m"],
        {"m": mod_assets.parse_assets_ini(mod)[0]},
        list(mod_assets.scan(game, mod_assets.EDITABLE)),
    )
    text = "\n".join(plan.errors)
    assert "2 editable objects named 'A' (A@ddd, A@eee): add @<member>" in text
    assert "0 editable objects named 'Nope'" in text
    assert "missing.png does not exist" in text and "split by Unity" in text


def test_build_writes_only_the_owners_file(game, tmp_path, monkeypatch):
    """Build writes each changed member once, into the owning mod under --out, with every
    replacement applied (both textures read back changed); nothing is written in the mods.
    """
    monkeypatch.setattr(mod_assets, "mod_order", lambda mods: [m.name for m in mods])
    hi = make_mod(tmp_path / "src", "hi", "A@ddd = a.png\nConfig = c.txt\n", ["a.png"])
    (hi / "c.txt").write_text("a=2")
    lo = make_mod(tmp_path / "src", "lo", "B = b.png\n", ["b.png"])
    out = tmp_path / "sd"
    written, warnings = mod_assets.build([hi, lo], out, game)
    assert sorted(p.relative_to(out).as_posix() for p in written) == [
        "hi/files/obb/assets/bin/Data/bbb",
        "hi/files/obb/assets/bin/Data/ddd",
    ]
    assert not warnings and not (hi / "files").exists()


def test_build_refusals(game, tmp_path, monkeypatch):
    """A wrong-size PNG, a raw files/ copy of a built member, and plan errors stop the build
    with all reasons; mods without assets.ini build nothing.
    """
    monkeypatch.setattr(mod_assets, "mod_order", lambda mods: [m.name for m in mods])
    big = make_mod(tmp_path, "big", "Logo = l.png\n")
    png(big / "l.png", (8, 8))
    with pytest.raises(ValueError, match="is 8x8, Logo is 4x2"):
        mod_assets.build([big], tmp_path / "out", game)
    raw = make_mod(tmp_path, "raw", "Logo = l.png\n", ["l.png"])
    (raw / "files/obb/assets/bin/Data").mkdir(parents=True)
    (raw / "files/obb/assets/bin/Data/aaa").write_bytes(b"x")
    with pytest.raises(ValueError, match=r"also built from assets\.ini"):
        mod_assets.build([raw], tmp_path / "out", game)
    plain = tmp_path / "plain"
    plain.mkdir()
    assert mod_assets.build([plain], tmp_path / "out", game) == ([], [])


def test_texture_size():
    """Texture sizes come from the list detail; other types have none."""
    assert mod_assets.texture_size(
        mod_assets.Asset("obb", "m", 1, "Texture2D", "n", "230x96 RGB24")
    ) == (230, 96)
    assert mod_assets.texture_size(mod_assets.Asset("obb", "m", 1, "TextAsset", "n", "")) is None


def test_roundtrip_detects_a_lost_object():
    """If the saved file no longer has the object, nothing is written for the mod."""
    with pytest.raises(ValueError):
        mod_assets.check_roundtrip(
            b"TEX-not-registered", mod_assets.Asset("obb", "m", 1, "T", "n", ""), None
        )


def test_main(game, tmp_path, capsys, monkeypatch):
    """List prints assets.ini lines (name@member for names used twice, non-editable objects as
    comments); export needs a unique name; build prints the written files or the errors.
    """
    assert mod_assets.main(["list", "--name", "^(Logo|A)$"]) == 0
    out = capsys.readouterr().out.splitlines()
    assert "Logo = <file>    # obb aaa 1 Texture2D Logo 4x2 RGB24" in out
    assert "A@ddd = <file>    # obb ddd 1 Texture2D A 4x2 RGB24" in out
    assert any(line.startswith("# obb aaa 2 Sprite Logo") for line in out)
    assert mod_assets.main(["export", "Nope", str(tmp_path / "x.png")]) == 1
    assert "0 objects named 'Nope'" in capsys.readouterr().err
    assert mod_assets.main(["export", "Logo", str(tmp_path / "x.png")]) == 0
    monkeypatch.setattr(mod_assets, "mod_order", lambda mods: [m.name for m in mods])
    mod = make_mod(tmp_path, "m", "Logo = l.png\n", ["l.png"])
    assert mod_assets.main(["build", str(mod), "--out", str(tmp_path / "sd")]) == 0
    assert capsys.readouterr().out.strip().endswith("m/files/obb/assets/bin/Data/aaa")
    (mod / "assets.ini").write_text("Nope = l.png\n")
    assert mod_assets.main(["build", str(mod), "--out", str(tmp_path / "sd")]) == 1
    assert "0 editable objects named 'Nope'" in capsys.readouterr().err


def test_main_without_archives(monkeypatch, tmp_path, capsys):
    """Without the user's archives the tool says so instead of listing nothing."""
    monkeypatch.setattr(mod_assets, "user_archives", lambda: {"obb": tmp_path / "none"})
    assert mod_assets.main(["list"]) == 1
    assert "no base.apk / OBB" in capsys.readouterr().err


def test_real_retexture_example(tmp_path):
    """On the real OBB the retexture example builds its one member and it reads back; the
    load order comes from mod.ini through the port's parser.
    """
    if not user_archives()["obb"].is_file():
        pytest.skip("needs original/<obb>")
    if hostc.CC is None:
        pytest.skip("no host C compiler")
    written, _ = mod_assets.build([ROOT / "mods/examples/retexture"], tmp_path)
    assert [p.name for p in written] == ["e3dd51a556549e14da4a4c45daf0102d"]
    assert written[0].stat().st_size > 60_000
