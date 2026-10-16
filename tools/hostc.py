"""Build host-side C programs (unit tests in tests/c/, tools/find_patch_sites.c, the
config.example.txt generator) with the system C compiler.

A program names the loader sources it links against in its first comment line:
    /* sources: source/a.c source/b.c */
and may name extra libraries in a line  /* libs: -lz */ . Paths are relative to the repo root.

    python -m tools.hostc tools/find_patch_sites.c build-host/find_patch_sites
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
CFLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
          f"-I{ROOT / 'source'}", f"-I{ROOT / 'tests/c'}"]
SANITIZE = ["-fsanitize=address,undefined"]


def linked_sources(program: Path) -> list[Path]:
    m = re.match(r"/\*\s*sources:(.*?)\*/", program.read_text())
    return [ROOT / s for s in m.group(1).split()] if m else []


def extra_libs(program: Path) -> list[str]:
    m = re.search(r"/\*\s*libs:(.*?)\*/", program.read_text())
    return m.group(1).split() if m else []


def build(program: Path, out: Path, sanitize: bool = False) -> subprocess.CompletedProcess:
    """Compiles `program` + its `sources:` into `out`. Returns the compiler run."""
    if CC is None:
        raise RuntimeError("no host C compiler (cc/gcc/clang)")
    out.parent.mkdir(parents=True, exist_ok=True)
    cmd = [CC, *CFLAGS, *(SANITIZE if sanitize else []), str(program),
           *map(str, linked_sources(program)), "-o", str(out), *extra_libs(program)]
    return subprocess.run(cmd, capture_output=True, text=True)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__)
        return 2
    r = build(Path(argv[1]).resolve(), Path(argv[2]).resolve())
    sys.stderr.write(r.stderr)
    return r.returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv))
