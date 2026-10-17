"""Find, export and replace the game's textures and text assets for asset mods.

The game's assets live in base.apk and the OBB as Unity serialized files (one archive member
each). A mod replaces a whole member: this tool loads the member from your own archive with
UnityPy, swaps one object's content, and writes the modified member into the mod's
files/apk or files/obb folder, where the port serves it in place of the original
(source/bionic/rd_vfile.h). Only the mod's replaced members are written -- never ship the
game's own files.

    uv run python -m tools.mod_assets list --name 'MainMenu_' --type Texture2D
    uv run python -m tools.mod_assets export MainMenu_Options options.png
    uv run python -m tools.mod_assets replace MainMenu_Options my.png --mod mods/retexture

A name must match exactly one object; add --member (from `list`) when several do.
Textures keep their size and format; the replacement is read back and compared before the
file is written. Members split by Unity (*.splitN) cannot be replaced.

MIT (see LICENSE).
"""

from __future__ import annotations

import argparse
import re
import sys
import zipfile
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import UnityPy
from PIL import Image
from UnityPy.enums import TextureFormat

from tools.game import user_archives

DATA_PREFIX = "assets/bin/Data/"
TYPES = ("Texture2D", "TextAsset", "AudioClip", "Sprite", "Mesh", "Material")
EDITABLE = ("Texture2D", "TextAsset")  # what export / replace handle
SPLIT = re.compile(r"\.split\d+$")


@dataclass(frozen=True)
class Asset:
    """One object in one archive member."""

    archive: str  # "apk" / "obb"
    member: str  # path inside the archive
    path_id: int
    type: str
    name: str
    detail: str  # "230x96 RGB24" for textures

    def line(self) -> str:
        """The compact one-line form `list` prints."""
        guid = self.member.removeprefix(DATA_PREFIX)
        return (
            f"{self.archive} {guid} {self.path_id} {self.type} {self.name} {self.detail}".rstrip()
        )


def describe(obj: Any, data: Any) -> str:
    """Size and format of a texture, or nothing."""
    if obj.type.name == "Texture2D":
        try:
            fmt = TextureFormat(data.m_TextureFormat).name
        except ValueError:
            fmt = str(data.m_TextureFormat)
        return f"{data.m_Width}x{data.m_Height} {fmt}"
    return ""


def scan_member(archive: str, member: str, data: bytes, types: tuple[str, ...]) -> Iterator[Asset]:
    """The named objects of the wanted types in one serialized file."""
    try:
        env = UnityPy.load(data)
    except Exception:  # UnityPy raises anything on non-Unity data
        return
    for obj in env.objects:
        if obj.type.name not in types:
            continue
        try:
            d = obj.read()
        except Exception:  # an object UnityPy cannot read is skipped
            continue
        name = getattr(d, "m_Name", "") or ""
        if name:
            yield Asset(archive, member, obj.path_id, obj.type.name, name, describe(obj, d))


def scan(archives: dict[str, Path], types: tuple[str, ...] = TYPES) -> Iterator[Asset]:
    """Every named object of `types` in the archives' assets/bin/Data members."""
    for archive, path in archives.items():
        if not path.is_file():
            continue
        with zipfile.ZipFile(path) as z:
            for member in z.namelist():
                if member.startswith(DATA_PREFIX) and not member.endswith(".resS"):
                    yield from scan_member(archive, member, z.read(member), types)


def select(assets: Iterator[Asset], name: str, member: str | None) -> list[Asset]:
    """Assets named exactly `name` (and in `member`, if given; a GUID or full path)."""
    want = None if member is None else member.removeprefix(DATA_PREFIX)
    return [
        a
        for a in assets
        if a.name == name and (want is None or a.member.removeprefix(DATA_PREFIX) == want)
    ]


def load_object(archives: dict[str, Path], a: Asset) -> tuple[Any, Any, Any]:
    """(environment, object reader, object data) of an asset."""
    with zipfile.ZipFile(archives[a.archive]) as z:
        env = UnityPy.load(z.read(a.member))
    obj = next(o for o in env.objects if o.path_id == a.path_id)
    return env, obj, obj.read()


def export(archives: dict[str, Path], a: Asset, out: Path) -> None:
    """Writes a texture as PNG (or a TextAsset's bytes) to `out`.

    Raises:
        ValueError: the type cannot be exported.
    """
    _, obj, d = load_object(archives, a)
    if obj.type.name == "Texture2D":
        d.image.save(out)
    elif obj.type.name == "TextAsset":
        script = d.m_Script
        out.write_bytes(
            script.encode("utf-8", "surrogateescape") if isinstance(script, str) else script
        )
    else:
        raise ValueError(f"{a.type} cannot be exported (Texture2D and TextAsset can)")


def replace(archives: dict[str, Path], a: Asset, src: Path, mod: Path) -> Path:
    """Puts `src` into asset a and writes the modified member under mod/files/<archive>/.

    Returns:
        The written file.

    Raises:
        ValueError: wrong size, unsupported type or split member, or the read-back differs.
    """
    if SPLIT.search(a.member):
        raise ValueError(f"{a.member} is split by Unity; split members cannot be replaced")
    env, obj, d = load_object(archives, a)
    img: Image.Image | None = None
    if obj.type.name == "Texture2D":
        img = Image.open(src).convert("RGBA")
        if img.size != (d.m_Width, d.m_Height):
            size = f"{img.size[0]}x{img.size[1]}"
            raise ValueError(f"{src.name} is {size}, the texture is {d.m_Width}x{d.m_Height}")
        d.image = img
        d.save()
    elif obj.type.name == "TextAsset":
        d.m_Script = src.read_bytes().decode("utf-8", "surrogateescape")
        d.save()
    else:
        raise ValueError(f"{a.type} cannot be replaced (Texture2D and TextAsset can)")
    data = env.file.save()
    check_roundtrip(data, a, img)
    out = mod / "files" / a.archive / a.member
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    return out


def check_roundtrip(data: bytes, a: Asset, img: Image.Image | None) -> None:
    """Reloads the written member and checks the object is there with the new content.

    Raises:
        ValueError: it is not.
    """
    env = UnityPy.load(data)
    objs = [o for o in env.objects if o.path_id == a.path_id]
    if not objs:
        raise ValueError("the written file lost the object (UnityPy cannot save this file)")
    if img is not None:
        back = objs[0].read().image.convert("RGBA")
        red = back.crop((0, 0, 1, 1)).tobytes()[0], img.crop((0, 0, 1, 1)).tobytes()[0]
        if back.size != img.size or abs(red[0] - red[1]) > 8:
            raise ValueError("the texture reads back different (format not encodable by UnityPy)")


def main(argv: list[str] | None = None) -> int:
    """Command line: list / export / replace."""
    ap = argparse.ArgumentParser(description="Game assets for asset mods (UnityPy).")
    sub = ap.add_subparsers(dest="cmd", required=True)
    ls = sub.add_parser("list", help="named objects, one line each")
    ls.add_argument("--name", default="", help="regex on the object name")
    ls.add_argument("--type", choices=TYPES, help="only this type")
    ls.add_argument("--archive", choices=("apk", "obb"), help="only this archive")
    ex = sub.add_parser("export", help="texture to PNG, TextAsset to a file")
    ex.add_argument("name")
    ex.add_argument("out", type=Path)
    ex.add_argument("--member", help="archive member (GUID) when the name is not unique")
    rp = sub.add_parser("replace", help="write a mod file with this object replaced")
    rp.add_argument("name")
    rp.add_argument("file", type=Path, help="PNG for a texture, any file for a TextAsset")
    rp.add_argument("--mod", type=Path, required=True, help="the mod folder")
    rp.add_argument("--member", help="archive member (GUID) when the name is not unique")
    args = ap.parse_args(argv)
    archives = user_archives()
    if not any(p.is_file() for p in archives.values()):
        print("mod_assets: no base.apk / OBB in original/", file=sys.stderr)
        return 1
    if args.cmd == "list":
        rx = re.compile(args.name)
        types = (args.type,) if args.type else TYPES
        wanted = {k: v for k, v in archives.items() if not args.archive or k == args.archive}
        for a in scan(wanted, types):
            if rx.search(a.name):
                print(a.line())
        return 0
    found = select(scan(archives, EDITABLE), args.name, args.member)
    if len(found) != 1:
        hint = "; pass --member" if found else ""
        print(f"mod_assets: {len(found)} objects named {args.name!r}{hint}", file=sys.stderr)
        for a in found:
            print(a.line(), file=sys.stderr)
        return 1
    try:
        if args.cmd == "export":
            export(archives, found[0], args.out)
            print(args.out)
        else:
            print(replace(archives, found[0], args.file, args.mod))
    except (ValueError, OSError) as e:
        print(f"mod_assets: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
