# D416

<!-- from the old section-F/H table row -->
| D416 | **2-player split-screen crashes (issue #99, v0.3.0 `4827bc8`, fault addr 0xa43a0ed4, PC 0x14009e758): MP setup files `Ump_setup*Z` were never converted N64→PC** — full `## D416` entry at file tail | RESOLVED in-harness (converter + D417 + D418 fix the 2P crash and split-screen flicker; 18/18 2P/3P/4P x 6-stage load runs clean; live pad play still owed). Also D417, D418.

## D416 — Local split-screen multiplayer crash: MP setup files never converted (issue #99)

**Report:** issue #99, 2P split screen, v0.3.0 `4827bc8`, fault addr `0xa43a0ed4`, crash PC `0x14009e758`.

**Symbolised (2026-09-29, `dist/goldeneye-pc-port-0.3.0-win64` exe, addr2line):** `load_proptype` (prop.c) called from `proplvreset2` (prop.c) — the propDefs record walk over `g_CurrentSetup`, i.e. the setup-file load, not menu code.

**Root cause:** `prop.c` loads `"U" + "mp_" + <stage>` (`Ump_setup*Z`) whenever `getPlayerCount() >= 2`. `tools_pc/d88_emit.py` (and `d88_propdefs.py`, `d88_propdef_scan.py`, `d125_check.py`) only matched `Usetup*Z`, so the 13 MP setups in `file_resource_table.inc.c` came out of pccg as raw big-endian N64 data and the walk read garbage. (Not the D190 class: D190 is closed as `-level_45` being MP-only with no solo setup.)

**Fix (tools only, no game code):** single predicate `is_setup_name()` = (`Usetup`|`Ump_setup`) + `Z`, used for the name list, the `--regen` row-drop, and the analysis tools. `convert_usetup()`'s full-tiling assertion is the gate that MP files share the solo format.

**Verified:** `d88_emit.py ntsc-final --regen` → `converted: 34/34`, ALL CHECKS PASSED; `d125_check.py` MATCH on all 13 MP + 21 solo files; pccg manifest 73→86 rows with all 73 pre-existing rows byte-identical. PAL/JP not run (NTSC ROM only).

**Follow-up (2026-09-29):** in-game 2P now runs (see D417, D418). Also added: `Input.MPMode` (Auto/PadsOnly/KbmP1) + slot->pad shift so one pad or kbm+pad can be P1/P2 and the game's `joyGetControllerCount() >= 2` MP gate opens; `GE_MPVIRT` / `GE_STARTMP` harness flags (this machine has 0 SDL gamepads). Input layer is still single-player shaped (see memory: mp-input-layer-direction). Separately, PC input hooks are pad-0-only and `input.c` re-asserts control type on the current player (can rewrite another player's control style in MP) — port-layer work planned for v0.5.0.

**Status:** RESOLVED in-harness (see header row).
