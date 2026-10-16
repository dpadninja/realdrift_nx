"""The supported game build and its files, read from source/rd_game.h -- the one place the
loader defines them. Tools and tests import these values instead of repeating them.

    from tools.game import GAME
    GAME.package, GAME.version_code, GAME.build_id, GAME.obb_name, GAME.libs, ...

rd_game.h is evaluated with a small macro reader that understands exactly what the header
uses: string literals (adjacent ones concatenated), integers, references to earlier macros
and RD_STR(x). tests/test_game.py checks the result against what the C compiler sees.
"""
import re
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GAME_H = ROOT / "source/rd_game.h"

# Analysis artefacts per release (gitignored; extracted from the user's own APKs).
ANALYSIS = ROOT / "analysis"
RELEASES = {
    "5.0.7": {"version_code": 74, "build_id": "18435530-159e-4d08-beba-0a1c12258416",
              "lib64": ANALYSIS / "lib64", "dump": ANALYSIS / "dump507"},
    "5.0.8": {"version_code": 76, "build_id": "a716b65c-2cb0-47ca-ab2b-f42b256c4e69",
              "lib64": ANALYSIS / "lib64_508", "dump": None},
}
# Real Drift Lite: a different package and build, not supported (stage_sd.py refuses it).
LITE_PACKAGE = "com.realdriftportnew.sipon"
LITE_BUILD_ID = "f450d0cc-c473-4332-a598-31d85e35eaf0"

_TOKEN = re.compile(r'"(?:[^"\\]|\\.)*"|\w+\s*\(\s*\w+\s*\)|\w+')


def _strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def read_defines(path: Path = GAME_H) -> dict[str, str | int]:
    """Object-like #defines of `path`, evaluated (strings joined, ints parsed)."""
    lines = _strip_comments(path.read_text()).replace("\\\n", " ").splitlines()
    raw: dict[str, str] = {}
    for line in lines:
        m = re.match(r"\s*#\s*define\s+(\w+)(\(\w*\))?\s*(.*)", line)
        if m and not m.group(2) and m.group(3).strip():
            raw[m.group(1)] = m.group(3).strip()

    def value(name: str) -> str | int:
        body = raw[name]
        if re.fullmatch(r"\d+", body):
            return int(body)
        parts = []
        for tok in _TOKEN.findall(body):
            if tok.startswith('"'):
                parts.append(bytes(tok[1:-1], "utf-8").decode("unicode_escape"))
            elif m := re.fullmatch(r"RD_STR\s*\(\s*(\w+)\s*\)", tok):
                parts.append(str(value(m.group(1))))
            elif tok in raw:
                parts.append(str(value(tok)))
            else:
                raise ValueError(f"{path.name}: cannot evaluate {name} ({body})")
        return "".join(parts)

    return {name: value(name) for name in raw if name != "RD_GAME_H"}


@dataclass(frozen=True)
class Game:
    package: str
    version_name: str
    version_code: int
    build_id: str
    unity_version: str
    home: str            # "sdmc:/switch/realdrift_nx"
    home_dir: str        # "/switch/realdrift_nx" (SD-relative, for staging)
    libs: tuple[str, ...]
    base_apk: str
    obb_name: str
    config_file: str


def load(path: Path = GAME_H) -> Game:
    d = read_defines(path)
    return Game(
        package=d["RD_GAME_PACKAGE"], version_name=d["RD_GAME_VERSION_NAME"],
        version_code=d["RD_GAME_VERSION_CODE"], build_id=d["RD_GAME_UNITY_BUILD_ID"],
        unity_version=d["RD_UNITY_VERSION"], home=d["RD_GAME_HOME"],
        home_dir=d["RD_GAME_HOME_DIR"],
        libs=(d["RD_LIB_MAIN"], d["RD_LIB_UNITY"], d["RD_LIB_IL2CPP"]),
        base_apk=d["RD_BASE_APK_NAME"], obb_name=d["RD_GAME_OBB_NAME"],
        config_file=d["RD_CONFIG_FILE"],
    )


GAME = load()


def obb_name(version_code: int, package: str = GAME.package) -> str:
    """main.<versionCode>.<package>.obb -- the name Android (and the loader) use."""
    return f"main.{version_code}.{package}.obb"
