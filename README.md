# Bot CPS Glow

Bot CPS Glow is a standalone Geode mod for Geometry Dash 2.2081 / Windows.

It watches `GJBaseGameLayer::handleButton` from an outer observer hook. If the immediate call came from a known bot DLL (or a configured bot-like module), it updates a local visual state. A separate scheduler hook then pulses Mega Hack's visible CPS label toward green.

## Why this design

The mod deliberately does **not** depend on private bot classes, replay formats, memory offsets, or exported bot APIs. This makes it suitable for closed-source builds such as current xdBot releases as long as they continue to submit gameplay inputs through the normal Geometry Dash `handleButton` path.

Manual player inputs normally enter the outer hook directly from Geometry Dash, so they are not classified as bot inputs. Bot-generated calls originate from the bot DLL and are classified by the caller module.

## Built-in bot/module recognition

- SiliFork (`peony.silicate`)
- xdBot / XDBotFork (`zilko.xdbot`)
- tcBot (`chagh.tcbot`)
- zBot (`fig.zbot`)
- yBot (`kepe.ybot`)
- Astral (`astralteam.astral`)
- Mega Hack automated/replay input (`absolllute.hackmega`, legacy `absolllute.megahack`)
- ReplayBot-style module names
- Generic bot/macro/replay/TAS module-name detection (configurable)
- User-defined module tokens in settings

## Mega Hack label compatibility

The mod first prefers CPS labels whose node ancestry looks like Mega Hack. If current Mega Hack builds expose no useful node IDs, it can fall back to a bounded scene scan for `CCLabelBMFont` labels containing `CPS`. SiliFork's own CPS label is explicitly excluded from the fallback.

## Visual semantics

- Bot press: starts/refreshes the green pulse.
- Bot hold: CPS remains pulsing while held.
- Bot release: a configurable short linger keeps 1-tick clicks visible.
- Reset/quit: input state is cleared.
- No active bot input: original label colour and opacity are restored.

## Build

Requires Geode SDK 5.8.2 (or a compatible newer SDK):

```bat
set GEODE_SDK=C:\path\to\geode
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

Or use your normal Geode CLI workflow.

## Notes on xdBot research

The public NakoMellia XDBotFork v2.6.6 source was used only to verify the public mod ID and that its gameplay input path uses `GJBaseGameLayer::handleButton`. No XDBot source code is included or copied into this project.
