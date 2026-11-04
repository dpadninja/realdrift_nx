"""Make a Switch icon from the Real Drift APK: the game's launcher icon as a 256x256 PNG.

    python3 make_icon.py base.apk icon.png

Takes the largest res/mipmap-*/app_icon.png from the APK, fills its transparent rounded
corners with the colours of the picture's edges (the Switch's icons are opaque), scales it to
256x256 and writes an RGB PNG (for a forwarder or a theme). The output defaults to icon.png.

Ships in the release next to stage_sd.py: standard library only, Python 3.8+.

realdrift_nx, MIT (see LICENSE in https://github.com/dpadninja/realdrift_nx).
"""

from __future__ import annotations

import argparse
import struct
import sys
import zipfile
import zlib
from pathlib import Path

ICON_SIZE = 256  # the Switch's application icon is 256x256
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


def main(argv: list[str] | None = None) -> int:
    """Command line: APK in, icon PNG out. Returns the exit code (1 on a user error)."""
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("apk", type=Path, help="base.apk of Real Drift")
    ap.add_argument(
        "out", type=Path, nargs="?", default=Path("icon.png"), help="the PNG to write (icon.png)"
    )
    args = ap.parse_args(argv)
    try:
        member = make_icon(args.apk, args.out)
    except (ValueError, OSError, zipfile.BadZipFile) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"icon: {args.out} ({ICON_SIZE}x{ICON_SIZE}, from {member})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
