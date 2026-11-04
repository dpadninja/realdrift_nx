"""Prepare the Switch SD-card folder for realdrift_nx from your own Real Drift APK and OBB.

    python3 stage_sd.py base.apk main.76.com.realdrift.sipon.obb

creates ./switch/realdrift_nx/ (copy the `switch` folder to the root of the SD card):
    realdrift_nx.nro                   (taken from next to this script, or --nro)
    libmain.so libunity.so libil2cpp.so  (lib/arm64-v8a/ from the APK)
    base.apk                           (the APK itself: the game data is read from it)
    main.<versionCode>.<package>.obb
    icon.png                           (the game's icon from the APK, 256x256, for a forwarder)

The APK and OBB must be the game version the loader supports; anything else is refused.
Needs Python 3.8+ and nothing else.

realdrift_nx, MIT (see LICENSE in https://github.com/dpadninja/realdrift_nx).
"""

from __future__ import annotations

import argparse
import re
import shutil
import struct
import sys
import zipfile
import zlib
from pathlib import Path

# >>> game identity: tools/release.py replaces this block with literal values
if __package__ in (None, ""):  # run as a script from the repo: make `tools` importable
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.game import GAME, LITE_PACKAGE, obb_name

# <<< game identity

LIB_DIR = "lib/arm64-v8a/"
GAME_DATA_MARKER = "assets/bin/Data/globalgamemanagers"  # any Unity game APK has it
GUID = re.compile(r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}")


# ---- APK identity from the binary AndroidManifest.xml (no third-party parser) ----------


def axml_strings(data: bytes) -> list[str]:
    """The string pool of an Android binary XML file (ResStringPool, UTF-8 or UTF-16)."""
    if len(data) < 8 + 28:
        raise ValueError("AndroidManifest.xml is too short")
    pool = 8  # first chunk after the XML header
    typ, _hdr, _size, count, _styles, flags, strings_at, _ = struct.unpack_from(
        "<HHIIIIII", data, pool
    )
    if typ != 0x0001:
        raise ValueError("AndroidManifest.xml has no string pool")
    utf8 = bool(flags & 0x100)
    offsets = struct.unpack_from(f"<{count}I", data, pool + 28)
    out: list[str] = []
    for off in offsets:
        p = pool + strings_at + off
        if utf8:
            for _ in range(2):  # char count, then byte count (1 or 2 bytes each)
                n = data[p]
                p += 1
                if n & 0x80:
                    n = ((n & 0x7F) << 8) | data[p]
                    p += 1
            out.append(data[p : p + n].decode("utf-8", "replace"))
        else:
            n = struct.unpack_from("<H", data, p)[0]
            p += 2
            if n & 0x8000:
                n = ((n & 0x7FFF) << 16) | struct.unpack_from("<H", data, p)[0]
                p += 2
            out.append(data[p : p + 2 * n].decode("utf-16-le", "replace"))
    return out


def apk_strings(apk: Path) -> list[str]:
    """The string pool of the APK's binary AndroidManifest.xml."""
    with zipfile.ZipFile(apk) as z:
        return axml_strings(z.read("AndroidManifest.xml"))


def check_apk_identity(strings: list[str], package: str, build_id: str) -> None:
    """Checks that the APK is the game build the loader supports.

    Its package name and the unity.build-id meta-data value (unique per build) must be in the
    manifest's string pool.

    Raises:
        ValueError: another package (e.g. Real Drift Lite) or another game version.
    """
    if package not in strings:
        if LITE_PACKAGE in strings:
            raise ValueError(
                f"this is Real Drift Lite ({LITE_PACKAGE}); only the full game "
                f"({package}) is supported"
            )
        raise ValueError(f"this APK is not {package} (Real Drift)")
    if build_id not in strings:
        found = [s for s in strings if GUID.fullmatch(s)]
        raise ValueError(
            f"unsupported game version: unity.build-id {found or '?'}, the loader needs "
            f"{GAME.version_name} (versionCode {GAME.version_code}, build-id {build_id})"
        )


def check_obb_marker(names: list[str], build_id: str) -> None:
    """Libunity mounts the OBB only if it contains assets/<unity.build-id>."""
    if f"assets/{build_id}" not in names:
        markers = [n for n in names if re.fullmatch(r"assets/" + GUID.pattern, n)]
        raise ValueError(
            f"the OBB is from another game version (marker {markers}, expected assets/{build_id})"
        )


# ---- staging ---------------------------------------------------------------------------


def plan_apk(names: list[str]) -> list[tuple[str, str]]:
    """Map APK member names to destination paths relative to the SD folder (the libraries)."""
    plan: list[tuple[str, str]] = []
    for lib in GAME.libs:
        member = LIB_DIR + lib
        if member not in names:
            raise ValueError(f"APK has no {member} (need the arm64-v8a build)")
        plan.append((member, lib))
    if GAME_DATA_MARKER not in names:
        raise ValueError(f"APK has no {GAME_DATA_MARKER} (not the game APK?)")
    return plan


def find_obb_member(names: list[str], version_code: int) -> str:
    """The main OBB of this build inside an archive that wraps it (as downloaded)."""
    want = obb_name(version_code)
    obbs = [n for n in names if Path(n).name == want]
    if len(obbs) != 1:
        found = [n for n in names if n.endswith(".obb")]
        raise ValueError(f"expected exactly one {want} in the archive, found {found}")
    return obbs[0]


# ---- forwarder icon from the APK's launcher icon (PNG in, PNG out, stdlib only) ------------

ICON_SIZE = 256  # the Switch's application icon is 256x256
ICON_FILE = "icon.png"  # written into the game folder with the rest
APP_ICONS = [  # the game's launcher icon, largest first
    f"res/mipmap-{d}/app_icon.png" for d in ("xxxhdpi", "xxhdpi", "xhdpi", "hdpi", "mdpi")
]
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_CHANNELS = {2: 3, 6: 4}  # colour type -> channels: RGB, RGBA (8 bits each)


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read_png(data: bytes) -> tuple[int, int, bytes]:
    """Decodes an 8-bit RGB or RGBA PNG without interlacing (what Android icons are).

    Args:
        data: the PNG file.

    Returns:
        Width, height and the pixels as RGBA rows (an RGB image gets alpha 255).

    Raises:
        ValueError: not a PNG, or a kind of PNG this decoder does not read.
    """
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("not a PNG file")
    pos, idat, header = len(PNG_SIGNATURE), [], b""
    while pos + 8 <= len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        kind, body = data[pos + 4 : pos + 8], data[pos + 8 : pos + 8 + length]
        if kind == b"IHDR":
            header = body
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
        pos += 12 + length
    if len(header) != 13:
        raise ValueError("PNG without a header")
    w, h, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", header)
    if depth != 8 or colour not in PNG_CHANNELS or interlace:
        raise ValueError(
            f"unsupported PNG (depth {depth}, colour type {colour}, interlace {interlace})"
        )
    ch = PNG_CHANNELS[colour]
    raw, stride = zlib.decompress(b"".join(idat)), w * ch
    if len(raw) < h * (stride + 1):
        raise ValueError("truncated PNG")
    rows, prev = bytearray(), bytearray(stride)
    for y in range(h):
        kind, line = (
            raw[y * (stride + 1)],
            bytearray(raw[y * (stride + 1) + 1 : (y + 1) * (stride + 1)]),
        )
        for x in range(stride):
            a = line[x - ch] if x >= ch else 0
            b = prev[x]
            c = prev[x - ch] if x >= ch else 0
            if kind == 1:
                line[x] = (line[x] + a) & 0xFF
            elif kind == 2:
                line[x] = (line[x] + b) & 0xFF
            elif kind == 3:
                line[x] = (line[x] + (a + b) // 2) & 0xFF
            elif kind == 4:
                line[x] = (line[x] + _paeth(a, b, c)) & 0xFF
            elif kind != 0:
                raise ValueError(f"bad PNG filter {kind}")
        rows += line
        prev = line
    if ch == 4:
        return w, h, bytes(rows)
    rgba = bytearray(w * h * 4)
    rgba[0::4], rgba[1::4], rgba[2::4] = rows[0::3], rows[1::3], rows[2::3]
    rgba[3::4] = b"\xff" * (w * h)
    return w, h, bytes(rgba)


def edge_fill(rgba: bytes, w: int, h: int) -> bytes:
    """Makes RGBA pixels opaque, extending the picture's edge colours into its transparent parts.

    The Android icon has transparent rounded corners and the Switch's icons have no alpha.
    Each pixel that is not fully opaque gets a background: the average of its already filled
    8 neighbours, growing ring by ring from the opaque pixels; the pixel's own colour is then
    laid over it by its alpha (the anti-aliased rim stays smooth). A fully transparent image
    becomes black.

    Returns:
        RGB pixels, w x h.
    """
    n = w * h
    bg = [[0, 0, 0] for _ in range(n)]
    known = [rgba[i * 4 + 3] == 255 for i in range(n)]
    for i in range(n):
        if known[i]:
            bg[i] = list(rgba[i * 4 : i * 4 + 3])
    todo = [i for i in range(n) if not known[i]]
    while todo and any(known):
        filled, rest = [], []
        for i in todo:
            x, y = i % w, i // w
            near = [
                (y + dy) * w + x + dx
                for dy in (-1, 0, 1)
                for dx in (-1, 0, 1)
                if (dx or dy)
                and 0 <= x + dx < w
                and 0 <= y + dy < h
                and known[(y + dy) * w + x + dx]
            ]
            if near:
                filled.append((i, [sum(bg[k][c] for k in near) // len(near) for c in range(3)]))
            else:
                rest.append(i)
        for i, colour in filled:
            bg[i], known[i] = colour, True
        todo = rest
    out = bytearray(n * 3)
    for i in range(n):
        alpha = rgba[i * 4 + 3]
        for c in range(3):
            out[i * 3 + c] = (rgba[i * 4 + c] * alpha + bg[i][c] * (255 - alpha) + 127) // 255
    return bytes(out)


def resize_rgb(rgb: bytes, w: int, h: int, nw: int, nh: int) -> bytes:
    """Bilinear resize of RGB pixels from w x h to nw x nh (pixel centres aligned)."""
    out = bytearray(nw * nh * 3)
    for y in range(nh):
        sy = min(max((y + 0.5) * h / nh - 0.5, 0.0), h - 1.0)
        y0 = int(sy)
        y1, fy = min(y0 + 1, h - 1), sy - y0
        for x in range(nw):
            sx = min(max((x + 0.5) * w / nw - 0.5, 0.0), w - 1.0)
            x0 = int(sx)
            x1, fx = min(x0 + 1, w - 1), sx - x0
            for c in range(3):
                top = rgb[(y0 * w + x0) * 3 + c] * (1 - fx) + rgb[(y0 * w + x1) * 3 + c] * fx
                bot = rgb[(y1 * w + x0) * 3 + c] * (1 - fx) + rgb[(y1 * w + x1) * 3 + c] * fx
                out[(y * nw + x) * 3 + c] = int(top * (1 - fy) + bot * fy + 0.5)
    return bytes(out)


def write_png(w: int, h: int, rgb: bytes) -> bytes:
    """Encodes RGB pixels as an 8-bit PNG (no filtering)."""

    def chunk(kind: bytes, body: bytes) -> bytes:
        return (
            struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
        )

    rows = b"".join(b"\x00" + rgb[y * w * 3 : (y + 1) * w * 3] for y in range(h))
    return (
        PNG_SIGNATURE
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows, 9))
        + chunk(b"IEND", b"")
    )


def make_icon(apk: Path, out: Path) -> str:
    """Writes the game's launcher icon as an opaque 256x256 PNG (corners filled from the edges).

    Args:
        apk: the game's base.apk.
        out: the PNG to write.

    Returns:
        The APK member the icon came from.

    Raises:
        ValueError: the APK has no launcher icon this script can read.
        OSError: a file cannot be read or written.
    """
    with zipfile.ZipFile(apk) as z:
        names = set(z.namelist())
        member = next((n for n in APP_ICONS if n in names), None)
        if member is None:
            raise ValueError("no launcher icon (res/mipmap-*/app_icon.png) in the APK")
        w, h, rgba = read_png(z.read(member))
    rgb = resize_rgb(edge_fill(rgba, w, h), w, h, ICON_SIZE, ICON_SIZE)
    out.write_bytes(write_png(ICON_SIZE, ICON_SIZE, rgb))
    return member


def stage(
    apk: Path,
    obb: Path,
    out: Path,
    nro: Path | None,
    version_code: int,
    build_id: str | None = None,
) -> None:
    """Writes the SD-card folder `out`.

    Args:
        apk: the game's base.apk; its three libraries are extracted, the APK itself is copied.
        obb: the main OBB, or a zip archive that contains it.
        out: the folder to create (switch/realdrift_nx).
        nro: the loader to copy next to the game files, or None.
        version_code: the game's versionCode (names the OBB).
        build_id: if given, the OBB must carry the matching assets/<unity.build-id> marker.

    Raises:
        ValueError: the APK or OBB is not the supported game build.
        OSError: a file cannot be read or written.
    """
    out.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(apk) as z:
        for member, dest in plan_apk(z.namelist()):
            with z.open(member) as src, (out / dest).open("wb") as dst:
                shutil.copyfileobj(src, dst)

    shutil.copyfile(apk, out / GAME.base_apk)

    obb_target = out / obb_name(version_code)
    if obb.suffix == ".obb":
        shutil.copyfile(obb, obb_target)
    else:  # an archive wrapping the .obb (as downloaded)
        with zipfile.ZipFile(obb) as z, z.open(
            find_obb_member(z.namelist(), version_code)
        ) as src, obb_target.open("wb") as dst:
            shutil.copyfileobj(src, dst)

    if build_id:
        with zipfile.ZipFile(obb_target) as z:
            check_obb_marker(z.namelist(), build_id)

    if nro:
        shutil.copyfile(nro, out / nro.name)


def main(argv: list[str] | None = None) -> int:
    """Command line; returns 0 on success, 1 with a one-line error otherwise."""
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("apk", type=Path, help="base.apk of Real Drift")
    ap.add_argument("obb", type=Path, help=f"{GAME.obb_name} (or a zip containing it)")
    ap.add_argument(
        "--out",
        type=Path,
        default=Path("switch") / Path(GAME.home_dir).name,
        help="output folder (default: ./switch/realdrift_nx)",
    )
    ap.add_argument(
        "--nro",
        type=Path,
        default=Path(__file__).resolve().parent / "realdrift_nx.nro",
        help="the loader (default: realdrift_nx.nro next to this script)",
    )
    args = ap.parse_args(argv)
    try:
        check_apk_identity(apk_strings(args.apk), GAME.package, GAME.build_id)
        nro = args.nro if args.nro.exists() else None
        stage(args.apk, args.obb, args.out, nro, GAME.version_code, GAME.build_id)
    except (ValueError, OSError, zipfile.BadZipFile) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(
        f"done: {args.out}"
        + ("" if nro else f"  (no {args.nro.name} found: copy it there yourself)")
    )
    icon = args.out / ICON_FILE
    try:  # a nicety: the SD folder is ready either way
        member = make_icon(args.apk, icon)
        print(f"icon: {icon} ({ICON_SIZE}x{ICON_SIZE}, from {member}) for a forwarder")
    except (ValueError, OSError, zipfile.BadZipFile, zlib.error) as e:
        print(f"warning: no icon: {e}", file=sys.stderr)
    if args.out.parent.name == "switch":
        print(f"Copy the '{args.out.parent}' folder to the root of the SD card.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
