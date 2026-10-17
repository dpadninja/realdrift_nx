"""tools/jni_natives.py: JNINativeMethod tables recovered from relocations.

MIT (see LICENSE).
"""

import pytest

from tools.game import RELEASES
from tools.jni_natives import NativeMethod, Segment, find_native_methods, load_elf, read_cstr

STRINGS = Segment(
    0x1000, b"initJni\0(Landroid/content/Context;)V\0nativeRender\0()Z\0\xff\xfe\0junk\0"
)


def addr(s: bytes) -> int:
    return STRINGS.vaddr + STRINGS.data.index(s + b"\0")


INIT_JNI, INIT_SIG = addr(b"initJni"), addr(b"(Landroid/content/Context;)V")
RENDER, RENDER_SIG = addr(b"nativeRender"), addr(b"()Z")
BAD, JUNK = addr(b"\xff\xfe"), addr(b"junk")


def test_read_cstr():
    assert read_cstr([STRINGS], INIT_JNI) == "initJni"
    assert read_cstr([STRINGS], INIT_SIG) == "(Landroid/content/Context;)V"


def test_read_cstr_rejects_non_ascii_and_unmapped():
    assert read_cstr([STRINGS], BAD) is None
    assert read_cstr([STRINGS], 0x9999) is None


def test_read_cstr_rejects_unterminated():
    assert read_cstr([Segment(0, b"abc")], 0) is None


def test_finds_consecutive_table_entries():
    relocs = {
        0x2000: INIT_JNI,
        0x2008: INIT_SIG,
        0x2010: 0x5000,
        0x2018: RENDER,
        0x2020: RENDER_SIG,
        0x2028: 0x5100,
    }
    assert find_native_methods([STRINGS], relocs) == [
        NativeMethod(0x2000, "initJni", "(Landroid/content/Context;)V", 0x5000),
        NativeMethod(0x2018, "nativeRender", "()Z", 0x5100),
    ]


def test_ignores_pointer_triples_that_are_not_methods():
    relocs = {
        0x3000: JUNK,
        0x3008: JUNK,
        0x3010: 0x5000,  # signature without "("
        0x4000: INIT_SIG,
        0x4008: INIT_SIG,
        0x4010: 1,  # name is not an identifier
        0x6000: INIT_JNI,
        0x6008: INIT_SIG,  # no fn pointer
    }
    assert find_native_methods([STRINGS], relocs) == []


@pytest.mark.parametrize("release", sorted(RELEASES))
def test_real_libunity_has_unity_player_natives(release: str):
    libunity = RELEASES[release]["lib64"] / "libunity.so"
    if not libunity.exists():
        pytest.skip(f"extract the {release} arm64 libs to {RELEASES[release]['lib64']} first")
    methods = {m.name: m.signature for m in find_native_methods(*load_elf(libunity))}
    assert methods["initJni"] == "(Landroid/content/Context;)V"
    assert methods["nativeDone"] == "()V"
    assert methods["nativeRender"] == "()Z"
