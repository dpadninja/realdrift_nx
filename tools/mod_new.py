"""Start a new mod from the hello example (mods/examples/hello).

    uv run python -m tools.mod_new mymod                 # -> mods/mymod/
    uv run python -m tools.mod_new mymod --dir ../my-mods --no-code

Writes mod.ini (the keys of the port's RD_MOD_INI_KEYS table, source/mods/rd_mod_ini.c, with
their help text) and, unless --no-code, src/main.c copied from the hello example. Then:
edit src/main.c, `python -m tools.mod_build`, `python -m tools.mod_check`.
MIT (see LICENSE).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from tools.game import ROOT

INI_SOURCE = ROOT / "source" / "mods" / "rd_mod_ini.c"
TEMPLATE = ROOT / "mods" / "examples" / "hello" / "src" / "main.c"
API_HEADER = ROOT / "sdk" / "rd_mod.h"
KEY_ROW = re.compile(r'\{\s*"(?P<key>\w+)",\s*(?P<req>[01]),\s*"(?P<help>[^"]*)"\s*\}')
API_VERSION = re.compile(r"#define RD_MOD_API_VERSION (\d+)")
ID = re.compile(r"^[a-z0-9_-]{1,32}$")


def ini_keys(source: Path = INI_SOURCE) -> list[tuple[str, bool, str]]:
    """(key, required, help) rows of RD_MOD_INI_KEYS, in table order."""
    text = source.read_text()
    table = text[
        text.index("RD_MOD_INI_KEYS[] = {") : text.index("};", text.index("RD_MOD_INI_KEYS[]"))
    ]
    return [(m["key"], m["req"] == "1", m["help"]) for m in KEY_ROW.finditer(table)]


def api_version(header: Path = API_HEADER) -> int:
    """RD_MOD_API_VERSION of sdk/rd_mod.h."""
    m = API_VERSION.search(header.read_text())
    if not m:
        raise ValueError(f"{header}: no RD_MOD_API_VERSION")
    return int(m.group(1))


def mod_ini(mod_id: str, keys: list[tuple[str, bool, str]], api: int) -> str:
    """mod.ini text for a new mod: every known key with its help as a comment."""
    values = {
        "id": mod_id,
        "name": mod_id,
        "version": "0.1",
        "api": str(api),
        "enabled": "1",
        "priority": "0",
    }
    lines = ["# mod.ini -- see docs/MODDING.md."]
    for key, required, help_text in keys:
        lines.append(f"# {key}{' (required)' if required else ''}: {help_text}")
        lines.append(f"{key} = {values.get(key, '')}")
    return "\n".join(lines) + "\n"


def create(mod_id: str, parent: Path, code: bool) -> Path:
    """Creates parent/<mod_id>/ and returns it.

    Raises:
        ValueError: the id is invalid or the folder exists.
    """
    if not ID.match(mod_id):
        raise ValueError(f"id {mod_id!r}: use 1-32 of a-z 0-9 _ -")
    mod = parent / mod_id
    if mod.exists():
        raise ValueError(f"{mod} already exists")
    mod.mkdir(parents=True)
    (mod / "mod.ini").write_text(mod_ini(mod_id, ini_keys(), api_version()))
    if code:
        (mod / "src").mkdir()
        main_c = TEMPLATE.read_text().replace("hello --", f"{mod_id} --", 1)
        (mod / "src" / "main.c").write_text(main_c)
    return mod


def main(argv: list[str] | None = None) -> int:
    """Command line: creates one mod folder."""
    ap = argparse.ArgumentParser(description="Create a new mod folder from the hello example.")
    ap.add_argument("id", help="mod id = folder name")
    ap.add_argument("--dir", type=Path, default=ROOT / "mods", help="parent folder (mods/)")
    ap.add_argument("--no-code", action="store_true", help="data-only mod (no src/main.c)")
    args = ap.parse_args(argv)
    try:
        mod = create(args.id, args.dir, not args.no_code)
    except (ValueError, OSError) as e:
        print(f"mod_new: {e}", file=sys.stderr)
        return 1
    print(mod)
    return 0


if __name__ == "__main__":
    sys.exit(main())
