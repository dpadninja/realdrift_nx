"""Report imports of Android .so files that the loader's shim tables do not provide.

Shim tables are the `{ "name", (uintptr_t)&fn }` initializers in source/*.c|h|inc.

Usage: python tools/import_coverage.py analysis/lib64/*.so
"""
import re
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]
TABLE_ENTRY = re.compile(r'\{\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*,')


def table_symbols(source_dir: Path) -> set[str]:
    names: set[str] = set()
    for pattern in ("*.c", "*.h", "*.inc"):
        for f in source_dir.glob(pattern):
            names.update(TABLE_ENTRY.findall(f.read_text(errors="replace")))
    return names


def undefined_imports(so_path: Path) -> set[str]:
    with open(so_path, "rb") as f:
        dynsym = ELFFile(f).get_section_by_name(".dynsym")
        return {
            s.name for s in dynsym.iter_symbols()
            if s.name and s["st_shndx"] == "SHN_UNDEF"
        }


def missing(so_paths: list[Path], source_dir: Path = ROOT / "source") -> dict[str, list[str]]:
    provided = table_symbols(source_dir)
    return {p.name: sorted(undefined_imports(p) - provided) for p in so_paths}


def main(paths: list[str]) -> int:
    report = missing([Path(p) for p in paths])
    for lib, names in report.items():
        print(f"{lib}: {len(names)} missing" + (": " + " ".join(names) if names else ""))
    return 1 if any(report.values()) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
