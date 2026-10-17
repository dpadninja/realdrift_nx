"""Check the documentation rules of docs/CODING_STYLE.md and docs/PYTHON_STYLE.md.

1. Every file of code starts with a header comment (C) or module docstring / comment (Python,
   Makefile, YAML) that states its license ("MIT", "license" or "Copyright").
2. Every function declared in a header under source/ has a comment right above it. A comment
   may cover a group: declarations that follow it on consecutive lines (no blank line) count
   as documented.

    python -m tools.doc_check            # whole repository
    python -m tools.doc_check source/game/rd_smoke.h

Exit code 1 and one line per problem if anything is missing. MIT (see LICENSE).
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CODE_SUFFIXES = {".c", ".h", ".py", ".yml", ".yaml"}
CODE_NAMES = {"Makefile"}
HEADER_LINES = 25  # the license must be stated within the file's first lines
LICENSE = re.compile(r"\bMIT\b|licen[cs]e|Copyright", re.IGNORECASE)
# A file-scope function declaration or definition: "type name(" with no leading keyword that
# makes it something else; continuation lines are joined before matching.
FUNC = re.compile(
    r"^(?!\s)(?!typedef\b|return\b|#|struct\s+\w+\s*\{|enum\b|union\b)"
    r"[A-Za-z_][\w\s\*]*?[\s\*]\**(?P<name>[A-Za-z_]\w*)\s*\("
)

# Declarations that may sit inside a comment-led group without ending it.
DECL_IN_GROUP = re.compile(r"^(extern\s[^({]*;|struct\s+\w+\s*;)$")


@dataclass(frozen=True)
class Problem:
    """One documentation problem."""

    path: Path
    line: int
    message: str

    def __str__(self) -> str:
        return f"{self.path}:{self.line}: {self.message}"


def is_code(path: Path) -> bool:
    """Whether the license/header rule applies to `path`."""
    return path.suffix in CODE_SUFFIXES or path.name in CODE_NAMES


def check_license(path: Path, text: str) -> list[Problem]:
    """A problem if the first lines of `text` do not state a license."""
    head = "\n".join(text.splitlines()[:HEADER_LINES])
    if LICENSE.search(head):
        return []
    return [Problem(path, 1, "no license in the header comment")]


def _logical_lines(text: str) -> list[tuple[int, str]]:
    """(line number, text) with declarations spread over several lines joined into one."""
    out: list[tuple[int, str]] = []
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        start, cur = i, lines[i]
        # join an unfinished declaration: open parenthesis without its closing one
        while cur.count("(") > cur.count(")") and i + 1 < len(lines):
            i += 1
            cur += " " + lines[i].strip()
        out.append((start + 1, cur))
        i += 1
    return out


def check_header_functions(path: Path, text: str) -> list[Problem]:
    """A problem for each function declared in the header without a comment above it."""
    problems: list[Problem] = []
    documented = False  # inside a comment-led block of declarations
    in_comment = False
    depth = 0  # brace depth: only file-scope declarations count
    for lineno, line in _logical_lines(text):
        s = line.strip()
        if in_comment:
            if "*/" in s:
                in_comment = False
                documented = True
            continue
        if s.startswith("/*"):
            if "*/" not in s:
                in_comment = True
            else:
                documented = True
            continue
        if s.startswith("//"):
            documented = True
            continue
        if not s:
            documented = False
            continue
        if depth == 0 and FUNC.match(line) and not s.startswith(("#", 'extern "C"')):
            if not documented:
                name = FUNC.match(line)
                problems.append(
                    Problem(path, lineno, f"{name.group('name') if name else '?'}: no comment")
                )
        elif depth == 0 and not s.startswith("#") and not DECL_IN_GROUP.match(s):
            documented = False  # a struct, typedef or definition ends the documented block
        depth += line.count("{") - line.count("}")
    return problems


def check_file(path: Path) -> list[Problem]:
    """All documentation problems of one file."""
    text = path.read_text(errors="replace")
    problems = check_license(path, text) if is_code(path) else []
    if path.suffix == ".h" and "source" in path.parts:
        problems += check_header_functions(path, text)
    return problems


def tracked_files(root: Path = ROOT) -> list[Path]:
    """The repository's tracked files of code (git ls-files), relative to `root`."""
    out = subprocess.run(
        ["git", "ls-files"], cwd=root, capture_output=True, text=True, check=True
    ).stdout
    return [Path(p) for p in out.splitlines() if is_code(Path(p))]


def main(argv: list[str] | None = None) -> int:
    """Command line; returns 1 if any problem was found."""
    ap = argparse.ArgumentParser(description="Check file headers, licenses and API comments.")
    ap.add_argument("paths", nargs="*", type=Path, help="files to check (default: repository)")
    args = ap.parse_args(argv)
    paths = args.paths or [ROOT / p for p in tracked_files()]
    problems = [p for path in paths for p in check_file(path)]
    for p in problems:
        print(p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
