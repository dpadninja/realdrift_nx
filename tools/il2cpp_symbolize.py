"""Name libil2cpp.so addresses using Il2CppDumper's script.json.

Usage:
  python tools/il2cpp_symbolize.py analysis/dump507/script.json 0x11223a8 0x675ab0
  python tools/il2cpp_symbolize.py analysis/dump507/script.json --log analysis/logs/run08-debug.log

With --log, every "libil2cpp.so+0x..." in the log is annotated with its method name.
"""
import bisect
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ADDR_IN_LOG = re.compile(r"libil2cpp\.so\+(0x[0-9a-fA-F]+)")


@dataclass(frozen=True)
class Symbolizer:
    starts: list[int]
    names: list[str]

    @classmethod
    def from_methods(cls, methods: list[dict]) -> "Symbolizer":
        pairs = sorted((m["Address"], m["Name"]) for m in methods if m.get("Address"))
        return cls([a for a, _ in pairs], [n for _, n in pairs])

    @classmethod
    def from_script_json(cls, path: Path) -> "Symbolizer":
        return cls.from_methods(json.loads(path.read_text())["ScriptMethod"])

    def lookup(self, rva: int) -> str | None:
        """Method containing `rva` (the closest method start at or below it)."""
        i = bisect.bisect_right(self.starts, rva) - 1
        if i < 0:
            return None
        return f"{self.names[i]}+{rva - self.starts[i]:#x}"

    def annotate(self, line: str) -> str:
        def repl(m: re.Match) -> str:
            name = self.lookup(int(m.group(1), 16))
            return f"{m.group(0)} [{name}]" if name else m.group(0)
        return ADDR_IN_LOG.sub(repl, line)


def main(argv: list[str]) -> int:
    sym = Symbolizer.from_script_json(Path(argv[0]))
    if len(argv) > 2 and argv[1] == "--log":
        for line in Path(argv[2]).read_text(errors="replace").splitlines():
            print(sym.annotate(line))
        return 0
    for a in argv[1:]:
        print(a, sym.lookup(int(a, 16)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
