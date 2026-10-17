"""Asset mods by name: find the game's textures and texts, and build the files replacing them.

A mod lists its replacements by object name in assets.ini:

    MainMenu_Options = art/options.png        # Texture2D: a PNG of the same size
    SomeConfig       = text/config.txt        # TextAsset: any bytes
    Logo@e3dd51a5... = art/logo.png           # name@member GUID when the name is not unique

`build` turns the assets.ini of several mods into the Unity files the port serves
(mods/<id>/files/<apk|obb>/<member>, source/bionic/rd_vfile.h): each member is loaded once
from YOUR base.apk / OBB, every replacement of every mod in it is applied, the result is read
back, and the file goes to the highest-priority mod that touches the member. Mods load in the
port's order (priority, then id); when two change the same object the first wins and the
other gets a warning. stage_sd.py --mods runs it; the built files go to the SD copy only.

    uv run python -m tools.mod_assets list --name 'MainMenu_' --type Texture2D
    uv run python -m tools.mod_assets export MainMenu_Options options.png
    uv run python -m tools.mod_assets build mods/retexture --out sd/switch/realdrift_nx/mods

`list` prints lines ready for assets.ini. Members split by Unity (*.splitN) cannot be changed.

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


ASSETS_INI = "assets.ini"
ASSET_LINE = re.compile(r"^(?P<name>[^=@]+?)(?:@(?P<member>[^=\s]+))?\s*=\s*(?P<file>.+?)\s*$")


@dataclass(frozen=True)
class Wanted:
    """One assets.ini line: put `file` into the object called `name`."""

    mod: str  # mod id (folder name)
    line: int
    name: str
    member: str | None  # GUID or member path when given as name@member
    file: Path


@dataclass(frozen=True)
class Replacement:
    """A Wanted line resolved to one object of the game."""

    wanted: Wanted
    asset: Asset


@dataclass
class Plan:
    """What build writes: replacements per member, the mod that owns each member's file."""

    members: dict[tuple[str, str], list[Replacement]]  # (archive, member) -> replacements
    owner: dict[tuple[str, str], str]  # (archive, member) -> mod id
    errors: list[str]
    warnings: list[str]


def parse_assets_ini(mod: Path) -> tuple[list[Wanted], list[str]]:
    """The lines of mod/assets.ini (none if absent) and the errors of the bad ones."""
    path = mod / ASSETS_INI
    if not path.is_file():
        return [], []
    wanted: list[Wanted] = []
    errors: list[str] = []
    for num, raw in enumerate(path.read_text().splitlines(), 1):
        text = raw.split("#", 1)[0].strip()
        if not text or text.startswith(";"):
            continue
        m = ASSET_LINE.match(text)
        if not m:
            errors.append(f'{ASSETS_INI}:{num}: not "Name = file"')
            continue
        wanted.append(Wanted(mod.name, num, m["name"].strip(), m["member"], mod / m["file"]))
    return wanted, errors


def texture_size(a: Asset) -> tuple[int, int] | None:
    """Width and height from a texture's `list` detail ("230x96 RGB24")."""
    m = re.match(r"(\d+)x(\d+) ", a.detail + " ")
    return (int(m[1]), int(m[2])) if m and a.type == "Texture2D" else None


def plan_build(order: list[str], wanted: dict[str, list[Wanted]], assets: list[Asset]) -> Plan:
    """Resolves every mod's lines (mods in load order) and groups them by archive member.

    A name must resolve to exactly one editable object; an object claimed by an earlier mod
    stays with it. The first mod to touch a member owns the member's file.
    """
    plan = Plan({}, {}, [], [])
    taken: dict[tuple[str, str, int], str] = {}
    for mod in order:
        for w in wanted.get(mod, []):
            where = f"{mod}: {ASSETS_INI}:{w.line}"
            found = select(iter(assets), w.name, w.member)
            if len(found) != 1:
                cands = ", ".join(f"{a.name}@{a.member.removeprefix(DATA_PREFIX)}" for a in found)
                plan.errors.append(
                    f"{where}: {len(found)} editable objects named {w.name!r}"
                    + (f" ({cands}): add @<member>" if found else "")
                )
                continue
            a = found[0]
            if SPLIT.search(a.member):
                plan.errors.append(f"{where}: {w.name} is in {a.member}, split by Unity")
                continue
            if not w.file.is_file():
                plan.errors.append(f"{where}: {w.file} does not exist")
                continue
            key = (a.archive, a.member, a.path_id)
            if key in taken:
                plan.warnings.append(
                    f"{where}: {w.name} is also replaced by {taken[key]}, which wins"
                )
                continue
            taken[key] = mod
            m = (a.archive, a.member)
            plan.members.setdefault(m, []).append(Replacement(w, a))
            plan.owner.setdefault(m, mod)
    return plan


def apply_member(
    archives: dict[str, Path], member: tuple[str, str], reps: list[Replacement]
) -> bytes:
    """The member with all its replacements applied and read back.

    Raises:
        ValueError: a PNG has the wrong size, or a replacement does not read back.
    """
    archive, name = member
    with zipfile.ZipFile(archives[archive]) as z:
        env: Any = UnityPy.load(z.read(name))  # UnityPy's types do not know .file.save()
    objs = {o.path_id: o for o in env.objects}
    images: dict[int, Image.Image | None] = {}
    for r in reps:
        obj = objs[r.asset.path_id]
        d = obj.read()
        if obj.type.name == "Texture2D":
            img = Image.open(r.wanted.file).convert("RGBA")
            if img.size != (d.m_Width, d.m_Height):
                size = f"{img.size[0]}x{img.size[1]}"
                raise ValueError(
                    f"{r.wanted.mod}: {r.wanted.file.name} is {size}, "
                    f"{r.wanted.name} is {d.m_Width}x{d.m_Height}"
                )
            d.image = img
            images[r.asset.path_id] = img
        else:
            d.m_Script = r.wanted.file.read_bytes().decode("utf-8", "surrogateescape")
            images[r.asset.path_id] = None
        d.save()
    data: bytes = env.file.save()
    for r in reps:
        check_roundtrip(data, r.asset, images[r.asset.path_id])
    return data


def mod_order(mods: list[Path]) -> list[str]:
    """Mod ids in the port's load order: priority descending, then id (source/mods/rd_modhost.c)."""
    from tools.mod_ini import read_mod  # here: the C parser is only needed to build

    prio = {m.name: read_mod(m).priority for m in mods}
    return sorted(prio, key=lambda i: (-prio[i], i))


def build(
    mods: list[Path], out: Path, archives: dict[str, Path] | None = None
) -> tuple[list[Path], list[str]]:
    """Builds the replaced members of `mods` (folders) under out/<owner id>/files/.

    Returns:
        The written files and the warnings.

    Raises:
        ValueError: any error (all of them, one per line).
    """
    archives = archives or user_archives()
    wanted: dict[str, list[Wanted]] = {}
    errors: list[str] = []
    for m in mods:
        lines, errs = parse_assets_ini(m)
        wanted[m.name] = lines
        errors += [f"{m.name}: {e}" for e in errs]
    if not any(wanted.values()):
        return [], []
    plan = plan_build(mod_order(mods), wanted, list(scan(archives, EDITABLE)))
    errors += plan.errors
    by_id = {m.name: m for m in mods}
    for (archive, member), owner in plan.owner.items():
        for m in mods:
            if (m / "files" / archive / member).is_file():
                errors.append(
                    f"{m.name}: files/{archive}/{member} is also built from assets.ini "
                    f"({owner}); remove one"
                )
    if errors:
        raise ValueError("\n".join(errors))
    written = []
    for key, reps in plan.members.items():
        dest = out / by_id[plan.owner[key]].name / "files" / key[0] / key[1]
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(apply_member(archives, key, reps))
        written.append(dest)
    return written, plan.warnings


def list_assets(
    archives: dict[str, Path], rx: re.Pattern[str], kind: str | None, archive: str | None
) -> int:
    """Prints matching objects; editable ones as assets.ini lines (name@member when needed)."""
    wanted = {k: v for k, v in archives.items() if not archive or k == archive}
    assets = [a for a in scan(wanted, (kind,) if kind else TYPES) if rx.search(a.name)]
    count: dict[str, int] = {}
    for a in scan(archives, EDITABLE):
        count[a.name] = count.get(a.name, 0) + 1
    for a in assets:
        guid = a.member.removeprefix(DATA_PREFIX)
        if a.type in EDITABLE:
            name = a.name if count.get(a.name) == 1 else f"{a.name}@{guid}"
            print(f"{name} = <file>    # {a.line()}")
        else:
            print(f"# {a.line()}   (not editable)")
    return 0


def main(argv: list[str] | None = None) -> int:
    """Command line: list / export / build."""
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
    bd = sub.add_parser("build", help="assets.ini of mods -> their files/ under --out")
    bd.add_argument("mods", nargs="+", type=Path, help="mod folders")
    bd.add_argument("--out", type=Path, required=True, help="where the mods are staged")
    args = ap.parse_args(argv)
    archives = user_archives()
    if not any(p.is_file() for p in archives.values()):
        print("mod_assets: no base.apk / OBB in original/", file=sys.stderr)
        return 1
    if args.cmd == "list":
        return list_assets(archives, re.compile(args.name), args.type, args.archive)
    if args.cmd == "build":
        try:
            written, warnings = build(args.mods, args.out, archives)
        except (ValueError, OSError) as e:
            print(f"mod_assets: {e}", file=sys.stderr)
            return 1
        for w in warnings:
            print(f"warning: {w}", file=sys.stderr)
        for f in written:
            print(f)
        return 0
    found = select(scan(archives, EDITABLE), args.name, args.member)
    if len(found) != 1:
        hint = "; pass --member" if found else ""
        print(f"mod_assets: {len(found)} objects named {args.name!r}{hint}", file=sys.stderr)
        for a in found:
            print(a.line(), file=sys.stderr)
        return 1
    try:
        export(archives, found[0], args.out)
        print(args.out)
    except (ValueError, OSError) as e:
        print(f"mod_assets: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
