"""A mod's mod.ini and patch.ini as the port reads them.

The port's own parser (source/mods/rd_mod_ini.c) is built into a small host program,
tools/mod_ini_check.c, so the tools apply exactly the rules the console applies. Used by
tools/mod_check.py and tools/mod_assets.py.

    from tools.mod_ini import read_mod
    ini = read_mod(Path("mods/hello"))      # ini.info["priority"], ini.patches, ini.errors

MIT (see LICENSE).
"""

from __future__ import annotations

import subprocess
from dataclasses import dataclass, field
from pathlib import Path

from tools import hostc
from tools.game import ROOT

CHECKER_SRC = ROOT / "tools" / "mod_ini_check.c"
CHECKER = ROOT / "build-host" / "mod_ini_check"


@dataclass(frozen=True)
class PatchLine:
    """One patch.ini value as the port reads it."""

    line: int
    assembly: str
    cls: str  # "Ns.Class" or "Class", nested as "Outer/Inner"
    field: str
    value: str
    filter: str = ""  # object name pattern of "[Asm:Class @ filter]", "" = every object


def checker() -> Path:
    """The mod_ini_check binary, rebuilt when its sources are newer.

    Raises:
        RuntimeError: it does not build.
    """
    sources = [CHECKER_SRC, *hostc.linked_sources(CHECKER_SRC)]
    if not CHECKER.exists() or any(s.stat().st_mtime > CHECKER.stat().st_mtime for s in sources):
        run = hostc.build(CHECKER_SRC, CHECKER)
        if run.returncode != 0:
            raise RuntimeError(f"cannot build {CHECKER_SRC.name}:\n{run.stderr}")
    return CHECKER


def parse_checker_output(text: str) -> tuple[dict[str, str], list[PatchLine], list[str]]:
    """The info keys, patch lines and errors printed by mod_ini_check."""
    info: dict[str, str] = {}
    patches: list[PatchLine] = []
    errors: list[str] = []
    for line in text.splitlines():
        if line.startswith("patch\t"):
            num, asm, cls, filt, fld, value = (line.split("\t")[1:] + [""] * 6)[:6]
            patches.append(PatchLine(int(num), asm, cls, fld, value, filt))
            continue
        kind, _, rest = line.partition(" ")
        if kind == "info":
            key, _, value = rest.partition("=")
            info[key] = value
        elif kind == "error":
            errors.append(rest)
    return info, patches, errors


@dataclass(frozen=True)
class ModIni:
    """What the port reads from a mod folder's ini files."""

    info: dict[str, str]  # id, name, version, api, enabled, priority
    patches: list[PatchLine] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)

    @property
    def priority(self) -> int:
        """The mod's load priority (0 when mod.ini is invalid)."""
        try:
            return int(self.info.get("priority", "0"))
        except ValueError:
            return 0


def read_mod(mod: Path) -> ModIni:
    """mod.ini and patch.ini of a mod folder through the port's parser.

    Raises:
        RuntimeError: the parser program does not build.
    """
    run = subprocess.run([str(checker()), str(mod)], capture_output=True, text=True, check=False)
    info, patches, errors = parse_checker_output(run.stdout)
    return ModIni(info, patches, errors)
