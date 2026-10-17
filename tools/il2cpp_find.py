"""Look up the game's C# classes, fields and methods in an Il2CppDumper dump.

dump.cs lists every type of the game with its field offsets and the address (RVA in
libil2cpp.so) of each method's compiled code. This tool answers the questions a hook or a log
raises without opening the 10 MB file:

    uv run python -m tools.il2cpp_find class Wheel        # fields + offsets, methods + RVAs
    uv run python -m tools.il2cpp_find method Wheel.CalcSkidSmoke
    uv run python -m tools.il2cpp_find grep 'Smoke|Skid'  # names matching a regex
    uv run python -m tools.il2cpp_find addr 0x95961c      # the method containing an address
    uv run python -m tools.il2cpp_find check              # parser self-check against the dump

--release picks the dump (default: the release the loader targets); make one with
tools/il2cpp_dump.py. Each result names its line in dump.cs.

MIT (see LICENSE).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections.abc import Iterator
from dataclasses import dataclass, field
from pathlib import Path

from tools.game import RELEASES, target_release
from tools.il2cpp_symbolize import Symbolizer

TYPE_HEADER = re.compile(
    r"^(?P<mods>[\w ]*?)\b(?P<kind>class|struct|enum|interface) (?P<name>.+?)"
    r"(?: : (?P<base>.+?))? // TypeDefIndex: (?P<index>\d+)$"
)
NAMESPACE = re.compile(r"^// Namespace: ?(?P<ns>.*)$")
METHOD_RVA = re.compile(r"^\t// RVA: (?P<rva>0x[0-9A-Fa-f]+|-1)")
GENERIC_RVA = re.compile(r"^\t\|-RVA: (?P<rva>0x[0-9A-Fa-f]+)")
GENERIC_NAME = re.compile(r"^\t\|-(?P<name>[^R].*)$")
GENERIC_PARAMS = re.compile(r"<[^<>]*(?:<[^<>]*>[^<>]*)*>")
FIELD = re.compile(r"^\t(?P<decl>[^(/\[][^(]*?);(?: // (?P<off>0x[0-9A-Fa-f]+))?$")
# Generic parameters of a method name, one level of nesting: <TSource, Func<T, bool>>.
GENERIC_ARGS = r"<[^()<>]*(?:<[^()<>]*>[^()<>]*)*>"
# A method: "{ }" after a body-less dump, ";" for abstract and interface methods.
METHOD = re.compile(
    rf"^\t(?P<decl>[^/\[|].*?\b(?P<name>[\w.$`|]+(?:{GENERIC_ARGS})?)\(.*\))(?: \{{ \}}|;)$"
)


@dataclass
class Method:
    """A method of a type: its declaration and where its code is."""

    name: str
    decl: str  # "private void CalcSkidSmoke()"
    rva: int | None  # None: abstract, extern or only generic instances
    line: int
    # Shared code of generic instantiations: (rva, instantiation names).
    instances: list[tuple[int, list[str]]] = field(default_factory=list)

    def matches(self, query: str) -> bool:
        """Whether `query` names this method.

        Generic parameters are optional ("Select" finds "Select<TSource, TResult>"), and an
        explicit interface implementation ("System.Collections.IEnumerator.MoveNext") is found
        by its full name or by its last part ("MoveNext").
        """
        bare = GENERIC_PARAMS.sub("", self.name)
        return query in (self.name, bare) or any(n.endswith("." + query) for n in (self.name, bare))

    @property
    def code(self) -> list[int]:
        """Where the code is: the method's RVA, or else its generic instances' RVAs."""
        return [self.rva] if self.rva is not None else [rva for rva, _ in self.instances]


@dataclass
class Field:
    """A field of a type and its offset in the object (None for constants)."""

    decl: str
    offset: int | None
    line: int


@dataclass
class TypeDef:
    """One type of dump.cs."""

    namespace: str
    name: str  # nested types keep their outer name: "CarDynamics.SurfaceType"
    kind: str  # class, struct, enum, interface
    base: str | None
    line: int
    fields: list[Field] = field(default_factory=list)
    properties: list[str] = field(default_factory=list)
    methods: list[Method] = field(default_factory=list)

    @property
    def full_name(self) -> str:
        """Namespace-qualified name: "UnityEngine.ParticleSystem"."""
        return f"{self.namespace}.{self.name}" if self.namespace else self.name

    def matches(self, query: str) -> bool:
        """Whether `query` names this type, with or without namespace and generic parameters."""
        bare = GENERIC_PARAMS.sub("", self.name)
        ns = f"{self.namespace}." if self.namespace else ""
        return query in (self.name, self.full_name, bare, ns + bare)


def parse_dump(text: str) -> list[TypeDef]:
    """The types of a dump.cs text, in file order."""
    types: list[TypeDef] = []
    ns = ""
    cur: TypeDef | None = None
    section = ""
    pending_rva: int | None = None
    have_rva = False
    for lineno, line in enumerate(text.splitlines(), 1):
        if m := NAMESPACE.match(line):
            ns = m["ns"]
            continue
        if m := TYPE_HEADER.match(line):
            cur = TypeDef(ns, m["name"], m["kind"], m["base"], lineno)
            types.append(cur)
            section = ""
            continue
        if cur is None:
            continue
        s = line.strip()
        if s in ("// Fields", "// Properties", "// Methods"):
            section = s[3:]
            continue
        if m := METHOD_RVA.match(line):
            pending_rva = None if m["rva"] == "-1" else int(m["rva"], 16)
            have_rva = True
            continue
        if (m := GENERIC_RVA.match(line)) and cur.methods:
            cur.methods[-1].instances.append((int(m["rva"], 16), []))
            continue
        if (m := GENERIC_NAME.match(line)) and cur.methods and cur.methods[-1].instances:
            cur.methods[-1].instances[-1][1].append(m["name"])
            continue
        if section == "Fields" and (m := FIELD.match(line)):
            off = int(m["off"], 16) if m["off"] else None
            cur.fields.append(Field(m["decl"], off, lineno))
        elif section == "Properties" and s.endswith("}") and "{" in s:
            cur.properties.append(s)
        elif section == "Methods" and have_rva and (m := METHOD.match(line)):
            cur.methods.append(Method(m["name"], m["decl"], pending_rva, lineno))
            have_rva = False
    return types


def split_member(query: str) -> list[tuple[str, str]]:
    """The ways to split a query into (type, member).

    "Wheel::X" and "Wheel$$X" split once. A dotted query splits at every dot, because both the
    namespace and an explicit interface member contain dots:
    "List.System.Collections.IEnumerable.GetEnumerator" is type "List" with that member.
    """
    for sep in ("$$", "::"):
        if sep in query:
            t, _, m = query.partition(sep)
            return [(t, m)]
    parts = query.split(".")
    return [(".".join(parts[:i]), ".".join(parts[i:])) for i in range(len(parts) - 1, 0, -1)]


def find_types(types: list[TypeDef], query: str) -> list[TypeDef]:
    """Types named `query` (short or namespace-qualified)."""
    return [t for t in types if t.matches(query)]


def find_methods(types: list[TypeDef], query: str) -> list[tuple[TypeDef, Method]]:
    """Methods named by "Type.Method" (every overload), in dump order."""
    found: dict[int, tuple[TypeDef, Method]] = {}
    for tname, mname in split_member(query):
        for t in find_types(types, tname):
            for m in t.methods:
                if m.matches(mname):
                    found.setdefault(m.line, (t, m))
    return [found[k] for k in sorted(found)]


def grep(types: list[TypeDef], pattern: str) -> Iterator[str]:
    """Type, field and method names matching the regex `pattern`, one line each."""
    rx = re.compile(pattern)
    for t in types:
        if rx.search(t.full_name):
            yield f"{t.line}: {t.kind} {t.full_name}"
        for f in t.fields:
            if rx.search(f.decl):
                yield f"{f.line}: {t.full_name} field {f.decl}{fmt_offset(f.offset)}"
        for m in t.methods:
            if rx.search(m.name):
                yield f"{m.line}: {t.full_name}.{m.name} {fmt_rva(m.rva)}"


def fmt_rva(rva: int | None) -> str:
    """An RVA as the dump writes it, or "(no code)"."""
    return f"RVA {rva:#x}" if rva is not None else "(no code)"


def fmt_names(names: list[str], limit: int = 4) -> str:
    """The first `limit` generic instantiation names and how many more there are."""
    more = f" (+{len(names) - limit} more)" if len(names) > limit else ""
    return ", ".join(names[:limit]) + more


def fmt_offset(off: int | None) -> str:
    """A field offset suffix, or nothing for constants."""
    return f" @ {off:#x}" if off is not None else ""


def format_type(t: TypeDef) -> Iterator[str]:
    """A type's members: fields with offsets, properties, methods with RVAs."""
    base = f" : {t.base}" if t.base else ""
    yield f"{t.kind} {t.full_name}{base}    (dump.cs:{t.line})"
    for f in t.fields:
        yield f"  {f.offset:#6x}  {f.decl}" if f.offset is not None else f"          {f.decl}"
    for p in t.properties:
        yield f"          {p}"
    for m in t.methods:
        rva = f"{m.rva:#9x}" if m.rva is not None else " " * 9
        yield f"  {rva}  {m.decl}"
        for irva, names in m.instances:
            yield f"  {irva:#9x}    generic: {fmt_names(names)}"


def format_method(t: TypeDef, m: Method) -> Iterator[str]:
    """One method: declaration, RVA, line, and its generic instances."""
    yield f"{t.full_name}: {m.decl}"
    yield f"  {fmt_rva(m.rva)}    (dump.cs:{m.line})"
    for irva, names in m.instances:
        yield f"  RVA {irva:#x}  generic: {fmt_names(names)}"


# Raw-text counts the parser must reproduce (check): type headers, method RVA comments and
# field-like lines ("...;" or "...; // 0xOFF" without parentheses) inside types.
RAW_TYPES = re.compile(r"// TypeDefIndex: \d+$", re.MULTILINE)
RAW_METHODS = re.compile(r"^\t// RVA: ", re.MULTILINE)
RAW_FIELDS = re.compile(r"^\t(?![/\[|])[^(\n]*;(?: // 0x[0-9A-Fa-f]+)?$", re.MULTILINE)
IDENT_NAME = re.compile(r"^[A-Za-z_.][\w.$`|]*(?:<.*>)?$")


@dataclass(frozen=True)
class CheckRow:
    """One self-check: what the parser found against what the raw text says."""

    what: str
    parsed: int
    expected: int

    @property
    def ok(self) -> bool:
        """Whether the two counts agree."""
        return self.parsed == self.expected


def check(text: str, types: list[TypeDef], script_methods: set[int]) -> list[CheckRow]:
    """Compares the parse of `text` with counts taken from the raw dump.cs and script.json.

    Run it after changing the parser or on another game's dump: a mismatch means some types,
    fields or methods are silently missing from every query.

    Args:
        text: dump.cs.
        types: parse_dump(text).
        script_methods: the method addresses of script.json (what il2cpp_disasm names calls by).
    """
    methods = [m for t in types for m in t.methods]
    with_code = {rva for m in methods for rva in m.code}
    return [
        CheckRow("types", len(types), len(RAW_TYPES.findall(text))),
        CheckRow("fields", sum(len(t.fields) for t in types), len(RAW_FIELDS.findall(text))),
        CheckRow("methods", len(methods), len(RAW_METHODS.findall(text))),
        CheckRow(
            "method names", sum(bool(IDENT_NAME.match(m.name)) for m in methods), len(methods)
        ),
        CheckRow("code addresses in script.json", len(with_code & script_methods), len(with_code)),
    ]


def load(dump_dir: Path) -> list[TypeDef]:
    """The parsed dump.cs of a dump folder.

    Raises:
        FileNotFoundError: there is no dump (run tools/il2cpp_dump.py).
    """
    path = dump_dir / "dump.cs"
    if not path.is_file():
        raise FileNotFoundError(f"{path} missing: run python -m tools.il2cpp_dump first")
    return parse_dump(path.read_text(errors="replace"))


def run_check(dump_dir: Path, types: list[TypeDef]) -> int:
    """Prints the self-check table; 1 if any row disagrees."""
    script = json.loads((dump_dir / "script.json").read_text())
    addrs = {m["Address"] for m in script["ScriptMethod"]}
    rows = check((dump_dir / "dump.cs").read_text(errors="replace"), types, addrs)
    for r in rows:
        print(f"{'ok' if r.ok else 'MISMATCH':8} {r.what:32} {r.parsed:7} / {r.expected}")
    return 0 if all(r.ok for r in rows) else 1


def main(argv: list[str] | None = None) -> int:
    """Command line: class / method / grep / addr queries."""
    ap = argparse.ArgumentParser(description="Query an Il2CppDumper dump (dump.cs, script.json).")
    ap.add_argument("--release", choices=sorted(RELEASES), help="default: the loader's target")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("class", help="a type's fields and methods").add_argument("name")
    sub.add_parser("method", help="Type.Method: RVA and declaration").add_argument("name")
    sub.add_parser("grep", help="names matching a regex").add_argument("pattern")
    sub.add_parser("addr", help="the method at libil2cpp RVAs").add_argument("rva", nargs="+")
    sub.add_parser("check", help="compare the parse with counts from the raw dump")
    args = ap.parse_args(argv)
    rel = RELEASES[args.release] if args.release else target_release()
    try:
        if args.cmd == "addr":
            sym = Symbolizer.from_script_json(rel.dump / "script.json")
            for a in args.rva:
                print(a, sym.lookup(int(a, 16)) or "?")
            return 0
        types = load(rel.dump)
        if args.cmd == "check":
            return run_check(rel.dump, types)
    except OSError as e:
        print(f"il2cpp_find: {e}", file=sys.stderr)
        return 1
    if args.cmd == "class":
        out = [line for t in find_types(types, args.name) for line in format_type(t)]
    elif args.cmd == "method":
        out = [line for t, m in find_methods(types, args.name) for line in format_method(t, m)]
    else:
        out = list(grep(types, args.pattern))
    if not out:
        what = args.pattern if args.cmd == "grep" else args.name
        print(f"il2cpp_find: nothing matches {what}", file=sys.stderr)
        return 1
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
