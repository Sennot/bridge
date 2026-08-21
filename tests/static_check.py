from pathlib import Path
import json

root = Path(__file__).resolve().parents[1]
manifest = json.loads((root / "mod.json").read_text())
assert manifest["id"] == "elkiteam.cpsbridge"
assert manifest["geode"] == "5.8.2"
assert manifest["gd"]["win"] == "2.2081"

src = (root / "src/main.cpp").read_text()
for forbidden in (
    "WriteProcessMemory",
    "VirtualProtect",
    "GetProcAddress",
    "LoadLibrary",
    "absolllute.hackmega.dll",
    "zilko.xdbot.dll",
):
    assert forbidden not in src, forbidden

assert "GJBaseGameLayer::handleButton(pressed, button, player1);" in src
assert "m_holdingButtons" in src
assert 'isModLoaded("absolllute.hackmega")' in src
print("static checks OK")
