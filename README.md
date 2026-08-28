# MegaHack CPS Bridge

Standalone Geode utility for Geometry Dash 2.2081 / Geode 5.8.2.

The bridge mirrors the actual `GJBaseGameLayer::handleButton` held state into the visible MegaHack CPS label using the same presentation style as SiliFork. It does **not** generate clicks, alter playback, modify TPS, or maintain its own CPS count.

## Why this exists

Many macro bots send valid gameplay inputs that MegaHack can numerically count, but MegaHack's separate held-state presentation may stay colourless. This mod observes the final GD input path and mirrors only that visual state.

Because it observes the shared gameplay input path rather than private bot internals, XDBot and other bots do not need a dedicated API adapter as long as their playback reaches `GJBaseGameLayer::handleButton`.

## Build

Set `GEODE_SDK`, then run `build.bat`.
