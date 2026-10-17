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

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

R_AARCH64_RELATIVE = 1027
MAX_CSTR = 256


@dataclass(frozen=True)
class Segment:
    """A PT_LOAD segment: its virtual address and file bytes."""

    vaddr: int
    data: bytes


@dataclass(frozen=True)
class NativeMethod:
    """One recovered JNINativeMethod {name, signature, fnPtr}."""

    entry: int  # vaddr of the JNINativeMethod struct
    name: str
    signature: str
    fn: int


def read_cstr(segments: list[Segment], vaddr: int) -> str | None:
    """The ASCII C string at `vaddr`, or None if it is not one (or longer than MAX_CSTR)."""
    for s in segments:
        if s.vaddr <= vaddr < s.vaddr + len(s.data):
            off = vaddr - s.vaddr
            end = s.data.find(b"\0", off)
            if end < 0 or end - off > MAX_CSTR:
                return None
            try:
                return s.data[off:end].decode("ascii")
            except UnicodeDecodeError:
                return None
    return None


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
    with path.open("rb") as f:
        elf = ELFFile(f)
        segments = [
            Segment(s["p_vaddr"], s.data()) for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"
        ]
        relocs: dict[int, int] = {}
        for sec in elf.iter_sections():
            if isinstance(sec, RelocationSection):
                for r in sec.iter_relocations():
                    if r["r_info_type"] == R_AARCH64_RELATIVE:
                        relocs[r["r_offset"]] = r["r_addend"]
    return segments, relocs


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
