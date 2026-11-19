# D976

## D976: Bunker 1 had no sky (solo) -- GE+ gives it Surface 1's sky -- BUILT + MAINTAINER-CONFIRMED 2026-11-16 (Ryujinx, Bunker 1 solo; Switch; port layer only)

**Symptom.** At the exit of Bunker 1 and through its windows, the sky is a flat dark navy, with no clouds; the GE+ sky (D971) is not drawn there.

**Cause.** `fog_tables` (`src/game/bgfog.c`) has Bunker 1 records for 2, 3 and 4 players only. In solo `fogLoadLevelEnvironment` finds none, nor in `fog_tables2`, and falls back to `fog_tables2[0]` (LEVELID_NONE: sky 0,16,64, no clouds, far 10000); `sky.c` then fills the background with that colour. Same on the cartridge, so not a port bug. GE+ only draws levels whose record has clouds (log: `SKYPLUS stage=9 GE sky: no clouds ... sky=0,16,64`).

**Change (port layer only, `port/src/skyplus.c`).** The bunker sits under Surface 1, the mission before it, so `skyPlusEnv` hands GE+ a copy of the current record with the LEVELID_SURFACE record's sky colour (0x60,0x60,0x80), clouds, cloud colour (240,120,30) and height (10000) when the stage is Bunker 1 and its record has no clouds; `skyPlusMood` gives Bunker 1 Surface's dusk mood (sun with a halo, default azimuth). Fog, draw distance and the game's own record are untouched; Sky = Original or the N64 display mode keeps the navy fill. Bunker 2 already has its own clouded record (Surface 2's clouds, darker sky).

**Other solo levels without clouds** (GE+ keeps the game's fill): Silo (no record, navy), Facility, Control, Caverns (indoor), Jungle (0x18,0x20,0 with dense fog), Cuba (fogless record, no clouds); Frigate has clouds but water, so GE's sky.

**Verification.** Switch NRO builds (Docker). Maintainer, Ryujinx, Bunker 1 solo (2026-11-16): the sky at the exit is right.

**Confidence.** High (log + record tables).
