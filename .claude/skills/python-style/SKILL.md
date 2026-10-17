---
name: python-style
description: Rules for writing or editing Python in realdrift_nx (tools/, tests/) - PEP 8 via ruff, Google docstrings, type hints checked by mypy --strict, argparse main() returning an exit code, stdlib-only Python 3.8 for stage_sd.py. Use before adding or changing any Python code in this repository.
---

# Writing Python in realdrift_nx

Full rules: `docs/PYTHON_STYLE.md`. Checklist:

- Module docstring: summary line, blank line, what it does, a command-line example, and
  `MIT (see LICENSE).`
- `main(argv: list[str] | None = None) -> int` with `argparse`; `sys.exit(main())` under
  `if __name__ == "__main__":`. User errors: one line on stderr and exit code 1, no traceback.
- Type hints on every signature (`list[str]`, `X | None`); `mypy --strict` passes.
- Google-style docstrings for public functions/classes (`Args:`, `Returns:`, `Raises:` when useful).
- `pathlib.Path` and `Path.open`, f-strings, `subprocess.run([...], check=...)` without `shell=True`.
- Game identity comes from `tools/game.py` (which reads `rd_game.h`); never a literal package
  name, versionCode or file name.
- `tools/stage_sd.py` ships to players: standard library only, Python 3.8 compatible, keep the
  `# >>> game identity` block for `tools/release.py`.
- Tests: pytest in `tests/`, `tmp_path`, `parametrize`; skip with a reason when `analysis/` files
  are missing; a docstring on every test, fixture and helper: the case and why it matters.
- Before finishing: `make lint`, `make test`, `make coverage`.
