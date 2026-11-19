# D422

<!-- from the old section-F/H table row -->
| D422 | **P2+ have no centred-aim mode in split-screen (single-slot aim state)** — full `## D422` entry at file tail | FIXED in tree (commit `ac92e0ab`: per-slot aim state `s_aimSt[4]`, `aimModeGet()` returns the configured mode for every slot); live split-screen verification of centred aim on slot 1+ still owed (needs a non-slot-0 aim device). |

## D422 — No centred aim for P2+ in split-screen

**Update (2026-09-30, verified by code reading):** fixed by `ac92e0ab` (an ancestor of `mp/virtual-seats`): `s_aimSt[4]` with the `AIMS` slot accessor, a stateless `padDirectCompute` on `slotPlayer()`, and `aimModeGet()` no longer forces `AIMMODE_N64` for slots > 0. Slot 0 stays byte-for-byte (`AIMS` falls back to slot 0). This entry was left saying 'parked'. Untested live on slots 1+.

Centred aim (`aimModeGet()==AIMMODE_CENTRED`, `padDirectCompute`, `s_gepdCross*`) keeps single-slot static state, so `aimModeGet()` returns `AIMMODE_N64` for slots > 0 (port/src/input.c). P1 keeps its full aim set. Making the state per slot is mechanical but touches the aim accumulators; parked as low priority.
