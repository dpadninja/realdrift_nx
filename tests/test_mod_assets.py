"""tools/mod_assets.py: listing, exporting and replacing game assets (UnityPy faked, plus one
real-file test that skips without the user's OBB).

MIT (see LICENSE).
"""

import zipfile
from types import SimpleNamespace

import pytest
from PIL import Image

from tools import mod_assets
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


def test_replace_texture(game, tmp_path):
    """A same-size PNG replaces the texture and the member is written under the mod's
    files/obb with its archive path; another size is refused before anything is written.
    """
    (tex,) = mod_assets.select(mod_assets.scan(game, mod_assets.EDITABLE), "Logo", None)
    new = tmp_path / "new.png"
    Image.new("RGBA", (4, 2), (10, 200, 10, 255)).save(new)
    out = mod_assets.replace(game, tex, new, tmp_path / "mod")
    assert out == tmp_path / "mod" / "files" / "obb" / "assets" / "bin" / "Data" / "aaa"
    assert out.read_bytes().startswith(b"SAVED")
    big = tmp_path / "big.png"
    Image.new("RGBA", (8, 8)).save(big)
    with pytest.raises(ValueError, match="is 8x8, the texture is 4x2"):
        mod_assets.replace(game, tex, big, tmp_path / "mod2")
    assert not (tmp_path / "mod2").exists()


def test_replace_text_and_refusals(game, tmp_path):
    """A TextAsset takes any file; split members and other types are refused."""
    (txt,) = mod_assets.select(mod_assets.scan(game), "Config", None)
    src = tmp_path / "c.txt"
    src.write_text("a=2")
    assert mod_assets.replace(game, txt, src, tmp_path / "mod").is_file()
    (split,) = mod_assets.select(mod_assets.scan(game), "Split", None)
    with pytest.raises(ValueError, match="split"):
        mod_assets.replace(game, split, src, tmp_path / "mod")
    sprite = mod_assets.Asset("obb", "assets/bin/Data/aaa", 2, "Sprite", "Logo", "")
    with pytest.raises(ValueError, match="cannot be replaced"):
        mod_assets.replace(game, sprite, src, tmp_path / "mod")


def test_roundtrip_detects_a_lost_object():
    """If the saved file no longer has the object, nothing is written for the mod."""
    with pytest.raises(ValueError):
        mod_assets.check_roundtrip(
            b"TEX-not-registered", mod_assets.Asset("obb", "m", 1, "T", "n", ""), None
        )


def test_main(game, tmp_path, capsys):
    """List filters by regex and type; export/replace need a unique name and say how to pick."""
    assert mod_assets.main(["list", "--name", "^Lo", "--type", "Texture2D"]) == 0
    assert capsys.readouterr().out.strip() == "obb aaa 1 Texture2D Logo 4x2 RGB24"
    assert mod_assets.main(["export", "Nope", str(tmp_path / "x.png")]) == 1
    assert "0 objects named 'Nope'" in capsys.readouterr().err
    assert mod_assets.main(["export", "Logo", str(tmp_path / "x.png")]) == 0
    src = tmp_path / "bad.png"
    Image.new("RGBA", (1, 1)).save(src)
    assert mod_assets.main(["replace", "Logo", str(src), "--mod", str(tmp_path / "m")]) == 1
    assert "the texture is 4x2" in capsys.readouterr().err


def test_main_without_archives(monkeypatch, tmp_path, capsys):
    """Without the user's archives the tool says so instead of listing nothing."""
    monkeypatch.setattr(mod_assets, "user_archives", lambda: {"obb": tmp_path / "none"})
    assert mod_assets.main(["list"]) == 1
    assert "no base.apk / OBB" in capsys.readouterr().err


def test_real_options_button(tmp_path):
    """On the real OBB the main menu's Options texture can be replaced and reads back (the
    retexture example's recipe).
    """
    archives = user_archives()
    if not archives["obb"].is_file():
        pytest.skip("needs original/<obb>")
    (tex,) = mod_assets.select(
        mod_assets.scan(archives, mod_assets.EDITABLE), "MainMenu_Options", None
    )
    out = mod_assets.replace(
        archives, tex, ROOT / "mods/examples/retexture/art/options.png", tmp_path / "mod"
    )
    assert out.stat().st_size > 60_000
