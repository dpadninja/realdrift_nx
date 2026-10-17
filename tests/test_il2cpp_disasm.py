"""tools/il2cpp_disasm.py: annotated disassembly of one C# method.

MIT (see LICENSE).
"""

import struct

import pytest

from tools import il2cpp_disasm
from tools.game import RELEASES, Release, target_release
from tools.il2cpp_disasm import (
    HELPER_CCTOR,
    HELPER_ICALL,
    HELPER_INIT,
    HELPER_NRE,
    Image,
    Insn,
    annotate,
    ends_flow,
    field_name,
    find_method,
    instance_fields,
    method_range,
    parse_objdump,
    this_fields_at,
)
from tools.il2cpp_find import parse_dump
from tools.jni_natives import Segment

RET = struct.pack("<I", 0xD65F03C0)
NOP = struct.pack("<I", 0xD503201F)

# Code at 0x1000: method A at 0x1000, a small function right after A's ret (0x1010), method B at
# 0x1100; il2cpp runtime code far below at 0x100. Data at 0x8000: a float, a C string, a GOT slot.
CODE = NOP * 3 + RET + NOP * 60
DATA = struct.pack("<f", 0.5) + b"\0" * 4 + b"UnityEngine.Foo::Bar()\0".ljust(24, b"\0")
DATA += struct.pack("<Q", 0x9000)


@pytest.fixture
def img():
    """A tiny library image with two named methods and some metadata."""
    return Image(
        methods={0x1000: "A$$Run", 0x1100: "B$$Go"},
        starts=[0x1000, 0x1100],
        metadata={0x9000: "Foo_TypeInfo", 0x9008: 'string "hi"'},
        segments=[Segment(0x1000, CODE), Segment(0x8000, DATA)],
        relocs={0x8020: 0x9000},
    )


OBJDUMP = """
lib.so:\tfile format elf64-littleaarch64

Disassembly of section il2cpp:

0000000000001000 <__start_il2cpp>:
    1000:      \tstp\tx29, x30, [sp, #-0x10]!
    1004:      \tmov\tx19, x0
    1008:      \tadrp\tx8, 0x8000
    100c:      \tldr\ts0, [x8]
    1010:      \tldr\ts1, [x19, #0x274]
    1014:      \tadrp\tx9, 0x8000
    1018:      \tadd\tx9, x9, #0x8
    101c:      \tadrp\tx10, 0x8000
    1020:      \tldr\tx10, [x10, #0x20]
    1024:      \tb.le\t0x1030 <__start_il2cpp+0x30>
    1028:      \tbl\t0x1100 <__start_il2cpp+0x100>
    102c:      \tbl\t0x1010 <__start_il2cpp+0x10>
    1030:      \tbl\t0x200 <memcpy@plt>
    1034:      \tldr\ts2, [x19, #0x270]
    1038:      \tret
"""


def test_parse_objdump_skips_headers():
    """Only instruction lines become instructions; file and section headers are dropped."""
    insns = parse_objdump(OBJDUMP)
    assert insns[0] == Insn(0x1000, "stp", "x29, x30, [sp, #-0x10]!")
    assert insns[-1] == Insn(0x1038, "ret", "")
    assert len(insns) == 15


def test_annotations(img):
    """Calls are named, branches inside the method are relative, constants and strings are read,
    GOT slots resolve to metadata, PLT stubs keep llvm-objdump's name, and `this` fields are
    named through the register `this` was copied to.
    """
    lines = annotate(parse_objdump(OBJDUMP), img, 0x1000, {0x270: "squeal"})
    text = "\n".join(lines)
    assert "ldr    s0, [x8]" in text and "; = 0.5" in text
    assert '; "UnityEngine.Foo::Bar()"' in text
    assert "; Foo_TypeInfo" in text
    assert "b.le   0x1030 <+0x30>" in text
    assert "; B$$Go" in text
    assert "; memcpy@plt" in text
    assert "; this.squeal+0x4" in text and "; this.squeal" in text


def test_this_is_forgotten_after_a_call(img):
    """The address of a field of `this` is named, and x0 no longer holds `this` after a call (it
    is the return value), so a later x0 load is not mislabelled as a field.
    """
    insns = [
        Insn(0x1000, "add", "x20, x0, #0x10"),
        Insn(0x1004, "bl", "0x1100"),
        Insn(0x1008, "ldr", "w8, [x0, #0x10]"),
    ]
    lines = annotate(insns, img, 0x1000, {0x10: "f"})
    assert lines[0].endswith("; &this.f")  # a struct field passed by address
    assert "this." not in lines[2]


def test_function_after_a_ret_is_not_inside_the_method(img):
    """A call to code right after a method's ret names an unnamed function, not "A+0x10"."""
    assert img.name_code(0x1010) == "sub_1010 (unnamed, after A$$Run)"
    assert img.name_code(0x1008) == "A$$Run+0x8"
    assert img.name_code(0x500) is None


def test_huge_gap_is_runtime_code():
    """Addresses far after the last method before a big gap are il2cpp's own code, not a method
    body hundreds of kilobytes long.
    """
    img = Image({0x1000: "A$$Run"}, [0x1000, 0x900000], {}, [], {})
    assert img.name_code(0x2000) is None


@pytest.mark.parametrize(
    ("prev", "kind"),
    [
        ([Insn(0, "mov", "x0, xzr")], HELPER_NRE),
        ([Insn(0, "ldr", "w0, [x8]")], HELPER_INIT),
        ([Insn(0, "ldrb", "w8, [x0, #0x127]"), Insn(4, "tbz", "w8, #0x1, 0x20")], HELPER_CCTOR),
        ([Insn(0, "adrp", "x0, 0x8000"), Insn(4, "add", "x0, x0, #0x8")], HELPER_ICALL),
        ([Insn(0, "mov", "x1, x2")], None),
        ([], None),
    ],
)
def test_runtime_helpers_by_call_pattern(img, prev, kind):
    """il2cpp runtime calls are recognised by the instructions in front of them: a null receiver
    (NullReferenceException), the metadata index load, the cctor flag test, an icall name in x0.
    """
    insns = [*prev, Insn(0x40, "bl", "0x100")]
    note = annotate(insns, img, 0x1000)[-1]
    assert note.endswith(kind or "il2cpp runtime")


def test_ends_flow():
    """ret, b and br end a function; other instructions and short reads do not."""
    assert ends_flow(RET)
    assert ends_flow(struct.pack("<I", 0x14000010))  # b
    assert ends_flow(struct.pack("<I", 0xD61F0220))  # br x17
    assert not ends_flow(NOP)
    assert not ends_flow(None) and not ends_flow(b"\0")


def test_field_name():
    """A field offset names the field; an offset inside a struct field names it with the delta."""
    fields = {0x18: "pos", 0x24: "speed"}
    assert field_name(fields, 0x18) == "pos"
    assert field_name(fields, 0x1C) == "pos+0x4"
    assert field_name(fields, 0x10) is None


def test_find_method_and_range():
    """Methods are found by Type.Method, Type$$Method or address; the range ends at the next
    known code start.
    """
    methods = {0x10: "Ns.Wheel$$Run", 0x40: "Wheel$$Run", 0x80: "Car$$Go"}
    assert find_method(methods, "Wheel.Run") == [0x10, 0x40]
    assert find_method(methods, "Car$$Go") == [0x80]
    assert find_method(methods, "0x1234") == [0x1234]
    assert method_range([0x10, 0x40, 0x80], 0x40) == (0x40, 0x80)
    assert method_range([0x10], 0x10) == (0x10, 0x10 + il2cpp_disasm.MAX_METHOD)


DUMP = """\
// Namespace:
public class Base : MonoBehaviour // TypeDefIndex: 1
{
\t// Fields
\tprotected int hp; // 0x18
}

// Namespace:
public class Car : Base // TypeDefIndex: 2
{
\t// Fields
\tprivate float speed; // 0x1C
\tprivate static int count; // 0x0

\t// Methods

\t// RVA: 0x100 Offset: 0x100 VA: 0x100
\tpublic void Drive() { }

\t// RVA: 0x200 Offset: 0x200 VA: 0x200
\tpublic static void Reset() { }
}
"""


def test_instance_fields_include_bases_not_statics():
    """`this` fields come from the class and its bases; static fields live elsewhere."""
    types = parse_dump(DUMP)
    assert instance_fields(types, types[1]) == {0x1C: "speed", 0x18: "hp"}
    assert this_fields_at(types, 0x100) == {0x1C: "speed", 0x18: "hp"}
    assert this_fields_at(types, 0x200) is None  # static: x0 is not `this`
    assert this_fields_at(types, 0x999) is None


def test_main_with_a_fake_objdump(tmp_path, monkeypatch, capsys, img):
    """The command line prints a header and the annotated listing of each matching method."""
    dump = tmp_path / "dump"
    dump.mkdir()
    (dump / "dump.cs").write_text(DUMP)
    rel = Release("9.9.9", 1, "id", tmp_path, tmp_path, tmp_path, dump)
    monkeypatch.setitem(RELEASES, "9.9.9", rel)
    monkeypatch.setattr(il2cpp_disasm, "load_image", lambda r: img)
    monkeypatch.setattr(il2cpp_disasm, "objdump", lambda lib, lo, hi: OBJDUMP)
    assert il2cpp_disasm.main(["--release", "9.9.9", "A.Run"]) == 0
    out = capsys.readouterr().out
    assert out.startswith("A$$Run  [0x1000, 0x1100)") and "; B$$Go" in out
    assert il2cpp_disasm.main(["--release", "9.9.9", "Nope.Nope"]) == 1
    assert "no method Nope.Nope" in capsys.readouterr().err


def test_main_without_a_dump(tmp_path, monkeypatch, capsys):
    """Without a dump the tool says how to make one instead of failing with a traceback."""
    rel = Release("9.9.9", 1, "id", tmp_path, tmp_path, tmp_path, tmp_path / "none")
    monkeypatch.setitem(RELEASES, "9.9.9", rel)
    assert il2cpp_disasm.main(["--release", "9.9.9", "A.Run"]) == 1
    assert "run python -m tools.il2cpp_dump" in capsys.readouterr().err


def test_metadata_tables_from_script_json():
    """script.json's TypeInfo, MethodInfo and string-literal slots are all named, and methods
    without code are left out of the call targets.
    """
    script = {
        "ScriptMethod": [{"Address": 0x10, "Name": "A$$B"}, {"Address": 0, "Name": "X$$Abstract"}],
        "ScriptMetadata": [{"Address": 1, "Name": "A_TypeInfo"}],
        "ScriptMetadataMethod": [{"Address": 2, "Name": "Method$A.B()"}],
        "ScriptString": [{"Address": 3, "Value": "hello"}],
        "Addresses": [0x10, 0x20],
    }
    assert il2cpp_disasm.metadata_names(script) == {
        1: "A_TypeInfo",
        2: "Method$A.B()",
        3: 'string "hello"',
    }
    assert il2cpp_disasm.method_names(script) == ({0x10: "A$$B"}, [0x10, 0x20])


def test_real_calc_skid_smoke():
    """On the real 5.0.8 build, Wheel.CalcSkidSmoke shows its ParticleSystem.Emit call by name and
    reads this.isSkidSmoke -- the facts the smoke work needed.
    """
    rel = target_release()
    lib = rel.lib64 / "libil2cpp.so"
    if not ((rel.dump / "script.json").is_file() and lib.is_file()):
        pytest.skip("needs the 5.0.8 dump and libil2cpp.so")
    if il2cpp_disasm.OBJDUMP is None:
        pytest.skip("llvm-objdump not installed")
    image = il2cpp_disasm.load_image(rel)
    types = il2cpp_disasm.load_dump(rel.dump)
    (start,) = find_method(image.methods, "Wheel.CalcSkidSmoke")
    lo, hi = method_range(image.starts, start)
    insns = parse_objdump(il2cpp_disasm.objdump(lib, lo, hi))
    text = "\n".join(annotate(insns, image, lo, this_fields_at(types, start)))
    assert "UnityEngine.ParticleSystem$$Emit" in text
    assert "this.isSkidSmoke" in text
