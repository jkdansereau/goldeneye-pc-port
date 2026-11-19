# D424

<!-- from the old section-F/H table row -->
| D424 | **`WeaponStats.RecoilSpeed`/`b44[]` byte order: no grenade-launcher recoil, shortened recoil on every gun (issue #118).** — full `## D424` entry at file tail | FIXED in tree (`WS_B44`, port-guarded); user-verified vs GEPD/1964 2026-09-29. |

## D424 — `WeaponStats.RecoilSpeed`/`b44[]` byte order: no recoil animation on the grenade launcher, shortened recoil on every gun (issue #118)

**Root cause:** `b44[4]` aliases the four bytes of the big-endian `RecoilSpeed` word (`gun.h`). Byte 0 is the MSB on N64 but the LSB on little-endian PC, so `gunfire.c` read rise/fall/retrigger/offset reversed. Grenade launcher `0x081000FF` (8,16,0,-1) read as (-1,0,16,8): `field_890 >= sp1A4+sp1A0` was true at once, so the recoil phase never ran. KF7/Klobb 4+12 -> 6+0 ticks, PP7 4+8 -> 0+3.
**Fix (port, `#ifdef PORT`, use-site, semantics-preserving):** `WS_B44(ws,i)` accessor in `gun.h` reads `b44[3-i]`; all `gunfire.c` reads switched. **Rule-2 note:** endianness rather than pointer widening, so it needs sign-off as an ABI-class exception.
**Rule-2:** user sign-off 2026-09-29 ("if justified and accurate"). Accuracy check: a standalone gcc test of the real union layout with the GL (`0x081000FF`) and KF7 (`0x040C0006`) words gives (-1,0,16,8)/(6,0,12,4) through the old read and exactly the N64 (8,16,0,-1)/(4,12,0,6) through `WS_B44`.
**Status:** FIXED; user-verified vs GEPD/1964 2026-09-29 (GL kick, PP7 recoil).
