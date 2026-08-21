# Bot CPS Bridge

A small compatibility mod for Geometry Dash 2.2081 / Geode 5.8.2.

Its only purpose is to keep MegaHack's CPS status visually responsive to macro holds/releases from bots such as SiliFork and xdBot.

The bridge does **not** inject extra gameplay inputs, does not modify replay timing or physics, and does not patch or redistribute MegaHack or xdBot binaries. MegaHack must already be installed normally.

## Compatibility approach

The bridge observes the final P1/P2 jump-hold state exposed by Geometry Dash and normal `GJBaseGameLayer::handleButton` transitions. It then updates only the visible CPS status presentation when MegaHack itself leaves the status in its idle colour during bot playback.

If MegaHack already changes the status colour normally, the bridge stays out of the way and learns that active style for later use.
