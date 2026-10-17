"""tools/stage_sd.py: identity checks of the APK/OBB and the staged SD folder.

MIT (see LICENSE).
"""

import struct
import zipfile
from pathlib import Path

import pytest

from tools.game import GAME, LITE_BUILD_ID, LITE_PACKAGE, RELEASES, ROOT, obb_name
from tools.stage_sd import (
    apk_strings,
    axml_strings,
    check_apk_identity,
    check_obb_marker,
    find_obb_member,
    main,
    plan_apk,
    stage,
)

BUILD_ID = GAME.build_id
BUILD_ID_507 = RELEASES["5.0.7"]["build_id"]
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


def write_apk(path: Path, manifest: list[str] = (GAME.package, "unity.build-id", BUILD_ID)) -> Path:
    with zipfile.ZipFile(path, "w") as z:
        for n in APK_NAMES:
            if not n.endswith("/"):
                z.writestr(n, make_axml(list(manifest)) if n == "AndroidManifest.xml" else n)
    return path


def write_obb(path: Path, build_id: str = BUILD_ID) -> Path:
    with zipfile.ZipFile(path, "w") as z:
        z.writestr(f"assets/{build_id}", b"")
    return path


@pytest.mark.parametrize("utf8", [False, True])
def test_axml_strings(utf8: bool):
    strings = ["manifest", GAME.package, "Ünïcode", "x" * 200]
    assert axml_strings(make_axml(strings, utf8)) == strings


def test_game_identity_comes_from_the_loader_header():
    assert GAME.package == "com.realdrift.sipon" and GAME.version_code == 76
    assert OBB == "main.76.com.realdrift.sipon.obb" == obb_name(76)
    assert GAME.base_apk == "base.apk"
    assert RELEASES["5.0.8"]["version_code"] == GAME.version_code
    assert RELEASES["5.0.8"]["build_id"] == GAME.build_id


def test_plan_apk_takes_arm64_libs_only():
    assert plan_apk(APK_NAMES) == [(f"lib/arm64-v8a/{lib}", lib) for lib in GAME.libs]


def test_plan_apk_rejects_non_game_apk():
    with pytest.raises(ValueError, match="globalgamemanagers"):
        plan_apk([f"lib/arm64-v8a/{lib}" for lib in GAME.libs])


def test_plan_apk_rejects_32bit_only_apk():
    with pytest.raises(ValueError, match="arm64-v8a"):
        plan_apk(["lib/armeabi-v7a/libunity.so", "assets/bin/Data/globalgamemanagers"])


def test_find_obb_member_checks_the_version_code():
    assert find_obb_member(["x/", f"{GAME.package}/{OBB}"], 76) == f"{GAME.package}/{OBB}"
    with pytest.raises(ValueError, match=r"main\.74"):
        find_obb_member([f"{GAME.package}/{OBB}"], 74)  # an OBB of another build
    with pytest.raises(ValueError):
        find_obb_member(["main.1.other.game.obb"], 76)


def test_stage_end_to_end(tmp_path: Path):
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
    check_obb_marker(["assets/bin/Data/x", f"assets/{BUILD_ID}"], BUILD_ID)
    with pytest.raises(ValueError, match="another game version"):
        check_obb_marker([f"assets/{BUILD_ID_507}"], BUILD_ID)


def test_stage_rejects_obb_without_marker(tmp_path: Path):
    apk = write_apk(tmp_path / "game.apk")
    obb = tmp_path / "main.obb"
    with zipfile.ZipFile(obb, "w") as z:
        z.writestr("assets/bin/Data/level1", b"")
    with pytest.raises(ValueError, match="marker"):
        stage(apk, obb, tmp_path / "sd", None, GAME.version_code, BUILD_ID)


def test_check_apk_identity():
    check_apk_identity([GAME.package, BUILD_ID], GAME.package, BUILD_ID)
    with pytest.raises(ValueError, match="Lite"):
        check_apk_identity([LITE_PACKAGE, LITE_BUILD_ID], GAME.package, BUILD_ID)
    with pytest.raises(ValueError, match=BUILD_ID_507):
        check_apk_identity([GAME.package, BUILD_ID_507], GAME.package, BUILD_ID)


def test_main_end_to_end(tmp_path: Path, monkeypatch):
    apk, obb = write_apk(tmp_path / "base.apk"), write_obb(tmp_path / OBB)
    monkeypatch.chdir(tmp_path)
    assert main([str(apk), str(obb), "--nro", str(tmp_path / "none.nro")]) == 0
    assert (tmp_path / "switch/realdrift_nx" / OBB).exists()
    bad = write_apk(tmp_path / "lite.apk", [LITE_PACKAGE, LITE_BUILD_ID])
    assert main([str(bad), str(obb)]) == 1


@pytest.mark.parametrize(
    "apk, ok", [("original/base.apk", True), ("analysis/realdrift-5.0.7-build74.apk", False)]
)
def test_real_apks(apk: str, ok: bool):
    if not (ROOT / apk).exists():
        pytest.skip(f"no {apk}")
    strings = apk_strings(ROOT / apk)
    if ok:
        check_apk_identity(strings, GAME.package, BUILD_ID)
    else:
        with pytest.raises(ValueError, match="unsupported game version"):
            check_apk_identity(strings, GAME.package, BUILD_ID)
