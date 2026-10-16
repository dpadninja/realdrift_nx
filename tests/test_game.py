"""tools/game.py must read source/rd_game.h exactly as the C compiler does."""
import subprocess
from pathlib import Path

import pytest

from tools import hostc
from tools.game import GAME, read_defines

MACROS = ["RD_GAME_PACKAGE", "RD_GAME_VERSION_NAME", "RD_GAME_UNITY_BUILD_ID", "RD_UNITY_VERSION",
          "RD_GAME_HOME", "RD_GAME_HOME_DIR", "RD_LIB_MAIN", "RD_LIB_UNITY", "RD_LIB_IL2CPP",
          "RD_BASE_APK_NAME", "RD_GAME_OBB_NAME", "RD_CONFIG_FILE", "RD_PREFS_FILE", "RD_LOG_PATH"]


@pytest.mark.skipif(hostc.CC is None, reason="no host C compiler")
def test_python_reader_matches_the_compiler(tmp_path: Path):
    prog = tmp_path / "dump.c"
    prog.write_text('#include <stdio.h>\n#include "rd_game.h"\nint main(void) {\n'
                    + "".join(f'  printf("%s\\n", {m});\n' for m in MACROS)
                    + '  printf("%d\\n", RD_GAME_VERSION_CODE);\n  return 0;\n}\n')
    exe = tmp_path / "dump"
    build = hostc.build(prog, exe)
    assert build.returncode == 0, build.stderr
    compiled = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.splitlines()
    d = read_defines()
    assert compiled == [d[m] for m in MACROS] + [str(d["RD_GAME_VERSION_CODE"])]


def test_reader_handles_comments_concatenation_and_rd_str(tmp_path: Path):
    h = tmp_path / "x.h"
    h.write_text('#define RD_STR_(x) #x\n#define RD_STR(x) RD_STR_(x)\n'
                 '#define A "a" /* c */ "b"   // d\n#define N 7\n'
                 '#define B A "-" RD_STR(N) ".x"\n')
    assert read_defines(h) == {"A": "ab", "N": 7, "B": "ab-7.x"}


def test_game_object():
    assert GAME.libs == ("libmain.so", "libunity.so", "libil2cpp.so")
    assert GAME.home == "sdmc:" + GAME.home_dir
