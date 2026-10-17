"""Disassemble one C# method of the game with names on everything the code refers to.

A raw disassembly shows a method body as bare addresses. This tool takes the method's range
from Il2CppDumper's output and annotates each instruction with what it touches:

- calls and branches: the C# method called (`Vector3$$op_Multiply`), an exported function or
  PLT stub (`memcpy@plt`), or a place in this method (`<+0x1ac>`);
- il2cpp runtime helpers, recognised once for the whole library by how they are called:
  method metadata initialisation, the static-constructor check, NullReferenceException,
  icall resolution;
- fields of `this` read, written or passed by address, followed through register copies and
  across branches;
- adrp + ldr/add pairs: type info, method info and string literals from the metadata, C strings
  (icall names) and float/double constants.

    uv run python -m tools.il2cpp_disasm Wheel.CalcSkidSmoke
    uv run python -m tools.il2cpp_disasm System.Linq.Enumerable.Select   # generic: its instances
    uv run python -m tools.il2cpp_disasm 0x95961c --release 5.0.7
    uv run python -m tools.il2cpp_disasm Drivetrain.CalcValues --callers   # who calls it

Needs llvm-mc (LLVM) and a dump from tools/il2cpp_dump.py. The helper table is cached in the
dump folder (helpers.json) and rebuilt when libil2cpp.so changes.

MIT (see LICENSE).
"""

from __future__ import annotations

import argparse
import bisect
import json
import re
import shutil
import struct
import subprocess
import sys
from collections import Counter
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from tools import elfimage
from tools.elfimage import ElfImage, read_cstr
from tools.game import RELEASES, Release, target_release
from tools.il2cpp_find import TypeDef, find_methods, find_types
from tools.il2cpp_find import load as load_dump

LLVM_MC = shutil.which("llvm-mc")
MAX_METHOD = 0x10000  # a range limit for the last method or a broken script.json
MAX_LISTINGS = 8  # a generic method can have dozens of instances
HELPER_CACHE = "helpers.json"
HELPER_CACHE_VERSION = 1  # bump when the classification rules change

MC_LINE = re.compile(
    r"^\t(?P<mn>[a-z][\w.]*)(?:\t(?P<ops>.*?))?\s*// encoding: \[(?P<enc>[^\]]*)\]"
)
IMM_LAST = re.compile(r"#(?P<imm>-?(?:0x[0-9a-f]+|\d+))$")
# A branch operand after absolutize(): [reg, [#bit,]] 0xTARGET
TARGET = re.compile(r"^(?:[wx]\d+, (?:#0x[0-9a-f]+, )?)?0x(?P<t>[0-9a-f]+)$")
ADRP = re.compile(r"^(?P<r>x\d+), 0x(?P<page>[0-9a-f]+)$")
LDR_OFF = re.compile(r"^(?P<d>[xwsdqbh]\d+), \[(?P<b>x\d+)(?:, #(?P<off>-?0x[0-9a-f]+))?\]$")
LDR_LITERAL = re.compile(r"^[xwsdq]\d+, #")
ADD_IMM = re.compile(r"^(?P<d>x\d+), (?P<b>x\d+), #(?P<off>0x[0-9a-f]+)$")
DEST = re.compile(r"^(?P<d>[xw]\d+)\b")
PAIR_DEST = re.compile(r"^(?P<a>[xw]\d+), (?P<b>[xw]\d+), \[")
MOV_REG = re.compile(r"^(?P<d>x\d+), (?P<s>x\d+)$")
BRANCHES = {"b", "bl", "cbz", "cbnz", "tbz", "tbnz"}
NO_FALLTHROUGH = {"b", "br", "ret", "retaa", "retab"}
CALLS = {"bl", "blr", "blraa", "blrab"}
NO_DEST = ("st", "cmp", "cmn", "fcmp", "tst", "ccmp", "ccmn", "prfm", "b", "ret", "nop", "dmb")
CALL_CLOBBERED = frozenset(f"x{i}" for i in range(19))  # AAPCS64: x0-x18 do not survive

# il2cpp runtime helpers, told apart by the instructions in front of their calls.
HELPERS = {
    "init": "il2cpp: initialise this method's metadata (first call only)",
    "cctor": "il2cpp: run the class's static constructor if it has not run",
    "nre": "il2cpp: throw NullReferenceException",
    "icall": "il2cpp: resolve the icall named in x0 (engine function pointer)",
}
MIN_HELPER_VOTES = 3  # call sites with the pattern needed before a target is named


def ends_flow(word: bytes | None) -> bool:
    """Whether the instruction `word` never falls through: ret, b <label> or br <reg>."""
    if word is None or len(word) != 4:
        return False
    w = struct.unpack("<I", word)[0]
    return (w & 0xFFFFFC1F) in (0xD65F0000, 0xD61F0000) or (w & 0xFC000000) == 0x14000000


@dataclass(frozen=True)
class Insn:
    """One disassembled instruction; branch and adrp targets are absolute ("0x95966c")."""

    addr: int
    mnemonic: str
    operands: str


@dataclass
class Image:
    """What the annotations need: names from script.json and the library itself."""

    methods: dict[int, str]  # method start -> "Type$$Method"
    starts: list[int]  # every code start (named or not), sorted
    metadata: dict[int, str]  # metadata slot -> "Foo_TypeInfo" / "Method$..." / 'string "..."'
    elf: ElfImage
    helpers: dict[int, str] = field(default_factory=dict)  # runtime function -> HELPERS key

    def name_code(self, addr: int) -> str | None:
        """The C# method at `addr`, with an offset when `addr` is inside it."""
        if addr in self.methods:
            return self.methods[addr]
        i = bisect.bisect_right(self.starts, addr) - 1
        if i < 0:
            return None
        start = self.starts[i]
        end = self.starts[i + 1] if i + 1 < len(self.starts) else start
        # A huge gap to the next start is il2cpp's own code after the last method, not a body.
        if start not in self.methods or addr >= end or end - start > MAX_METHOD:
            return None
        if ends_flow(self.elf.read(addr - 4, 4)):
            # Code right after a ret or jump is a separate function the metadata does not list
            # (small struct accessors are emitted this way), not the middle of `start`.
            return f"sub_{addr:x} (unnamed, after {self.methods[start]})"
        return f"{self.methods[start]}+{addr - start:#x}"

    def name_target(self, addr: int) -> str:
        """Everything a call target can be named by, best first."""
        if name := self.name_code(addr) or self.elf.symbols.get(addr):
            return name
        return HELPERS[self.helpers[addr]] if addr in self.helpers else "il2cpp runtime"

    def describe_data(self, addr: int) -> str | None:
        """Metadata, method or C string at `addr`, following one GOT indirection."""
        if addr in self.metadata:
            return self.metadata[addr]
        if addr in self.elf.relocs:
            target = self.elf.relocs[addr]
            return self.metadata.get(target) or self.name_code(target)
        s = read_cstr(self.elf.segments, addr)
        return f'"{s}"' if s and len(s) >= 3 and s.isprintable() else None


def metadata_names(script: dict[str, Any]) -> dict[int, str]:
    """Metadata slot address -> description, from script.json's metadata tables."""
    out: dict[int, str] = {}
    for e in script.get("ScriptMetadata", []):
        out[e["Address"]] = e["Name"]
    for e in script.get("ScriptMetadataMethod", []):
        out[e["Address"]] = e["Name"]
    for e in script.get("ScriptString", []):
        out[e["Address"]] = f'string "{e["Value"]}"'
    return out


def method_names(script: dict[str, Any]) -> tuple[dict[int, str], list[int]]:
    """Named method starts and every known code start of script.json."""
    named = {m["Address"]: m["Name"] for m in script["ScriptMethod"] if m.get("Address")}
    starts = sorted(set(script.get("Addresses", [])) | set(named))
    return named, starts


# ----------------------------------------------------------------------------- disassembly


def absolutize(mnemonic: str, operands: str, pc: int) -> str:
    """llvm-mc prints PC-relative targets as offsets; make them absolute addresses."""
    m = IMM_LAST.search(operands)
    if not m:
        return operands
    imm = int(m["imm"], 0)
    if mnemonic in BRANCHES or mnemonic.startswith("b."):
        target = pc + imm
    elif mnemonic == "adrp":
        target = (pc & ~0xFFF) + imm
    elif mnemonic == "adr" or (mnemonic.startswith("ldr") and LDR_LITERAL.match(operands)):
        target = pc + imm
    else:
        return operands
    return operands[: m.start()] + f"{target:#x}"


def parse_mc(text: str, code: bytes, base: int) -> list[Insn]:
    """Instructions from llvm-mc --show-encoding output for `code` loaded at `base`.

    llvm-mc skips words it cannot decode (with a warning), so lines are matched to the input by
    their encoding; an undecodable word becomes a ".word" pseudo-instruction at its address.
    """
    words = [code[i : i + 4] for i in range(0, len(code) - len(code) % 4, 4)]
    out: list[Insn] = []
    pos = 0
    for line in text.splitlines():
        m = MC_LINE.match(line)
        if not m:
            continue
        enc = bytes(int(b, 16) for b in m["enc"].split(","))
        while pos < len(words) and words[pos] != enc:
            out.append(Insn(base + 4 * pos, ".word", f"{struct.unpack('<I', words[pos])[0]:#x}"))
            pos += 1
        if pos == len(words):
            break
        pc = base + 4 * pos
        out.append(Insn(pc, m["mn"], absolutize(m["mn"], (m["ops"] or "").strip(), pc)))
        pos += 1
    for p in range(pos, len(words)):
        out.append(Insn(base + 4 * p, ".word", f"{struct.unpack('<I', words[p])[0]:#x}"))
    return out


def disassemble(code: bytes, base: int) -> list[Insn]:
    """The instructions of `code` (AArch64) loaded at `base`.

    Raises:
        RuntimeError: llvm-mc is not installed or fails.
    """
    if LLVM_MC is None:
        raise RuntimeError("llvm-mc not found (install LLVM)")
    cmd = [LLVM_MC, "--disassemble", "-triple=aarch64", "--print-imm-hex", "--show-encoding"]
    hexs = " ".join(f"0x{b:02x}" for b in code)
    run = subprocess.run(cmd, input=hexs, capture_output=True, text=True, check=False)
    if run.returncode != 0 and not run.stdout:
        raise RuntimeError(f"llvm-mc failed: {run.stderr.strip()[:200]}")
    return parse_mc(run.stdout, code, base)


# ----------------------------------------------------------------- runtime helpers (global)


def decode_bl(word: int, pc: int) -> int | None:
    """The target of a `bl` instruction word, or None for any other instruction."""
    if word & 0xFC000000 != 0x94000000:
        return None
    imm = word & 0x3FFFFFF
    return pc + ((imm - (1 << 26) if imm & (1 << 25) else imm) << 2)


def decode_adrp_x0(word: int, pc: int) -> int | None:
    """The page an `adrp x0, page` loads, or None for any other instruction."""
    if word & 0x9F00001F != 0x90000000:
        return None
    imm = (((word >> 5) & 0x7FFFF) << 2) | ((word >> 29) & 3)
    imm = imm - (1 << 21) if imm & (1 << 20) else imm
    return (pc & ~0xFFF) + (imm << 12)


def word_helper_kind(prev: list[int], prev_pc: int, elf: ElfImage) -> str | None:
    """What a call does, from the instruction words before it (prev[0] is at `prev_pc`)."""
    if not prev:
        return None
    last = prev[-1]
    if last == 0xAA1F03E0:  # mov x0, xzr
        return "nre"
    if last & 0xFFFFFC1F == 0xB9400000:  # ldr w0, [xN]
        return "init"
    if len(prev) >= 2 and last & 0xFFC003FF == 0x91000000:  # add x0, x0, #imm
        page = decode_adrp_x0(prev[-2], prev_pc + 4 * (len(prev) - 2))
        name = read_cstr(elf.segments, page + ((last >> 10) & 0xFFF)) if page is not None else None
        if name and "::" in name:
            return "icall"
    ldrb_x0 = any(w & 0xFFC003E0 == 0x39400000 for w in prev)  # ldrb wN, [x0, #imm]
    if ldrb_x0 and any(w & 0x7E000000 == 0x36000000 for w in prev):  # tbz / tbnz
        return "cctor"
    return None


def scan_helpers(elf: ElfImage, is_known: Callable[[int], bool]) -> dict[int, str]:
    """Runtime functions of the whole library -> HELPERS key, by majority of their call sites.

    A target is named only when at least MIN_HELPER_VOTES call sites show one pattern and that
    pattern is at least half of all its calls, so a function is named the same everywhere.
    """
    calls: Counter[int] = Counter()
    votes: dict[int, Counter[str]] = {}
    for lo, hi in elf.code:
        data = elf.read(lo, hi - lo) or b""
        words = struct.unpack(f"<{len(data) // 4}I", data[: len(data) - len(data) % 4])
        for i, w in enumerate(words):
            t = decode_bl(w, lo + 4 * i)
            if t is None or is_known(t):
                continue
            calls[t] += 1
            first = max(0, i - 4)
            if kind := word_helper_kind(list(words[first:i]), lo + 4 * first, elf):
                votes.setdefault(t, Counter())[kind] += 1
    out = {}
    for t, c in votes.items():
        kind, n = c.most_common(1)[0]
        if n >= MIN_HELPER_VOTES and 2 * n >= calls[t]:
            out[t] = kind
    return out


def decode_branch(word: int, pc: int) -> int | None:
    """The target of a `bl` or `b` (tail call) instruction word, or None."""
    if word & 0x7C000000 != 0x14000000:
        return None
    imm = word & 0x3FFFFFF
    return pc + ((imm - (1 << 26) if imm & (1 << 25) else imm) << 2)


def find_callers(elf: ElfImage, targets: set[int]) -> dict[int, list[int]]:
    """Addresses of the bl / b instructions in the library's code that go to each target.

    Only direct calls: virtual calls (blr through a vtable), delegates and Unity messages
    (Update, Start, ... called by the engine) do not show up.
    """
    out: dict[int, list[int]] = {t: [] for t in targets}
    for lo, hi in elf.code:
        data = elf.read(lo, hi - lo) or b""
        words = struct.unpack(f"<{len(data) // 4}I", data[: len(data) - len(data) % 4])
        for i, w in enumerate(words):
            t = decode_branch(w, lo + 4 * i)
            if t in out:
                out[t].append(lo + 4 * i)
    return out


def cached_helpers(cache: Path, lib: Path, img: Image) -> dict[int, str]:
    """scan_helpers for `lib`, cached in `cache` until the library changes."""
    st = lib.stat()
    key = f"{HELPER_CACHE_VERSION}:{st.st_size}:{st.st_mtime_ns}"
    if cache.is_file():
        data = json.loads(cache.read_text())
        if data.get("key") == key:
            return {int(a, 16): k for a, k in data["helpers"].items()}
    helpers = scan_helpers(img.elf, lambda t: img.name_code(t) is not None or t in img.elf.symbols)
    cache.write_text(
        json.dumps({"key": key, "helpers": {f"{a:#x}": k for a, k in helpers.items()}})
    )
    return helpers


# --------------------------------------------------------------------- register tracking


@dataclass(frozen=True)
class State:
    """What is known before an instruction: adrp pages in registers and copies of `this`."""

    pages: dict[str, int]
    this: frozenset[str]

    def meet(self, other: State) -> State:
        """What holds on both paths into a join point."""
        pages = {r: p for r, p in self.pages.items() if other.pages.get(r) == p}
        return State(pages, self.this & other.this)


EMPTY = State({}, frozenset())


def written(ins: Insn) -> list[str]:
    """The x registers `ins` overwrites (w registers count as their x register)."""
    mn, ops = ins.mnemonic, ins.operands
    if mn in CALLS:
        return sorted(CALL_CLOBBERED)
    if mn.startswith(("stxr", "stlxr")):  # the status register is written
        d = DEST.match(ops)
        return ["x" + d["d"][1:]] if d else []
    if mn.startswith(NO_DEST) or mn.startswith(("b.", "cb", "tb")):
        return []
    if mn.startswith("ldp") or mn.startswith("ldnp"):
        p = PAIR_DEST.match(ops)
        return ["x" + p["a"][1:], "x" + p["b"][1:]] if p else []
    d = DEST.match(ops)
    return ["x" + d["d"][1:]] if d else []


def transfer(ins: Insn, s: State) -> State:
    """The state after `ins`."""
    pages = dict(s.pages)
    this = set(s.this)
    for r in written(ins):
        pages.pop(r, None)
        this.discard(r)
    if ins.mnemonic == "adrp" and (m := ADRP.match(ins.operands)):
        pages[m["r"]] = int(m["page"], 16)
    elif ins.mnemonic == "mov" and (m := MOV_REG.match(ins.operands)) and m["s"] in s.this:
        this.add(m["d"])
    return State(pages, frozenset(this))


def successors(insns: list[Insn], i: int, index: dict[int, int]) -> list[int]:
    """Instruction indices control can reach from insns[i] within the listing."""
    ins = insns[i]
    out = [] if ins.mnemonic in NO_FALLTHROUGH else [i + 1]
    if is_branch(ins.mnemonic) and ins.mnemonic != "bl" and (m := TARGET.match(ins.operands)):
        t = index.get(int(m["t"], 16))
        if t is not None:
            out.append(t)
    return [j for j in out if j < len(insns)]


def flow(insns: list[Insn], this_on_entry: bool) -> list[State]:
    """The state before each instruction, merged over every path that reaches it.

    Instructions no path reaches (data, padding) get the empty state.
    """
    index = {ins.addr: i for i, ins in enumerate(insns)}
    states: list[State | None] = [None] * len(insns)
    if not insns:
        return []
    states[0] = State({}, frozenset({"x0"}) if this_on_entry else frozenset())
    work = [0]
    while work:
        i = work.pop()
        s = states[i]
        assert s is not None
        after = transfer(insns[i], s)
        for j in successors(insns, i, index):
            old = states[j]
            new = after if old is None else old.meet(after)
            if new != old:
                states[j] = new
                work.append(j)
    return [s or EMPTY for s in states]


# ----------------------------------------------------------------------------- annotation


def is_branch(mnemonic: str) -> bool:
    """Whether `mnemonic` is a direct branch or call (b, bl, b.cond, cbz, tbz, ...)."""
    return mnemonic in BRANCHES or mnemonic.startswith("b.")


def field_name(fields: dict[int, str], off: int) -> str | None:
    """The field at object offset `off` ("pos", or "pos+0x4" inside a struct field)."""
    offs = sorted(fields)
    i = bisect.bisect_right(offs, off) - 1
    if i < 0:
        return None
    base = offs[i]
    return fields[base] if base == off else f"{fields[base]}+{off - base:#x}"


def note_for(
    ins: Insn, s: State, img: Image, span: tuple[int, int], fields: dict[int, str] | None
) -> tuple[str, str]:
    """(operands to print, note) for one instruction in state `s`; span is the method's range."""
    ops, mn = ins.operands, ins.mnemonic
    mem = LDR_OFF.match(ops) if mn.startswith(("ldr", "str", "ldur", "stur")) else None
    if is_branch(mn) and (m := TARGET.match(ops)):
        t = int(m["t"], 16)
        if span[0] <= t <= span[1]:
            return f"{ops} <+{t - span[0]:#x}>", ""
        return ops, img.name_target(t)
    if mem and fields and mem["b"] in s.this:
        return ops, f"this.{field_name(fields, int(mem['off'] or '0', 16)) or '?'}"
    if mem and mn.startswith("ldr") and mem["b"] in s.pages:
        return ops, describe_load(img, mem["d"], s.pages[mem["b"]] + int(mem["off"] or "0", 16))
    if mn == "add" and (m := ADD_IMM.match(ops)):
        if m["b"] in s.pages:
            return ops, img.describe_data(s.pages[m["b"]] + int(m["off"], 16)) or ""
        if fields and m["b"] in s.this:  # the address of a (struct) field of `this`
            return ops, f"&this.{field_name(fields, int(m['off'], 16)) or '?'}"
    if mn == "adr" and (m := TARGET.match(ops.split(", ")[-1])):
        return ops, img.describe_data(int(m["t"], 16)) or ""
    return ops, ""


def annotate(
    insns: list[Insn], img: Image, start: int, this_fields: dict[int, str] | None = None
) -> list[str]:
    """The listing with names.

    Args:
        insns: the method's instructions.
        img: names and bytes of the library.
        start: the method's address (local branches are shown relative to it).
        this_fields: offset -> field name of `this` for an instance method; loads, stores and
            addresses through `this` (x0 on entry and the registers it is copied to, on every
            path) get the field name.

    Returns:
        One line per instruction, notes after a ";".
    """
    span = (start, insns[-1].addr if insns else start)
    out = []
    for ins, s in zip(insns, flow(insns, bool(this_fields)), strict=True):
        ops, note = note_for(ins, s, img, span, this_fields)
        line = f"  {ins.addr:8x}:  {ins.mnemonic:<6} {ops}"
        out.append(f"{line:<56} ; {note}" if note else line)
    return out


def describe_load(img: Image, dest: str, addr: int) -> str:
    """What a load from `addr` into `dest` reads: a constant, metadata or a pointer."""
    if dest[0] in "sd":
        size, fmt = (4, "<f") if dest[0] == "s" else (8, "<d")
        raw = img.elf.read(addr, size)
        return f"= {struct.unpack(fmt, raw)[0]:g}" if raw else ""
    return img.describe_data(addr) or ""


# ----------------------------------------------------------------------------- lookup


def find_method(methods: dict[int, str], query: str) -> list[int]:
    """Starts named in script.json: an address, "Type.Method" or "Type$$Method"."""
    if re.fullmatch(r"0x[0-9a-fA-F]+", query):
        return [int(query, 16)]
    t, sep, m = query.rpartition("$$") if "$$" in query else query.rpartition(".")
    want = f"{t}$${m}" if sep else query
    return sorted(a for a, n in methods.items() if n == want or n.endswith("." + want))


def method_starts(query: str, types: list[TypeDef], methods: dict[int, str]) -> list[int]:
    """Code addresses for a query.

    The dump's methods first (a generic one by its instances), else script.json names such as
    "List<object>$$Add", or an address.
    """
    found = [rva for _, m in find_methods(types, query) for rva in m.code]
    return list(dict.fromkeys(found)) or find_method(methods, query)


def method_range(starts: list[int], start: int) -> tuple[int, int]:
    """[start, end) of the code at `start`: up to the next known code start."""
    i = bisect.bisect_right(starts, start)
    end = starts[i] if i < len(starts) else start + MAX_METHOD
    return start, min(end, start + MAX_METHOD)


def instance_fields(types: list[TypeDef], t: TypeDef) -> dict[int, str]:
    """Offset -> name of the instance fields of `t` and its base classes from the dump."""
    out: dict[int, str] = {}
    seen: set[str] = set()
    cur: TypeDef | None = t
    while cur is not None and cur.full_name not in seen:
        seen.add(cur.full_name)
        for f in cur.fields:
            words = f.decl.split()
            if f.offset is not None and "static" not in words and "const" not in words:
                out.setdefault(f.offset, words[-1])
        base = (cur.base or "").split(",")[0].strip()
        found = find_types(types, base) if base else []
        cur = found[0] if found else None
    return out


def this_fields_at(types: list[TypeDef], rva: int) -> dict[int, str] | None:
    """Fields of `this` for the instance method whose code starts at `rva`.

    None for a static method, or when no single type owns the code (shared generic code).
    """
    for t in types:
        for m in t.methods:
            if m.rva == rva:
                is_static = "static" in m.decl.split("(")[0].split()
                return None if is_static else instance_fields(types, t)
    return None


def load_image(rel: Release) -> Image:
    """script.json of the release's dump plus its libil2cpp.so (and the helper table).

    Raises:
        FileNotFoundError: the dump or the library is missing.
    """
    script_path = rel.dump / "script.json"
    if not script_path.is_file():
        raise FileNotFoundError(f"{script_path} missing: run python -m tools.il2cpp_dump first")
    script = json.loads(script_path.read_text())
    named, starts = method_names(script)
    lib = rel.lib64 / "libil2cpp.so"
    img = Image(named, starts, metadata_names(script), elfimage.load(lib))
    img.helpers = cached_helpers(rel.dump / HELPER_CACHE, lib, img)
    return img


def main(argv: list[str] | None = None) -> int:
    """Command line: one method (every overload) as an annotated listing."""
    ap = argparse.ArgumentParser(description="Annotated disassembly of a C# method (libil2cpp).")
    ap.add_argument("method", help="Type.Method, Type$$Method or an RVA (0x...)")
    ap.add_argument("--release", choices=sorted(RELEASES), help="default: the loader's target")
    ap.add_argument("--callers", action="store_true", help="list who calls it instead")
    args = ap.parse_args(argv)
    rel = RELEASES[args.release] if args.release else target_release()
    try:
        img = load_image(rel)
        types = load_dump(rel.dump)
        found = method_starts(args.method, types, img.methods)
        if not found:
            print(f"il2cpp_disasm: no method {args.method}", file=sys.stderr)
            return 1
        if args.callers:
            for target, sites in find_callers(img.elf, set(found)).items():
                print(f"{img.name_code(target) or hex(target)}: {len(sites)} direct callers")
                for site in sites:
                    print(f"  {site:8x}  {img.name_code(site) or 'il2cpp runtime'}")
            return 0
        if len(found) > MAX_LISTINGS:
            more = f"{len(found)} bodies, showing {MAX_LISTINGS}"
            print(f"il2cpp_disasm: {more}; pass an address for another", file=sys.stderr)
        for start in found[:MAX_LISTINGS]:
            lo, hi = method_range(img.starts, start)
            print(f"{img.name_code(start) or hex(start)}  [{lo:#x}, {hi:#x})")
            insns = disassemble(img.elf.read(lo, hi - lo) or b"", lo)
            print("\n".join(annotate(insns, img, lo, this_fields_at(types, start))))
            print()
    except (OSError, RuntimeError) as e:
        print(f"il2cpp_disasm: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
