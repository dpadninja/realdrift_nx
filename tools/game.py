"""The supported game build and its files, read from source/game/rd_game.h.

rd_game.h is the one place the loader defines them; tools and tests import these values
instead of repeating them.

    from tools.game import GAME
    GAME.package, GAME.version_code, GAME.build_id, GAME.obb_name, GAME.libs, ...

rd_game.h is evaluated with a small macro reader that understands exactly what the header
uses: string literals (adjacent ones concatenated), integers, references to earlier macros
and RD_STR(x). tests/test_game.py checks the result against what the C compiler sees.

MIT (see LICENSE).
"""

import re
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GAME_H = ROOT / "source/game/rd_game.h"

# Analysis artefacts per release (gitignored; extracted from the user's own APKs).
ANALYSIS = ROOT / "analysis"


@dataclass(frozen=True)
class Release:
    """A game release we have analysis files for, and where they live."""

    name: str  # versionName, "5.0.8"
    version_code: int
    build_id: str  # unity.build-id of its libunity
    apk: Path  # the full APK
    lib64: Path  # its lib/arm64-v8a/*.so
    data: Path  # its assets/ (global-metadata.dat under bin/Data/Managed/Metadata/)
    dump: Path  # Il2CppDumper output (tools/il2cpp_dump.py)

    @property
    def metadata(self) -> Path:
        """The release's global-metadata.dat."""
        return self.data / METADATA_IN_APK


METADATA_IN_APK = "assets/bin/Data/Managed/Metadata/global-metadata.dat"
RELEASES = {
    r.name: r
    for r in (
        Release(
            "5.0.7",
            74,
            "18435530-159e-4d08-beba-0a1c12258416",
            apk=ANALYSIS / "realdrift-5.0.7-build74.apk",
            lib64=ANALYSIS / "lib64",
            data=ANALYSIS / "data507",
            dump=ANALYSIS / "dump507",
        ),
        Release(
            "5.0.8",
            76,
            "a716b65c-2cb0-47ca-ab2b-f42b256c4e69",
            apk=ROOT / "original/base.apk",
            lib64=ANALYSIS / "lib64_508",
            data=ANALYSIS / "data508",
            dump=ANALYSIS / "dump508",
        ),
    )
}
# Real Drift Lite: a different package and build, not supported (stage_sd.py refuses it).
LITE_PACKAGE = "com.realdriftportnew.sipon"
LITE_BUILD_ID = "f450d0cc-c473-4332-a598-31d85e35eaf0"

_TOKEN = re.compile(r'"(?:[^"\\]|\\.)*"|\w+\s*\(\s*\w+\s*\)|\w+')


def strip_comments(text: str) -> str:
    """C source without /* */ and // comments."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def read_defines(path: Path = GAME_H) -> dict[str, str | int]:
    """Object-like #defines of `path`, evaluated (strings joined, ints parsed)."""
    lines = strip_comments(path.read_text()).replace("\\\n", " ").splitlines()
    raw: dict[str, str] = {}
    for line in lines:
        m = re.match(r"\s*#\s*define\s+(\w+)(\(\w*\))?\s*(.*)", line)
        if m and not m.group(2) and m.group(3).strip():
            raw[m.group(1)] = m.group(3).strip()

    def value(name: str) -> str | int:
        body = raw[name]
        if re.fullmatch(r"\d+", body):
            return int(body)
        parts: list[str] = []
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
    """Identity and file names of the supported game build (the RD_* macros of rd_game.h)."""

    package: str
    version_name: str
    version_code: int
    build_id: str
    unity_version: str
    home: str  # "sdmc:/switch/realdrift_nx"
    home_dir: str  # "/switch/realdrift_nx" (SD-relative, for staging)
    libs: tuple[str, ...]
    base_apk: str
    obb_name: str
    config_file: str


def load(path: Path = GAME_H) -> Game:
    """Reads rd_game.h at `path` into a Game.

    Raises:
        KeyError: a macro the Game needs is missing.
        TypeError: a macro has the wrong kind of value (string vs integer).
    """
    d = read_defines(path)

    def s(name: str) -> str:
        v = d[name]
        if not isinstance(v, str):
            raise TypeError(f"{path.name}: {name} should be a string, is {v!r}")
        return v

    def i(name: str) -> int:
        v = d[name]
        if not isinstance(v, int):
            raise TypeError(f"{path.name}: {name} should be an integer, is {v!r}")
        return v

    return Game(
        package=s("RD_GAME_PACKAGE"),
        version_name=s("RD_GAME_VERSION_NAME"),
        version_code=i("RD_GAME_VERSION_CODE"),
        build_id=s("RD_GAME_UNITY_BUILD_ID"),
        unity_version=s("RD_UNITY_VERSION"),
        home=s("RD_GAME_HOME"),
        home_dir=s("RD_GAME_HOME_DIR"),
        libs=(s("RD_LIB_MAIN"), s("RD_LIB_UNITY"), s("RD_LIB_IL2CPP")),
        base_apk=s("RD_BASE_APK_NAME"),
        obb_name=s("RD_GAME_OBB_NAME"),
        config_file=s("RD_CONFIG_FILE"),
    )


GAME = load()


def obb_name(version_code: int, package: str = GAME.package) -> str:
    """main.<versionCode>.<package>.obb -- the name Android (and the loader) use."""
    return f"main.{version_code}.{package}.obb"


def target_release() -> Release:
    """The release the loader targets (the one whose build-id rd_game.h names)."""
    for r in RELEASES.values():
        if r.build_id == GAME.build_id:
            return r
    raise LookupError(f"no entry in RELEASES for build-id {GAME.build_id}")


def user_archives() -> dict[str, Path]:
    """The user's game archives by the name mods use for them (files/apk, files/obb)."""
    return {"apk": target_release().apk, "obb": ROOT / "original" / GAME.obb_name}
