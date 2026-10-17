"""config.example.txt must be exactly what the loader writes with default settings."""

import subprocess
from pathlib import Path

import pytest

from tools import hostc

GENERATOR = hostc.ROOT / "tests/c/gen_config_example.c"


def generate(tmp_path: Path) -> str:
    exe = tmp_path / "gen"
    build = hostc.build(GENERATOR, exe)
    assert build.returncode == 0, build.stderr
    out = tmp_path / "config.txt"
    subprocess.run([str(exe), str(out)], check=True)
    return out.read_text()


@pytest.mark.skipif(hostc.CC is None, reason="no host C compiler")
def test_config_example_matches_defaults(tmp_path: Path):
    actual = (hostc.ROOT / "config.example.txt").read_text()
    assert actual == generate(tmp_path), (
        "config.example.txt is stale; regenerate: make config-example"
    )
