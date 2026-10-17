"""tools/il2cpp_disasm.py: annotated disassembly of one C# method.

MIT (see LICENSE).
"""

import json
import struct

import pytest

from tools import il2cpp_disasm
from tools.elfimage import ElfImage, Segment
from tools.game import RELEASES, Release, target_release
from tools.il2cpp_disasm import (
    HELPERS,
    Image,
    Insn,
    State,
    absolutize,
    annotate,
    decode_adrp_x0,
    decode_bl,
    ends_flow,
    field_name,
    find_method,
    flow,
    instance_fields,
    method_range,
    method_starts,
    parse_mc,
    scan_helpers,
    this_fields_at,
    word_helper_kind,
    written,
)
from tools.il2cpp_find import parse_dump


def w(x):
    """One little-endian instruction word."""
    return struct.pack("<I", x)


RET, NOP = w(0xD65F03C0), w(0xD503201F)
MOV_X0_XZR, LDR_W0_X8, MOV_X19_X0 = 0xAA1F03E0, 0xB9400100, 0xAA0003F3


def bl(offset):
    """A `bl` word branching `offset` bytes forward or back."""
    return 0x94000000 | ((offset >> 2) & 0x3FFFFFF)


# Code at 0x1000: method A at 0x1000 (a ret at 0x100c), a small unnamed function right after
# it at 0x1010, method B at 0x1100. Data at 0x8000: a float, a C string, a GOT slot.
CODE = NOP * 3 + RET + NOP * 60
DATA = struct.pack("<f", 0.5) + b"\0" * 4 + b"UnityEngine.Foo::Bar()\0".ljust(24, b"\0")
DATA += struct.pack("<Q", 0x9000)


@pytest.fixture
def img():
    """A tiny library image with two named methods, metadata, a PLT stub and one helper."""
    elf = ElfImage(
        [Segment(0x1000, CODE), Segment(0x8000, DATA)],
        code=[(0x1000, 0x1100)],
        relocs={0x8020: 0x9000},
        symbols={0x200: "memcpy@plt"},
    )
    return Image(
        methods={0x1000: "A$$Run", 0x1100: "B$$Go"},
        starts=[0x1000, 0x1100],
        metadata={0x9000: "Foo_TypeInfo", 0x9008: 'string "hi"'},
        elf=elf,
        helpers={0x300: "nre"},
    )


LISTING = [
    Insn(0x1000, "stp", "x29, x30, [sp, #-0x10]!"),
    Insn(0x1004, "mov", "x19, x0"),
    Insn(0x1008, "adrp", "x8, 0x8000"),
    Insn(0x100C, "ldr", "s0, [x8]"),
    Insn(0x1010, "ldr", "s1, [x19, #0x274]"),
    Insn(0x1014, "adrp", "x9, 0x8000"),
    Insn(0x1018, "add", "x9, x9, #0x8"),
    Insn(0x101C, "adrp", "x10, 0x8000"),
    Insn(0x1020, "ldr", "x10, [x10, #0x20]"),
    Insn(0x1024, "b.le", "0x1030"),
    Insn(0x1028, "bl", "0x1100"),
    Insn(0x102C, "bl", "0x200"),
    Insn(0x1030, "bl", "0x300"),
    Insn(0x1034, "ldr", "s2, [x19, #0x270]"),
    Insn(0x1038, "add", "x20, x19, #0x270"),
    Insn(0x103C, "ret", ""),
]


def test_annotations(img):
    """Calls are named (method, PLT stub, runtime helper), branches inside the method are
    relative, constants and strings are read, GOT slots resolve to metadata, and `this` fields
    are named through the register `this` was copied to.
    """
    text = "\n".join(annotate(LISTING, img, 0x1000, {0x270: "squeal"}))
    assert "ldr    s0, [x8]" in text and "; = 0.5" in text
    assert '; "UnityEngine.Foo::Bar()"' in text
    assert "; Foo_TypeInfo" in text
    assert "b.le   0x1030 <+0x30>" in text
    assert "; B$$Go" in text and "; memcpy@plt" in text and f"; {HELPERS['nre']}" in text
    assert "; this.squeal+0x4" in text and "; this.squeal" in text
    assert "; &this.squeal" in text


def test_calls_and_pair_loads_overwrite_registers():
    """The bl and blr (virtual call) instructions clobber x0-x18, ldp writes both registers and a
    store writes none -- otherwise a later x0 load would be shown as a field of `this`.
    """
    assert "x0" in written(Insn(0, "blr", "x8")) and "x19" not in written(Insn(0, "bl", "0x10"))
    assert written(Insn(0, "ldp", "x19, x20, [sp, #0x10]")) == ["x19", "x20"]
    assert written(Insn(0, "str", "x0, [x19]")) == []
    assert written(Insn(0, "cbz", "x0, 0x10")) == []
    assert written(Insn(0, "stxr", "w1, x2, [x0]")) == ["x1"]
    insns = [Insn(0x1000, "blr", "x8"), Insn(0x1004, "ldr", "w8, [x0, #0x10]")]
    s = flow(insns, this_on_entry=True)
    assert "x0" in s[0].this and "x0" not in s[1].this


def test_join_points_keep_only_what_all_paths_agree_on():
    """Where two paths meet, a register keeps `this` (or an adrp page) only if it holds it on
    both: x19 is `this` on both paths, x20 and the page in x8 only on one. Code after a ret that
    nothing jumps to gets no state.
    """
    insns = [
        Insn(0x0, "mov", "x19, x0"),
        Insn(0x4, "cbz", "x1, 0x10"),
        Insn(0x8, "mov", "x20, x0"),
        Insn(0xC, "adrp", "x8, 0x5000"),
        Insn(0x10, "ldr", "w9, [x19]"),
        Insn(0x14, "ret", ""),
        Insn(0x18, "ldr", "w9, [x0]"),
    ]
    s = flow(insns, this_on_entry=True)
    assert s[4].this == frozenset({"x0", "x19"}) and s[4].pages == {}
    assert s[3].this == frozenset({"x0", "x19", "x20"})
    assert s[6] == State({}, frozenset())
    assert flow([], this_on_entry=True) == []


def test_unconditional_branch_skips_the_next_instruction():
    """After `b` control does not fall through, so what the skipped code sets is not merged in."""
    insns = [Insn(0x0, "b", "0x8"), Insn(0x4, "mov", "x5, x0"), Insn(0x8, "ret", "")]
    assert flow(insns, this_on_entry=True)[2].this == frozenset({"x0"})


def test_function_after_a_ret_is_not_inside_the_method(img):
    """A call to code right after a method's ret names an unnamed function, not "A+0x10"."""
    assert img.name_code(0x1010) == "sub_1010 (unnamed, after A$$Run)"
    assert img.name_code(0x1008) == "A$$Run+0x8"
    assert img.name_code(0x500) is None
    assert img.name_target(0x500) == "il2cpp runtime"


def test_huge_gap_is_runtime_code(img):
    """Addresses far after the last method before a big gap are il2cpp's own code, not a method
    body hundreds of kilobytes long.
    """
    big = Image({0x1000: "A$$Run"}, [0x1000, 0x900000], {}, img.elf)
    assert big.name_code(0x2000) is None


@pytest.mark.parametrize(
    ("mn", "ops", "pc", "want"),
    [
        ("bl", "#-0x257820", 0x959660, "0x701e40"),
        ("b.le", "#0x72c", 0x95967C, "0x959da8"),
        ("tbnz", "w8, #0x0, #0x1c", 0x959650, "w8, #0x0, 0x95966c"),
        ("adrp", "x20, #23040000", 0x959644, "x20, 0x1f52000"),
        ("adr", "x1, #0x20", 0x1000, "x1, 0x1020"),
        ("ldr", "x2, #0x40", 0x1000, "x2, 0x1040"),
        ("ldr", "x2, [x8, #0x40]", 0x1000, "x2, [x8, #0x40]"),
        ("mov", "w1, #0x168", 0x1000, "w1, #0x168"),
    ],
)
def test_absolutize(mn, ops, pc, want):
    """The PC-relative operands llvm-mc prints become absolute addresses (values checked against
    the real CalcSkidSmoke listing); ordinary immediates are left alone.
    """
    assert absolutize(mn, ops, pc) == want


def test_parse_mc_keeps_addresses_across_undecodable_words():
    """llvm-mc drops words it cannot decode; lines are matched by encoding so every later
    instruction keeps its real address and the bad word shows as .word.
    """
    code = w(MOV_X19_X0) + w(0xFFFFFFFF) + RET
    text = (
        "\t.text\n"
        "\tmov\tx19, x0                         // encoding: [0xf3,0x03,0x00,0xaa]\n"
        "\tret                                // encoding: [0xc0,0x03,0x5f,0xd6]\n"
    )
    assert parse_mc(text, code, 0x100) == [
        Insn(0x100, "mov", "x19, x0"),
        Insn(0x104, ".word", "0xffffffff"),
        Insn(0x108, "ret", ""),
    ]
    assert parse_mc("", w(1), 0) == [Insn(0, ".word", "0x1")]


def test_word_decoders():
    """The bl and adrp-x0 words are decoded for the whole-library helper scan, including backward
    branches and pages below the pc.
    """
    assert decode_bl(bl(0x100), 0x1000) == 0x1100
    assert decode_bl(bl(-4), 0x1000) == 0xFFC
    assert decode_bl(MOV_X0_XZR, 0x1000) is None
    assert decode_adrp_x0(0x90000000, 0x5123) == 0x5000
    assert decode_adrp_x0(0xB0000008, 0) is None  # adrp x8
    assert decode_adrp_x0(0xF0FFFFE0, 0x10000) == 0xF000  # adrp x0, one page back


@pytest.mark.parametrize(
    ("prev", "kind"),
    [
        ([MOV_X0_XZR], "nre"),
        ([LDR_W0_X8], "init"),
        ([0x39449C08, 0x370000E8], "cctor"),  # ldrb w8, [x0, #0x127]; tbnz w8, ...
        ([0x90000000, 0x91002000], "icall"),  # adrp x0, page; add x0, x0, #0x8 -> "Foo::Bar()"
        ([0x90000000, 0x91000000], None),  # adrp + add to a float, not an icall name
        ([MOV_X19_X0], None),
        ([], None),
    ],
)
def test_helper_patterns(prev, kind):
    """The il2cpp runtime calls are recognised by the words in front of them: a null receiver
    (NullReferenceException), the metadata index load, the cctor flag test, an icall name in x0.
    """
    elf = ElfImage([Segment(0, DATA)])
    assert word_helper_kind(prev, 0, elf) == kind


def test_scan_helpers_needs_agreeing_call_sites():
    """A runtime function is named only when at least three call sites show its pattern and they
    are the majority; calls to known code are never counted.
    """
    # four calls to 0x1100: three after `mov x0, xzr`, one after another instruction
    words = [MOV_X0_XZR, bl(0x100 - 4), MOV_X0_XZR, bl(0x100 - 12), MOV_X0_XZR, bl(0x100 - 20)]
    words += [MOV_X19_X0, bl(0x100 - 28)]
    code = b"".join(w(x) for x in words)
    elf = ElfImage([Segment(0x1000, code + NOP * 0x80)], code=[(0x1000, 0x1000 + len(code))])
    assert scan_helpers(elf, lambda t: False) == {0x1100: "nre"}
    assert scan_helpers(elf, lambda t: True) == {}
    two = ElfImage([Segment(0x1000, code[:16] + NOP * 0x80)], code=[(0x1000, 0x1010)])
    assert scan_helpers(two, lambda t: False) == {}  # two sites are not enough


def test_helper_cache(tmp_path, img, monkeypatch):
    """The helper table is computed once per library build and reused from the dump folder."""
    lib = tmp_path / "lib.so"
    lib.write_bytes(b"x")
    calls = []

    def scan(elf, known):
        calls.append(known(0x1000))
        return {5: "nre"}

    monkeypatch.setattr(il2cpp_disasm, "scan_helpers", scan)
    cache = tmp_path / "helpers.json"
    assert il2cpp_disasm.cached_helpers(cache, lib, img) == {5: "nre"}
    assert il2cpp_disasm.cached_helpers(cache, lib, img) == {5: "nre"}
    assert calls == [True]
    lib.write_bytes(b"changed")
    il2cpp_disasm.cached_helpers(cache, lib, img)
    assert len(calls) == 2


def test_ends_flow():
    """The ret, b and br instructions end a function; others and short reads do not."""
    assert ends_flow(RET)
    assert ends_flow(w(0x14000010))  # b
    assert ends_flow(w(0xD61F0220))  # br x17
    assert not ends_flow(NOP)
    assert not ends_flow(None) and not ends_flow(b"\0")


def test_field_name():
    """A field offset names the field; an offset inside a struct field names it with the delta."""
    fields = {0x18: "pos", 0x24: "speed"}
    assert field_name(fields, 0x18) == "pos"
    assert field_name(fields, 0x1C) == "pos+0x4"
    assert field_name(fields, 0x10) is None


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

\t// RVA: -1 Offset: -1
\tpublic T Get<T>() { }
\t/* GenericInstMethod :
\t|
\t|-RVA: 0x300 Offset: 0x300 VA: 0x300
\t|-Car.Get<object>
\t*/
}
"""


def test_instance_fields_include_bases_not_statics():
    """The fields of `this` come from the class and its bases; static fields live elsewhere."""
    types = parse_dump(DUMP)
    assert instance_fields(types, types[1]) == {0x1C: "speed", 0x18: "hp"}
    assert this_fields_at(types, 0x100) == {0x1C: "speed", 0x18: "hp"}
    assert this_fields_at(types, 0x200) is None  # static: x0 is not `this`
    assert this_fields_at(types, 0x999) is None


def test_method_lookup():
    """Methods come from the dump (a generic one by its instances' code), then from script.json
    names or an address; a range ends at the next known code start.
    """
    types = parse_dump(DUMP)
    script = {0x10: "Ns.Wheel$$Run", 0x40: "Wheel$$Run", 0x80: "Car$$Go"}
    assert method_starts("Car.Drive", types, script) == [0x100]
    assert method_starts("Car.Get", types, script) == [0x300]
    assert method_starts("Wheel.Run", types, script) == [0x10, 0x40]
    assert find_method(script, "Car$$Go") == [0x80]
    assert find_method(script, "0x1234") == [0x1234]
    assert method_range([0x10, 0x40, 0x80], 0x40) == (0x40, 0x80)
    assert method_range([0x10], 0x10) == (0x10, 0x10 + il2cpp_disasm.MAX_METHOD)


def test_main_with_a_fake_disassembler(tmp_path, monkeypatch, capsys, img):
    """The command line prints a header and the annotated listing of each matching method, and
    says how many bodies it left out.
    """
    dump = tmp_path / "dump"
    dump.mkdir()
    (dump / "dump.cs").write_text(DUMP)
    rel = Release("9.9.9", 1, "id", tmp_path, tmp_path, tmp_path, dump)
    monkeypatch.setitem(RELEASES, "9.9.9", rel)
    monkeypatch.setattr(il2cpp_disasm, "load_image", lambda r: img)
    monkeypatch.setattr(il2cpp_disasm, "disassemble", lambda code, base: LISTING)
    assert il2cpp_disasm.main(["--release", "9.9.9", "A.Run"]) == 0
    out = capsys.readouterr().out
    assert out.startswith("A$$Run  [0x1000, 0x1100)") and "; B$$Go" in out
    assert il2cpp_disasm.main(["--release", "9.9.9", "Nope.Nope"]) == 1
    assert "no method Nope.Nope" in capsys.readouterr().err
    monkeypatch.setattr(il2cpp_disasm, "MAX_LISTINGS", 0)
    assert il2cpp_disasm.main(["--release", "9.9.9", "A.Run"]) == 0
    assert "1 bodies, showing 0" in capsys.readouterr().err


def test_main_without_a_dump(tmp_path, monkeypatch, capsys):
    """Without a dump the tool says how to make one instead of failing with a traceback."""
    rel = Release("9.9.9", 1, "id", tmp_path, tmp_path, tmp_path, tmp_path / "none")
    monkeypatch.setitem(RELEASES, "9.9.9", rel)
    assert il2cpp_disasm.main(["--release", "9.9.9", "A.Run"]) == 1
    assert "run python -m tools.il2cpp_dump" in capsys.readouterr().err


def test_metadata_tables_from_script_json():
    """The TypeInfo, MethodInfo and string-literal slots of script.json are all named, and
    methods without code are left out of the call targets.
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


def test_disassemble_with_llvm_mc():
    """The real llvm-mc decodes a few words into absolute-address instructions."""
    if il2cpp_disasm.LLVM_MC is None:
        pytest.skip("llvm-mc not installed")
    insns = il2cpp_disasm.disassemble(w(MOV_X19_X0) + w(bl(0x100)) + RET, 0x1000)
    assert insns == [
        Insn(0x1000, "mov", "x19, x0"),
        Insn(0x1004, "bl", "0x1104"),
        Insn(0x1008, "ret", ""),
    ]


def test_real_calc_skid_smoke():
    """On the real 5.0.8 build, Wheel.CalcSkidSmoke shows its ParticleSystem.Emit call by name,
    reads this.isSkidSmoke, and its runtime calls are named by the whole-library helper table --
    the facts the smoke work needed.
    """
    rel = target_release()
    if not ((rel.dump / "script.json").is_file() and (rel.lib64 / "libil2cpp.so").is_file()):
        pytest.skip("needs the 5.0.8 dump and libil2cpp.so")
    if il2cpp_disasm.LLVM_MC is None:
        pytest.skip("llvm-mc not installed")
    image = il2cpp_disasm.load_image(rel)
    types = il2cpp_disasm.load_dump(rel.dump)
    (start,) = method_starts("Wheel.CalcSkidSmoke", types, image.methods)
    lo, hi = method_range(image.starts, start)
    insns = il2cpp_disasm.disassemble(image.elf.read(lo, hi - lo) or b"", lo)
    text = "\n".join(annotate(insns, image, lo, this_fields_at(types, start)))
    assert "UnityEngine.ParticleSystem$$Emit" in text
    assert "this.isSkidSmoke" in text
    assert HELPERS["nre"] in text and HELPERS["init"] in text
    assert json.loads((rel.dump / il2cpp_disasm.HELPER_CACHE).read_text())["helpers"]


def test_find_callers():
    """Direct calls (bl) and tail calls (b) to a target are found by address, forward and
    backward; other branches and other targets are not -- the --callers answer to "who calls
    this method".
    """
    code = [
        bl(0x40),
        0xD503201F,
        0x14000000 | (0x38 >> 2),
        bl(0x100),
        bl(-4),
    ]  # b +0x38 from 0x1008
    elf = ElfImage([Segment(0x1000, b"".join(w(x) for x in code))], code=[(0x1000, 0x1014)])
    found = il2cpp_disasm.find_callers(elf, {0x1040, 0x110C, 0x100C})
    assert found == {0x1040: [0x1000, 0x1008], 0x110C: [0x100C], 0x100C: [0x1010]}
    assert il2cpp_disasm.decode_branch(0xD503201F, 0) is None
