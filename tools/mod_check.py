"""Check a mod folder before putting it on the console.

Everything the port checks at boot, plus what it can only find out on the console, checked on
the PC with the game's own files:

- mod.ini and patch.ini, read by the port's own parser (tools/mod_ini_check.c, built with
  tools/hostc.py) so the rules cannot drift apart;
- patch.ini classes and fields exist in the dump and have a type patch.ini supports;
- cars.ini car names are drivable cars of the game (tools/mod_assets.py cars), and each key
  names exactly one field of a car component (RD_CAR_COMPONENTS in rd_game_symbols.h) or a
  Class.field, of a supported type;
- mod.so is an aarch64 shared library exporting rd_mod_entry, with only imports and
  relocations the loader provides, and the engine icall names it contains are registered in
  this game (tools/unity_ref.py status "game");
- files/apk and files/obb paths are members of base.apk / the OBB (a new path is allowed but
  usually a typo).

    uv run python -m tools.mod_check mods/examples/hello
    uv run python -m tools.mod_check mods/*            # several

One line per problem ("error:" fails the check, "warning:" does not), then "ok <id>" or
"FAILED <id>". Exit code 1 if any mod failed.

MIT (see LICENSE).
"""

from __future__ import annotations

import argparse
import re
import sys
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from elftools.elf.sections import SymbolTableSection
from PIL import Image

from tools.game import ROOT, strip_comments, target_release, user_archives
from tools.il2cpp_find import TypeDef, find_types
from tools.il2cpp_find import load as load_dump
from tools.import_coverage import table_symbols
from tools.mod_assets import (
    ASSETS_INI,
    EDITABLE,
    Asset,
    car_names,
    parse_assets_ini,
    plan_build,
    scan,
    texture_size,
)
from tools.mod_ini import CarLine, ModIni, PatchLine, car_keys, read_mod
from tools.unity_ref import library_icalls

ENTRY = "rd_mod_entry"
# Relocations so_util.c applies (R_AARCH64_*).
LOADER_RELOCS = {
    "R_AARCH64_RELATIVE",
    "R_AARCH64_JUMP_SLOT",
    "R_AARCH64_GLOB_DAT",
    "R_AARCH64_ABS64",
}
# Field types patch.ini can write (source/mods/rd_mod_patch.c), as dump.cs spells them.
PATCH_TYPES = {"bool", "int", "uint", "long", "float", "double"}
CARS_INI = "cars.ini"
# 0 and below are refused by the port (rd_mods_patches.c) or make no sense
POSITIVE_KEYS = {"power", "torque", "weight", "final_ratio"} | {f"gear_{n}" for n in range(1, 7)}
CURVE_KEY = "torque_curve"  # rpm:Nm pairs, checked by the port's parser (rd_mod_cars.c)
SYMBOLS_H = ROOT / "source/game/rd_game_symbols.h"
ICALL_IN_STRINGS = re.compile(rb"UnityEngine\.[\w.]*(?:/\w+)*::\w+")


@dataclass
class Report:
    """Problems found in one mod."""

    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)  # what was checked, for the ok line

    def error(self, msg: str) -> None:
        """Adds a problem that fails the check."""
        self.errors.append(msg)

    def warn(self, msg: str) -> None:
        """Adds a problem that does not fail the check."""
        self.warnings.append(msg)


def check_ini(mod: Path, rep: Report) -> ModIni:
    """mod.ini, patch.ini and cars.ini through the port's parser."""
    ini = read_mod(mod)
    for e in ini.errors:
        rep.error(e)
    return ini


def car_components(header: Path = SYMBOLS_H) -> list[str]:
    """The classes a cars.ini key may name without "Class." (RD_CAR_COMPONENTS), in order."""
    m = re.search(
        r"#\s*define\s+RD_CAR_COMPONENTS\s*\{([^}]*)\}", strip_comments(header.read_text())
    )
    if not m:
        raise RuntimeError(f"no RD_CAR_COMPONENTS in {header}")
    return re.findall(r'"([^"]+)"', m.group(1))


def field_type(types: list[TypeDef], t: TypeDef, name: str) -> str | None:
    """The declared type of instance field `name` of t or its bases, or None."""
    cur: TypeDef | None = t
    seen: set[str] = set()
    while cur is not None and cur.full_name not in seen:
        seen.add(cur.full_name)
        for f in cur.fields:
            words = f.decl.split()
            if words and words[-1] == name and "static" not in words and "const" not in words:
                return words[-2] if len(words) >= 2 else None
        base = (cur.base or "").split(",")[0].strip()
        found = find_types(types, base) if base else []
        cur = found[0] if found else None
    return None


def check_patches(patches: list[PatchLine], types: list[TypeDef] | None, rep: Report) -> None:
    """Classes and fields of patch.ini against the dump."""
    if types is None:
        if patches:
            rep.warn("no dump to check patch.ini against (python -m tools.il2cpp_dump)")
        return
    for p in patches:
        found = find_types(types, p.cls.replace("/", "."))
        where = f"patch.ini:{p.line}"
        if not found:
            rep.error(f"{where}: no class {p.cls} in the game")
            continue
        ftype = field_type(types, found[0], p.field)
        if ftype is None:
            rep.error(f"{where}: {p.cls} has no instance field {p.field}")
        elif ftype not in PATCH_TYPES:
            rep.error(
                f"{where}: {p.cls}.{p.field} is {ftype}; patch.ini sets only "
                f"{', '.join(sorted(PATCH_TYPES))}"
            )


def check_cars(
    cars: list[CarLine], types: list[TypeDef] | None, names: list[str] | None, rep: Report
) -> None:
    """cars.ini: car names against the game's cars, keys against the car components' fields."""
    if not cars:
        return
    known = set(names or [])
    for car in sorted({c.car for c in cars if c.car != "*"}):
        if names is None:
            rep.warn(f"no base.apk / OBB to check the car name [{car}] against")
        elif car not in known:
            rep.error(f"{CARS_INI}: no car [{car}] (uv run python -m tools.mod_assets cars)")
    for c in cars:
        if c.key:  # one of cars.ini's own keys: the port knows where it goes
            check_car_key(c, f"{CARS_INI}:{c.line}", rep)
    fields = [c for c in cars if not c.key]
    if types is None:
        if fields:
            rep.warn(f"no dump to check {CARS_INI} against (python -m tools.il2cpp_dump)")
        return
    components = car_components()
    for c in fields:
        where = f"{CARS_INI}:{c.line}"
        classes = [c.cls] if c.cls else components
        typed = []
        for cls in classes:
            found = find_types(types, cls)
            if not found and c.cls:
                rep.error(f"{where}: no class {cls} in the game")
            ftype = field_type(types, found[0], c.field) if found else None
            if ftype is not None:
                typed.append((cls, ftype))
        if not typed:
            if c.cls and find_types(types, c.cls):
                rep.error(f"{where}: {c.cls} has no instance field {c.field}")
            elif not c.cls:
                rep.error(f"{where}: {c.field} is no field of {', '.join(components)}")
        elif len(typed) > 1:
            rep.error(
                f"{where}: {c.field} is in {', '.join(t[0] for t in typed)}; "
                f"write it as Class.{c.field}"
            )
        elif typed[0][1] not in PATCH_TYPES:
            cls, ftype = typed[0]
            rep.error(
                f"{where}: {cls}.{c.field} is {ftype}; {CARS_INI} sets only "
                f"{', '.join(sorted(PATCH_TYPES))}"
            )


def check_car_key(c: CarLine, where: str, rep: Report) -> None:
    """The value of one of cars.ini's own keys: a number (names and curves: the parser's)."""
    key = car_keys().get(c.key)
    if key is None or key.names or c.key == CURVE_KEY:
        return
    try:
        value = float(c.value)
    except ValueError:
        rep.error(f"{where}: {c.key} = {c.value!r} is not a number")
        return
    if value <= 0 and c.key in POSITIVE_KEYS:
        rep.error(f"{where}: {c.key} = {c.value} must be above 0")


def check_so(path: Path, rep: Report, game_icalls: set[str] | None) -> None:
    """mod.so: format, entry point, imports, relocations, icall names."""
    try:
        with path.open("rb") as f:
            elf = ELFFile(f)
            if elf["e_machine"] != "EM_AARCH64" or elf["e_type"] != "ET_DYN":
                rep.error(f"mod.so is {elf['e_machine']} {elf['e_type']}, not an aarch64 .so")
                return
            dynsym = elf.get_section_by_name(".dynsym")
            if not isinstance(dynsym, SymbolTableSection):
                rep.error("mod.so has no .dynsym")
                return
            syms = list(dynsym.iter_symbols())
            if not any(s.name == ENTRY and s["st_shndx"] != "SHN_UNDEF" for s in syms):
                rep.error(f"mod.so does not export {ENTRY} (use RD_MOD_ENTRY)")
            imports = {s.name for s in syms if s.name and s["st_shndx"] == "SHN_UNDEF"}
            for name in sorted(imports - table_symbols(ROOT / "source")):
                rep.error(f"mod.so imports {name}, which the port does not provide")
            for sec in elf.iter_sections():
                if isinstance(sec, RelocationSection):
                    for r in sec.iter_relocations():
                        kind = describe_reloc(r["r_info_type"])
                        if kind not in LOADER_RELOCS:
                            rep.error(f"mod.so has relocation {kind} the loader does not apply")
                            break
        data = path.read_bytes()
    except (OSError, ValueError, ELFError) as e:
        rep.error(f"mod.so: {e}")
        return
    names = sorted({m.group(0).decode() for m in ICALL_IN_STRINGS.finditer(data)})
    for name in names:
        if game_icalls is not None and name not in game_icalls:
            rep.warn(f"icall {name} is not registered in this game (unity_ref status not 'game')")
    rep.notes.append("mod.so")


def describe_reloc(rtype: int) -> str:
    """R_AARCH64_* name of a relocation type number."""
    names = {
        1027: "R_AARCH64_RELATIVE",
        1026: "R_AARCH64_JUMP_SLOT",
        1025: "R_AARCH64_GLOB_DAT",
        257: "R_AARCH64_ABS64",
    }
    return names.get(rtype, f"type {rtype}")


def check_files(mod: Path, rep: Report) -> None:
    """files/apk and files/obb paths against the archives' members."""
    for sub, path in user_archives().items():
        base = mod / "files" / sub
        if not base.is_dir():
            continue
        files = sorted(p.relative_to(base).as_posix() for p in base.rglob("*") if p.is_file())
        rep.notes.append(f"{len(files)} {sub} files")
        if not path.is_file():
            rep.warn(f"files/{sub}: {path.name} not found, members not checked")
            continue
        with zipfile.ZipFile(path) as z:
            members = set(z.namelist())
        for f in files:
            if f not in members:
                rep.warn(f"files/{sub}/{f} is not in {path.name}: it will be ADDED, not replaced")


def check_assets(mod: Path, rep: Report, assets: list[Asset] | None) -> None:
    """assets.ini: names resolve to one editable object, files exist, PNGs have the size."""
    wanted, errors = parse_assets_ini(mod)
    for e in errors:
        rep.error(e)
    if not wanted:
        return
    rep.notes.append(f"{len(wanted)} assets")
    if assets is None:
        rep.warn("no base.apk / OBB in original/: assets.ini names not checked")
        return
    plan = plan_build([mod.name], {mod.name: wanted}, assets)
    for e in plan.errors:
        rep.error(e.removeprefix(f"{mod.name}: "))
    for reps in plan.members.values():
        for r in reps:
            size = texture_size(r.asset)
            if size and Image.open(r.wanted.file).size != size:
                got = "x".join(map(str, Image.open(r.wanted.file).size))
                rep.error(
                    f"{ASSETS_INI}:{r.wanted.line}: {r.wanted.file.name} is {got}, "
                    f"{r.wanted.name} is {size[0]}x{size[1]}"
                )
            raw = mod / "files" / r.asset.archive / r.asset.member
            if raw.is_file():
                rep.warn(
                    f"files/{r.asset.archive}/{r.asset.member} is replaced by the build of "
                    f"{ASSETS_INI}; remove it"
                )


def check_mod(
    mod: Path,
    types: list[TypeDef] | None,
    game_icalls: set[str] | None,
    assets: list[Asset] | None = None,
    cars: list[str] | None = None,
) -> Report:
    """All checks of one mod folder."""
    rep = Report()
    if not (mod / "mod.ini").is_file():
        rep.error("no mod.ini")
        return rep
    ini = check_ini(mod, rep)
    check_patches(ini.patches, types, rep)
    check_cars(ini.cars, types, cars, rep)
    if ini.patches:
        rep.notes.append(f"{len(ini.patches)} patch values")
    if ini.cars:
        rep.notes.append(f"{len(ini.cars)} car values")
    if (mod / "mod.so").is_file():
        check_so(mod / "mod.so", rep, game_icalls)
    check_files(mod, rep)
    check_assets(mod, rep, assets)
    return rep


def load_context() -> tuple[list[TypeDef] | None, set[str] | None]:
    """The dump and the game's registered icall names, when they are available."""
    rel = target_release()
    try:
        types = load_dump(rel.dump)
    except OSError:
        types = None
    lib = rel.lib64 / "libil2cpp.so"
    icalls = None
    if lib.is_file():
        icalls = library_icalls(lib)
    return types, icalls


def load_assets(mods: list[Path]) -> list[Asset] | None:
    """The game's editable objects when a mod has assets.ini and the archives exist."""
    archives = user_archives()
    if not any((m / ASSETS_INI).is_file() for m in mods):
        return []
    if not any(p.is_file() for p in archives.values()):
        return None
    return list(scan(archives, EDITABLE))


def load_cars(mods: list[Path]) -> list[str] | None:
    """The game's car names when a mod has cars.ini and the archives exist."""
    archives = user_archives()
    if not any((m / CARS_INI).is_file() for m in mods):
        return []
    if not any(p.is_file() for p in archives.values()):
        return None
    return car_names(archives)


def main(argv: list[str] | None = None) -> int:
    """Command line: checks each mod folder given."""
    ap = argparse.ArgumentParser(description="Check realdrift_nx mod folders.")
    ap.add_argument("mods", nargs="+", type=Path, help="mod folders (mods/<id>)")
    args = ap.parse_args(argv)
    types, icalls = load_context()
    assets = load_assets(args.mods)
    cars = load_cars(args.mods)
    failed = 0
    for mod in args.mods:
        try:
            rep = check_mod(mod, types, icalls, assets, cars)
        except RuntimeError as e:
            print(f"mod_check: {e}", file=sys.stderr)
            return 1
        for msg in rep.errors:
            print(f"{mod.name}: error: {msg}")
        for w in rep.warnings:
            print(f"{mod.name}: warning: {w}")
        status = "FAILED" if rep.errors else "ok"
        failed += bool(rep.errors)
        print(f"{status} {mod.name}" + (f": {', '.join(rep.notes)}" if rep.notes else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
