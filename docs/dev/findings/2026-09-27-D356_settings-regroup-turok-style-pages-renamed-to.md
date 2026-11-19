# D356

<!-- from the old section-F/H table row -->
| D356 | **Settings regroup: Turok-style pages renamed to functional sections (INPUT/GAMEPLAY/GRAPHICS/AUDIO/VIDEO) with PD-style "(File N)" scoping annotations, the redundant N64-watch rows (`Bond.Look`, `Bond.AimControl`) off the menu surface, and a real per-section Reset-to-defaults (two-step arm->confirm, watch rows to BLANKSAVEDATA via the D352 commit path, ini rows to the port's C initializers; `__Resolution` a documented exclusion) replacing the Turok reset rows the PC build never honored (2026-09-27).** — full `## D356` entry at file tail | FIXED (port-only, `port/src/optionsoverlay.{h,c}` + `port/src/watchsettings.{h,c}` + `port/src/frontoptions.c`; plan `docs/dev/D356-SETTINGS-REGROUP-PLAN.md`): 5 sections all below MAX_PROWS, hidden rows D181/D216-style (keys + watch paths stay live), edge-triggered reset with 3 s arm window + maintain/clear disarm, GE_WSPROBE_RESET front + in-stage probes headless-verified (all sections failures=0, scope isolation OK, saved bytes 0xFF), GE_WSPROBE/GE_WSPROBE_FRONT regressions clean. |

## D356 — Settings regroup: functional sections, per-section reset-to-defaults, N64-watch rows off the menu (2026-09-27)

**Problem.** The D353 regroup left the F10 overlay with a Turok-style layout
and Turok-style per-page "Reset to defaults" rows (options.c:121/140/159/173/190,
all N64-watch defaults, none of which the PC build honors -- a D330-era known
gap). The N64-watch `Bond.Look` (stacks double-negation with the PC invert
rows) and `Bond.AimControl` (collides with `Input.AimMode`) rows were
menu-surface hazards. The plan
(docs/dev/D356-SETTINGS-REGROUP-PLAN.md, signed off 2026-09-27): rename the
page model to functional sections, scope the per-file rows under a file
annotation, drop the N64-watch rows from the menu (keep the code paths), and
replace the Turok reset row with a real per-section reset-to-defaults.

**Fix (port-only; `optionsoverlay.{h,c}`, `watchsettings.{h,c}`,
`frontoptions.c`, + plan doc; zero game-code changes).**
1. **Sections (plan §4).** `INPUT` (7 rows: the old MOUSE + PAD headers
   merged, `__HdrMouse`/`__HdrPad` dropped) / `GAMEPLAY` (File N; 8 rows:
   surviving watch toggles `Bond.AutoAim`/`LookAhead`/`Sight`/`Ammo` + the
   `Game.*` ini rows + Quit) / `GRAPHICS` (11 rows, D353's DISPLAY+GRAPHICS
   merged) / `AUDIO` (File N; `Bond.Music` + `Bond.FX` watch sliders, out of
   the Bond-file page) / `VIDEO` (5 rows: the D353 DISPLAY rename,
   `Video.DisplayFPS` moved from GAME). All pages below the MAX_PROWS cap.
   `Bond.Look`/`Bond.AimControl` are commented out of `rows[]` with a D356
   note (D181/D216 style: keys + watch paths stay live; ini power users can
   still set them). The `__HdrBond` header + `__BondFile` chooser row are
   retired, replaced by the front-only top `Save file: File N [change]` row
   (frontoptions.c; hidden in F10 via the generalized
   `optionsRowIsBondChooser` mechanism).
2. **Save-file scoping.** Sections that carry per-file rows (GAMEPLAY,
   AUDIO) get a PD-style header annotation `"(File N)"` / `"(no file)"`
   driven by `Row.saveScoped` + `watchSettingsActiveFolder()` (in-stage: the
   active file; front: the resolved front target); per-file rows inside the
   mixed-scope GAMEPLAY section carry a dim `"(save)"` tag (the section
   title already says which file). The D353 `optionsRowIsBondChooser`
   special-case is generalized to the `saveScoped` flag (both UIs: F10
   overlay + front options screen).
3. **Per-section reset-to-defaults (Gate E).** The Turok rows are deleted and
   replaced by a trailing `Reset to defaults` ROW_ACTION on every section
   (`__ResetInput`/`__ResetGameplay`/`__ResetGraphics`/`__ResetAudio`/
   `__ResetVideo`). Activation is edge-triggered, two-step arm -> confirm
   within 3 s (`RESET_ARM_US`), one commit per confirmed activation, disarm
   on timeout or navigating away (`optionsResetMaintain`), disarm on
   close (`optionsResetClear`); the arm state is shared by the F10 overlay
   and the front screen (never open at once; both clear it). The front
   screen suppresses held-key repeat for reset rows; F10's held-repeat
   branches skip them -- callers guarantee edges, so hold never repeats
   into confirm. Confirm clears the pending arm first, then
   `rowResetSection(i)` walks the section's DECLARED row range (header to
   next header, so conditionally hidden rows are reset too):
   watch rows -> `watchSettingsSet(field, watchSettingsBlankValue(field),
   1)` (BLANKSAVEDATA, through the D352 commit path: front direct write,
   stage queue drained + persisted by the game thread); ini rows -> the
   `kResetDefaults[]` table (the port's own C initializers, each entry
   citing its source variable) applied via `rowSetCommit` and persisted like
   any ini edit. `__Resolution` is a documented exclusion (action-backed,
   not a registered config row -- a numeric default cannot express it; the
   player's resolution choice survives a VIDEO reset). No new write path.

**Verification (headless, this session).**
- `GE_WSPROBE_RESET=1` front boot: all 5 sections dirty -> arm -> confirm ->
  verify, `failures=0` each; `__Resolution` untouched (exclusion holds);
  `file 2 unchanged (scope isolation)` -- the second-file negative test.
- `GE_WSPROBE_RESET=1 -level_33`: `stage dispatch: 6 command(s) queued`
  (4 GAMEPLAY + 2 AUDIO watch rows), all 5 sections `failures=0` after the
  game-thread drain, `saved bytes OK (music/sfx = 0xFF)` -- the persisted
  save holds BLANKSAVEDATA raw.
- Probe phase machine (`watchSettingsGameTick`, same env gate): front tick
  120 = prepare (front-only, direct commits), stage tick 120 = dispatch
  (queue), stage tick 121 = verify (post-drain). A direct `-level_XX` boot
  enters the stage long before the front counter would reach 120, so a
  stage run starts its timer from phase 0 (the stage dispatch builds its
  section list lazily).
- Regressions: `GE_WSPROBE=120 -level_33` live music/fx/t1 32767->4096 +
  save 255->32 (D355 resync still fires); `GE_WSPROBE_FRONT=120` default
  folder 0 commit 4096 -> save.music=32. All runs end with the orderly
  `video: exiting (render parked ...)` quit.
- Build: full reconfigure + 248/248 targets + link clean
  (`build-pc-cmd.bat`; the pi agent shell drops MSYS env at the
  msys-2.0.dll boundary, so the build goes through a `cmd.exe` wrapper with
  the MSYS2 toolchain on PATH).

**Residuals.** By-ear confirmation of the in-stage Music slider is owed
(D355 territory, unchanged by this entry). The hidden N64-watch rows
(`Bond.Look`, `Bond.AimControl`) are still reachable by hand-editing the
save file, exactly as on N64 -- only the menu surface is gone
(D181/D216/D304 pattern).
