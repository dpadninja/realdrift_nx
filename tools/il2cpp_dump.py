"""Dump a release's C# types and method addresses with Il2CppDumper.

Il2CppDumper (Perfare, https://github.com/Perfare/Il2CppDumper, MIT) reads libil2cpp.so and
global-metadata.dat and writes dump.cs (every class with field offsets and method RVAs),
script.json (address -> name), stringliteral.json, il2cpp.h and DummyDll/. The other research
tools (il2cpp_find, il2cpp_disasm, il2cpp_symbolize) read that output.

The missing inputs are extracted from the release's APK, Il2CppDumper is downloaded on first use
into analysis/tools/il2cppdumper, and it runs in the .NET runtime docker image, so nothing has to
be installed on the host besides docker.

    uv run python -m tools.il2cpp_dump                  # the release the loader targets
    uv run python -m tools.il2cpp_dump --release 5.0.7

MIT (see LICENSE).
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

from tools.game import ANALYSIS, METADATA_IN_APK, RELEASES, Release, target_release

DUMPER_VERSION = "v6.7.46"
DUMPER_URL = (
    "https://github.com/Perfare/Il2CppDumper/releases/download/"
    f"{DUMPER_VERSION}/Il2CppDumper-net6-{DUMPER_VERSION}.zip"
)
# sha256 of that zip, so a changed download is refused rather than run.
DUMPER_SHA256 = "db9bbbc538e33abfb057c7757ae5d6c1f16a05fdc0d13af8a5a67ea31faaba0c"
DUMPER_DIR = ANALYSIS / "tools" / "il2cppdumper"
DOTNET_IMAGE = "mcr.microsoft.com/dotnet/runtime:6.0"
LIB_IN_APK = "lib/arm64-v8a/libil2cpp.so"
# The dump files the other tools need; a dump is complete when all of them exist.
OUTPUTS = ("dump.cs", "script.json", "stringliteral.json")


def patch_config(text: str) -> str:
    """Il2CppDumper's config.json, changed so it does not wait for a key press at the end."""
    cfg = json.loads(text)
    cfg["RequireAnyKey"] = False
    return json.dumps(cfg, indent=2) + "\n"


def install_dumper(
    dest: Path = DUMPER_DIR, url: str = DUMPER_URL, sha256: str = DUMPER_SHA256
) -> None:
    """Downloads and unpacks Il2CppDumper into `dest` unless it is already there.

    Raises:
        RuntimeError: the download does not have the pinned checksum.
    """
    if (dest / "Il2CppDumper.dll").is_file():
        return
    print(f"downloading {url}", file=sys.stderr)
    with urllib.request.urlopen(url) as r:
        data = r.read()
    if hashlib.sha256(data).hexdigest() != sha256:
        raise RuntimeError(f"{url}: unexpected sha256, refusing to run it")
    dest.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        z.extractall(dest)
    cfg = dest / "config.json"
    cfg.write_text(patch_config(cfg.read_text()))


def extract_from_apk(apk: Path, member: str, out: Path) -> None:
    """Writes `member` of the APK to `out` unless `out` already exists.

    Raises:
        FileNotFoundError: the APK is missing.
    """
    if out.is_file():
        return
    if not apk.is_file():
        raise FileNotFoundError(f"{apk}: APK not found (needed for {member})")
    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(apk) as z:
        out.write_bytes(z.read(member))


def docker_command(rel: Release, uid: int, gid: int) -> list[str]:
    """The docker run that dumps `rel`; paths are relative to the mounted analysis/ folder."""

    def inside(p: Path) -> str:
        return "/a/" + p.relative_to(ANALYSIS).as_posix()

    return [
        "docker", "run", "--rm", "-u", f"{uid}:{gid}",
        "-v", f"{ANALYSIS}:/a", "-w", "/a", DOTNET_IMAGE,
        "dotnet", inside(DUMPER_DIR / "Il2CppDumper.dll"),
        inside(rel.lib64 / "libil2cpp.so"), inside(rel.metadata), inside(rel.dump),
    ]  # fmt: skip


def is_complete(dump: Path) -> bool:
    """Whether `dump` holds every file the other tools read."""
    return all((dump / f).is_file() for f in OUTPUTS)


def dump(rel: Release, force: bool = False) -> Path:
    """Dumps `rel` into rel.dump (skipped if a complete dump is there and not `force`).

    Returns:
        The dump folder.

    Raises:
        RuntimeError: Il2CppDumper failed or did not write the expected files.
    """
    if is_complete(rel.dump) and not force:
        return rel.dump
    extract_from_apk(rel.apk, METADATA_IN_APK, rel.metadata)
    extract_from_apk(rel.apk, LIB_IN_APK, rel.lib64 / "libil2cpp.so")
    install_dumper()
    # Il2CppDumper writes next to its own .dll when the output folder does not exist.
    rel.dump.mkdir(parents=True, exist_ok=True)
    run = subprocess.run(
        docker_command(rel, os.getuid(), os.getgid()), capture_output=True, text=True, check=False
    )
    if run.returncode != 0 or not is_complete(rel.dump):
        raise RuntimeError(f"Il2CppDumper failed for {rel.name}:\n{run.stdout}{run.stderr}")
    return rel.dump


def main(argv: list[str] | None = None) -> int:
    """Command line: dumps one release."""
    ap = argparse.ArgumentParser(description="Dump a release with Il2CppDumper (docker).")
    ap.add_argument("--release", choices=sorted(RELEASES), help="default: the loader's target")
    ap.add_argument("--force", action="store_true", help="dump again even if a dump exists")
    args = ap.parse_args(argv)
    rel = RELEASES[args.release] if args.release else target_release()
    try:
        out = dump(rel, args.force)
    except (OSError, RuntimeError) as e:
        print(f"il2cpp_dump: {e}", file=sys.stderr)
        return 1
    print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
