# D357 — Settings thresholds & values: align display units/defaults with the PD port + Turok PC

Status: **IMPLEMENTED 2026-09-30 (findings D443) for mouse sens ×, deadzone whole-%, volume step, FOV degrees, MSAA 16×, Restart game; texture filter unchanged by decision; frame-rate cap deferred (§0 presets + custom = separate project). Approved with changes; see §0.** Proposal dated 2026-09-27. Baselines: the PD port
checkout (`<repos>\pd_port`, `port/src/optionsmenu.c` +
`input.c`/`video.c` config registrations), the Turok PC (Nightdive) list from
the maintainer's notes (`docs/dev/notes/OPTIONS-MENU-PLAN.md` §6 — *taken from
user notes, not checked independently*), and the N64 watch (ground truth for
the `Bond.*` rows only). Triggered by maintainer report: the current values
are "a bit confusing".

Companion change landed with this doc's review: the terminology settled on
**"Profile"** as the end-user term for a per-player game file -- the
front top row reads **Profile**, the per-file row tag reads
**"(per profile)"** and the F10 unavailable value reads **"Select a
profile"** (§4).

## 0. Sign-off (2026-09-30) — these override §3 where they differ

- **FOV:** display in **degrees** (the PC standard). Storage stays unchanged.
- **Frame rate cap:** presets **30 / 60 / 90 / 120 / 144 / 240 / Uncapped**,
  **plus** an arbitrary custom value. This keeps the ini's free range and
  adds a custom entry in the menu, instead of the plan's presets-only grid.
- **MSAA:** add **16×**.
- **Mouse invert:** stays a separate On/Off toggle.
- **Texture filter: do NOT rename.** The §3 premise ("3-point *is*
  trilinear") is wrong. 3-point is the N64's own 3-sample bilinear
  approximation, and "Linear" and "Bilinear" name the same thing. Keep
  "N64 3-point" and check the current enum strings before touching them.
- **Refresh §3's "Now" column before implementing.** The stick deadzone is
  already split L/R, and the draw/LOD defaults are 250, not 150.
- Related QoL, tracked separately in ROADMAP §5a: a **Restart game** action
  next to Quit to desktop, for restart-flagged rows such as MSAA.

## 1. Problem

The D356 regroup fixed the *layout*; the *values* still mix three unit
families with no consistent rule:

- **raw internal units shown as-is**: `Input.MouseSensitivity` (1–500, 100 =
  1× — the "100" is a % of a 1.0 multiplier, not a count),
  `Input.PadDeadzone` (raw 0–30000, `dispDiv=300` → "23%" but the step of 500
  raw is 1.67 display-%, so the slider lands on 23, 25, 26…),
  `Bond.Music`/`Bond.FX` (0–32767, `dispDiv=328`, step 128 = 0.39 display-%);
- **N64-era vocabulary on PC rows**: texture filter "Nearest / Bilinear /
  **3-Point**" (the N64 watch term for trilinear), "FOV scale %" instead of
  degrees;
- **defaults that don't read as defaults**: MSAA shows "4x" fine, but
  `FpsCap` 0–1000 with step 1 invites 117 FPS; `Video.LodDistance` default
  150% vs `DrawDistance` 150% with a 25-step grid (100/125/150/175…).

The PD port is the house style to copy: per-stick sensitivity as a **±10
multiplier (default 1.0, negatives invert)** (`input.c:1537`), mouse speed
**-30..30 (default 2.5)**, deadzone **raw 0–32767 per stick+axis**, rumble
**0–1**, MSAA as a **2x/4x/8x/16x dropdown**, frame cap as **"N FPS"**,
volumes as 0–100 %. Turok's (user's list) conventions: single mouse
sensitivity knob, per-section resets (landed, D356), master/sound/music
volume trio (master still M3-backlog).

## 2. Principles

1. **Display, don't rescale the world.** Every change is port-only (row
   table, `dispDiv`/step/`unit` fields, value-text, `kResetDefaults`): no
   `src/game` change, and config *storage units* are kept wherever a change
   would migrate existing ini files (principle 3).
2. **Human units in the value column**: `%` on a 0–100 grid, `×` multipliers
   with 1.0 as the default, `N FPS`, `Off/2x/4x/8x`, `Unlimited`. A slider
   step must map to a **whole display unit** (the deadzone 1.67%-step bug is
   the canonical example to fix).
3. **Ini stability**: renaming a config key's storage unit is a migration.
   Where the plan needs it, register a *new* key (e.g.
   `Input.PadDeadzonePct`) and keep the old key as a read-once legacy alias —
   the D333 `Input.AimMode`→`Input.AimStyle` alias pattern (`input.c:1962`).
   Each migration gets a findings note.
4. **`kResetDefaults` stays in lockstep** with the config C initializers
   (D356 Gate E): every default change in this plan updates the table in
   `optionsoverlay.c` in the same commit; the `GE_WSPROBE_RESET` probe
   re-verifies.
5. **Defaults = the config C initializers**, never a new menu-only default.

## 3. Row-by-row audit & proposal

Current state from `optionsoverlay.c` `rows[]` + the `configRegister*`
ranges (2026-09-27). "Display" = what the value column shows today.

### Implemented 2026-09-30 (D443) -- refreshed "Now" vs. result

The per-row tables below keep the 2026-09-27 audit text for history. Actual
pre-change state and what shipped (all port-only; no default or storage change,
so `kResetDefaults` is untouched and `GE_WSPROBE_RESET` stays failures=0):

| Row | Actual "Now" (pre-D443) | Shipped |
|---|---|---|
| Mouse horizontal sensitivity | 1-500 · step 5 · shown as calibrated `N/100` (50/100 at raw 100) | UI range 10-300, step 10, shown `%.1fx` (raw/100), linear bar; left the calibrated mapping |
| Stick deadzone L / R | already split `Input.PadDeadzoneL/R` 0-30000 · step 500 · calibrated `N/100` | step 300 + `dispDiv=300` `%` (whole-% grid, no key migration); left the calibrated mapping |
| Music / FX volume | step 128, `/328` | step 328 (1 display-%) |
| FOV | `Video.FovScale` 50-150 · step 5 · `%` | row "Field of view", horizontal degrees (see D443 formula); key/storage unchanged, step walks the displayed degree |
| Anti-aliasing | 1/2/4/8 · default 2 (C initialiser) | + 16x; range 1-16 (GL clamps to `GL_MAX_SAMPLES`) |
| Texture filter | Nearest / Bilinear / 3-Point | unchanged (decision §0) |
| Frame-rate cap | 30/60 toggle (sim ticks at 60 Hz) | unchanged this round |
| Restart game | -- | new action row after Quit to desktop (both UIs, shared table) |

### INPUT

| Row | Now (default · range · step · display) | PD port | Turok (notes) | Proposal |
|---|---|---|---|---|
| Mouse sensitivity | 100 · 1–500 · 5 · raw `100` | `MouseSpeedX/Y` float −30..30, def 2.5 (sign = invert) | single knob | **Display as a × multiplier**: keep storage (100 = 1.0×), value text `%.1f×` (v/100); range 10–300 (0.1–3.0×), step 10 (0.1×). Label stays "Mouse sensitivity". |
| Invert look (mouse) | Off/On | folded into the −/× sign of MouseSpeed | — | **Keep On/Off** (PD's negative-multiplier style is an alternative; a separate invert is simpler next to the × slider). Decision: confirm. |
| Aim style / Aim range | N64/Centred (PC) · PC/N64 | — (GE-only, D337/D338) | — | Keep as-is (GE-specific; hiddenIfOn already sane). |
| Invert look (controller) | Off/On | sign of stick scale | — | Keep. |
| Stick deadzone | 7000 raw · 0–30000 · 500 · ÷300 → `23%` (1.67%-steps) | raw 0–32767 per stick+axis, `DEFAULT_DEADZONE` | MISSING | **New key `Input.PadDeadzonePct` 0–100, step 1, default 23** (7000/30000×100); `input.c` scales back to raw at apply; old key becomes a legacy alias (principle 3). Display `N%` on a whole-% grid. |
| Trigger threshold | 23 · 1–99 · 1 · `%` | — (GE extra) | — | Keep. |

### GAMEPLAY

| Row | Now | Baseline | Proposal |
|---|---|---|---|
| Auto-aim / Sight / Look ahead / Ammo (profile) | Off/On (N64 watch 0/1) | N64 watch = ground truth | Keep; tag now reads **(profile)** (this doc's companion change). |
| Skip intro / No hit flash / All unlocked | Off/On | Turok: screen-shake/hit-flash family | Keep. |
| HUD scale | 100 · 75–150 · 5 · `%` | — | Keep (whole-% grid already; range cap D226). |

### GRAPHICS

| Row | Now | Baseline | Proposal |
|---|---|---|---|
| Anti-aliasing | 1/2/4/8, def 4, shows `None/2x/4x/8x`, restart | PD dropdown 2x/4x/8x/**16x**, def 4x | **Add 16x to the sequence** (`kMsaaSeq`, range clamp 1–16) — SDL/GL supports it; PD has it. Decision: perf cost on mid GPUs (restart already flagged). |
| Texture filter | Nearest / Bilinear / **3-Point** | PC convention: Nearest/Linear/Bilinear | **Rename the enum names to `Nearest / Linear / Bilinear`** (3-point *is* trilinear; the N64 term is the last one left on a PC row). Storage 0/1/2 unchanged. |
| Anisotropic filtering | 4 · 1–16 · 1 · `x` | Turok 1–16 | Keep; **dim/disable when filter = Nearest** (anisotropy only applies to filtered sampling) — `hiddenIfOff`-style guard, UI-only. |
| FOV scale | 100 · 50–150 · 5 · `%` | Turok 3: 60–120° slider | **Display in degrees**: the 100% baseline is the N64 vertical FOV scaled to the port's widescreen math — value text `N°` (v/100 × baseline, 1 decimal); storage unchanged. Decision: degrees vs % (Turok uses degrees; % is the existing mental model — pick one, recommend degrees). |
| Native widescreen / Widescreen auto FOV / Crop overscan | Off/On | — | Keep. |
| Draw distance | 150 · 100–400 · 25 · `%` (hidden under auto-FOV) | Turok: draw-distance row | Keep the % grid (25% steps read fine); **step 25 → display already whole-%**. No change. |
| LOD distance | 150 · 25–400 · 25 · `%` | — | Keep; note default 150 vs DrawDistance 150 is intentional parity. |

### AUDIO

| Row | Now | Baseline | Proposal |
|---|---|---|---|
| Music volume (profile) | 32767 · 0–32767 · 128 · ÷328 → 0.39%-steps | Turok: 0–100% | **step 128 → 328** (exactly 1 display-%; 32767/328 = 99.9 ≈ "100%"). Storage unchanged (N64 `VOLUME_MAX` semantics). |
| FX volume (profile) | same | same | same. |
| *(master volume)* | — | Turok trio | M3-backlog, out of scope (shape the section for it, D356 note). |

### VIDEO

| Row | Now | Baseline | Proposal |
|---|---|---|---|
| Fullscreen | Off/On | Turok: window mode (borderless missing) | Keep; borderless = separate backlog item. |
| Resolution | action row (presets) | both | Keep. |
| VSync | On/Off | both | Keep. |
| Frame rate cap | 60 · 0–1000 · 1 · `Uncapped/N FPS` | Turok: "up to 120 FPS" / uncapped | **Preset grid**: 0/30/60/120/144 (step = next preset, wraps to Uncapped); value text already `N FPS`/`Uncapped`. Decision: preset grid vs free 1-step (recommend presets; 117 FPS has no purpose). |
| Show FPS | Off/On | both | Keep. |

## 4. Terminology (end-user term = "Profile")

Decision (2026-09-27, UX review): the end-user term for a per-player
game file is **"Profile"** — the per-player bundle of progress +
per-file settings. Rejected: **"game file"** (the instruction manual's
current term) reads, in modern PC usage, as the *program's* own files
("Verify integrity of game files"), not the user's save; **"Save"** was
the fallback (universally understood) but reads as a verb in the row tag
and as progress-only, which these files are not. "Profile" covers both
and fits the dossier styling of the screen.

Final string set (both UIs: front options + F10 overlay):

| String | Where |
|---|---|
| **"Profile"** + value `N` | front options top row (was "Save file: File N") |
| **"(Profile N)"** / **"(none)"** | section-title annotation on save-scoped sections (was "(File N)"/"(no file)") |
| **"(per profile)"** | per-file row tag (was "(save)", then "(profile)", then "(game file)") |
| **"Select a profile"** | F10 unavailable value (was "Select file") |

**Manual sync TODO:** the instruction manual's "game file" should be
updated to "profile" (the manual is a port document we own).

"Bond file" remains in code identifiers/config keys (`Bond.*` rows,
`watchSettingsChooseFile`, ini keys) only -- never user-visible.

## 5. Gates

1. Port-only per principle 1; the deadzone key migration (principle 3) is
   the only config-surface change and follows the D333 alias pattern.
2. `kResetDefaults` updated in the same commit as every default/range
   change; `GE_WSPROBE_RESET` (front + `-level_33`) re-run, all sections
   `failures=0`.
3. Build via `build-pc-cmd.bat` (the pi-shell MSYS env gap, D356 note).
4. Visual check owed (maintainer): front options screen + F10 in all five
   sections; the overlap fix from the same session (the level-1 save-file
   row no longer paints under the first content row).

## 6. Out of scope

- Key rebinding (Phase 4), master volume (M3), output-device selection,
  borderless window, per-axis controller sensitivity (PD-style four-knob —
  GE's one-knob design is D238/D304).
- Any `src/game` change; the N64 watch keeps its own values and wording.
