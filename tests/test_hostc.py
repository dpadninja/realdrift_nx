"""tools/hostc.py: building host-side C programs from their /* sources: */ header.

MIT (see LICENSE).
"""

import subprocess
from pathlib import Path

import pytest

from tools import hostc

pytestmark = pytest.mark.skipif(hostc.CC is None, reason="no host C compiler")


def test_sources_and_libs_headers(tmp_path: Path):
    prog = tmp_path / "p.c"
    prog.write_text(
        "/* sources: source/game/rd_smoke.c */\n/* libs: -lm */\nint main(void){return 0;}\n"
    )
    assert hostc.linked_sources(prog) == [hostc.ROOT / "source/game/rd_smoke.c"]
    assert hostc.extra_libs(prog) == ["-lm"]


def test_a_program_without_headers_links_nothing(tmp_path: Path):
    prog = tmp_path / "p.c"
    prog.write_text("int main(void){return 0;}\n")
    assert hostc.linked_sources(prog) == [] and hostc.extra_libs(prog) == []


def test_command_line_builds_a_program(tmp_path: Path):
    prog = tmp_path / "ok.c"
    prog.write_text(
        '/* sources: source/game/rd_smoke.c */\n#include "rd_smoke.h"\n'
        "int main(void){return rd_smoke_active(100, 100);}\n"
    )
    exe = tmp_path / "ok"
    assert hostc.main([str(prog), str(exe)]) == 0
    assert subprocess.run([str(exe)], check=False).returncode == 0


def test_command_line_reports_a_compile_error(tmp_path: Path, capsys):
    prog = tmp_path / "bad.c"
    prog.write_text("int main(void){ return undefined_name; }\n")
    assert hostc.main([str(prog), str(tmp_path / "bad")]) != 0
    assert "undefined_name" in capsys.readouterr().err


def test_coverage_switch(monkeypatch):
    monkeypatch.setenv("RD_COVERAGE", "1")
    assert hostc.coverage_enabled()
    monkeypatch.setenv("RD_COVERAGE", "0")
    assert not hostc.coverage_enabled()
