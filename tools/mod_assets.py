"""Asset mods by name: find the game's textures and texts, and build the files replacing them.

MIT (see LICENSE). Guide: docs/MODDING_GUIDE.md.

Where the game's assets are: base.apk and the OBB are zip archives whose assets/bin/Data/
holds Unity files. A Unity file contains many objects (textures, texts, sounds, meshes,
scripts' data). Large files are stored cut into parts (sharedassets0.assets.split0..40 in the
APK); some have their bulk data next to them (<file>.resS / <file>.resource). This tool reads
a Unity file whole -- parts joined, its data file next to it -- so a mod never needs to know
how it is stored.

A mod lists its replacements by object name in assets.ini:

    MainMenu_Options = art/options.png        # Texture2D: a PNG of the same size
    SomeConfig       = text/config.txt        # TextAsset: any bytes
    Logo@e3dd51a5... = art/logo.png           # name@<Unity file> when the name is not unique

`build` turns the assets.ini of several mods into what the port serves
(mods/<id>/files/<apk|obb>/<archive member>, source/bionic/rd_vfile.h): each Unity file is
loaded once from YOUR base.apk / OBB, every replacement of every mod in it is applied, the
result is read back and written (cut into the same parts if the original was). The files go to
the highest-priority mod that touches the Unity file. Mods load in the port's order (priority,
then id); when two change the same object the first wins and the other gets a warning.
stage_sd.py --mods runs it; the built files go to the SD copy only.

    uv run python -m tools.mod_assets list --name 'MainMenu_' --type Texture2D
    uv run python -m tools.mod_assets export MainMenu_Options options.png
    uv run python -m tools.mod_assets build mods/retexture --out sd/switch/realdrift_nx/mods

`list` prints lines ready for assets.ini. Game objects by the scripts on them (no typetree
needed: the script reference and the object name are Unity's own types):

    uv run python -m tools.mod_assets cars                         # the car names for cars.ini
    uv run python -m tools.mod_assets objects --component Drivetrain
"""

from __future__ import annotations

import argparse
import functools
import re
import sys
import tempfile
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
EDITABLE = ("Texture2D", "TextAsset")  # what export and assets.ini handle
SPLIT = re.compile(r"\.split(\d+)$")
DATA_FILES = (".resS", ".resource")  # bulk data stored next to a Unity file
ASSETS_INI = "assets.ini"
# The script only the drivable cars carry: their object names are the cars.ini sections. The
# garage and menu models ("<car>_Garage", "<car>_cover") have Drivetrain but not this.
CAR_SCRIPT = "MobileCarController"
ASSET_LINE = re.compile(r"^(?P<name>[^=@]+?)(?:@(?P<file>[^=\s]+))?\s*=\s*(?P<path>.+?)\s*$")


# ---- Unity files in an archive ------------------------------------------------------------


@dataclass(frozen=True)
class UnityFile:
    """A Unity file as stored in an archive: one member, or parts; maybe a data file."""

    archive: str  # "apk" / "obb"
    name: str  # logical member path: assets/bin/Data/sharedassets0.assets
    parts: tuple[str, ...]  # the members holding it, in order (one if not split)
    data: tuple[str, ...]  # members with its bulk data (.resS / .resource)
    part_size: int  # size of each part but the last (0 if not split)


def unity_files(z: zipfile.ZipFile, archive: str) -> list[UnityFile]:
    """The Unity files under assets/bin/Data/ of an open archive."""
    names = set(z.namelist())
    parts: dict[str, list[tuple[int, str]]] = {}
    for n in names:
        if not n.startswith(DATA_PREFIX) or n.endswith(DATA_FILES):
            continue
        m = SPLIT.search(n)
        base = n[: m.start()] if m else n
        parts.setdefault(base, []).append((int(m.group(1)) if m else 0, n))
    out = []
    for base, ps in sorted(parts.items()):
        ordered = tuple(n for _, n in sorted(ps))
        size = z.getinfo(ordered[0]).file_size if len(ordered) > 1 else 0
        data = tuple(base + ext for ext in DATA_FILES if base + ext in names)
        out.append(UnityFile(archive, base, ordered, data, size))
    return out


def open_env(z: zipfile.ZipFile, uf: UnityFile) -> tuple[Any, Any]:
    """(UnityPy environment, the Unity file's object inside it) with parts joined.

    With a data file next to it, both are put into a temporary folder so UnityPy finds the
    texture/sound data the file refers to.
    """
    joined = b"".join(z.read(p) for p in uf.parts)
    if not uf.data:
        env: Any = UnityPy.load(joined)
        return env, env.file
    tmp = Path(tempfile.mkdtemp(prefix="mod_assets_"))
    base = Path(uf.name).name
    (tmp / base).write_bytes(joined)
    for d in uf.data:
        (tmp / Path(d).name).write_bytes(z.read(d))
    env = UnityPy.load(str(tmp))
    return env, env.files[base]


def split_parts(uf: UnityFile, data: bytes) -> list[tuple[str, bytes]]:
    """(member, bytes) to write for new contents of uf, cut like the original.

    A split file keeps its part size. If it got shorter, the leftover parts are written empty
    (the archive still has them and Unity joins all parts); if longer, parts are added.
    """
    if not uf.part_size:
        return [(uf.parts[0], data)]
    n = max(len(uf.parts), -(-len(data) // uf.part_size))
    return [
        (f"{uf.name}.split{i}", data[i * uf.part_size : (i + 1) * uf.part_size]) for i in range(n)
    ]


# ---- objects ------------------------------------------------------------------------------


@dataclass(frozen=True)
class Asset:
    """One object in one Unity file."""

    archive: str  # "apk" / "obb"
    member: str  # the Unity file (logical member path)
    path_id: int
    type: str
    name: str
    detail: str  # "230x96 RGB24" for textures

    def line(self) -> str:
        """The compact one-line form: archive, Unity file, path id, type, name, detail."""
        short = self.member.removeprefix(DATA_PREFIX)
        return (
            f"{self.archive} {short} {self.path_id} {self.type} {self.name} {self.detail}".rstrip()
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


def scan_file(uf: UnityFile, joined: bytes, types: tuple[str, ...]) -> Iterator[Asset]:
    """The named objects of the wanted types in one Unity file."""
    try:
        env = UnityPy.load(joined)
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
            yield Asset(uf.archive, uf.name, obj.path_id, obj.type.name, name, describe(obj, d))


def scan(archives: dict[str, Path], types: tuple[str, ...] = TYPES) -> Iterator[Asset]:
    """Every named object of `types` in the archives' Unity files."""
    for archive, path in archives.items():
        if not path.is_file():
            continue
        with zipfile.ZipFile(path) as z:
            for uf in unity_files(z, archive):
                yield from scan_file(uf, b"".join(z.read(p) for p in uf.parts), types)


@dataclass(frozen=True)
class Holder:
    """A game object carrying a script (MonoBehaviour) of some class."""

    name: str  # the GameObject's name
    archive: str
    member: str  # the Unity file


def holders_in(uf: UnityFile, joined: bytes, script: str) -> Iterator[Holder]:
    """The objects of one Unity file that carry a script of class `script`."""
    try:
        env = UnityPy.load(joined)
    except Exception:  # UnityPy raises anything on non-Unity data
        return
    for obj in env.objects:
        if obj.type.name != "MonoBehaviour":
            continue
        try:  # the script's own fields need a typetree; the header fields do not
            d = obj.read(check_read=False)
            if d.m_Script.read().m_ClassName == script:
                yield Holder(d.m_GameObject.read().m_Name, uf.archive, uf.name)
        except Exception:  # an object UnityPy cannot read is skipped
            continue


def holders(archives: dict[str, Path], script: str) -> list[Holder]:
    """Every object in the archives carrying a script of class `script`, sorted by name."""
    out: list[Holder] = []
    for archive, path in archives.items():
        if not path.is_file():
            continue
        with zipfile.ZipFile(path) as z:
            for uf in unity_files(z, archive):
                out.extend(holders_in(uf, b"".join(z.read(p) for p in uf.parts), script))
    return sorted(set(out), key=lambda h: (h.name, h.member))


def car_names(archives: dict[str, Path]) -> list[str]:
    """The names of the drivable cars (the cars.ini sections), sorted.

    Cached per archive file and its modification time: a scan reads every Unity file (10 s).
    """
    key = tuple(
        (a, str(p), p.stat().st_mtime_ns if p.is_file() else 0) for a, p in sorted(archives.items())
    )
    return list(_car_names(key))


@functools.cache
def _car_names(key: tuple[tuple[str, str, int], ...]) -> tuple[str, ...]:
    """car_names for archives given as (archive, path, mtime) -- hashable, so it can be cached."""
    archives = {a: Path(p) for a, p, _ in key}
    return tuple(sorted({h.name for h in holders(archives, CAR_SCRIPT)}))


def select(assets: Iterator[Asset], name: str, file: str | None) -> list[Asset]:
    """Assets named exactly `name` (and in Unity file `file`, short or full, if given)."""
    want = None if file is None else file.removeprefix(DATA_PREFIX)
    return [
        a
        for a in assets
        if a.name == name and (want is None or a.member.removeprefix(DATA_PREFIX) == want)
    ]


def find_file(z: zipfile.ZipFile, a: Asset) -> UnityFile:
    """The Unity file of an open archive that holds asset a."""
    return next(u for u in unity_files(z, a.archive) if u.name == a.member)


def export(archives: dict[str, Path], a: Asset, out: Path) -> None:
    """Writes a texture as PNG (or a TextAsset's bytes) to `out`.

    Raises:
        ValueError: the type cannot be exported.
    """
    if a.type not in EDITABLE:
        raise ValueError(f"{a.type} cannot be exported (Texture2D and TextAsset can)")
    with zipfile.ZipFile(archives[a.archive]) as z:
        env, _ = open_env(z, find_file(z, a))
    d = next(o for o in env.objects if o.path_id == a.path_id).read()
    if a.type == "Texture2D":
        d.image.save(out)
    else:
        script = d.m_Script
        out.write_bytes(
            script.encode("utf-8", "surrogateescape") if isinstance(script, str) else script
        )


def check_roundtrip(data: bytes, a: Asset, img: Image.Image | None) -> None:
    """Reloads a written Unity file and checks the object is there with the new content.

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


# ---- assets.ini and the build -------------------------------------------------------------


@dataclass(frozen=True)
class Wanted:
    """One assets.ini line: put `file` into the object called `name`."""

    mod: str  # mod id (folder name)
    line: int
    name: str
    member: str | None  # the Unity file when given as name@file
    file: Path  # the mod's file to put in


@dataclass(frozen=True)
class Replacement:
    """A Wanted line resolved to one object of the game."""

    wanted: Wanted
    asset: Asset


@dataclass
class Plan:
    """What build writes: replacements per Unity file and the mod that owns each file."""

    members: dict[tuple[str, str], list[Replacement]]  # (archive, Unity file) -> replacements
    owner: dict[tuple[str, str], str]  # (archive, Unity file) -> mod id
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
        wanted.append(Wanted(mod.name, num, m["name"].strip(), m["file"], mod / m["path"]))
    return wanted, errors


def texture_size(a: Asset) -> tuple[int, int] | None:
    """Width and height from a texture's detail ("230x96 RGB24")."""
    m = re.match(r"(\d+)x(\d+) ", a.detail + " ")
    return (int(m[1]), int(m[2])) if m and a.type == "Texture2D" else None


def plan_build(order: list[str], wanted: dict[str, list[Wanted]], assets: list[Asset]) -> Plan:
    """Resolves every mod's lines (mods in load order) and groups them by Unity file.

    A name must resolve to exactly one editable object; an object claimed by an earlier mod
    stays with it. The first mod to touch a Unity file owns the written file.
    """
    plan = Plan({}, {}, [], [])
    taken: dict[tuple[str, str, int], str] = {}
    for mod in order:
        for w in wanted.get(mod, []):
            where = f"{mod}: {ASSETS_INI}:{w.line}"
            found = select(iter(assets), w.name, w.member)
            if len(found) != 1:
                cands = ", ".join(f"{a.name}@{a.member.removeprefix(DATA_PREFIX)}" for a in found)
                hint = f" ({cands}): add @<file>" if found else " (see tools/mod_assets.py list)"
                plan.errors.append(f"{where}: {len(found)} editable objects named {w.name!r}{hint}")
                continue
            if not w.file.is_file():
                plan.errors.append(f"{where}: {w.file} does not exist")
                continue
            a = found[0]
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


def apply_file(z: zipfile.ZipFile, uf: UnityFile, reps: list[Replacement]) -> bytes:
    """The Unity file with all its replacements applied and read back.

    Raises:
        ValueError: a PNG has the wrong size, or a replacement does not read back.
    """
    env, file = open_env(z, uf)
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
    data: bytes = file.save()
    for r in reps:
        check_roundtrip(data, r.asset, images[r.asset.path_id])
    return data


def mod_order(mods: list[Path]) -> list[str]:
    """Mod ids in the port's load order: priority descending, then id (rd_modhost.c)."""
    from tools.mod_ini import read_mod  # here: the C parser is only needed to build

    prio = {m.name: read_mod(m).priority for m in mods}
    return sorted(prio, key=lambda i: (-prio[i], i))


def build(
    mods: list[Path], out: Path, archives: dict[str, Path] | None = None
) -> tuple[list[Path], list[str]]:
    """Builds the replaced Unity files of `mods` (folders) under out/<owner id>/files/.

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
        if errors:
            raise ValueError("\n".join(errors))
        return [], []
    plan = plan_build(mod_order(mods), wanted, list(scan(archives, EDITABLE)))
    errors += plan.errors
    jobs: list[tuple[UnityFile, list[Replacement], str]] = []
    for (archive, name), reps in plan.members.items():
        with zipfile.ZipFile(archives[archive]) as z:
            uf = next(u for u in unity_files(z, archive) if u.name == name)
        owner = plan.owner[(archive, name)]
        for m in mods:
            for part in uf.parts:
                if (m / "files" / archive / part).is_file():
                    errors.append(
                        f"{m.name}: files/{archive}/{part} is also built from {ASSETS_INI} "
                        f"({owner}); remove one"
                    )
        jobs.append((uf, reps, owner))
    if errors:
        raise ValueError("\n".join(errors))
    written: list[Path] = []
    for uf, reps, owner in jobs:
        with zipfile.ZipFile(archives[uf.archive]) as z:
            data = apply_file(z, uf, reps)
        for member, chunk in split_parts(uf, data):
            dest = out / owner / "files" / uf.archive / member
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(chunk)
            written.append(dest)
    return written, plan.warnings


# ---- command line -------------------------------------------------------------------------


def list_assets(
    archives: dict[str, Path], rx: re.Pattern[str], kind: str | None, archive: str | None
) -> int:
    """Prints matching objects; editable ones as assets.ini lines (name@file when needed)."""
    wanted = {k: v for k, v in archives.items() if not archive or k == archive}
    assets = [a for a in scan(wanted, (kind,) if kind else TYPES) if rx.search(a.name)]
    count: dict[str, int] = {}
    for a in scan(archives, EDITABLE):
        count[a.name] = count.get(a.name, 0) + 1
    for a in assets:
        short = a.member.removeprefix(DATA_PREFIX)
        if a.type in EDITABLE:
            name = a.name if count.get(a.name) == 1 else f"{a.name}@{short}"
            print(f"{name} = <file>    # {a.line()}")
        else:
            print(f"# {a.line()}   (not editable)")
    return 0


def main(argv: list[str] | None = None) -> int:
    """Command line: list / export / build / cars / objects."""
    ap = argparse.ArgumentParser(description="Game assets for asset mods (UnityPy).")
    sub = ap.add_subparsers(dest="cmd", required=True)
    ls = sub.add_parser("list", help="named objects, one line each")
    ls.add_argument("--name", default="", help="regex on the object name")
    ls.add_argument("--type", choices=TYPES, help="only this type")
    ls.add_argument("--archive", choices=("apk", "obb"), help="only this archive")
    ex = sub.add_parser("export", help="texture to PNG, TextAsset to a file")
    ex.add_argument("name")
    ex.add_argument("out", type=Path)
    ex.add_argument("--file", help="the Unity file (from list) when the name is not unique")
    bd = sub.add_parser("build", help="assets.ini of mods -> their files/ under --out")
    bd.add_argument("mods", nargs="+", type=Path, help="mod folders")
    bd.add_argument("--out", type=Path, required=True, help="where the mods are staged")
    sub.add_parser("cars", help="the drivable cars' names (cars.ini sections)")
    ob = sub.add_parser("objects", help="game objects carrying a script of a class")
    ob.add_argument("--component", required=True, help="the script's class, e.g. Drivetrain")
    args = ap.parse_args(argv)
    archives = user_archives()
    if not any(p.is_file() for p in archives.values()):
        print("mod_assets: no base.apk / OBB in original/", file=sys.stderr)
        return 1
    if args.cmd == "cars":
        print("\n".join(car_names(archives)))
        return 0
    if args.cmd == "objects":
        for h in holders(archives, args.component):
            print(f"{h.name}    # {h.archive} {h.member.removeprefix(DATA_PREFIX)}")
        return 0
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
    found = select(scan(archives, EDITABLE), args.name, args.file)
    if len(found) != 1:
        hint = "; pass --file" if found else ""
        print(f"mod_assets: {len(found)} objects named {args.name!r}{hint}", file=sys.stderr)
        for a in found:
            print(a.line(), file=sys.stderr)
        return 1
    try:
        export(archives, found[0], args.out)
    except (ValueError, OSError) as e:
        print(f"mod_assets: {e}", file=sys.stderr)
        return 1
    print(args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
