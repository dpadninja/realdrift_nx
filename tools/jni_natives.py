"""Dump JNINativeMethod tables {name, signature, fnPtr} from an Android arm64 .so.

Pointers in a PIE library are stored as R_AARCH64_RELATIVE relocations, so the
table is recovered from relocation addends rather than from raw section bytes.

Usage: python tools/jni_natives.py analysis/lib64/libunity.so
"""
import sys
from dataclasses import dataclass

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

R_AARCH64_RELATIVE = 1027
MAX_CSTR = 256


@dataclass(frozen=True)
class Segment:
    vaddr: int
    data: bytes


@dataclass(frozen=True)
class NativeMethod:
    entry: int  # vaddr of the JNINativeMethod struct
    name: str
    signature: str
    fn: int


def read_cstr(segments: list[Segment], vaddr: int) -> str | None:
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
    """relocs maps r_offset -> r_addend of R_AARCH64_RELATIVE relocations."""
    out = []
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


def load_elf(path: str) -> tuple[list[Segment], dict[int, int]]:
    with open(path, "rb") as f:
        elf = ELFFile(f)
        segments = [
            Segment(s["p_vaddr"], s.data())
            for s in elf.iter_segments()
            if s["p_type"] == "PT_LOAD"
        ]
        relocs: dict[int, int] = {}
        for sec in elf.iter_sections():
            if isinstance(sec, RelocationSection):
                for r in sec.iter_relocations():
                    if r["r_info_type"] == R_AARCH64_RELATIVE:
                        relocs[r["r_offset"]] = r["r_addend"]
    return segments, relocs


def main(path: str) -> None:
    for m in find_native_methods(*load_elf(path)):
        print(f"{m.entry:#010x}  {m.name:40s} {m.signature:60s} fn={m.fn:#x}")


if __name__ == "__main__":
    main(sys.argv[1])
