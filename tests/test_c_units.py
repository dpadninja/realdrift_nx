"""Build and run the host-side C unit tests in tests/c/.

See tools/hostc.py for the /* sources: */ and /* libs: */ header lines.

The binary gets the test's tmp dir as argv[1]; a sample zip is written there as test.zip.

MIT (see LICENSE).
"""

import subprocess
import zipfile
from pathlib import Path

import pytest

from tools import hostc

C_TESTS = sorted((hostc.ROOT / "tests/c").glob("test_*.c"))


def write_sample_zip(path: Path) -> None:
    """Writes the zip test_rd_zip.c reads: one stored entry, one deflated, one deflated in a
    subdirectory (the APK layout).
    """
    with zipfile.ZipFile(path, "w") as z:
        z.writestr(zipfile.ZipInfo("stored.txt"), "hello stored")
        z.writestr("deflated.txt", "hello deflated " * 100, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("dir/boot.config", "gfx-enable-gfx-jobs=\n", compress_type=zipfile.ZIP_DEFLATED)


@pytest.mark.skipif(hostc.CC is None, reason="no host C compiler")
@pytest.mark.parametrize("test", C_TESTS, ids=[t.stem for t in C_TESTS])
def test_c_unit(test: Path, tmp_path: Path):
    """Each tests/c/test_*.c builds with ASan/UBSan and -Werror and exits 0; a sanitizer report
    or a failed CHECK fails it with the program's stderr.
    """
    coverage = hostc.coverage_enabled()
    exe = (hostc.COVERAGE_DIR if coverage else tmp_path) / test.stem
    build = hostc.build(test, exe, sanitize=True, coverage=coverage)
    assert build.returncode == 0, build.stderr
    write_sample_zip(tmp_path / "test.zip")
    run = subprocess.run([str(exe), str(tmp_path)], capture_output=True, text=True)
    assert run.returncode == 0, run.stderr
