"""tools/il2cpp_find.py: querying an Il2CppDumper dump.cs.

MIT (see LICENSE).
"""

import json

import pytest

from tools import il2cpp_find
from tools.game import RELEASES, Release, target_release
from tools.il2cpp_find import find_methods, find_types, fmt_names, grep, parse_dump, split_member

# A cut-down dump.cs in Il2CppDumper's layout: namespaces, a nested enum, attributes with their
# own RVA comments, a generic method with shared instances, and an abstract method.
DUMP = """\
// Image 0: Assembly-CSharp.dll - 0

// Namespace:
public class Wheel : MonoBehaviour // TypeDefIndex: 5162
{
\t// Fields
\t[HideInInspector] // RVA: 0x670170 Offset: 0x670170 VA: 0x670170
\tpublic WheelPos wheelPos; // 0x18
\tprivate float squeal; // 0x270
\tpublic const int MAX = 4;

\t// Properties
\tpublic float Speed { get; }

\t// Methods

\t// RVA: 0x95961C Offset: 0x95961C VA: 0x95961C
\tprivate void CalcSkidSmoke() { }

\t// RVA: 0x959514 Offset: 0x959514 VA: 0x959514
\tprivate void CalcSkidmarks(float a) { }

\t// RVA: 0x959600 Offset: 0x959600 VA: 0x959600
\tprivate void CalcSkidmarks() { }
}

// Namespace:
public enum Wheel.Pos // TypeDefIndex: 5163
{
\t// Fields
\tpublic int value__; // 0x0
\tpublic const Wheel.Pos FL = 0;
}

// Namespace: System.Collections.Generic
public class List<T> : IList<T> // TypeDefIndex: 1455
{
\t// Methods

\t[DebuggerHiddenAttribute] // RVA: 0x6123C8 Offset: 0x6123C8 VA: 0x6123C8
\t// RVA: -1 Offset: -1
\tpublic void Add(T item) { }
\t/* GenericInstMethod :
\t|
\t|-RVA: 0x15B80C4 Offset: 0x15B80C4 VA: 0x15B80C4
\t|-List<object>.Add
\t|-List<string>.Add
\t*/

\t// RVA: -1 Offset: -1 Slot: 4
\tpublic abstract void Clear();
}
"""


@pytest.fixture
def types():
    """The fixture dump, parsed."""
    return parse_dump(DUMP)


def test_types_namespaces_and_kinds(types):
    """Every type is found with its namespace, kind and base class, nested types keep their outer
    name, so `class Wheel.Pos` and `UnityEngine.X` queries work.
    """
    assert [(t.full_name, t.kind, t.base) for t in types] == [
        ("Wheel", "class", "MonoBehaviour"),
        ("Wheel.Pos", "enum", None),
        ("System.Collections.Generic.List<T>", "class", "IList<T>"),
    ]


def test_fields_with_offsets_and_constants(types):
    """Fields keep their object offset (what a hook reads), constants have none, and attribute
    lines are not mistaken for fields.
    """
    wheel = types[0]
    assert [(f.decl, f.offset) for f in wheel.fields] == [
        ("public WheelPos wheelPos", 0x18),
        ("private float squeal", 0x270),
        ("public const int MAX = 4", None),
    ]
    assert wheel.properties == ["public float Speed { get; }"]


def test_methods_with_rva_and_overloads(types):
    """Each method gets the RVA of the comment above it, and overloads are all returned."""
    ((_, m),) = find_methods(types, "Wheel.CalcSkidSmoke")
    assert (m.rva, m.decl) == (0x95961C, "private void CalcSkidSmoke()")
    assert sorted(m.rva for _, m in find_methods(types, "Wheel::CalcSkidmarks")) == [
        0x959514,
        0x959600,
    ]


def test_generic_method_instances(types):
    """A generic method has no code of its own; its shared instances carry the RVAs, which is what
    a breakpoint or symbolized address needs.
    """
    ((_, add),) = find_methods(types, "List.Add")
    assert add.rva is None
    assert add.instances == [(0x15B80C4, ["List<object>.Add", "List<string>.Add"])]
    ((_, clear),) = find_methods(types, "System.Collections.Generic.List.Clear")
    assert clear.rva is None and clear.instances == []


def test_type_queries(types):
    """Types are found by short name, with namespace, or without their generic parameters."""
    assert len(find_types(types, "List")) == 1
    assert len(find_types(types, "List<T>")) == 1
    assert len(find_types(types, "System.Collections.Generic.List")) == 1
    assert find_types(types, "Missing") == []


def test_split_member():
    """Type.Member, Type::Member and Il2CppDumper's Type$$Member all split the same way."""
    assert split_member("Wheel.CalcSkidSmoke") == ("Wheel", "CalcSkidSmoke")
    assert split_member("UnityEngine.ParticleSystem::Emit") == (
        "UnityEngine.ParticleSystem",
        "Emit",
    )
    assert split_member("Wheel$$CalcSkidSmoke") == ("Wheel", "CalcSkidSmoke")


def test_grep_types_fields_methods(types):
    """The grep query reports types, fields and methods with their dump.cs line to open."""
    hits = list(grep(types, "Skid|squeal"))
    assert any("Wheel field private float squeal @ 0x270" in h for h in hits)
    assert any("Wheel.CalcSkidSmoke RVA 0x95961c" in h for h in hits)
    assert all(h.split(":")[0].isdigit() for h in hits)


def test_long_instance_lists_are_shortened():
    """A method shared by a hundred instantiations prints a few names and a count, not a wall."""
    assert fmt_names(["a", "b"]) == "a, b"
    assert fmt_names([str(i) for i in range(10)], 3) == "0, 1, 2 (+7 more)"


@pytest.fixture
def release(tmp_path, monkeypatch):
    """A release whose dump folder holds the fixture dump.cs and a script.json."""
    dump = tmp_path / "dump"
    dump.mkdir()
    (dump / "dump.cs").write_text(DUMP)
    script = {"ScriptMethod": [{"Address": 0x95961C, "Name": "Wheel$$CalcSkidSmoke"}]}
    (dump / "script.json").write_text(json.dumps(script))
    rel = Release("9.9.9", 1, "id", tmp_path, tmp_path, tmp_path, dump)
    monkeypatch.setitem(RELEASES, "9.9.9", rel)
    return rel


def test_main_queries(release, capsys):
    """The command line answers class, method, grep and addr queries from the release's dump."""
    assert il2cpp_find.main(["--release", "9.9.9", "class", "Wheel"]) == 0
    out = capsys.readouterr().out
    assert "0x270  private float squeal" in out and "0x95961c  private void CalcSkidSmoke()" in out
    assert il2cpp_find.main(["--release", "9.9.9", "method", "Wheel.CalcSkidSmoke"]) == 0
    assert "RVA 0x95961c" in capsys.readouterr().out
    assert il2cpp_find.main(["--release", "9.9.9", "grep", "Pos"]) == 0
    assert "enum Wheel.Pos" in capsys.readouterr().out
    assert il2cpp_find.main(["--release", "9.9.9", "addr", "0x959620"]) == 0
    assert "Wheel$$CalcSkidSmoke+0x4" in capsys.readouterr().out


def test_main_nothing_found_and_missing_dump(release, tmp_path, capsys):
    """An unknown name and a missing dump each give one line on stderr and exit 1."""
    assert il2cpp_find.main(["--release", "9.9.9", "class", "Nope"]) == 1
    assert "nothing matches Nope" in capsys.readouterr().err
    (release.dump / "dump.cs").unlink()
    assert il2cpp_find.main(["--release", "9.9.9", "grep", "x"]) == 1
    assert "run python -m tools.il2cpp_dump" in capsys.readouterr().err


def test_real_dump_has_the_smoke_method():
    """On the real 5.0.8 dump, Wheel.CalcSkidSmoke (the smoke research entry point) is found."""
    rel = target_release()
    if not (rel.dump / "dump.cs").is_file():
        pytest.skip(f"no dump in {rel.dump}: run python -m tools.il2cpp_dump")
    types = il2cpp_find.load(rel.dump)
    assert find_methods(types, "Wheel.CalcSkidSmoke")
