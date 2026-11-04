"""tools/make_icon.py: the Switch icon from the APK's launcher icon (PNG decode, edge fill,
resize, encode) and its command line.

MIT (see LICENSE).
"""

import struct
import zipfile
import zlib
from pathlib import Path

import pytest

from tools.game import ROOT
from tools.make_icon import (
    APP_ICONS,
    ICON_SIZE,
    edge_fill,
    main,
    make_icon,
    read_png,
    resize_rgb,
    write_png,
)


def encode_png(w: int, h: int, pixels: bytes, channels: int, filters: list[int]) -> bytes:
    """A PNG of `pixels` whose row y uses filter filters[y % len(filters)] (0..4)."""

    def paeth(a: int, b: int, c: int) -> int:
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        return a if pa <= pb and pa <= pc else b if pb <= pc else c

    stride, out, prev = w * channels, bytearray(), bytes(w * channels)
    for y in range(h):
        line, f = pixels[y * stride : (y + 1) * stride], filters[y % len(filters)]
        enc = bytearray()
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b, c = prev[x], prev[x - channels] if x >= channels else 0
            pred = [0, a, b, (a + b) // 2, paeth(a, b, c)][f]
            enc.append((line[x] - pred) & 0xFF)
        out += bytes([f]) + enc
        prev = line

    def chunk(kind: bytes, body: bytes) -> bytes:
        return (
            struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
        )

    colour = {3: 2, 4: 6}[channels]
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, colour, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(out)))
        + chunk(b"IEND", b"")
    )


def pattern(w: int, h: int, channels: int) -> bytes:
    """Pixels that differ in every channel, row and column (so each filter really predicts)."""
    return bytes(
        (x * 37 + y * 101 + c * 59) & 0xFF
        for y in range(h)
        for x in range(w)
        for c in range(channels)
    )


@pytest.mark.parametrize("channels", [3, 4])
def test_read_png_every_filter(channels: int):
    """Android icons use all five PNG row filters; RGBA comes back as is, RGB gets alpha 255."""
    w, h = 7, 10
    px = pattern(w, h, channels)
    rw, rh, rgba = read_png(encode_png(w, h, px, channels, [0, 1, 2, 3, 4]))
    assert (rw, rh) == (w, h)
    if channels == 4:
        assert rgba == px
    else:
        assert rgba[0::4] == px[0::3] and rgba[2::4] == px[2::3]
        assert set(rgba[3::4]) == {255}


def test_read_png_rejects_what_it_cannot_read():
    """Not a PNG, a palette / 16-bit / interlaced PNG and truncated data are errors, not garbage."""
    with pytest.raises(ValueError, match="not a PNG"):
        read_png(b"GIF89a")
    good = encode_png(2, 2, pattern(2, 2, 4), 4, [0])
    for colour, depth, interlace in [(3, 8, 0), (6, 16, 0), (6, 8, 1)]:
        ihdr = struct.pack(">IIBBBBB", 2, 2, depth, colour, 0, 0, interlace)
        bad = good[:16] + ihdr + struct.pack(">I", zlib.crc32(b"IHDR" + ihdr)) + good[33:]
        with pytest.raises(ValueError, match="unsupported PNG"):
            read_png(bad)
    short = encode_png(2, 2, pattern(2, 2, 4), 4, [0])
    body = zlib.compress(b"\x00" * 5)
    trunc = short[:33] + struct.pack(">I", len(body)) + b"IDAT" + body + b"\0\0\0\0"
    with pytest.raises(ValueError, match="truncated"):
        read_png(trunc)


def test_edge_fill_extends_the_edges():
    """Transparent corners take the colour of the picture next to them (no black corners on the
    Switch), opaque pixels keep theirs, and a half-transparent rim pixel is laid over that fill.
    """
    red, blue, clear = [255, 0, 0, 255], [0, 0, 255, 255], [0, 0, 0, 0]
    # 3x2: row 0 clear red red, row 1 clear clear blue
    rgb = edge_fill(bytes(clear + red + red + clear + clear + blue), 3, 2)
    assert rgb[3:9] == bytes([255, 0, 0] * 2) and rgb[15:18] == bytes([0, 0, 255])
    assert rgb[0:3] == bytes([255, 0, 0])  # corner: only red is next to it
    assert rgb[12:15] == bytes([170, 0, 85])  # two reds and a blue around it
    # a pixel no opaque one touches is filled in a later ring, from the filled ones
    assert edge_fill(bytes(blue + clear * 3), 4, 1) == bytes([0, 0, 255] * 4)
    rim = edge_fill(bytes([255, 255, 255, 128, 0, 0, 0, 255]), 2, 1)
    assert rim == bytes([128, 128, 128, 0, 0, 0])  # white at half alpha over the black edge
    assert edge_fill(bytes(clear * 4), 2, 2) == bytes(12)  # nothing opaque: black


def test_resize_rgb():
    """Same size is the identity, a flat colour stays flat, and scaling keeps the image's order
    (left stays left).
    """
    px = pattern(5, 4, 3)
    assert resize_rgb(px, 5, 4, 5, 4) == px
    assert set(resize_rgb(bytes([9, 8, 7]) * 4, 2, 2, 6, 6)) == {9, 8, 7}
    two = bytes([0, 0, 0, 255, 255, 255])  # black | white
    big = resize_rgb(two, 2, 1, 8, 1)
    assert big[0] == 0 and big[-1] == 255 and list(big[0::3]) == sorted(big[0::3])


def test_write_png_round_trips():
    """What write_png encodes, read_png reads back (opaque)."""
    px = pattern(9, 6, 3)
    w, h, rgba = read_png(write_png(9, 6, px))
    assert (w, h) == (9, 6) and rgba[0::4] == px[0::3] and set(rgba[3::4]) == {255}


def write_icon_apk(path: Path, member: str | None) -> Path:
    """An APK (a zip) with, if member is given, a 4x4 red icon there."""
    with zipfile.ZipFile(path, "w") as z:
        z.writestr("AndroidManifest.xml", b"")
        if member:
            z.writestr(member, encode_png(4, 4, bytes([255, 0, 0, 255]) * 16, 4, [0]))
    return path


def test_make_icon(tmp_path: Path):
    """The largest launcher icon in the APK becomes an opaque 256x256 PNG; an APK without one is
    an error.
    """
    apk = write_icon_apk(tmp_path / "base.apk", APP_ICONS[1])
    assert make_icon(apk, tmp_path / "icon.png") == APP_ICONS[1]
    w, h, rgba = read_png((tmp_path / "icon.png").read_bytes())
    assert (w, h) == (ICON_SIZE, ICON_SIZE)
    assert set(rgba[0::4]) == {255} and set(rgba[1::4]) == {0} and set(rgba[3::4]) == {255}
    with pytest.raises(ValueError, match="no launcher icon"):
        make_icon(write_icon_apk(tmp_path / "none.apk", None), tmp_path / "x.png")


def test_main(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """The output defaults to icon.png in the current folder; an APK without an icon or a
    missing file is one error line and exit code 1, not a traceback.
    """
    apk = write_icon_apk(tmp_path / "base.apk", APP_ICONS[0])
    monkeypatch.chdir(tmp_path)
    assert main([str(apk)]) == 0
    assert read_png((tmp_path / "icon.png").read_bytes())[:2] == (ICON_SIZE, ICON_SIZE)
    assert main([str(apk), "other.png"]) == 0 and (tmp_path / "other.png").exists()
    assert main([str(write_icon_apk(tmp_path / "noicon.apk", None)), "i.png"]) == 1
    assert main([str(tmp_path / "missing.apk")]) == 1


def test_real_icon(tmp_path: Path):
    """On the user's real APK: the game's icon is found and decoded."""
    if not (ROOT / "original/base.apk").exists():
        pytest.skip("no original/base.apk")
    assert make_icon(ROOT / "original/base.apk", tmp_path / "icon.png") == APP_ICONS[0]
    assert read_png((tmp_path / "icon.png").read_bytes())[:2] == (ICON_SIZE, ICON_SIZE)
