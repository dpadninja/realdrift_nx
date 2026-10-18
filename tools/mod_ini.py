"""A mod's mod.ini, patch.ini and cars.ini as the port reads them.

The port's own parser (source/mods/rd_mod_ini.c) is built into a small host program,
tools/mod_ini_check.c, so the tools apply exactly the rules the console applies. Used by
tools/mod_check.py and tools/mod_assets.py.

    from tools.mod_ini import read_mod
    ini = read_mod(Path("mods/hello"))      # ini.info["priority"], ini.patches, ini.errors

    uv run python -m tools.mod_ini keys     # cars.ini's own keys: when written, unit or names

MIT (see LICENSE).
"""

from __future__ import annotations

import functools
import subprocess
import sys
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


def parse_cars(text: str) -> list[CarLine]:
    """The cars.ini values printed by mod_ini_check."""
    out = []
    for line in text.splitlines():
        if line.startswith("car\t"):
            num, car, cls, fld, value, key = (line.split("\t")[1:] + [""] * 6)[:6]
            out.append(CarLine(int(num), car, cls, fld, value, key))
    return out


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
class CarLine:
    """One cars.ini value as the port reads it."""

    line: int
    car: str  # a car name, or "*" for every car
    cls: str  # "" = the field of a car component (RD_CAR_COMPONENTS)
    field: str
    value: str
    key: str = ""  # one of cars.ini's own keys (car_keys), "" for a field


@dataclass(frozen=True)
class ModIni:
    """What the port reads from a mod folder's ini files."""

    info: dict[str, str]  # id, name, version, api, enabled, priority
    patches: list[PatchLine] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    cars: list[CarLine] = field(default_factory=list)

    @property
    def priority(self) -> int:
        """The mod's load priority (0 when mod.ini is invalid)."""
        try:
            return int(self.info.get("priority", "0"))
        except ValueError:
            return 0


@dataclass(frozen=True)
class CarKey:
    """One of cars.ini's own keys (a row of rd_car_props in source/mods/rd_mod_cars.c)."""

    unit: str
    kind: str  # "field" (a car component's), "axle" (front / rear Axle) or "special"
    when: str  # "live" (while driving), "once" (per scene / car build), "build" (originals)
    cls: str
    field: str
    axle: str  # "front", "rear", "both" or ""
    names: tuple[str, ...]  # an enum's value names, () if not an enum


@functools.cache
def car_keys() -> dict[str, CarKey]:
    """cars.ini's own keys, in table order, from the port's table.

    Raises:
        RuntimeError: the parser program does not build.
    """
    run = subprocess.run([str(checker()), "--keys"], capture_output=True, text=True, check=True)
    rows = [line.split("\t") for line in run.stdout.splitlines() if line.startswith("carkey\t")]
    return {
        r[1]: CarKey(r[2], r[3], r[4], r[5], r[6], r[7], tuple(r[8].split()))
        for r in rows
        if len(r) == 9
    }


def read_mod(mod: Path) -> ModIni:
    """mod.ini, patch.ini and cars.ini of a mod folder through the port's parser.

    Raises:
        RuntimeError: the parser program does not build.
    """
    run = subprocess.run([str(checker()), str(mod)], capture_output=True, text=True, check=False)
    info, patches, errors = parse_checker_output(run.stdout)
    return ModIni(info, patches, errors, parse_cars(run.stdout))


def main(argv: list[str] | None = None) -> int:
    """Command line: `keys` prints cars.ini's own keys, one per line."""
    args = argv if argv is not None else sys.argv[1:]
    if args != ["keys"]:
        print("usage: python -m tools.mod_ini keys", file=sys.stderr)
        return 2
    for key, k in car_keys().items():
        unit = " ".join(k.names) if k.names else k.unit
        print(f"{key:24} {k.when:6} {unit}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
