# Python style

The Python code is the developer tooling in `tools/` and the tests in `tests/`. It runs on
Python 3.13 through uv (`uv run ...`), except `tools/stage_sd.py`, which ships to players.

We follow the community standards and let tools check them:

| Practice | Reference | Checked by |
|---|---|---|
| Code layout and naming | [PEP 8](https://peps.python.org/pep-0008/) | `ruff format`, `ruff check` |
| Docstrings | [PEP 257](https://peps.python.org/pep-0257/), Google style | `ruff check` (pydocstyle rules) |
| Type hints | [PEP 484](https://peps.python.org/pep-0484/), [PEP 604](https://peps.python.org/pep-0604/) | `mypy --strict` |
| Modern syntax | pyupgrade rules | `ruff check` (`UP`) |
| Imports sorted | isort rules | `ruff check` (`I`) |

`make lint` runs `ruff check`, `ruff format --check` and `mypy`; CI runs it too. The settings are
in `pyproject.toml`.

## Files

- Every file starts with a module docstring: what the tool does, how to run it (a command line
  example), and the license line `MIT (see LICENSE).` Forked code keeps its upstream notice.
- A tool is a module with a `main(argv: list[str] | None = None) -> int` that parses arguments
  with `argparse`, and ends with
  ```python
  if __name__ == "__main__":
      sys.exit(main())
  ```
  so tests can call `main([...])` and check the exit code.
- Shared facts come from one place: game identity from `source/game/rd_game.h` via
  `tools/game.py`; never repeat a package name, version or file name as a literal.

## Code

- 4-space indent, 100-column lines (`ruff format`), double quotes.
- Type hints on every function signature and on module-level constants whose type is not obvious;
  `list[str]`, `dict[str, int]`, `X | None` (PEP 585/604). `mypy --strict` passes.
- Google-style docstrings on public functions and classes (`Args:`, `Returns:`, `Raises:` when
  they add information); one line is enough for an obvious helper. Comment the *why*.
- `pathlib.Path` for paths, f-strings for formatting, `subprocess.run(..., check=True)` with an
  argument list (never `shell=True`), context managers for files.
- Raise specific exceptions with a message that names the file or value at fault; no bare
  `except:`, no swallowing errors. A tool reports user errors as one line on stderr and a non-zero
  exit code, not a traceback.
- Small pure functions that tests can call directly; I/O at the edges.
- `@dataclass(frozen=True)` for plain records; constants in `UPPER_CASE` at module level.

## stage_sd.py (runs on players' PCs)

- Standard library only, Python 3.8 compatible: `from __future__ import annotations`, no `match`,
  no 3.9+ library calls. Its ruff target version is `py38` (`pyproject.toml` per-file setting).
- `tools/release.py` inlines the game identity into it; keep the marked block intact.

## Tests

- pytest, one behaviour per test, descriptive names (`test_rejects_lite_apk`).
- `tmp_path` for files, `pytest.mark.parametrize` for tables of cases.
- Tests that need files from `analysis/` skip when they are missing (`pytest.skip` with the reason).
- Every tool has tests; `make coverage` reports line coverage of `tools/` and CI fails below the
  threshold.
