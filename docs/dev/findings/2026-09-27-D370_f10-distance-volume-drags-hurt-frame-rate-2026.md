# D370

<!-- from the old section-F/H table row -->
| D370 | **F10 distance/volume drags hurt frame rate (2026-09-27).** — full `## D370` entry at file tail | **CLOSED (2026-09-28).** Shipped in v0.4.0; accepted in the user's final review / campaign playtest. (user: fixed.) |

## D370 — Avoid redundant work during F10 slider drags (2026-09-27)

**Reported:** dragging Draw/LOD distance, Music or FX volume drops FPS.
**Diagnosis:** `sliderSetFromX` runs on every controller poll while LMB is
held, including repeated polls over the *same snapped value*. `rowSetCommit`
then re-enqueues watch audio updates and, for *any* `Video.*` key, requests a
`videoStartFrame` live-config pass: `set_swap_interval`, FPS cap, texture
filter, image state and a log line. Draw/LOD multipliers are already read
straight from their config pointers by the render path, so no such pass is
needed. The active playtest log has hundreds of `video: live config applied`
lines and multiple watch-setting persists; log alone does not isolate all
of those calls to one knob. Physically increasing Draw/LOD can independently
increase rendering load, which this fix will not hide.

**Fix (port-only `optionsoverlay.c`):** early-return before live-apply/queue
when a snapped value has not changed (including queued watch settings); held
mouse polls no longer repeat the same audio setter or GL state work. Retain
slider-release `watchSettingsCommit` so a drag persists exactly once. Exempt
only `Video.DrawDistance` and `Video.LodDistance` from
`videoRequestLiveConfig`; all other Video keys retain their old live-apply
semantics. Audio updates still preview live for genuinely changed detents.
No user config/save or game code edited. NTSC compile + staged link passed;
active Windows game initially held the executable; the local launcher
installed the staged binary on the user's next safe launch (now running).
Live FPS retest (ideally compare dragging
Music/FX while paused vs simply raising Draw/LOD values) owed.
**User playtest:** "it can still drop it down if you spam it but mostly good."
Residual not isolated: the latest playtest log shows repeated full video
config passes while changing FOV (not Draw/LOD) and several watch EEPROM
persists; sustained high Draw/LOD also increases actual GPU work. Parked as
polish until a focused repro identifies which slider/spam pattern dominates.
