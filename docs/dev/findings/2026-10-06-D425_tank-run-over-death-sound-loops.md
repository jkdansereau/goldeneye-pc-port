# D425

<!-- from the old section-F/H table row -->
| D425 | **Tank run-over death sound loops: `(chrflags << 7) >= 0` is always true on unsigned GCC enum (issue #115).** — full `## D425` entry at file tail | FIXED (2026-09-29 bookkeeping: use-site `(s32)` cast; user-verified vs GEPD/1964 2026-09-29, #115). Earlier: FIXED in tree (use-site `(s32)` cast); live check owed. |

## D425 — Tank run-over death sound loops: `(chrflags << 7) >= 0` sign-bit latch is always true on GCC (issue #115)

`CHRFLAG` contains `0x80000000`, so GCC makes the enum unsigned; `bondview2.c` tank-crush latch (`CHRFLAG_01000000` << 7 = sign bit) never blocked, so `CRUSHED_YELL_SFX`/`TANK_CRUSH_MAN_SFX` retrigger every tick while an ACT_DIE chr sits in the tank bounds. **Fix:** `(s32)chrflags << 7` (use-site cast; a `-1` sentinel would risk widening the enum to 64-bit). Cross-tag porting-notes D3. **Status:** FIXED; user-verified 2026-09-29 (Runway run-over plays the crush yell once).
