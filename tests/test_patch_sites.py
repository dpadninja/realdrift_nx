"""tools/find_patch_sites: the loader's pattern finders on the real binaries of both releases.

The expected addresses were derived by hand from llvm-objdump (analysis/research/08-device-runs.md
run 02 / run 08 / run 12 for 5.0.7; analysis/research/10-patch-sites.md for 5.0.8).

MIT (see LICENSE).
"""

import shutil
import subprocess
from pathlib import Path

import pytest

from tools import hostc
from tools.game import RELEASES

EXPECTED = {
    "5.0.7": {
        "choreo_gate": 0x30F834,
        "choreo_skip": 0x30F89C,
        "blit.get_settings": 0x3EB124,
        "blit.get_manager": 0x480220,
        "blit.field": 0x240,
        "gc.sig_suspend": 0x1F16A98,
        "gc.sig_restart": 0x1F16A9C,
        "gc.retry_signals": 0x1F56908,
        "gc.suspend_ack_sem": 0x1F6AC20,
        "gc.stop_count": 0x1F568F8,
        "gc.threads": 0x1F56920,
    },
    "5.0.8": {
        "choreo_gate": 0x3136DC,
        "choreo_skip": 0x313744,
        "blit.get_settings": 0x3ECDE8,
        "blit.get_manager": 0x480EE4,
        "blit.field": 0x240,
        "gc.sig_suspend": 0x1F1C730,
        "gc.sig_restart": 0x1F1C734,
        "gc.retry_signals": 0x1F5C548,
        "gc.suspend_ack_sem": 0x1F707B0,
        "gc.stop_count": 0x1F5C538,
        "gc.threads": 0x1F5C560,
    },
}


@pytest.fixture(scope="module")
def tool(tmp_path_factory) -> Path:
    """find_patch_sites built once for the module (skips without a host C compiler)."""
    if hostc.CC is None:
        pytest.skip("no host C compiler")
    exe = tmp_path_factory.mktemp("fps") / "find_patch_sites"
    build = hostc.build(hostc.ROOT / "tools/find_patch_sites.c", exe)
    assert build.returncode == 0, build.stderr
    return exe


def run(tool: Path, *libs: Path) -> dict[str, int]:
    """The tool's key=0x... output as a dict; the tool must succeed."""
    out = subprocess.run([str(tool), *map(str, libs)], capture_output=True, text=True)
    assert out.returncode == 0, out.stdout + out.stderr
    kv = dict(line.split("=", 1) for line in out.stdout.splitlines())
    return {k: int(v, 16) for k, v in kv.items() if v.startswith("0x")}


def libdir(release: str) -> Path:
    """The extracted libraries of a release, or a skip with the command to extract them."""
    d = RELEASES[release].lib64
    if not (d / "libunity.so").exists():
        pytest.skip(f"extract the {release} arm64 libs to {d.relative_to(hostc.ROOT)} first")
    return d


@pytest.mark.parametrize("release", sorted(EXPECTED))
def test_finds_all_sites(tool: Path, release: str):
    """Every pattern finds exactly the hand-verified address in both releases: the loader patches
    these sites, a wrong one would corrupt the engine.
    """
    d = libdir(release)
    assert run(tool, d / "libunity.so", d / "libil2cpp.so") == EXPECTED[release]


def test_wrong_library_fails_cleanly(tool: Path, tmp_path: Path):
    """In a library without the sites nothing is reported as found and the tool fails, instead of
    returning a bogus address.
    """
    d = libdir("5.0.8")
    fake = tmp_path / "libunity.so"  # libmain renamed: nothing may be "found" in it
    shutil.copy(d / "libmain.so", fake)
    out = subprocess.run([str(tool), str(fake)], capture_output=True, text=True)
    assert out.returncode != 0 and "choreo_gate.result=0" in out.stdout
