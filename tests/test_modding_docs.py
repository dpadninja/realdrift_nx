"""Docs and SDK of the mod system stay small and match the code.

The mod workflow is meant to need only sdk/rd_mod.h, docs/MODDING.md and the mod-dev skill,
so their size is budgeted, and the docs must name every mod.ini key the port reads and every
function of the API table.

MIT (see LICENSE).
"""

import re

import pytest

from tools import mod_new
from tools.game import ROOT

BUDGET = {
    "sdk/rd_mod.h": 200,
    "docs/MODDING.md": 200,
    ".claude/skills/mod-dev/SKILL.md": 50,
    "mods/examples/hello/src/main.c": 40,
}
# A struct member: a function pointer "(*name)(" or a plain field "uint32_t name;".
API_MEMBER = re.compile(
    r"^\s+(?:[\w\s\*]+?\(\*(\w+)\)\(|(?:uint32_t|const char \*)\s*(\w+);)", re.MULTILINE
)


@pytest.mark.parametrize(("path", "lines"), BUDGET.items())
def test_size_budget(path, lines):
    """A session writing a mod reads these files whole; they must stay short."""
    n = len((ROOT / path).read_text().splitlines())
    assert n <= lines, f"{path}: {n} lines, budget {lines}"


def test_doc_lists_every_mod_ini_key():
    """Every key of the port's RD_MOD_INI_KEYS table is in the mod.ini table of MODDING.md."""
    doc = (ROOT / "docs/MODDING.md").read_text()
    for key, _, _ in mod_new.ini_keys():
        assert f"| `{key}` |" in doc, key


def test_doc_names_every_api_member():
    """Each member of rd_mod_api is mentioned in MODDING.md, so a new API function cannot
    stay undocumented there.
    """
    header = (ROOT / "sdk/rd_mod.h").read_text()
    struct = header[header.index("struct rd_mod_api {") : header.index("};")]
    members = {a or b for a, b in API_MEMBER.findall(struct)}
    assert {"log", "inline_hook", "version", "dir"} <= members
    doc = (ROOT / "docs/MODDING.md").read_text()
    missing = sorted(m for m in members if f"`{m}`" not in doc)
    assert not missing, missing
