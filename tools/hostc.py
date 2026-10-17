"""Build host-side C programs with the system C compiler.

Used for the unit tests in tests/c/, tools/find_patch_sites.c and the config.example.txt
generator. A program names the loader sources it links against in its first comment line:
    /* sources: source/a.c source/b.c */
and may name extra libraries in a line  /* libs: -lz */ . Paths are relative to the repo root.

    python -m tools.hostc tools/find_patch_sites.c build-host/find_patch_sites

MIT (see LICENSE).
"""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
# One -I per layer directory of source/ (headers are included by file name), plus tests/c.
LAYER_DIRS = sorted(d for d in (ROOT / "source").iterdir() if d.is_dir())
CFLAGS = [
    "-std=gnu11",
    "-Wall",
    "-Wextra",
    "-Werror",
    *(f"-I{d}" for d in LAYER_DIRS),
    f"-I{ROOT / 'tests/c'}",
]
SANITIZE = ["-fsanitize=address,undefined"]


def linked_sources(program: Path) -> list[Path]:
    """The loader sources named in `program`'s `/* sources: ... */` first line."""
    m = re.match(r"/\*\s*sources:(.*?)\*/", program.read_text())
    return [ROOT / s for s in m.group(1).split()] if m else []


def extra_libs(program: Path) -> list[str]:
    """Linker flags named in a `/* libs: ... */` line of `program`."""
    m = re.search(r"/\*\s*libs:(.*?)\*/", program.read_text())
    return m.group(1).split() if m else []


def build(program: Path, out: Path, sanitize: bool = False) -> subprocess.CompletedProcess[str]:
    """Compiles `program` and its `sources:` into `out`.

    Args:
        program: the C file with the `sources:` header.
        out: the executable to write (its directory is created).
        sanitize: build with ASan/UBSan (the unit tests do).

    Returns:
        The finished compiler run; check `returncode` and `stderr`.

    Raises:
        RuntimeError: there is no host C compiler.
    """
    if CC is None:
        raise RuntimeError("no host C compiler (cc/gcc/clang)")
    out.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        CC,
        *CFLAGS,
        *(SANITIZE if sanitize else []),
        str(program),
        *map(str, linked_sources(program)),
        "-o",
        str(out),
        *extra_libs(program),
    ]
    return subprocess.run(cmd, capture_output=True, text=True, check=False)


def main(argv: list[str] | None = None) -> int:
    """Command line: build PROGRAM into OUT; returns the compiler's exit code."""
    ap = argparse.ArgumentParser(description="Build a host-side C program of realdrift_nx.")
    ap.add_argument("program", type=Path, help="C file with a /* sources: ... */ first line")
    ap.add_argument("out", type=Path, help="executable to write")
    args = ap.parse_args(argv)
    r = build(args.program.resolve(), args.out.resolve())
    sys.stderr.write(r.stderr)
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
