"""tools/il2cpp_dump.py: running Il2CppDumper on a release (docker and the download are faked).

MIT (see LICENSE).
"""

import hashlib
import io
import json
import subprocess
import zipfile

import pytest

from tools import il2cpp_dump
from tools.game import ANALYSIS, METADATA_IN_APK, RELEASES, Release


def zip_bytes(files):
    """A zip archive in memory holding `files` (name -> bytes)."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        for name, data in files.items():
            z.writestr(name, data)
    return buf.getvalue()


def test_patch_config_disables_the_key_prompt():
    """Il2CppDumper waits for a key press by default, which would hang the docker run."""
    cfg = json.loads(il2cpp_dump.patch_config('{"RequireAnyKey": true, "DumpField": true}'))
    assert cfg == {"RequireAnyKey": False, "DumpField": True}


class FakeResponse(io.BytesIO):
    """A urlopen() result."""

    def __enter__(self):
        """Context manager like the real response."""
        return self

    def __exit__(self, *exc):
        """Nothing to close."""


def test_install_checks_the_pinned_checksum(tmp_path, monkeypatch):
    """The download is unpacked and its config patched only when the sha256 matches the pin, so a
    replaced release asset is never run.
    """
    data = zip_bytes({"Il2CppDumper.dll": b"dll", "config.json": '{"RequireAnyKey": true}'})
    monkeypatch.setattr(il2cpp_dump.urllib.request, "urlopen", lambda url: FakeResponse(data))
    with pytest.raises(RuntimeError, match="sha256"):
        il2cpp_dump.install_dumper(tmp_path / "bad", "https://x", "0" * 64)
    assert not (tmp_path / "bad").exists()
    dest = tmp_path / "ok"
    il2cpp_dump.install_dumper(dest, "https://x", hashlib.sha256(data).hexdigest())
    assert (dest / "Il2CppDumper.dll").read_bytes() == b"dll"
    assert json.loads((dest / "config.json").read_text())["RequireAnyKey"] is False
    il2cpp_dump.install_dumper(dest, "https://unused", "0" * 64)  # already there: no download


def test_extract_from_apk(tmp_path):
    """A member is extracted once; an existing file is kept and a missing APK is reported."""
    apk = tmp_path / "game.apk"
    apk.write_bytes(zip_bytes({METADATA_IN_APK: b"meta"}))
    out = tmp_path / "data" / "m.dat"
    il2cpp_dump.extract_from_apk(apk, METADATA_IN_APK, out)
    assert out.read_bytes() == b"meta"
    out.write_bytes(b"kept")
    il2cpp_dump.extract_from_apk(apk, METADATA_IN_APK, out)
    assert out.read_bytes() == b"kept"
    with pytest.raises(FileNotFoundError, match="APK not found"):
        il2cpp_dump.extract_from_apk(tmp_path / "no.apk", "x", tmp_path / "x")


def test_docker_command_uses_paths_inside_the_mount():
    """The container sees analysis/ as /a, so every path is rewritten relative to it and the run
    happens as the caller's user (outputs not owned by root).
    """
    cmd = il2cpp_dump.docker_command(RELEASES["5.0.8"], 1000, 100)
    assert cmd[:6] == ["docker", "run", "--rm", "-u", "1000:100", "-v"]
    assert cmd[6] == f"{ANALYSIS}:/a"
    assert cmd[-4:] == [
        "/a/tools/il2cppdumper/Il2CppDumper.dll",
        "/a/lib64_508/libil2cpp.so",
        "/a/data508/" + METADATA_IN_APK,
        "/a/dump508",
    ]


@pytest.fixture
def release(tmp_path):
    """A release under a temporary analysis folder, with an APK holding both inputs."""
    apk = tmp_path / "game.apk"
    apk.write_bytes(zip_bytes({METADATA_IN_APK: b"meta", il2cpp_dump.LIB_IN_APK: b"\x7fELF"}))
    return Release("9.9.9", 1, "id", apk, tmp_path / "lib", tmp_path / "data", tmp_path / "dump")


def test_dump_extracts_inputs_and_creates_the_output_folder(release, monkeypatch):
    """Inputs come from the APK and the output folder exists before the run (Il2CppDumper writes
    next to its own .dll otherwise -- seen on the first 5.0.8 run).
    """
    monkeypatch.setattr(il2cpp_dump, "install_dumper", lambda: None)
    monkeypatch.setattr(il2cpp_dump, "docker_command", lambda rel, u, g: ["fake"])

    def run(cmd, **kw):
        assert release.dump.is_dir()
        for f in il2cpp_dump.OUTPUTS:
            (release.dump / f).write_text("x")
        return subprocess.CompletedProcess(cmd, 0, "", "")

    monkeypatch.setattr(il2cpp_dump.subprocess, "run", run)
    assert il2cpp_dump.dump(release) == release.dump
    assert release.metadata.read_bytes() == b"meta"
    assert (release.lib64 / "libil2cpp.so").read_bytes() == b"\x7fELF"
    monkeypatch.setattr(il2cpp_dump.subprocess, "run", None)  # complete: not run again
    assert il2cpp_dump.dump(release) == release.dump


def test_failed_run_is_reported(release, monkeypatch, capsys):
    """A failing Il2CppDumper run is one error line naming the release, not a traceback."""
    monkeypatch.setattr(il2cpp_dump, "install_dumper", lambda: None)
    monkeypatch.setattr(il2cpp_dump, "docker_command", lambda rel, u, g: ["fake"])
    monkeypatch.setattr(
        il2cpp_dump.subprocess,
        "run",
        lambda cmd, **kw: subprocess.CompletedProcess(cmd, 1, "", "boom"),
    )
    monkeypatch.setitem(RELEASES, "9.9.9", release)
    assert il2cpp_dump.main(["--release", "9.9.9"]) == 1
    assert "Il2CppDumper failed for 9.9.9" in capsys.readouterr().err


def test_main_prints_the_dump_folder(release, monkeypatch, capsys):
    """A complete dump is reused and its folder printed for the next command."""
    release.dump.mkdir()
    for f in il2cpp_dump.OUTPUTS:
        (release.dump / f).write_text("x")
    monkeypatch.setitem(RELEASES, "9.9.9", release)
    assert il2cpp_dump.main(["--release", "9.9.9"]) == 0
    assert capsys.readouterr().out.strip() == str(release.dump)
