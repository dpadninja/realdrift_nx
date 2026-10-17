"""tools/import_coverage.py: imports of the game libraries the shim tables do not provide.

MIT (see LICENSE).
"""

from pathlib import Path

import pytest

from tools.game import GAME, RELEASES
from tools.import_coverage import missing, table_symbols


def test_table_symbols_parses_initializers(tmp_path: Path):
    (tmp_path / "a.c").write_text('{ "open", (uintptr_t)&open_fake }, { "read",(uintptr_t)&r },')
    (tmp_path / "b.inc").write_text('  { "slCreateEngine", (uintptr_t)&x },\n')
    (tmp_path / "c.h").write_text('printf("{ \\"not a table\\" }"); { "9bad", 0 },')
    assert table_symbols(tmp_path) == {"open", "read", "slCreateEngine"}


@pytest.mark.parametrize("release", sorted(RELEASES))
def test_loader_provides_every_import_of_real_drift_libs(release: str):
    libs = [RELEASES[release]["lib64"] / n for n in GAME.libs]
    if not all(p.exists() for p in libs):
        pytest.skip(f"extract the {release} arm64 libs to {RELEASES[release]['lib64']} first")
    assert missing(libs) == {p.name: [] for p in libs}
