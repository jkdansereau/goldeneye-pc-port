# D432

<!-- from the old section-F/H table row -->
| D432 | **Gunshot sounds "off" after D427: sound-trigger gate (`SoundTriggerRate`) is per-tick on PC but frame-granular on the N64 (user: Cavern, Mac-10 run).** — full `## D432` entry at file tail | FIXED in tree (gate offset rounded to odd at 1 tick/frame); measured shot/sound cadence identical to N64 rules; residual audible difference vs 1964 tracked as D433. |

## D432 — Gunshot sound cadence drifted from the shots after D427 (user: sounds "off", Caverns, Mac-10-class gun)

`gunfire.c` re-triggers the shot sound when `field_A4C < g_GlobalTimer`, with `field_A4C = SoundTriggerRate + g_GlobalTimer`. The gate is evaluated once per gun-tick call (per rendered frame; each frame carries `g_ClockTimer` ticks of `g_GlobalTimer`). On the N64 (2 ticks/frame) it is only evaluated every second tick, so the next sound lands on the first frame boundary after rate+1 ticks (AK47 rate 4 -> 6). At 1 tick/frame the port opened it after exactly rate+1 (5). D427 moved the shots to 6 ticks, so shots (6) and sounds (5) drifted. **Fix:** at `g_ClockTimer < 2` add `| 1` to the rate (rate 4 -> 5, 3 -> 3, 5 -> 5, 11 -> 11), which opens the gate on the same even boundary the console used; unchanged at >= 2 ticks/frame.
**Status:** FIXED; cadence measured identical to the N64 rules (AK47 6/6, Klobb 6/12, weapon 9 4/6; zero SKIPs). Residual slight audible difference tracked as D433.
