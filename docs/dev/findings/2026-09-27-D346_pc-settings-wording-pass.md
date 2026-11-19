# D346

<!-- from the old section-F/H table row -->
| D346 | **PC-settings wording pass: option labels/values aligned to Nightdive/Turok + PD-port conventions (title-case On/Off, "Frame rate cap", "Anti-aliasing"/"None", units in values not labels, self-explanatory auto-FOV toggles); VIEW section merged into VIDEO (2026-09-27).** — full `## D346` entry at file tail | **CLOSED (2026-09-28).** Shipped in v0.4.0; accepted in the user's final review / campaign playtest. |

## D346 — PC-settings wording pass: Nightdive/Turok + PD-port conventions (2026-09-27)

User request: make all PC setting labels/values consistent with the PD port and
Nightdive remasters (Turok) where possible. Both settings UIs (the F10 overlay
and `MENU_PC_OPTIONS`) share one table — `rows[]` in `port/src/optionsoverlay.c`
— so a single pass covers both. Display-only: config keys, stored values, and
adjustment behavior are untouched (existing inis read/write identically).

**Label changes** (`rows[]`, optionsoverlay.c):

| Before | After | Rationale |
|---|---|---|
| `DISPLAY` (section) | `VIDEO` | Nightdive section naming ("Video"); pairs with the existing `VIEW` section |
| `Frame cap` | `Frame rate cap` | Turok 3's exact wording; values already Nightdive-style (`Uncapped` / `120 FPS`) |
| `MSAA` | `Anti-aliasing` | Nightdive standard; jargon-free |
| `Anisotropic` | `Anisotropic filtering` | Nightdive standard |
| `FOV scale %` / `Draw distance %` / `LOD distance %` / `HUD scale %` / `Trigger threshold %` | `%` dropped from labels, moved to the value (`100%`, `150%`, …) | Nightdive shows units in the value column, not the label |
| `Crop overscan bars` | `Crop overscan` | trimmed; "bars" was internal phrasing |
| `Draw dist. auto` / `LOD dist. auto` | `Draw dist. follows FOV` / `LOD dist. follows FOV` | the old labels didn't say what follows what (they couple the distance to `Video.FovScale`, video.c:203-231) |
| `Mouse invert Y` / `Pad invert look Y` | `Invert look (mouse)` / `Invert look (controller)` | Nightdive per-device convention; now a consistent pair |

**Value changes:** `OFF/ON` → `Off/On` (kOnOff); `NEAREST/BILINEAR/3-POINT` →
`Nearest/Bilinear/3-Point`; `CENTRED (PC)` → `Centred (PC)`; MSAA `OFF` →
`None` (Nightdive AA vocabulary). The options screen's ON-colour match in
`frontoptions.c:454` updated `"ON"` → `"On"` to follow.

**Mechanism:** two display-only fields appended to `struct Row` (`unit`,
`dispDiv`; C zero-fill keeps every existing positional initializer valid) and a
small branch at the integer-slider tail of `valueText()`: optional raw→display
divide, then unit suffix. Used by: FovScale/DrawDistance/LodDistance/HudScale/
PadTriggerPct (`%`), Anisotropy (`x`), PadDeadzone (`%`, dispDiv 300 — raw
0..30000 stick units → % of full travel; default 7000 now reads "23%" instead
of "7000").

**Kept deliberately:** `VSync`, `Fullscreen`, `Resolution`, `Texture filter`
(universal); `Aim style`/`Aim range` + their `N64`/`PC` values (GE-specific,
already clear); `No hit flash` (community-mod name; inverting the polarity to
"Hit flash: Off" would change what On means — not a wording fix); `Skip intro`,
`Show FPS`, `All unlocked`, `Quit to desktop` (plain English, no reference
counterpart worth matching).

**Known cosmetic follow-up (not done):** the deadzone slider's raw step is 500
stick units, so its % display advances ~1.7% per press (23→24→25→27…). A clean
1%-per-press grid would mean changing `step` to 300 — a behavior change to the
adjustment feel, out of scope for a wording pass; flagged for a future input-QoL
item.

**D346b — VIEW section merged into VIDEO (user request, same day).** The old
`VIEW` header row was deleted from `rows[]`; its rows (FOV scale, Native
widescreen, Widescreen auto FOV, Crop overscan, draw/LOD distance pairs) now
follow the display/quality rows in one Nightdive-style "Video" section. The
options screen's page model is one-header-one-page with a row cap, so the cap
moved 12→15 (`MAX_PROWS`) and the row pitch 20→18 px (`ROW_DY`, frontoptions.c)
to keep the worst case (15 rows: "Draw dist. follows FOV" off reveals both its
toggle and the slider) on one page — last row text at y=317, inside the 330
paper; its 22px highlight box overflows by 1px in that state only (clipped,
invisible). Hit bands stay contiguous at the tighter pitch (band = [y-3, y+15),
next starts y+15). F10 overlay unaffected (own pitch, scrolls). Section list is
now 4 entries: Video / Mouse / Aim / Controller / Game.

**Verification:** `build-pc.sh ntsc-final` clean (no new warnings); headless
boot into the options screen (`GE_STARTMENU=26 GE_QUITFRAME=300`) orderly
self-quit, zero crash lines. All strings plain ASCII (JP-region safe, #87).
Port-only: no `src/game` edits, N64 build untouched.
