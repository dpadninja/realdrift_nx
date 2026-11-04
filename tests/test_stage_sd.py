"""tools/stage_sd.py: identity checks of the APK/OBB and the staged SD folder.

MIT (see LICENSE).
"""

import struct
import zipfile
import zlib
from pathlib import Path

import pytest

from tools.game import GAME, LITE_BUILD_ID, LITE_PACKAGE, RELEASES, ROOT, obb_name
from tools.stage_sd import (
    APP_ICONS,
    ICON_SIZE,
    apk_strings,
    axml_strings,
    check_apk_identity,
    check_obb_marker,
    edge_fill,
    find_obb_member,
    main,
    make_icon,
    plan_apk,
    read_png,
    resize_rgb,
    stage,
    write_png,
)

BUILD_ID = GAME.build_id
BUILD_ID_507 = RELEASES["5.0.7"].build_id
OBB = GAME.obb_name  # main.76.com.realdrift.sipon.obb

APK_NAMES = [
    "AndroidManifest.xml",
    "lib/armeabi-v7a/libunity.so",
    *(f"lib/arm64-v8a/{lib}" for lib in GAME.libs),
    "assets/bin/Data/",
    "assets/bin/Data/boot.config",
    "assets/bin/Data/globalgamemanagers",
]


def utf8_len(n: int) -> bytes:
    """Length prefix of a UTF-8 string-pool entry (one byte, or two with the high bit set)."""
    return bytes([n]) if n < 0x80 else bytes([0x80 | (n >> 8), n & 0xFF])


def make_axml(strings: list[str], utf8: bool = False) -> bytes:
    """A minimal Android binary XML: header + string pool (enough for axml_strings)."""
    data, offsets = b"", []
    for s in strings:
        offsets.append(len(data))
        if utf8:
            b = s.encode()
            data += utf8_len(len(s)) + utf8_len(len(b)) + b + b"\0"
        else:
            data += struct.pack("<H", len(s)) + s.encode("utf-16-le") + b"\0\0"
    strings_at = 28 + 4 * len(strings)
    pool = struct.pack(
        "<HHIIIIII",
        0x0001,
        28,
        strings_at + len(data),
        len(strings),
        0,
        0x100 if utf8 else 0,
        strings_at,
        0,
    )
    pool += struct.pack(f"<{len(strings)}I", *offsets) + data
    return struct.pack("<HHI", 0x0003, 8, 8 + len(pool)) + pool


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


def write_apk(
    path: Path,
    manifest: list[str] = (GAME.package, "unity.build-id", BUILD_ID),
    icon: str | None = APP_ICONS[0],
) -> Path:
    """A fake game APK: the arm64 and armv7 libraries, the Unity data marker, a manifest with
    the given strings and, unless icon is None, a 4x4 red launcher icon at that member.
    """
    with zipfile.ZipFile(path, "w") as z:
        for n in APK_NAMES:
            if not n.endswith("/"):
                z.writestr(n, make_axml(list(manifest)) if n == "AndroidManifest.xml" else n)
        if icon:
            z.writestr(icon, encode_png(4, 4, bytes([255, 0, 0, 255]) * 16, 4, [0]))
    return path


def write_obb(path: Path, build_id: str = BUILD_ID) -> Path:
    """A fake OBB with the assets/<unity.build-id> marker libunity checks before mounting it."""
    with zipfile.ZipFile(path, "w") as z:
        z.writestr(f"assets/{build_id}", b"")
    return path


@pytest.mark.parametrize("utf8", [False, True])
def test_axml_strings(utf8: bool):
    """The binary-manifest reader decodes both string-pool encodings (UTF-16 and UTF-8), non-
    ASCII text and long strings with two-byte lengths.
    """
    strings = ["manifest", GAME.package, "Ünïcode", "x" * 200]
    assert axml_strings(make_axml(strings, utf8)) == strings


def test_game_identity_comes_from_the_loader_header():
    """The staging script's game identity is the one in rd_game.h (package, versionCode, OBB and
    APK names) and agrees with the release table.
    """
    assert GAME.package == "com.realdrift.sipon" and GAME.version_code == 76
    assert OBB == "main.76.com.realdrift.sipon.obb" == obb_name(76)
    assert GAME.base_apk == "base.apk"
    assert RELEASES["5.0.8"].version_code == GAME.version_code
    assert RELEASES["5.0.8"].build_id == GAME.build_id


def test_plan_apk_takes_arm64_libs_only():
    """Only the three arm64-v8a libraries are taken from the APK; the 32-bit ones would not load."""
    assert plan_apk(APK_NAMES) == [(f"lib/arm64-v8a/{lib}", lib) for lib in GAME.libs]


def test_plan_apk_rejects_non_game_apk():
    """An APK without Unity's globalgamemanagers is not the game and is refused with that reason."""
    with pytest.raises(ValueError, match="globalgamemanagers"):
        plan_apk([f"lib/arm64-v8a/{lib}" for lib in GAME.libs])


def test_plan_apk_rejects_32bit_only_apk():
    """An APK with only 32-bit libraries is refused and the message names arm64-v8a, the build
    the user needs.
    """
    with pytest.raises(ValueError, match="arm64-v8a"):
        plan_apk(["lib/armeabi-v7a/libunity.so", "assets/bin/Data/globalgamemanagers"])


def test_find_obb_member_checks_the_version_code():
    """Inside a downloaded archive only the OBB of this versionCode is accepted; an OBB of
    another build or another game is refused.
    """
    assert find_obb_member(["x/", f"{GAME.package}/{OBB}"], 76) == f"{GAME.package}/{OBB}"
    with pytest.raises(ValueError, match=r"main\.74"):
        find_obb_member([f"{GAME.package}/{OBB}"], 74)  # an OBB of another build
    with pytest.raises(ValueError):
        find_obb_member(["main.1.other.game.obb"], 76)


def test_stage_end_to_end(tmp_path: Path):
    """Staging from an APK and a zipped OBB yields the libraries, base.apk, the OBB and the .nro,
    and nothing extracted from the APK (the game reads its data from base.apk).
    """
    apk = write_apk(tmp_path / "game.apk")
    obb_zip = tmp_path / "obb.zip"
    with zipfile.ZipFile(obb_zip, "w") as z:
        inner = tmp_path / "inner.obb"
        with zipfile.ZipFile(inner, "w") as o:
            o.writestr(f"assets/{BUILD_ID}", b"")
        z.write(inner, f"{GAME.package}/{OBB}")
    nro = tmp_path / "realdrift_nx.nro"
    nro.write_bytes(b"NRO")

    out = tmp_path / "sd"
    stage(apk, obb_zip, out, nro, GAME.version_code, BUILD_ID)

    assert (out / "libunity.so").read_text() == "lib/arm64-v8a/libunity.so"
    assert not (out / "assets").exists()  # data stays inside base.apk
    with zipfile.ZipFile(out / OBB) as z:
        assert z.namelist() == [f"assets/{BUILD_ID}"]
    assert (out / "realdrift_nx.nro").read_bytes() == b"NRO"
    assert (out / GAME.base_apk).read_bytes() == apk.read_bytes()
    assert not (out / "lib").exists()


def test_check_obb_marker():
    """The OBB must carry the marker of the supported build; the 5.0.7 marker is refused as
    another game version.
    """
    check_obb_marker(["assets/bin/Data/x", f"assets/{BUILD_ID}"], BUILD_ID)
    with pytest.raises(ValueError, match="another game version"):
        check_obb_marker([f"assets/{BUILD_ID_507}"], BUILD_ID)


def test_stage_rejects_obb_without_marker(tmp_path: Path):
    """Staging fails when the OBB lacks the build marker, instead of producing a folder the game
    would not start from.
    """
    apk = write_apk(tmp_path / "game.apk")
    obb = tmp_path / "main.obb"
    with zipfile.ZipFile(obb, "w") as z:
        z.writestr("assets/bin/Data/level1", b"")
    with pytest.raises(ValueError, match="marker"):
        stage(apk, obb, tmp_path / "sd", None, GAME.version_code, BUILD_ID)


def test_check_apk_identity():
    """The APK must be the full game of the supported build: Real Drift Lite and the 5.0.7 build
    are refused with messages naming them.
    """
    check_apk_identity([GAME.package, BUILD_ID], GAME.package, BUILD_ID)
    with pytest.raises(ValueError, match="Lite"):
        check_apk_identity([LITE_PACKAGE, LITE_BUILD_ID], GAME.package, BUILD_ID)
    with pytest.raises(ValueError, match=BUILD_ID_507):
        check_apk_identity([GAME.package, BUILD_ID_507], GAME.package, BUILD_ID)


def test_main_end_to_end(tmp_path: Path, monkeypatch):
    """The command line stages the folder and exits 0, and exits 1 with a message (no traceback)
    for an unsupported APK.
    """
    apk, obb = write_apk(tmp_path / "base.apk"), write_obb(tmp_path / OBB)
    monkeypatch.chdir(tmp_path)
    assert main([str(apk), str(obb), "--nro", str(tmp_path / "none.nro")]) == 0
    assert (tmp_path / "switch/realdrift_nx" / OBB).exists()
    w, h, _ = read_png((tmp_path / "switch/realdrift_nx/icon.png").read_bytes())
    assert (w, h) == (ICON_SIZE, ICON_SIZE)
    bad = write_apk(tmp_path / "lite.apk", [LITE_PACKAGE, LITE_BUILD_ID])
    assert main([str(bad), str(obb)]) == 1


def test_main_without_icon_still_stages(tmp_path: Path, monkeypatch, capsys):
    """An APK whose icon cannot be read still gives the SD folder (exit 0) with a warning: the
    icon is a nicety, the game does not need it.
    """
    apk, obb = write_apk(tmp_path / "base.apk", icon=None), write_obb(tmp_path / OBB)
    monkeypatch.chdir(tmp_path)
    assert main([str(apk), str(obb), "--out", "sd/switch/realdrift_nx"]) == 0
    assert (tmp_path / "sd/switch/realdrift_nx" / OBB).exists()
    assert not (tmp_path / "sd/switch/realdrift_nx/icon.png").exists()
    assert "warning: no icon" in capsys.readouterr().err


@pytest.mark.parametrize(
    "apk, ok", [("original/base.apk", True), ("analysis/realdrift-5.0.7-build74.apk", False)]
)
def test_real_apks(apk: str, ok: bool):
    """On the user's real files: the 5.0.8 APK passes the identity check and the old 5.0.7 APK is
    refused.
    """
    if not (ROOT / apk).exists():
        pytest.skip(f"no {apk}")
    strings = apk_strings(ROOT / apk)
    if ok:
        check_apk_identity(strings, GAME.package, BUILD_ID)
    else:
        with pytest.raises(ValueError, match="unsupported game version"):
            check_apk_identity(strings, GAME.package, BUILD_ID)


# ---- forwarder icon ---------------------------------------------------------------------


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


def test_make_icon(tmp_path: Path):
    """The largest launcher icon in the APK becomes an opaque 256x256 PNG; an APK without one is
    an error.
    """
    apk = write_apk(tmp_path / "base.apk", icon=APP_ICONS[1])
    assert make_icon(apk, tmp_path / "icon.png") == APP_ICONS[1]
    w, h, rgba = read_png((tmp_path / "icon.png").read_bytes())
    assert (w, h) == (ICON_SIZE, ICON_SIZE)
    assert set(rgba[0::4]) == {255} and set(rgba[1::4]) == {0} and set(rgba[3::4]) == {255}
    with pytest.raises(ValueError, match="no launcher icon"):
        make_icon(write_apk(tmp_path / "none.apk", icon=None), tmp_path / "x.png")


def test_real_icon(tmp_path: Path):
    """On the user's real APK: the game's icon is found and decoded."""
    if not (ROOT / "original/base.apk").exists():
        pytest.skip("no original/base.apk")
    assert make_icon(ROOT / "original/base.apk", tmp_path / "icon.png") == APP_ICONS[0]
    assert read_png((tmp_path / "icon.png").read_bytes())[:2] == (ICON_SIZE, ICON_SIZE)
