"""tools/elfimage.py: the loaded image of an arm64 library (segments, code, relocations, names).

MIT (see LICENSE).
"""

import struct

import pytest

from tools import elfimage
from tools.elfimage import ElfImage, Segment, parse_rela
from tools.game import RELEASES


def test_parse_rela_keeps_one_type():
    """Only entries of the requested relocation type are returned, and a truncated tail is
    ignored rather than misread.
    """
    data = struct.pack("<QQq", 0x10, elfimage.R_AARCH64_RELATIVE, 0x2000)
    data += struct.pack("<QQq", 0x18, (5 << 32) | elfimage.R_AARCH64_JUMP_SLOT, 0)
    data += struct.pack("<QQq", 0x20, elfimage.R_AARCH64_RELATIVE, -8)
    assert parse_rela(data + b"\0" * 7, elfimage.R_AARCH64_RELATIVE) == {0x10: 0x2000, 0x20: -8}


def test_read_by_virtual_address():
    """Reads go by virtual address and refuse ranges that leave the loaded segments."""
    img = ElfImage([Segment(0x1000, b"abcd"), Segment(0x8000, b"xy")])
    assert img.read(0x1001, 2) == b"bc"
    assert img.read(0x8000, 2) == b"xy"
    assert img.read(0x1003, 2) is None
    assert img.read(0x5000, 1) is None


@pytest.mark.parametrize("release", sorted(RELEASES))
def test_real_library(release):
    """On the real libil2cpp: the code sections, a GOT slot pointing at il2cpp metadata, and the
    PLT stub of memcpy -- the values il2cpp_disasm's names were checked against (5.0.8).
    """
    lib = RELEASES[release].lib64 / "libil2cpp.so"
    if not lib.is_file():
        pytest.skip(f"extract the {release} arm64 libs to {RELEASES[release].lib64} first")
    img = elfimage.load(lib)
    assert len(img.relocs) > 100_000
    assert len(img.code) == 2
    assert "memcpy@plt" in img.symbols.values()
    if release == "5.0.8":
        assert img.relocs[0x1E20758] == 0x1F21A80
        assert img.symbols[0x6072A0] == "memcpy@plt"
        assert img.code == [(0x607510, 0x7A0520), (0x7A0520, 0x168F770)]
    bare = elfimage.load(lib, relocs=False, symbols=False)
    assert bare.relocs == {} and bare.symbols == {} and bare.segments
