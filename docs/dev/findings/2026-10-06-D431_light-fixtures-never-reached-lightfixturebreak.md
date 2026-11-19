# D431

<!-- from the old section-F/H table row -->
| D431 | **Light fixtures never reached `lightFixtureBreak` on PC: bullet-hit texnum is stubbed to -1 (D154), so `check_if_imageID_is_light` is always false (issue #119; user: Cavern lights do not take damage).** — full `## D431` entry at file tail | FIXED in tree (fixture table records the image id; bg.c hit code looks it up; free-slot guard); user-verified vs GEPD/1964 2026-09-29. |

## D431 — Shot light fixtures never break on PC: the bullet-hit path returns texnum -1, so `check_if_imageID_is_light` is never true (issue #119, user-confirmed on Caverns)

`chrprop.c` calls `lightFixtureBreak()` only when `check_if_imageID_is_light(bghit.texturenum)`. On the N64 `bg.c`'s room hit-test recovers `texnum` by dereferencing 8 bytes before the SETTIMG texture data in KSEG0; on PC that is invalid for the converted room GDLs, so D154 stubbed it to `-1` (both sites, G_TRI1 and G_TRI4 loops). Result: `lightFixtureBreak` is never called for any shot (the `GE_D430` probe logged zero `break` lines during a Caverns session where the user shot lights). D430 (index parser) was a second, independent bug behind it.
**Fix (`#ifdef PORT`, no logic change):** `tex.c` already knows the image id when it opens a fixture entry; it now stores it in the entry's unused `RESERVED` slot (`lightFixtureSetTexnum`); the two `bg.c` sites return `lightFixtureTexnumForGfx(gdl, roomnum)` (same room/DL-range test `lightFixtureBreak` uses) instead of -1. Non-light textures still return -1 (impact-sound flavour unchanged, D77 parked).
**Consequence found by sub-agent review (D434):** real image ids for light hits now flow into the `g_Textures[]` consumers in `chrprop.c`/`propobj.c`, which read a raw byte 0 as the hit type (wrong on PC) -- fixed in D434.
**Status:** FIXED; user-verified vs GEPD/1964 (Caverns) 2026-09-29.
