"""tools/release.py: the released stage_sd.py must work alone, with plain Python."""

import subprocess
import sys
from pathlib import Path

from tests.test_stage_sd import OBB, write_apk, write_obb
from tools.game import GAME
from tools.release import build, standalone_stage_sd


def test_identity_block_is_replaced_by_literals():
    src = standalone_stage_sd()
    assert "tools.game" not in src and "sys.path.insert" not in src
    assert repr(GAME.build_id) in src and repr(GAME.package) in src


def test_released_script_runs_standalone(tmp_path: Path):
    nro = tmp_path / "realdrift_nx.nro"
    nro.write_bytes(b"NRO")
    rel = tmp_path / "release"
    build(nro, rel)
    assert sorted(p.name for p in rel.iterdir()) == [
        "config.example.txt",
        "realdrift_nx.nro",
        "stage_sd.py",
    ]

    work = tmp_path / "user"  # elsewhere, as a user would run it
    work.mkdir()
    write_apk(work / "base.apk")
    write_obb(work / OBB)
    # -I: isolated mode -- no repo on sys.path, no site-packages
    r = subprocess.run(
        [sys.executable, "-I", str(rel / "stage_sd.py"), "base.apk", OBB],
        cwd=work,
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stderr
    out = work / "switch" / Path(GAME.home_dir).name
    assert sorted(p.name for p in out.iterdir()) == sorted(
        [*GAME.libs, GAME.base_apk, OBB, "realdrift_nx.nro"]
    )
