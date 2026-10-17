"""Dump JNINativeMethod tables {name, signature, fnPtr} from an Android arm64 .so.

Pointers in a PIE library are stored as R_AARCH64_RELATIVE relocations, so the
table is recovered from relocation addends rather than from raw section bytes.

    python tools/jni_natives.py analysis/lib64/libunity.so

MIT (see LICENSE).
"""

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

from tools.elfimage import Segment, load, read_cstr

__all__ = ["NativeMethod", "Segment", "find_native_methods", "load_elf", "read_cstr"]


@dataclass(frozen=True)
class NativeMethod:
    """One recovered JNINativeMethod {name, signature, fnPtr}."""

    entry: int  # vaddr of the JNINativeMethod struct
    name: str
    signature: str
    fn: int


def find_native_methods(segments: list[Segment], relocs: dict[int, int]) -> list[NativeMethod]:
    """Finds JNINativeMethod-shaped triples of relocated pointers.

    Args:
        segments: the library's loadable segments.
        relocs: r_offset -> r_addend of its R_AARCH64_RELATIVE relocations.

    Returns:
        The tables' entries in address order.
    """
    out: list[NativeMethod] = []
    for off in sorted(relocs):
        sig_ptr, fn_ptr = relocs.get(off + 8), relocs.get(off + 16)
        if sig_ptr is None or fn_ptr is None:
            continue
        sig = read_cstr(segments, sig_ptr)
        if not sig or not sig.startswith("(") or ")" not in sig:
            continue
        name = read_cstr(segments, relocs[off])
        if name and name.isidentifier():
            out.append(NativeMethod(off, name, sig, fn_ptr))
    return out


def load_elf(path: Path) -> tuple[list[Segment], dict[int, int]]:
    """Loadable segments and R_AARCH64_RELATIVE relocations (offset -> addend) of `path`."""
    img = load(path, symbols=False)
    return img.segments, img.relocs


def main(argv: list[str] | None = None) -> int:
    """Command line: prints every recovered native method."""
    ap = argparse.ArgumentParser(description="Dump JNINativeMethod tables of an arm64 .so.")
    ap.add_argument("lib", type=Path, help="Android arm64 shared library")
    args = ap.parse_args(argv)
    for m in find_native_methods(*load_elf(args.lib)):
        print(f"{m.entry:#010x}  {m.name:40s} {m.signature:60s} fn={m.fn:#x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
