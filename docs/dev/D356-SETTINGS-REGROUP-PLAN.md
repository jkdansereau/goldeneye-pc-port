# D356 — Settings regroup: functional sections, save-file scoping, per-section reset

Status: **LANDED (D356, 2026-09-27, port-only)** — signed off and implemented
this session; verification in `docs/dev/findings.md` D356 (GE_WSPROBE_RESET
front + in-stage probes, regressions, build). Supersedes the "BOND FILE"
section introduced in D353. Baselines: Turok PC port options (Nightdive, list
provided by the maintainer), the PD port's `port/src/optionsmenu.c`
(`<repos>\pd_port`), and plan §6 of
`docs/dev/OPTIONS-MENU-PLAN.md` (the original "align with Turok" note).

## 1. Goal

The D353 "BOND FILE" section is the one section named after *where a value is
saved* — an implementation detail — while every other section is functional
(DISPLAY, GRAPHICS, MOUSE/AIM, CONTROLLER, GAME). Its contents are also a
mishmash (aim/look toggles + HUD toggles + audio), and it exposes N64-watch
rows that **collide with or duplicate** the PC-native rows the port added
(watch look-invert vs `Input.MouseInvertY`/`Input.PadLookInvertY`; watch aim
control vs `Input.AimMode`). The baseline ports (Turok, PD) never name a
section after the save and never re-expose legacy overrides.

D356 rewrites the row table so that:

1. Every section is functional, using the baseline names:
   **INPUT, GAMEPLAY, GRAPHICS, AUDIO, VIDEO**.
2. Save-file scope is communicated the PD way — *in the section title*
   ("GAMEPLAY (File 1)"), generalized from the D353 BOND-FILE annotation —
   plus a single **Save file: File N [change]** row at the top of the
   options screen (PD "Player N" pattern; D354's default resolution applies).
   "Bond file" appears nowhere in the UI.
3. Redundant/colliding watch rows are removed from the **menu surface only**
   (the established D181/D216/D304 pattern): keys, in-game watch behavior,
   and ini hand-editing all remain untouched. No game-code change (rule 2).
4. A **Reset to defaults** action row per section (Turok's per-category
   reset; plan §6 Gate E, made concrete) with a **confirmation contract**:
   edge-triggered activation (no auto-repeat), two-step arm→confirm, never
   repeatable (WATCH-SETTINGS-PLAN §6 gate: "separate confirmation and
   persistence policy before wiring a button" — §5.4 below is that policy).
5. A **context-aware file accessor** (front-end target vs active stage
   target) with the "none" chooser position retired, so "Save file: File N"
   is always a true statement (§5.5).
6. Section resets iterate the section's **declared** rows (including
   conditionally hidden ones, §5.6) and reset verification exercises the UI
   action itself, headlessly (§5.8).

## 2. Non-goals

- No `src/game` changes; the N64 watch menu keeps all its options and its
  N64 wording (ground truth).
- No key rebinding (Phase 4) — but the section layout leaves room for a
  "KEY BINDINGS" page exactly where Turok/PD have one.
- Master volume stays M3-backlog; the AUDIO section is shaped for it
  (Turok's Master/Sound/Music trio).
- N64 build untouched.

## 3. Exposure filter (menu surface only)

| Row (key) | Verdict | Reason |
|---|---|---|
| `Bond.Look` ("Look up/down (watch; stacks)") | **Hide** | Duplicates `Input.MouseInvertY` + `Input.PadLookInvertY`; stacking both produces the double-negation trap. |
| `Bond.AimControl` ("Aim control") | **Hide** | Collides with `Input.AimMode` (N64 vs PC aim model) — two rows fighting over the same domain. |
| `Bond.AutoAim` | Keep | Unique gameplay assist; orthogonal to aim style/range. |
| `Bond.LookAhead` | Keep | Unique; no PC counterpart, no conflict. |
| `Bond.Sight`, `Bond.Ammo` | Keep | Unique HUD-visibility options; compose with `Game.HudScale`. |
| `Bond.Music`, `Bond.FX` | Keep | Core of D350–D355. |

Hidden rows are commented out in `rows[]` with a D356 note (D181/D216 style:
config keys + watch path stay live; ini power users can still set them).

## 4. New layout (5 pages + save-file row)

`MAX_PAGES=8`, `MAX_PROWS=15` — all pages below the caps (D353 overflow log
still guards).

Top row (drawn by `frontoptions.c` above the paged content, not paged):
**`Save file: File 1 [change]`** — cycles files via `watchSettingsChooseFile`
(D354 default resolution: explicit pick > viewed folder > first valid;
auto-init fallback). Hidden in F10 (existing `optionsRowIsBondChooser`
mechanism, generalized). F10 targets the active file, as today.

| Page | Rows (key · kind) | Title annotation |
|---|---|---|
| **INPUT** | `Input.MouseSensitivity` · `Input.MouseInvertY` · `Input.AimMode` · `Input.AimRange` · `Input.PadLookInvertY` · `Input.PadDeadzone` · `Input.PadTriggerPct` · *Reset to defaults* | |
| **GAMEPLAY** (File N) | `Bond.AutoAim` · `Bond.LookAhead` · `Bond.Sight` · `Bond.Ammo` *(save, dim "(save)" tag)* · `Game.SkipIntro` · `Game.NoHitFlash` · `Game.AllUnlocked` · `Game.HudScale` · *Reset to defaults* · `__QuitToDesktop` | (File 1) |
| **GRAPHICS** | 11 rows unchanged from D353 · *Reset to defaults* | |
| **AUDIO** (File N) | `Bond.Music` · `Bond.FX` *(save; title annotation only — homogeneous section, PD-style)* · (master volume, M3) · *Reset to defaults* | (File 1) |
| **VIDEO** | `Video.Fullscreen` · `__Resolution` · `Video.VSync` · `Video.FpsCap` · `Video.DisplayFPS` · *Reset to defaults* | |

Moves from D353 layout: mouse/aim + controller rows merge into **INPUT**
(the two old headers `__HdrMouse`/`__HdrPad` are dropped); DISPLAY → **VIDEO**
(rename, `Video.DisplayFPS` moves from GAME); GAME section is renamed
**GAMEPLAY** and absorbs the surviving watch toggles; the `__HdrBond` header
and `__BondFile` chooser row are retired (replaced by the top save-file row);
`Bond.Look`/`Bond.AimControl` rows are commented out (§3).

Scope annotation: the D353 "(File N)"/"(no file)" title annotation — today
special-cased to the BOND FILE page via `optionsRowIsBondChooser()` — is
generalized: **any page containing per-file rows annotates**. Sections with
per-file rows: GAMEPLAY, AUDIO.

F10: same table (F10 already shows all rows, chooser hidden). The
(Gameplay/Audio) annotation in-stage shows the active file number (the
context-aware accessor, §5.8 — *not* the raw chooser value). "Reset to
defaults" rows stay visible in F10 (O2) under the same confirmation
contract (§5.4).

**(save) tag (O1):** the four mixed-scope GAMEPLAY save rows carry a subtle
dim "(save)" suffix — the only place a section mixes scopes. AUDIO is
homogeneously per-file, so its title annotation is sufficient (no tags).
Implementation: one `int saveScoped` field on `struct Row` + dim-ink suffix in
the row-draw path (frontoptions.c already draws the dim annotation).

## 5. Code changes

All in `port/` (D350-family); files: `port/src/optionsoverlay.c`,
`port/src/frontoptions.c`, `port/include/optionsoverlay.h`,
`port/src/watchsettings.c` (only if reset needs a new enqueue helper —
expected not to, see below).

### 5.1 Row table (`optionsoverlay.c`)

- Rename headers: `__HdrDisplay`→VIDEO (last page), add `__HdrInput`
  (INPUT), `__HdrGameplay` (GAMEPLAY); delete `__HdrBond`, `__HdrMouse`,
  `__HdrPad`, and rename `__HdrGame`→GAMEPLAY. New section order in the
  table: INPUT, GAMEPLAY, GRAPHICS, AUDIO, VIDEO (Turok's order: Input,
  Gameplay, Graphics, Audio, Video).
- Move the 7 mouse/aim + 3 controller rows under INPUT; move the 4 surviving
  `Bond.*` toggles + music/FX into GAMEPLAY/AUDIO; comment out
  `Bond.Look`/`Bond.AimControl` (§3).
- New `ROW_ACTION` rows, one per section: `.key="__Reset<Section>"`,
  `.label="Reset to defaults"`. All five use one shared handler
  (`rowResetSection(section)`).

### 5.2 Save-file top row (`frontoptions.c`)

- New non-paged line above the page content: "Save file: File N" + a
  "change" control (left/right or Enter cycles, reusing
  `watchSettingsChooseFile(dir)`; D354 lazy default fills N). Dim ink, same
  visual language as the D353 annotation. `selected_folder_num` unchanged —
  the row never touches the game's active file (plan Gate B invariant).
  **Display value reads through the context-aware accessor (§5.5), never
  the raw chooser value.**
- **Unavailable state (maintainer consistency fix):** the D354 auto-init
  fallback creates the blank file on a *later game tick*, so at an
  empty/corrupt-save startup the accessor can return -1 for a few ticks.
  The row then shows the unavailable state ("(no file)"), never a file
  number the write path cannot use. Once creation succeeds the same tick
  the row shows "File 1".
- F10 context: row suppressed (extend the D353 accessor to cover it).

### 5.3 Reset to defaults (Gate E)

- **Per-file rows** (GAMEPLAY/AUDIO save rows): defaults = **BLANKSAVEDATA**
  (`src/game/file2.c` — the game's own factory save; per WATCH-SETTINGS-PLAN
  §6, watch rows reset to BLANKSAVEDATA/`DEFAULT_OPTIONS` for the selected
  file, *not* from a global ini snapshot; new saves start Music/FX 0xFF,
  auto-aim/sight/look-ahead/ammo enabled — read the fields from
  `BLANKSAVEDATA` at reset time, no hardcoded table). Reset re-applies live
  via the existing `watchsettings` apply path **and** persists field-scoped
  to the selected file (`watchSettingsPersistField`), exactly like a normal
  commit. In-stage: enqueued through the D352 command queue (N field commits
  with `commit=1`) — no new threading; game thread applies + persists.
- **Ini rows** (INPUT/GRAPHICS/VIDEO/GAMEPLAY-ini): `config.c` has no
  central default table (first-write "defaults" = current values,
  config.c:280), so the section handler uses an explicit
  `static const struct { const char *key; double def; }` reset table in
  `optionsoverlay.c`, mirroring the port's C initializers (each entry
  commented with its source variable; **verified against the initializer at
  implementation time** — e.g. `Game.AllUnlocked` = **0**,
  `port/src/video.c:116,348`; the D257 comment in optionsoverlay.c that
  says "default ON" is stale and is corrected as part of D356). Front-end:
  write live + `configSave()` (existing). In-stage ini resets: applied
  **immediately** (O3 — consistent with how every other ini row already
  behaves when changed in F10; video.c applies most display changes on the
  fly).
- **Resolution is excluded from the VIDEO reset (maintainer item 2).**
  `__Resolution` is an action backed by `s_resSel` +
  `videoRequestWindowSize()` — not a registered config row, so the numeric
  default table cannot reset it, and a "default window size" is a
  display-capability choice, not a tunable. The D356 findings entry
  documents the exception explicitly: VIDEO reset covers Fullscreen, VSync,
  FpsCap, DisplayFPS — Resolution keeps the player's current choice.
- **Scope-aware by construction:** a section's reset touches each row
  **by its own scope** (file rows → that file's BLANKSAVEDATA values;
  ini rows → ini defaults). No cross-scope surprise (Gate E: "do not make
  a global reset wipe per-file watch settings" — a reset of the selected
  file never touches other files; §5.8 verifies this).
- **Activation contract (§5.4) applies to every section's reset row.**

### 5.4 Reset activation contract (maintainer item 1)

A reset is a *destructive bulk write* — GAMEPLAY's in particular changes
both ini values **and** the selected save in one action. WATCH-SETTINGS-PLAN
§6 gates confirmation + persistence policy; this is that policy:

- **Edge-triggered:** reset actions fire only on a *fresh* event. The
  frontoptions left/right key repeat (held keys re-issue the action) is
  suppressed for `ROW_ACTION` reset rows — the arming/confirm events must
  carry a no-repeat flag (SDL keydown with `repeat==0` / a fresh click),
  so a held key or held mouse button can never arm *and* confirm, nor
  re-trigger.
- **Two-step confirm:** first activation **arms** the row (label swaps to
  "Confirm reset…", armed highlight); a second activation within **3 s**
  commits. Timeout, or navigating to another row, disarms (label
  restores). Arm state is per-row, per-open; opening/closing F10 or the
  options screen clears all armed resets.
- **One commit per confirmed activation.** No auto-repeat, no re-arming on
  the same event. F10 and the options screen share the identical contract
  (O2: rows stay visible in F10; confirmation makes accidental in-game
  resets unlikely).
- The confirmed action then dispatches the section handler (§5.3) exactly
  once.

### 5.5 Context-aware file accessor; retire "none" (maintainer item 3)

`watchSettingsFolder()` returns the raw `chosen`, which may be -1 before
`frontFolder()` lazily resolves, and in-stage `chosen` can differ from the
active file (`snapFolder`). The generalized title/top-row accessor must be
**context-aware**, not a repurposed chooser-value read:

- New `watchSettingsActiveFolder(void)` (name tentative):
  - **Front-end contexts** (options screen, F10-on-file-select, title):
    the *resolved front target* — explicit `chosen` if set, else
    `selected_folder_num` if valid, else first valid folder (i.e. the
    `frontFolder()` resolution; the accessor is pure-read and does not
    mutate `chosen` — lazy mutation stays in `frontFolder()`/the
    auto-init fallback).
  - **In-stage (F10):** the *active stage target* — `selected_folder_num`
    (the file the stage loaded, `snapFolder` at apply time).
  - The D353 title annotation and the §5.2 top row both read through this
    accessor, in both surfaces.
- **The chooser's "none" position is retired.** `watchSettingsChooseFile`
  cycles FOLDER1..MAX only (dropping the -1 stop). Rationale: D354's
  default resolution + boot-time BLANKSAVEDATA slots + the auto-init
  fallback guarantee a valid target, so "none" was only reachable by
  deliberate cycling and serves no remaining purpose — and "Save file:
  File N" (the §5.2 invariant) is not true when `chosen == -1`.
  `watchSettingsFolder()`'s "-1 = none" contract is updated/superseded by
  the new accessor.

### 5.6 Reset iterates declared rows (maintainer item 4)

The section handler iterates the section's **declared row range in
`rows[]`** (from its header to the next header), *not* `s_visIdx`/the
currently visible list — conditionally hidden rows (GRAPHICS draw/LOD
sliders while their auto-FOV toggles are on) must be reset too, or they
silently survive a purported section reset. Hidden-row defaults come from
the same default table; their *live* values are written directly (the
`ptr`/`rowSet` path), visibility is irrelevant. The `(save)` tag, hidden-row
resets, and the reset table are all keyed off `rows[]` indices, never the
visible list.

### 5.7 F10

Unchanged machinery: shared table, save-file row suppressed (the D353
accessor pattern extended to the §5.2 top row). Annotation shows the active
file via §5.5. "Reset to defaults" rows stay **visible** in F10 (O2) under
the §5.4 confirmation contract.

### 5.8 Reset verification probe (maintainer item 5)

New env-gated probe **`GE_WSPROBE_RESET=<tick>`** (GE_D314/GE_WSPROBE
pattern): at tick N of a *front-end* boot it drives the **UI action path**
through the same handler the confirm step calls (arm → confirm → dispatch,
no bypass). **Every section being verified is itself dispatched:** the
probe dispatches all five section resets (INPUT, GAMEPLAY, GRAPHICS, AUDIO,
VIDEO) via arm → confirm, and after *each* dispatch verifies that
section's rows:

- **INPUT / GRAPHICS / VIDEO (ini rows):** values == the default table.
  GRAPHICS specifically covers the **hidden** draw/LOD sliders (hidden
  while their auto-FOV toggles are on) — a section reset must land them
  even while they are not visible; VIDEO's `__Resolution` is the documented
  exclusion (unchanged, logged).
- **GAMEPLAY (mixed scope):** the four save rows == BLANKSAVEDATA
  (auto-aim/sight/look-ahead/ammo) *and* the ini rows (SkipIntro/
  NoHitFlash/AllUnlocked/HudScale) == the ini default table — the two
  scopes land independently in one action.
- **AUDIO (save rows):** Music/FX == BLANKSAVEDATA (0xFF raw = 32767,
  the `VOLUME_MAX` endpoint), not the probe's pre-dirtied 4096.
- **Scope isolation (after all dispatches):** a *second* folder's
  `save_data` is byte-identical to its pre-reset snapshot (a reset of the
  selected file never touches other files).

Front-end prep (tick N) first dirties live values (Music/FX → 4096,
VSync/HudScale → non-defaults) and snapshots the second folder, so the
verifications at tick N+1 prove the resets actually moved values back.

In-stage dispatch is covered by the same env var on a `-level_XX` run:
arm → confirm at tick N (file rows enqueue through the D352 queue — the
probe logs the enqueued command count), drain at N+1, then the same
per-section value verification against the post-apply state.

### 5.9 No changes

- `buildPages` in `frontoptions.c` (generic; 5 pages < MAX_PAGES),
  `watchSettingsGameTick`/queue (D352/D354/D355 logic untouched), the D355
  track-1 resync, `BLANKSAVEDATA` itself.

## 6. Threading / persistence gates (unchanged, restated)

- D350: no GE writes from the F10 input/render thread except the sanctioned
  front-end commit path; in-stage edits queue to the game thread.
- D352: queue holds across transitions; per-field coalescing.
- D354: front contexts (including F10-on-file-select) route through the
  chooser path; `frontFolder()` default resolution; auto-init fallback.
- D355: Music commit + level-entry resync of the BGM (track 1) player.
- D356 adds no new write path: reset = N existing commits (file rows) +
  existing ini writes (ini rows).

## 7. Docs

- `docs/dev/findings.md` D356 entry + §F index row; regenerate
  `findings-index.csv`.
- `docs/dev/WATCH-SETTINGS-PLAN.md`: status + D353/D354 "Bond File" section
  references updated; Gate E marked landed (subset: per-section rows).
- `docs/dev/OPTIONS-MENU-PLAN.md` (tracked) + local notes: §6 "align with
  Turok" annotated as landed via D356 (M3 master volume + key bindings
  remain).

## 8. Verification

1. Clean NTSC build (`./build-pc.sh` / `cmake --build build-pc`).
2. Headless smoke: boots to front end; log page count (5, under MAX_PAGES),
   no `buildPages` overflow warning, save-file row resolves to File 1 by
   default (D354), annotation on GAMEPLAY/AUDIO pages, no "none" reachable
   (chooser cycles files only, §5.5).
3. `GE_WSPROBE` / `GE_WSPROBE_FRONT` regression: identical pass as D355
   (queue/commit paths untouched).
4. **`GE_WSPROBE_RESET` (§5.8) exercises the UI action, not just direct
   commits:** per-section arm → confirm → dispatch of all five sections,
   with each section's rows verified right after its own dispatch
   (ini == table, save == BLANKSAVEDATA, hidden GRAPHICS rows included,
   VIDEO resolution untouched), then the second-folder byte-identity
   check. In-stage dispatch: `GE_WSPROBE`-style queue check on a
   `-level_XX` run (enqueued command count + post-apply state).
5. **Confirmation contract check (§5.4):** a held key/click arms but never
   confirms (edge-triggered); second activation within 3 s commits exactly
   once; timeout/navigate disarms. Headlessly assertable via the probe
   (arm-then-timeout leaves values untouched).
6. Manual visual pass (owed, same list as D353): arrows through 5 pages,
   save-file cycling updates the annotation live, `(save)` tag on the four
   GAMEPLAY save rows, F10 in-stage, reset in stage vs front (including the
   armed/confirm visual state), rapid slider drag.

## 9. Open questions — RESOLVED (maintainer sign-off, 2026-09-27)

- **O1:** **yes** — subtle dim "(save)" tag on the four mixed-scope
  GAMEPLAY rows; AUDIO's title annotation alone is sufficient (homogeneous
  section).
- **O2:** **visible in F10**, provided resets require the §5.4 confirmation
  step (which they do).
- **O3:** **immediate** in-stage ini resets, consistent with existing ini
  edits in F10.
- **O4:** `__QuitToDesktop` stays in GAMEPLAY, **literally last — after the
  Reset row** (section order: rows…, *Reset to defaults*, *Quit to desktop*;
  the §4 page table is corrected accordingly).

Layout itself is signed off by the maintainer (2026-09-27): five-page
regroup + menu-only exposure filter, with the §5.4–§5.8 contracts as
specified above.

## 10. Estimate

One commit. ~5 files (`optionsoverlay.c`, `frontoptions.c`,
`watchsettings.c`, `port/include/optionsoverlay.h`,
`port/include/watchsettings.h`), net +400/−140 (table reshuffle + reset
contract + context-aware accessor + `GE_WSPROBE_RESET` are the new code).
Findings/csv/plan updates ride along.
