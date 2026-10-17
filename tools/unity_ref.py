"""Look things up in Unity's C# reference source for the engine version the game uses.

Unity publishes the managed half of the engine (UnityEngine.*: what a C# call does before it
reaches native code) as https://github.com/Unity-Technologies/UnityCsReference (reference-only
license; it is cloned into analysis/ref/, never into the repository). The tag matching the game
is GAME.unity_version from rd_game.h.

The `extern` members there are the icalls: managed methods implemented in libunity, registered by
name as "Namespace.Class/Nested::Method". This tool lists them with that name and, with the
game's libraries, says whether the game can use each one:

- game:   the name is in libil2cpp.so -- the game's code calls it, so the engine registers it
          and an icall override (rd_icall) can find it;
- engine: only libunity.so has it -- the engine has the function, but it is not registered in
          this game (the smoke-knob trap: ParticleSystemRenderer::get_maxParticleSize);
- -:      neither library names it (stripped, or a different name).

    uv run python -m tools.unity_ref fetch                    # clone the matching tag (once)
    uv run python -m tools.unity_ref find ParticleSystem.Emit # C# definitions, file:line
    uv run python -m tools.unity_ref find 'UnityEngine.ParticleSystem::Internal_EmitOld'
    uv run python -m tools.unity_ref icalls ParticleSystemRenderer

MIT (see LICENSE).
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from pathlib import Path

from tools import elfimage
from tools.game import ANALYSIS, GAME, RELEASES, Release, target_release
from tools.il2cpp_find import CheckRow

REPO_URL = "https://github.com/Unity-Technologies/UnityCsReference"
REF_DIR = ANALYSIS / "ref" / "UnityCsReference"
# Comments, string/char literals and preprocessor lines: blanked out (newlines kept) before
# scanning braces.
NOISE = re.compile(
    r'//[^\n]*|/\*.*?\*/|@"(?:[^"]|"")*"|"(?:[^"\\\n]|\\.)*"|\'(?:[^\'\\\n]|\\.)*\''
    r"|^[ \t]*#[^\n]*",
    re.DOTALL | re.MULTILINE,
)
ATTRIBUTES = re.compile(r"^(?:\s*\[[^\[\]]*(?:\[[^\[\]]*\][^\[\]]*)*\])+")
NAMESPACE = re.compile(r"\bnamespace\s+(?P<name>[\w.]+)")
TYPE_DECL = re.compile(r"\b(?:class|struct|interface|enum)\s+(?P<name>\w+)")
METHOD_NAME = re.compile(r"(?P<name>[\w]+)\s*(?:<[^()]*>)?\s*\($")
ICALL_NAME = re.compile(r"^(?P<cls>[\w.]+(?:/[\w]+)*)::(?P<member>\w+)")


@dataclass(frozen=True)
class Member:
    """A method or property of the reference source."""

    namespace: str
    types: tuple[str, ...]  # outer to inner: ("ParticleSystem", "MainModule")
    name: str
    kind: str  # "method" or "property"
    extern: bool
    setter: bool  # property with a set accessor
    path: Path
    line: int
    decl: str  # the declaration, attributes removed, whitespace collapsed

    @property
    def icall_class(self) -> str:
        """The class part of the icall name: "UnityEngine.ParticleSystem/MainModule"."""
        nested = "/".join(self.types)
        return f"{self.namespace}.{nested}" if self.namespace else nested

    def icall_candidates(self) -> list[str]:
        """Names the icall of this extern member may be registered under.

        The new bindings generator registers struct-passing members as Name_Injected; the old
        one declares its INTERNAL_CALL_ wrappers explicitly, so they appear as members already.
        """
        if self.kind == "property":
            names = [f"get_{self.name}"] + ([f"set_{self.name}"] if self.setter else [])
        else:
            names = [self.name]
        return [f"{self.icall_class}::{n}{s}" for n in names for s in ("", "_Injected")]


def blank_noise(text: str) -> str:
    """`text` with comments and string literals replaced by spaces (line numbers kept)."""
    return NOISE.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)


def classify_head(head: str, end: str) -> tuple[str, str] | None:
    """(kind, name) of a member declaration head.

    Args:
        head: the text before the `;`, `{` or `=>` that ends the declaration.
        end: that terminator; a property ends in `{` or `=>`, a plain `;` is a field.
    """
    head = ATTRIBUTES.sub("", head).strip()
    if not head or head.startswith(("using ", "return ", "get", "set", "add ", "remove ")):
        return None
    if "(" in head:
        before = head[: head.index("(") + 1]
        m = METHOD_NAME.search(before)
        if m and "=" not in before and not re.search(r"\b(?:new|if|while|for|switch)\s*\(", before):
            return "method", m["name"]
        return None
    if "=" in head or re.search(r"\bthis\s*\[", head):
        return None  # a field with an initialiser, or an indexer
    words = head.split()
    return ("property", words[-1]) if len(words) >= 2 and end in ("{", "=>") else None


def scan(path: Path, text: str) -> Iterator[Member]:
    """The methods and properties declared directly in types of one C# file."""
    clean = blank_noise(text)
    stack: list[tuple[str, str]] = []  # ("ns" | "type" | "block", name)
    head_start = 0
    for i, ch in enumerate(clean):
        if ch not in "{};":
            continue
        head, end = clean[head_start:i], ch
        if "=>" in head and ch == ";":
            head, end = head[: head.index("=>")], "=>"
        in_type = bool(stack) and stack[-1][0] == "type"
        if ch == "{":
            if m := NAMESPACE.search(head):
                stack.append(("ns", m["name"]))
            elif (m := TYPE_DECL.search(bare := ATTRIBUTES.sub("", head))) and "(" not in bare:
                stack.append(("type", m["name"]))
            else:
                if in_type:
                    yield from member(stack, path, clean, head_start, head, end, body_from=i)
                stack.append(("block", ""))
        elif ch == "}":
            if stack:
                stack.pop()
        elif in_type:
            yield from member(stack, path, clean, head_start, head, end, body_from=None)
        head_start = i + 1


def member(
    stack: list[tuple[str, str]],
    path: Path,
    clean: str,
    start: int,
    head: str,
    end: str,
    body_from: int | None,
) -> Iterator[Member]:
    """The Member for one declaration head, if it declares a method or property."""
    kind_name = classify_head(head, end)
    if kind_name is None:
        return
    kind, name = kind_name
    setter = False
    if kind == "property" and body_from is not None:
        setter = (
            re.search(r"\bset\b", clean[body_from : matching_brace(clean, body_from)]) is not None
        )
    lead = head.lstrip()
    attrs = ATTRIBUTES.match(lead)
    rest = lead[attrs.end() :] if attrs else lead
    stripped = rest.strip()
    # the declaration's line is that of its first word, below any attributes
    offset = (
        start + (len(head) - len(lead)) + (len(lead) - len(rest)) + (len(rest) - len(rest.lstrip()))
    )
    line = clean.count("\n", 0, offset) + 1
    yield Member(
        namespace=".".join(n for k, n in stack if k == "ns"),
        types=tuple(n for k, n in stack if k == "type"),
        name=name,
        kind=kind,
        extern=re.search(r"\bextern\b", stripped) is not None,
        setter=setter,
        path=path,
        line=line,
        decl=" ".join(stripped.split())[:200],
    )


def matching_brace(text: str, open_at: int) -> int:
    """Index of the `}` closing the `{` at `open_at` (end of text if unbalanced)."""
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return len(text)


def scan_tree(root: Path) -> list[Member]:
    """Every member of every .cs file under `root` (Runtime/, Modules/, the generated bindings)."""
    out: list[Member] = []
    for path in sorted(root.rglob("*.cs")):
        if "Editor" in path.relative_to(root).parts:
            continue  # editor-only code never runs in the player
        out.extend(scan(path.relative_to(root), path.read_text(errors="replace")))
    return out


def parse_query(query: str) -> tuple[str, str]:
    """(type, member) of "Type.Member", "Ns.Type.Member" or an icall name "Ns.Type/Nested::member".

    An icall name loses its get_/set_ prefix and _Injected suffix, so it finds the C# member.
    """
    if m := ICALL_NAME.match(query):
        member = re.sub(r"_Injected$", "", m["member"])
        member = re.sub(r"^(?:get|set)_", "", member)
        return m["cls"].replace("/", "."), member
    t, _, name = query.rpartition(".")
    return t, name


def type_matches(m: Member, query: str) -> bool:
    """Whether `query` ("ParticleSystem.MainModule", with or without namespace) names m's type."""
    dotted = ".".join(m.types)
    full = f"{m.namespace}.{dotted}" if m.namespace else dotted
    return query in (dotted, full) or (bool(m.types) and query == m.types[-1])


def find(members: Iterable[Member], query: str) -> list[Member]:
    """Members named by `query` (see parse_query)."""
    t, name = parse_query(query)
    return [m for m in members if m.name == name and type_matches(m, t)]


def icalls_of(members: Iterable[Member], type_query: str) -> list[Member]:
    """The extern members of a type (its icalls)."""
    return [m for m in members if m.extern and type_matches(m, type_query)]


def library_icalls(lib: Path) -> set[str]:
    """Icall names ("Ns.Class::Method", signature dropped) among the strings of a library."""
    segments = elfimage.load(lib, relocs=False, symbols=False).segments
    rx = re.compile(rb"[A-Z][\w.]*(?:/\w+)*::\w+")
    out: set[str] = set()
    for s in segments:
        out.update(m.group(0).decode() for m in rx.finditer(s.data))
    return out


def icall_status(name: str, game: set[str], engine: set[str]) -> str:
    """The status of one icall name: game, engine or - (see the module docstring)."""
    if name in game:
        return "game"
    return "engine" if name in engine else "-"


def describe_icalls(m: Member, game: set[str] | None, engine: set[str] | None) -> list[str]:
    """The registered icall names of an extern member with their status.

    Without the libraries every candidate is listed with "?"; with them, only the names a
    library has (or the plain name marked "-" when neither has any).
    """
    cands = m.icall_candidates()
    if game is None or engine is None:
        return [f"? {c}" for c in cands]
    found = [f"{icall_status(c, game, engine)} {c}" for c in cands if c in game or c in engine]
    return found or [f"- {cands[0]}"]


def fetch(version: str = GAME.unity_version, dest: Path | None = None) -> Path:
    """Shallow-clones UnityCsReference at tag `version` into `dest` (REF_DIR) unless it is there.

    Raises:
        RuntimeError: git failed (no network, or no such tag).
    """
    dest = dest or REF_DIR
    if (dest / ".git").is_dir():
        return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    cmd = ["git", "clone", "-q", "--depth", "1", "--branch", version, REPO_URL, str(dest)]
    run = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if run.returncode != 0:
        raise RuntimeError(f"git clone {REPO_URL} {version} failed: {run.stderr.strip()}")
    return dest


def load_members(root: Path | None = None) -> list[Member]:
    """The scanned reference source (default: REF_DIR).

    Raises:
        FileNotFoundError: it has not been fetched.
    """
    root = root or REF_DIR
    if not root.is_dir():
        raise FileNotFoundError(f"{root} missing: run python -m tools.unity_ref fetch first")
    return scan_tree(root)


def libraries(rel: Release) -> tuple[set[str], set[str]] | None:
    """Icall names of the release's libil2cpp and libunity, or None if they are not extracted."""
    il2cpp, unity = rel.lib64 / "libil2cpp.so", rel.lib64 / "libunity.so"
    if not (il2cpp.is_file() and unity.is_file()):
        return None
    return library_icalls(il2cpp), library_icalls(unity)


def unbalanced_files(root: Path) -> list[Path]:
    """Player sources whose braces do not balance after blanking (the scanner gets lost there).

    Comments, strings and preprocessor lines are blanked first, as for scanning.
    """
    out = []
    for path in sorted(root.rglob("*.cs")):
        if "Editor" in path.relative_to(root).parts:
            continue
        clean = blank_noise(path.read_text(errors="replace"))
        if clean.count("{") != clean.count("}"):
            out.append(path.relative_to(root))
    return out


def check(
    members: list[Member], game: set[str], unbalanced: int
) -> tuple[list[CheckRow], list[str]]:
    """The scanner against the game: every UnityEngine icall libil2cpp names should be found.

    Returns:
        The rows, and the icalls not found as an extern -- each marked "stub" when the
        reference has the member but not as an extern (UnityEngine.AndroidJNI: only the
        non-Android stub is published), or "missing" when it has no such member at all.
    """
    cands = {c for m in members if m.extern for c in m.icall_candidates()}
    wanted = sorted(g for g in game if g.startswith("UnityEngine."))
    unmatched = [g for g in wanted if g not in cands]
    notes, missing = [], 0
    for g in unmatched:
        stub = bool(find(members, g))
        missing += not stub
        notes.append(f"{'stub' if stub else 'missing':8} {g}")
    rows = [
        CheckRow("files with unbalanced braces", unbalanced, 0),
        CheckRow("game icalls found (or stub)", len(wanted) - missing, len(wanted)),
    ]
    return rows, notes


def main(argv: list[str] | None = None) -> int:
    """Command line: fetch / find / icalls / check."""
    ap = argparse.ArgumentParser(description="Unity C# reference source for the game's version.")
    ap.add_argument("--release", choices=sorted(RELEASES), help="libraries for the icall check")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("fetch", help=f"clone {REPO_URL} at {GAME.unity_version}")
    sub.add_parser("find", help="Type.Member or an icall name").add_argument("name")
    sub.add_parser("icalls", help="a type's icalls and their status").add_argument("type")
    sub.add_parser("check", help="scanner self-check against the game's icalls")
    args = ap.parse_args(argv)
    try:
        if args.cmd == "fetch":
            print(fetch())
            return 0
        members = load_members()
        rel = RELEASES[args.release] if args.release else target_release()
        libs = libraries(rel)
    except (OSError, RuntimeError) as e:
        print(f"unity_ref: {e}", file=sys.stderr)
        return 1
    game, engine = libs if libs else (None, None)
    if args.cmd == "check":
        return run_check(members, game)
    found = find(members, args.name) if args.cmd == "find" else icalls_of(members, args.type)
    if not found:
        what = args.name if args.cmd == "find" else args.type
        print(f"unity_ref: nothing found for {what}", file=sys.stderr)
        return 1
    if libs is None:
        print(f"(no {rel.lib64}: icall status not checked)", file=sys.stderr)
    for m in found:
        print(f"{m.path}:{m.line}: {m.decl}")
        if m.extern:
            for line in describe_icalls(m, game, engine):
                print(f"    {line}")
    return 0


def run_check(members: list[Member], game: set[str] | None) -> int:
    """Prints the self-check; 1 if a file or an icall is lost."""
    if game is None:
        print("unity_ref: check needs the game's libil2cpp.so", file=sys.stderr)
        return 1
    rows, notes = check(members, game, len(unbalanced_files(REF_DIR)))
    for r in rows:
        print(f"{'ok' if r.ok else 'MISMATCH':8} {r.what:32} {r.parsed:7} / {r.expected}")
    stubs = sum(n.startswith("stub") for n in notes)
    if stubs:
        print(f"         ({stubs} icalls only as non-extern stubs in the reference)")
    for n in notes:
        if n.startswith("missing"):
            print(f"  {n}")
    return 0 if all(r.ok for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
