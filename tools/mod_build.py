"""Build a mod's src/*.c into mod.so in the devkitPro docker image (sdk/mod.mk).

    uv run python -m tools.mod_build mods/examples/hello

The mod folder and sdk/ are mounted into the container, so the mod may live anywhere.
MIT (see LICENSE).
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

from tools.game import ROOT

IMAGE = "devkitpro/devkita64:latest"
SDK = ROOT / "sdk"


def docker_command(mod: Path, uid: int, gid: int, sdk: Path = SDK) -> list[str]:
    """The docker run that builds `mod` with sdk/mod.mk."""
    return [
        "docker", "run", "--rm", "-u", f"{uid}:{gid}",
        "-v", f"{mod.resolve()}:/mod", "-v", f"{sdk.resolve()}:/sdk:ro", "-w", "/mod",
        IMAGE, "make", "-f", "/sdk/mod.mk",
    ]  # fmt: skip


def main(argv: list[str] | None = None) -> int:
    """Command line: builds each mod folder given; 1 if one fails."""
    ap = argparse.ArgumentParser(description="Build mods/<id>/src/*.c into mod.so (docker).")
    ap.add_argument("mods", nargs="+", type=Path, help="mod folders")
    args = ap.parse_args(argv)
    for mod in args.mods:
        if not (mod / "src").is_dir():
            print(f"mod_build: {mod} has no src/ folder", file=sys.stderr)
            return 1
        run = subprocess.run(docker_command(mod, os.getuid(), os.getgid()), check=False)
        if run.returncode != 0:
            print(f"mod_build: {mod.name} failed", file=sys.stderr)
            return 1
        print(mod / "mod.so")
    return 0


if __name__ == "__main__":
    sys.exit(main())
