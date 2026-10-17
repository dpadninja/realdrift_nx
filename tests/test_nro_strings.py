"""The built realdrift_nx.nro must not contain a stringified macro NAME (e.g. "RD_AUDIO_RATE").

That is what RD_STR(X) yields when X is not defined where it is used (a header not included):
the game then gets the name instead of the value -- FMOD once got "RD_AUDIO_FRAMES_PER_BUFFER"
as its buffer size and audio stayed silent. Skips if the .nro has not been built.

MIT (see LICENSE).
"""

import re

import pytest

from tools.game import ROOT

NRO = ROOT / "realdrift_nx.nro"


@pytest.mark.skipif(not NRO.exists(), reason="build realdrift_nx.nro first")
def test_no_macro_names_leaked_into_strings():
    leaked = sorted(
        set(re.findall(rb"(?<![A-Za-z0-9_])RD_[A-Z0-9_]{3,}(?=\x00)", NRO.read_bytes()))
    )
    assert leaked == []
