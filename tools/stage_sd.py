"""Prepare the Switch SD-card folder for realdrift_nx from your own Real Drift APK and OBB.

    python3 stage_sd.py base.apk main.76.com.realdrift.sipon.obb

creates ./switch/realdrift_nx/ (copy the `switch` folder to the root of the SD card):
    realdrift_nx.nro                   (taken from next to this script, or --nro)
    libmain.so libunity.so libil2cpp.so  (lib/arm64-v8a/ from the APK)
    base.apk                           (the APK itself: the game data is read from it)
    main.<versionCode>.<package>.obb

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
    if args.out.parent.name == "switch":
        print(f"Copy the '{args.out.parent}' folder to the root of the SD card.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
