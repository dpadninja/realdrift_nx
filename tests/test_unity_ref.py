"""tools/unity_ref.py: Unity's C# reference source and the icalls it declares.

MIT (see LICENSE).
"""

from pathlib import Path

import pytest

from tools import unity_ref
from tools.game import RELEASES, Release, target_release
from tools.jni_natives import Segment
from tools.unity_ref import (
    Member,
    blank_noise,
    classify_head,
    describe_icalls,
    find,
    icall_status,
    icalls_of,
    parse_query,
    scan,
)

# The shapes the 2018.4 reference uses: new bindings (extern properties, struct members that
# become _Injected), old generated bindings (extern methods in a nested struct), a #pragma and
# attributes with parentheses in front of a class, comments and strings holding braces.
SOURCE = """\
using System;
namespace UnityEngine
{
#pragma warning disable 414
    [NativeHeader("Runtime/ParticleSystem.h")]
    [RequireComponent(typeof(Transform))]
    public partial class ParticleSystemRenderer : Renderer
    {
        [NativeName("RenderAlignment")]
        extern public ParticleSystemRenderSpace alignment { get; set; }
        extern public UInt16[] triangles
        {
            get;
        }
        private int m_Count;                       // a field, not a property
        public int Count => m_Count;
        public string Name { get { return "}{"; } } // braces in a string
        /* extern public float commented { get; set; } */
        extern public void BakeMesh(Mesh mesh, bool useTransform = false);
        public void Bake(Mesh mesh) { if (mesh != null) { BakeMesh(mesh); } }
        public int this[int i] { get { return i; } }
    }

    public sealed partial class ParticleSystem : Component
    {
        public partial struct MainModule
        {
            extern private static int GetMaxParticles (ParticleSystem system) ;
        }
        extern private void Internal_EmitOld (ref ParticleSystem.Particle particle) ;
    }
}
"""


@pytest.fixture
def members():
    """The fixture source, scanned."""
    return list(scan(Path("Runtime/X.cs"), SOURCE))


def test_scan_finds_members_with_types_and_lines(members):
    """Methods and properties are found with their namespace, nesting and line; fields,
    indexers, statements in bodies and commented-out code are not members.
    """
    got = [(m.icall_class, m.name, m.kind, m.extern, m.line) for m in members]
    assert got == [
        ("UnityEngine.ParticleSystemRenderer", "alignment", "property", True, 10),
        ("UnityEngine.ParticleSystemRenderer", "triangles", "property", True, 11),
        ("UnityEngine.ParticleSystemRenderer", "Count", "property", False, 16),
        ("UnityEngine.ParticleSystemRenderer", "Name", "property", False, 17),
        ("UnityEngine.ParticleSystemRenderer", "BakeMesh", "method", True, 19),
        ("UnityEngine.ParticleSystemRenderer", "Bake", "method", False, 20),
        ("UnityEngine.ParticleSystem/MainModule", "GetMaxParticles", "method", True, 28),
        ("UnityEngine.ParticleSystem", "Internal_EmitOld", "method", True, 30),
    ]


def test_setters_and_declarations(members):
    """A property's icalls include set_ only when it has a setter; the declaration is shown
    without its attributes.
    """
    alignment, triangles = members[0], members[1]
    assert alignment.setter and not triangles.setter
    assert alignment.decl == "extern public ParticleSystemRenderSpace alignment"
    assert triangles.icall_candidates() == [
        "UnityEngine.ParticleSystemRenderer::get_triangles",
        "UnityEngine.ParticleSystemRenderer::get_triangles_Injected",
    ]


def test_blank_noise_keeps_lines():
    """Comments, strings and preprocessor lines are blanked without moving later lines, so
    reported line numbers stay right.
    """
    text = 'a // x {\n"{"\n#if X\n/* {\n} */b'
    out = blank_noise(text)
    assert out.count("\n") == text.count("\n")
    assert "{" not in out and "}" not in out and out.endswith("b")


@pytest.mark.parametrize(
    ("head", "end", "want"),
    [
        ("extern public float x", "{", ("property", "x")),
        ("private float x", ";", None),
        ("public float x = 1", ";", None),
        ("public int Count", "=>", ("property", "Count")),
        ("public static T[] Find<T>(int a) where T : Object", "{", ("method", "Find")),
        ("if (x)", "{", None),
        ("var a = new Foo(1)", ";", None),
        ("get", "{", None),
        ("public int this[int i]", "{", None),
    ],
)
def test_classify_head(head, end, want):
    """Declaration heads are told apart: properties need a body or =>, a `;` after a name is a
    field, and statements or constructor calls inside bodies are not methods.
    """
    assert classify_head(head, end) == want


def test_queries(members):
    """Members are found by Type.Member, with namespace, nested, or by their icall name (get_,
    set_ and _Injected stripped) -- the form a log or rd_icall uses.
    """
    assert [m.name for m in find(members, "ParticleSystemRenderer.BakeMesh")] == ["BakeMesh"]
    assert len(find(members, "UnityEngine.ParticleSystem.MainModule.GetMaxParticles")) == 1
    assert len(find(members, "UnityEngine.ParticleSystemRenderer::get_alignment_Injected")) == 1
    assert len(find(members, "UnityEngine.ParticleSystem/MainModule::GetMaxParticles")) == 1
    assert parse_query("A.B.C") == ("A.B", "C")
    assert [m.name for m in icalls_of(members, "MainModule")] == ["GetMaxParticles"]


def test_icall_status_and_description(members):
    """The status says whether the game registers the icall (libil2cpp names it), only the
    engine has it (the maxParticleSize trap), or neither; the _Injected variant is found too.
    """
    game = {"UnityEngine.ParticleSystem::Internal_EmitOld"}
    engine = game | {
        "UnityEngine.ParticleSystemRenderer::get_alignment",
        "UnityEngine.ParticleSystemRenderer::set_alignment_Injected",
    }
    assert icall_status("UnityEngine.ParticleSystem::Internal_EmitOld", game, engine) == "game"
    alignment, emit, bake = members[0], members[-1], members[4]
    assert describe_icalls(alignment, game, engine) == [
        "engine UnityEngine.ParticleSystemRenderer::get_alignment",
        "engine UnityEngine.ParticleSystemRenderer::set_alignment_Injected",
    ]
    assert describe_icalls(emit, game, engine) == [
        "game UnityEngine.ParticleSystem::Internal_EmitOld"
    ]
    assert describe_icalls(bake, game, engine) == ["- UnityEngine.ParticleSystemRenderer::BakeMesh"]
    assert describe_icalls(bake, None, None)[0] == "? UnityEngine.ParticleSystemRenderer::BakeMesh"


def test_library_icalls(monkeypatch):
    """Icall names are read from a library's strings, with the signature libil2cpp appends
    dropped so both libraries compare equal.
    """
    data = b"\0UnityEngine.Foo/Bar::Baz(System.Int32)\0UnityEngine.Foo::Qux\0not an icall\0"
    monkeypatch.setattr(unity_ref, "load_elf", lambda p: ([Segment(0, data)], {}))
    assert unity_ref.library_icalls(Path("x")) == {
        "UnityEngine.Foo/Bar::Baz",
        "UnityEngine.Foo::Qux",
    }


def test_scan_tree_skips_editor_code(tmp_path):
    """Editor-only sources never run in the player, so they are left out of the lookups."""
    (tmp_path / "Runtime").mkdir()
    (tmp_path / "Editor").mkdir()
    for d in ("Runtime", "Editor"):
        (tmp_path / d / "A.cs").write_text("namespace N { class A { extern void F(); } }")
    found = unity_ref.scan_tree(tmp_path)
    assert [str(m.path) for m in found] == ["Runtime/A.cs"]


def test_fetch(tmp_path, monkeypatch):
    """The reference is cloned shallowly at the game's Unity tag once; a failed clone is an
    error naming the tag.
    """
    calls = []

    class Run:
        returncode = 0
        stderr = ""

    def run(cmd, **kw):
        calls.append(cmd)
        return Run()

    monkeypatch.setattr(unity_ref.subprocess, "run", run)
    dest = tmp_path / "ref"
    assert unity_ref.fetch("2018.4.14f1", dest) == dest
    assert calls[0][:6] == ["git", "clone", "-q", "--depth", "1", "--branch"]
    assert calls[0][6] == "2018.4.14f1"
    (dest / ".git").mkdir(parents=True)
    unity_ref.fetch("2018.4.14f1", dest)
    assert len(calls) == 1
    Run.returncode, Run.stderr = 128, "no such tag"
    with pytest.raises(RuntimeError, match="no such tag"):
        unity_ref.fetch("0.0", tmp_path / "other")


def test_main(tmp_path, monkeypatch, capsys):
    """The find and icalls queries print file:line and the icall status; without the libraries
    the status is marked unchecked, and an unknown name fails with one line.
    """
    src = tmp_path / "Runtime"
    src.mkdir()
    (src / "X.cs").write_text(SOURCE)
    monkeypatch.setattr(unity_ref, "REF_DIR", tmp_path)
    monkeypatch.setattr(unity_ref, "load_members", lambda: unity_ref.scan_tree(tmp_path))
    rel = Release("9.9.9", 1, "id", tmp_path, tmp_path / "nolib", tmp_path, tmp_path)
    monkeypatch.setitem(RELEASES, "9.9.9", rel)
    assert unity_ref.main(["--release", "9.9.9", "icalls", "MainModule"]) == 0
    io = capsys.readouterr()
    assert "Runtime/X.cs:28: extern private static int GetMaxParticles" in io.out
    assert "? UnityEngine.ParticleSystem/MainModule::GetMaxParticles" in io.out
    assert "not checked" in io.err
    monkeypatch.setattr(
        unity_ref, "libraries", lambda r: ({"UnityEngine.ParticleSystem::Internal_EmitOld"}, set())
    )
    assert unity_ref.main(["--release", "9.9.9", "find", "ParticleSystem.Internal_EmitOld"]) == 0
    assert "game UnityEngine.ParticleSystem::Internal_EmitOld" in capsys.readouterr().out
    assert unity_ref.main(["--release", "9.9.9", "find", "Nope.Nope"]) == 1
    assert "nothing found for Nope.Nope" in capsys.readouterr().err


def test_main_without_the_reference(tmp_path, monkeypatch, capsys):
    """Before fetch, every query says how to get the reference."""
    monkeypatch.setattr(unity_ref, "REF_DIR", tmp_path / "missing")
    assert unity_ref.main(["find", "A.B"]) == 1
    assert "unity_ref fetch" in capsys.readouterr().err


def test_real_reference_matches_the_game():
    """Every icall the game uses resolves to a declaration in the reference, except AndroidJNI,
    for which Unity publishes only a non-Android stub; and the smoke trap shows: maxParticleSize
    is engine-only, Internal_EmitOld is the game's.
    """
    if not unity_ref.REF_DIR.is_dir():
        pytest.skip("run python -m tools.unity_ref fetch")
    libs = unity_ref.libraries(target_release())
    if libs is None:
        pytest.skip("extract the 5.0.8 libraries first")
    game, engine = libs
    members = unity_ref.load_members()
    cands = {c for m in members if m.extern for c in m.icall_candidates()}
    missing = {g for g in game if g.startswith("UnityEngine.") and g not in cands}
    assert all("AndroidJNI" in g for g in missing), sorted(missing)[:10]
    (renderer,) = find(members, "ParticleSystemRenderer.maxParticleSize")
    assert describe_icalls(renderer, game, engine)[0].startswith("engine ")
    (emit,) = find(members, "ParticleSystem.Internal_EmitOld")
    assert describe_icalls(emit, game, engine) == [
        "game UnityEngine.ParticleSystem::Internal_EmitOld"
    ]


def test_member_without_namespace():
    """Types outside a namespace have no leading dot in their icall class."""
    m = Member("", ("A",), "F", "method", True, False, Path("x"), 1, "")
    assert m.icall_class == "A"
