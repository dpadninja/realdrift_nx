"""Disassemble one C# method of the game with names on everything the code refers to.

llvm-objdump alone shows a method body as bare addresses. This tool takes the method's range
from Il2CppDumper's script.json and annotates each instruction with what it touches:

- calls and branches: the C# method called (`Vector3$$op_Multiply`), or a place in this method
  (`<+0x1ac>`);
- il2cpp runtime helpers, recognised by how they are called: method metadata initialisation,
  the static-constructor check, NullReferenceException;
- adrp + ldr/add pairs: type info, method info and string literals from the metadata, C strings
  (icall names such as "UnityEngine.ParticleSystem::Internal_EmitOld") and float/double constants.

    uv run python -m tools.il2cpp_disasm Wheel.CalcSkidSmoke
    uv run python -m tools.il2cpp_disasm 0x95961c --release 5.0.7

Needs llvm-objdump and a dump from tools/il2cpp_dump.py.

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
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from tools.game import RELEASES, Release, target_release
from tools.il2cpp_find import TypeDef, find_types
from tools.il2cpp_find import load as load_dump
from tools.jni_natives import Segment, load_elf, read_cstr

OBJDUMP = shutil.which("llvm-objdump")
MAX_METHOD = 0x10000  # a range limit for the last method or a broken script.json
INSN = re.compile(r"^\s*(?P<addr>[0-9a-f]+):\s+(?P<mn>[a-z][\w.]*)(?:\s+(?P<ops>.*?))?\s*$")
# A branch operand: [reg, [#bit,]] 0xTARGET [<symbol[+off]>]
TARGET = re.compile(
    r"^(?:[wx]\d+, (?:#0x[0-9a-f]+, )?)?0x(?P<t>[0-9a-f]+)"
    r"(?: <(?P<sym>[^+>]*)(?P<plus>\+[^>]*)?>)?$"
)
ADRP = re.compile(r"^(?P<r>x\d+), 0x(?P<page>[0-9a-f]+)$")
LDR_OFF = re.compile(r"^(?P<d>[xwsdq]\d+), \[(?P<b>x\d+)(?:, #(?P<off>-?0x[0-9a-f]+))?\]$")
ADD_IMM = re.compile(r"^(?P<d>x\d+), (?P<b>x\d+), #(?P<off>0x[0-9a-f]+)$")
DEST = re.compile(r"^(?P<d>[xw]\d+)\b")
BRANCHES = {"b", "bl", "cbz", "cbnz", "tbz", "tbnz"}
MOV_REG = re.compile(r"^(?P<d>x\d+), (?P<s>x\d+)$")
CALL_CLOBBERED = {f"x{i}" for i in range(19)}  # AAPCS64: x0-x18 do not survive a call

# il2cpp runtime helpers, told apart by the instructions in front of the call.
HELPER_INIT = "il2cpp: initialise this method's metadata (first call only)"
HELPER_CCTOR = "il2cpp: run the class's static constructor if it has not run"
HELPER_NRE = "il2cpp: throw NullReferenceException"
HELPER_ICALL = "il2cpp: resolve the icall named in x0 (engine function pointer)"


def ends_flow(word: bytes | None) -> bool:
    """Whether the instruction `word` never falls through: ret, b <label> or br <reg>."""
    if word is None or len(word) != 4:
        return False
    w = struct.unpack("<I", word)[0]
    return (w & 0xFFFFFC1F) in (0xD65F0000, 0xD61F0000) or (w & 0xFC000000) == 0x14000000


@dataclass(frozen=True)
class Insn:
    """One disassembled instruction."""

    addr: int
    mnemonic: str
    operands: str


@dataclass
class Image:
    """What the annotations need: names from script.json and the library's bytes."""

    methods: dict[int, str]  # method start -> "Type$$Method"
    starts: list[int]  # every code start (named or not), sorted
    metadata: dict[int, str]  # metadata slot -> "TypeInfo Foo" / "MethodInfo ..." / "string ..."
    segments: list[Segment]
    relocs: dict[int, int]  # GOT slot -> the address it holds (R_AARCH64_RELATIVE)

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
        if ends_flow(self.read(addr - 4, 4)):
            # Code right after a ret or jump is a separate function the metadata does not list
            # (small struct accessors are emitted this way), not the middle of `start`.
            return f"sub_{addr:x} (unnamed, after {self.methods[start]})"
        return f"{self.methods[start]}+{addr - start:#x}"

    def read(self, addr: int, size: int) -> bytes | None:
        """`size` bytes of the file image at `addr`, or None outside the loaded segments."""
        for s in self.segments:
            if s.vaddr <= addr and addr + size <= s.vaddr + len(s.data):
                return s.data[addr - s.vaddr : addr - s.vaddr + size]
        return None

    def describe_data(self, addr: int) -> str | None:
        """Metadata, method or C string at `addr`, following one GOT indirection."""
        if addr in self.metadata:
            return self.metadata[addr]
        if addr in self.relocs:
            target = self.relocs[addr]
            return self.metadata.get(target) or self.name_code(target)
        s = read_cstr(self.segments, addr)
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


def parse_objdump(text: str) -> list[Insn]:
    """The instructions of llvm-objdump -d --no-show-raw-insn output."""
    out = []
    for line in text.splitlines():
        m = INSN.match(line)
        if m:
            out.append(Insn(int(m["addr"], 16), m["mn"], m["ops"] or ""))
    return out


def helper_kind(prev: list[Insn], img: Image) -> str | None:
    """What an il2cpp runtime call does, from up to four instructions before it."""
    if not prev:
        return None
    last = prev[-1]
    if len(prev) >= 2 and (m := ADRP.match(prev[-2].operands)) and m["r"] == "x0":
        add = ADD_IMM.match(last.operands) if last.mnemonic == "add" else None
        if add and add["d"] == "x0" and add["b"] == "x0":
            name = read_cstr(img.segments, int(m["page"], 16) + int(add["off"], 16)) or ""
            if "::" in name:
                return HELPER_ICALL
    if last.mnemonic == "mov" and last.operands == "x0, xzr":
        return HELPER_NRE
    if last.mnemonic == "ldr" and re.match(r"^w0, \[x\d+\]$", last.operands):
        return HELPER_INIT
    if any(p.mnemonic == "ldrb" and ", [x0, #" in p.operands for p in prev) and any(
        p.mnemonic in ("tbz", "tbnz") for p in prev
    ):
        return HELPER_CCTOR
    return None


def classify_helpers(insns: list[Insn], img: Image) -> dict[int, str]:
    """Runtime helper targets of this listing -> what they do (majority of the call sites)."""
    votes: dict[int, Counter[str]] = {}
    for i, ins in enumerate(insns):
        if ins.mnemonic != "bl" or not (m := TARGET.match(ins.operands)):
            continue
        t = int(m["t"], 16)
        if img.name_code(t) is None and (kind := helper_kind(insns[max(0, i - 4) : i], img)):
            votes.setdefault(t, Counter())[kind] += 1
    return {t: c.most_common(1)[0][0] for t, c in votes.items()}


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


def annotate(
    insns: list[Insn], img: Image, start: int, this_fields: dict[int, str] | None = None
) -> list[str]:
    """The listing with names.

    Args:
        insns: the method's instructions.
        img: names and bytes of the library.
        start: the method's address (local branches are shown relative to it).
        this_fields: offset -> field name of `this` for an instance method; loads and stores
            through `this` (x0 on entry and the registers it is copied to) get the field name.

    Returns:
        One line per instruction, notes after a ";".
    """
    end = insns[-1].addr if insns else start
    helpers = classify_helpers(insns, img)
    pages: dict[str, int] = {}  # register -> page loaded by adrp
    this_regs = {"x0"} if this_fields else set()
    out = []
    for ins in insns:
        ops, note = ins.operands, ""
        mem = LDR_OFF.match(ops) if ins.mnemonic.startswith(("ldr", "str")) else None
        if this_fields and mem and mem["b"] in this_regs:
            note = f"this.{field_name(this_fields, int(mem['off'] or '0', 16)) or '?'}"
        if is_branch(ins.mnemonic) and (m := TARGET.match(ops)):
            t = int(m["t"], 16)
            ops = ops[: m.start("t") - 2] + f"{t:#x}"
            if start <= t <= end:
                ops += f" <+{t - start:#x}>"
            elif name := img.name_code(t):
                note = name
            elif m["sym"] and not m["plus"]:
                note = m["sym"]  # llvm-objdump's own exact name: a PLT stub or an export
            else:
                note = helpers.get(t, "il2cpp runtime")
        elif ins.mnemonic == "adrp" and (m := ADRP.match(ops)):
            pages[m["r"]] = int(m["page"], 16)
            out.append(f"  {ins.addr:8x}:  {ins.mnemonic:<6} {ops}")
            continue
        elif ins.mnemonic.startswith("ldr") and mem and mem["b"] in pages:
            addr = pages[mem["b"]] + int(mem["off"] or "0", 16)
            note = describe_load(img, mem["d"], addr)
        elif ins.mnemonic == "add" and (m := ADD_IMM.match(ops)) and m["b"] in pages:
            note = img.describe_data(pages[m["b"]] + int(m["off"], 16)) or ""
        elif this_fields and ins.mnemonic == "add" and (m := ADD_IMM.match(ops)):
            if m["b"] in this_regs:  # the address of a (struct) field of `this`
                note = f"&this.{field_name(this_fields, int(m['off'], 16)) or '?'}"
        track_registers(ins, pages, this_regs)
        line = f"  {ins.addr:8x}:  {ins.mnemonic:<6} {ops}"
        out.append(f"{line:<56} ; {note}" if note else line)
    return out


def track_registers(ins: Insn, pages: dict[str, int], this_regs: set[str]) -> None:
    """Forgets what `ins` overwrites: adrp pages and copies of `this`."""
    if ins.mnemonic == "bl":
        this_regs -= CALL_CLOBBERED
        for r in CALL_CLOBBERED:
            pages.pop(r, None)
        return
    if is_branch(ins.mnemonic) or ins.mnemonic.startswith(("str", "stp", "cmp", "fcmp", "tst")):
        return
    d = DEST.match(ins.operands)
    if not d:
        return
    reg = "x" + d["d"][1:]
    pages.pop(reg, None)
    m = MOV_REG.match(ins.operands)
    if ins.mnemonic == "mov" and m and m["s"] in this_regs:
        this_regs.add(reg)
    else:
        this_regs.discard(reg)


def describe_load(img: Image, dest: str, addr: int) -> str:
    """What a load from `addr` into `dest` reads: a constant, metadata or a pointer."""
    if dest[0] in "sd":
        size, fmt = (4, "<f") if dest[0] == "s" else (8, "<d")
        raw = img.read(addr, size)
        return f"= {struct.unpack(fmt, raw)[0]:g}" if raw else ""
    return img.describe_data(addr) or ""


def find_method(methods: dict[int, str], query: str) -> list[int]:
    """Starts of the methods `query` names: an address, "Type.Method" or "Type$$Method"."""
    if re.fullmatch(r"0x[0-9a-fA-F]+", query):
        return [int(query, 16)]
    t, sep, m = query.rpartition("$$") if "$$" in query else query.rpartition(".")
    want = f"{t}$${m}" if sep else query
    return sorted(a for a, n in methods.items() if n == want or n.endswith("." + want))


def method_range(starts: list[int], start: int) -> tuple[int, int]:
    """[start, end) of the code at `start`: up to the next known code start."""
    i = bisect.bisect_right(starts, start)
    end = starts[i] if i < len(starts) else start + MAX_METHOD
    return start, min(end, start + MAX_METHOD)


def objdump(lib: Path, start: int, end: int) -> str:
    """llvm-objdump's listing of [start, end) of `lib`.

    Raises:
        RuntimeError: llvm-objdump is not installed or fails.
    """
    if OBJDUMP is None:
        raise RuntimeError("llvm-objdump not found")
    cmd = [OBJDUMP, "-d", "--no-show-raw-insn", f"--start-address={start:#x}"]
    cmd += [f"--stop-address={end:#x}", str(lib)]
    run = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if run.returncode != 0:
        raise RuntimeError(f"llvm-objdump failed: {run.stderr.strip()}")
    return run.stdout


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
    """script.json of the release's dump plus its libil2cpp.so.

    Raises:
        FileNotFoundError: the dump or the library is missing.
    """
    script_path = rel.dump / "script.json"
    if not script_path.is_file():
        raise FileNotFoundError(f"{script_path} missing: run python -m tools.il2cpp_dump first")
    script = json.loads(script_path.read_text())
    named, starts = method_names(script)
    segments, relocs = load_elf(rel.lib64 / "libil2cpp.so")
    return Image(named, starts, metadata_names(script), segments, relocs)


def main(argv: list[str] | None = None) -> int:
    """Command line: one method (every overload) as an annotated listing."""
    ap = argparse.ArgumentParser(description="Annotated disassembly of a C# method (libil2cpp).")
    ap.add_argument("method", help="Type.Method, Type$$Method or an RVA (0x...)")
    ap.add_argument("--release", choices=sorted(RELEASES), help="default: the loader's target")
    args = ap.parse_args(argv)
    rel = RELEASES[args.release] if args.release else target_release()
    try:
        img = load_image(rel)
        types = load_dump(rel.dump)
        found = find_method(img.methods, args.method)
        if not found:
            print(f"il2cpp_disasm: no method {args.method}", file=sys.stderr)
            return 1
        for start in found:
            lo, hi = method_range(img.starts, start)
            print(f"{img.name_code(start) or hex(start)}  [{lo:#x}, {hi:#x})")
            insns = parse_objdump(objdump(rel.lib64 / "libil2cpp.so", lo, hi))
            print("\n".join(annotate(insns, img, lo, this_fields_at(types, start))))
            print()
    except (OSError, RuntimeError) as e:
        print(f"il2cpp_disasm: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
