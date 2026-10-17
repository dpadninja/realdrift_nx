"""Build and run the host-side C unit tests in tests/c/ (see tools/hostc.py for the
/* sources: */ and /* libs: */ header lines).

The binary gets the test's tmp dir as argv[1]; a sample zip is written there as test.zip.
"""

import subprocess
import zipfile
from pathlib import Path

import pytest

from tools import hostc

C_TESTS = sorted((hostc.ROOT / "tests/c").glob("test_*.c"))


def write_sample_zip(path: Path) -> None:
    with zipfile.ZipFile(path, "w") as z:
        z.writestr(zipfile.ZipInfo("stored.txt"), "hello stored")
        z.writestr("deflated.txt", "hello deflated " * 100, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("dir/boot.config", "gfx-enable-gfx-jobs=\n", compress_type=zipfile.ZIP_DEFLATED)


@pytest.mark.skipif(hostc.CC is None, reason="no host C compiler")
@pytest.mark.parametrize("test", C_TESTS, ids=[t.stem for t in C_TESTS])
def test_c_unit(test: Path, tmp_path: Path):
    exe = tmp_path / test.stem
    build = hostc.build(test, exe, sanitize=True)
    assert build.returncode == 0, build.stderr
    write_sample_zip(tmp_path / "test.zip")
    run = subprocess.run([str(exe), str(tmp_path)], capture_output=True, text=True)
    assert run.returncode == 0, run.stderr
