# D291

<!-- from the old section-F/H table row -->
| D291 | **Dam: texture glitches when the truck drives past (M-141, Steam Deck user playtest).** — full `## D291` entry at file tail | CLOSED — duplicate of D306, faithful (M-201, 2026-09-26: user-captured, Dam intro camera 5 only). Earlier: OPEN — observed, not investigated. Needs a live repro with the truck's exact position/timing and, ideally, a screenshot. |

## D291 — Dam: texture glitches when the truck drives past (M-141, Steam Deck user playtest).

**Dam: texture glitches when the truck drives past (M-141, Steam Deck user playtest).** No further detail captured (which surface/texture, timing, one-shot vs. every pass). Could be a moving-vehicle-specific instance of a known texture-cache/TMEM-residency class (cf. D124's stale-DL-marker mechanism, a different object family) or something new.

**Status (from §F table):** OPEN — observed, not investigated. Needs a live repro with the truck's exact position/timing and, ideally, a screenshot.

**v0.4.0 check (2026-09-23): not reachable headlessly.** Bare `-level_33` capture (300–3100 every 70 frames): the truck is parked in the intro flyby (frames 580–720, textures correct) and never drives through the spawn view; it is set off by the player moving toward the checkpoint. Needs the playtest brief item (Dam, watch the truck pass, F12 if glitched).

**2026-09-25 (M-200 session): headless walk attempted, truck not reached; AI mapped.** The truck is the Dam `Vehicle` record (setup index 321, `UsetupdamZ.c:1152`). Its AI list id `0x040a` = `ai_9` (`:2028`, debug-logs "brrm brrm"): `vehicle_start_path(0x07)` + `vehicle_speed(0x0002, 0x7800)`, then sleeps. So it drives patrol path 7 as soon as its AI ticks, and nothing else gates it in script. The "set off by walking" behaviour from the 2026-09-23 note is therefore prop activation (its room coming into play), not an AI trigger. Headless: `GE_INPUTSCRIPT` stick-up is *look* in the default control style; forward is `CUP` pulses (6 polls each, max 64 entries ⇒ ~6 s of walking). A straight walk from spawn drifts onto the ramp by the wall and draws guard fire (`GE_D320R="850:cheat=inv"` keeps Bond alive); no truck in view by frame 3300. Reaching it headless needs a steered route (turn + walk) or a teleport action in the harness; not worth it before a user sighting says which surface glitches. The intro wheel depth-fight (D306/D308, real per `GE_ZF`) remains the only captured truck-area rendering defect. D291 may be that same effect seen in motion.

**Status:** OPEN; still needs the playtest item (Dam: watch the truck pass, F12 screenshot if glitched, note whether it's the wheels, i.e. D306).

**Current status (2026-09-29 bookkeeping):** the Status line above is historical; the authoritative verdict is the §F row — CLOSED — duplicate of D306, faithful (M-201, 2026-09-26: user-captured, Dam intro camera 5 only). Earlier: OPEN — observed, not investigated. Needs a live repro with the truck's exact position/timing and, ideally, a screenshot.


**2026-09-26 (M-201): merged into D306.** Captured by the user: intro-only (Dam level intro/attract, truck over the walkway camera), wheels drawing through the body; reproduces at FovScale 100. Tracked under D306 from here.

**Status: CLOSED with D306 (faithful).**
