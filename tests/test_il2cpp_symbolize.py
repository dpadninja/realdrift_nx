"""tools/il2cpp_symbolize.py: libil2cpp addresses named from Il2CppDumper's script.json.

MIT (see LICENSE).
"""

import json

from tools.il2cpp_symbolize import Symbolizer, main

METHODS = [
    {"Address": 0x2000, "Name": "B$$Run"},
    {"Address": 0x1000, "Name": "A$$Start"},
    {"Address": 0, "Name": "Abstract$$NoBody"},
]


def test_lookup_picks_containing_method():
    """An address is named by the method whose code contains it, with the offset into that
    method.
    """
    s = Symbolizer.from_methods(METHODS)
    assert s.lookup(0x1000) == "A$$Start+0x0"
    assert s.lookup(0x1FFC) == "A$$Start+0xffc"
    assert s.lookup(0x2010) == "B$$Run+0x10"


def test_lookup_below_first_method_and_zero_addresses_ignored():
    """Addresses before the first method have no name, and methods without code (address 0) are
    dropped instead of swallowing low addresses.
    """
    s = Symbolizer.from_methods(METHODS)
    assert s.lookup(0x10) is None
    assert "Abstract$$NoBody" not in s.names


def test_annotate_log_line():
    """Log lines get the method name after every libil2cpp.so address; addresses in other
    libraries are left alone.
    """
    s = Symbolizer.from_methods(METHODS)
    line = "[wd]     bt[4] libil2cpp.so+0x2010 then libunity.so+0x10"
    assert (
        s.annotate(line) == "[wd]     bt[4] libil2cpp.so+0x2010 [B$$Run+0x10] then libunity.so+0x10"
    )


def test_command_line_names_addresses_and_annotates_a_log(tmp_path, capsys):
    """The command line names given addresses and annotates a whole debug.log."""
    script = tmp_path / "script.json"
    script.write_text(json.dumps({"ScriptMethod": METHODS}))
    assert main([str(script), "0x2004"]) == 0
    assert "B$$Run+0x4" in capsys.readouterr().out
    log = tmp_path / "debug.log"
    log.write_text("crash at libil2cpp.so+0x1008\n")
    assert main([str(script), "--log", str(log)]) == 0
    assert "[A$$Start+0x8]" in capsys.readouterr().out
