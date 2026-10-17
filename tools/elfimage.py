"""Read what the research tools need from an Android arm64 shared library, fast.

A library is read once into memory and looked at as its loaded image: PT_LOAD segments by
virtual address, the code sections, the R_AARCH64_RELATIVE relocations (the pointers a PIE
stores, e.g. GOT slots of il2cpp metadata) and the names of exported functions and PLT stubs.
Relocation tables are unpacked with struct rather than pyelftools, which is ~20x faster on
libil2cpp's 260 000 entries.

    from tools.elfimage import load
    img = load(Path("analysis/lib64_508/libil2cpp.so"))
    img.relocs[0x1e20758], img.symbols[0x6072a0]       # metadata slot, "memcpy@plt"

MIT (see LICENSE).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from elftools.elf.sections import SymbolTableSection

R_AARCH64_RELATIVE = 1027
R_AARCH64_JUMP_SLOT = 1026
SHF_EXECINSTR = 0x4
PF_X = 0x1
MAX_CSTR = 256
# AArch64 PLT: a 32-byte header, then one 16-byte stub per .rela.plt entry, in order.
PLT_HEADER = 32
PLT_ENTRY = 16


@dataclass(frozen=True)
class Segment:
    """A PT_LOAD segment: its virtual address and file bytes."""

    vaddr: int
    data: bytes


@dataclass
class ElfImage:
    """The parts of a library the tools read."""

    segments: list[Segment]
    code: list[tuple[int, int]] = field(default_factory=list)  # [start, end) of code sections
    relocs: dict[int, int] = field(default_factory=dict)  # R_AARCH64_RELATIVE offset -> addend
    symbols: dict[int, str] = field(default_factory=dict)  # function address -> name

    def read(self, addr: int, size: int) -> bytes | None:
        """`size` bytes at virtual address `addr`, or None outside the loaded segments."""
        for s in self.segments:
            if s.vaddr <= addr and addr + size <= s.vaddr + len(s.data):
                return s.data[addr - s.vaddr : addr - s.vaddr + size]
        return None


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


def parse_rela(data: bytes, rtype: int) -> dict[int, int]:
    """The Elf64_Rela entries of `data` with relocation type `rtype`, as offset -> addend."""
    usable = len(data) - len(data) % 24
    return {
        off: addend
        for off, info, addend in struct.iter_unpack("<QQq", data[:usable])
        if info & 0xFFFFFFFF == rtype
    }


def load(path: Path, relocs: bool = True, symbols: bool = True) -> ElfImage:
    """Reads `path`; `relocs` / `symbols` can be skipped when only the bytes are needed."""
    raw = path.read_bytes()
    with path.open("rb") as f:
        elf = ELFFile(f)
        img = ElfImage(
            [
                Segment(s["p_vaddr"], raw[s["p_offset"] : s["p_offset"] + s["p_filesz"]])
                for s in elf.iter_segments()
                if s["p_type"] == "PT_LOAD"
            ]
        )
        for sec in elf.iter_sections():
            if sec["sh_flags"] & SHF_EXECINSTR and sec.name != ".plt":
                img.code.append((sec["sh_addr"], sec["sh_addr"] + sec["sh_size"]))
            if relocs and sec["sh_type"] == "SHT_RELA":
                body = raw[sec["sh_offset"] : sec["sh_offset"] + sec["sh_size"]]
                img.relocs.update(parse_rela(body, R_AARCH64_RELATIVE))
        if symbols:
            img.symbols = function_names(elf)
    return img


def function_names(elf: ELFFile) -> dict[int, str]:
    """Exported functions and PLT stubs ("memcpy@plt") by address."""
    out: dict[int, str] = {}
    dynsym = elf.get_section_by_name(".dynsym")
    if isinstance(dynsym, SymbolTableSection):
        for s in dynsym.iter_symbols():
            if s["st_value"] and s["st_info"]["type"] == "STT_FUNC":
                out[s["st_value"]] = s.name
    plt, rela_plt = elf.get_section_by_name(".plt"), elf.get_section_by_name(".rela.plt")
    if plt is not None and isinstance(rela_plt, RelocationSection):
        syms = elf.get_section(rela_plt["sh_link"])
        for i, r in enumerate(rela_plt.iter_relocations()):
            if r["r_info_type"] == R_AARCH64_JUMP_SLOT and isinstance(syms, SymbolTableSection):
                name = syms.get_symbol(r["r_info_sym"]).name
                out[plt["sh_addr"] + PLT_HEADER + PLT_ENTRY * i] = f"{name}@plt"
    return out
