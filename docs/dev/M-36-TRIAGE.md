# M-36 — graphics / QA bug triage sweep

Branch `qol/native-pc-input-menu` (local). Goal: sort the open
`GRAPHICS-BACKLOG.md` + QA-report bugs into **root-cause families** so a
fix hits several at once, and correct one mis-attribution.

Method: headless `GE_PCDUMP` captures (`-level_36` Surface, `GE_STARTMENU=10`
briefing) + static read of the implicated port code. Frames in
`scratchpad/` this session; not committed (auto window size = 1957×1468,
can't framediff — visual only).

---

## Headline findings

### 1. The "comb interlacing" is NOT the D159 family

`GRAPHICS-BACKLOG.md` attributes D176(b) and D182(2) to `texSwapAltRowBytes`
"recurring on re-import". **That function is a full `#ifdef PORT` no-op** —
`src/game/image.c:2199` `return;` before any work. It cannot be the cause of
anything. The mis-attribution is now corrected in the backlog.

The real defect: `port/fast3d/gfx_pc.cpp` `import_texture_*` assume
`full_image_line_size_bytes == line_size_bytes` (explicit `SUPPORT_CHECK`
on rgba32/ia4/ia8/ia16/…; the rgba16 one is commented out with
*"this trips in some places with a garbage size in
full_image_line_size_bytes"*, `gfx_pc.cpp:658`). When a tile's `line`
(SETTILE pitch) differs from the source image row width — non-power-of-two
textures, or a sub-rect tile load — the importer does a flat linear read and
every row walks off by a few bytes → **diagonal shear that reads as grey
static**. Matches the Surface cliff/tree walls (`-level_36` frame 300–900:
structured rock texture tiling but each tile sheared to noise) and the
file-select spiral after re-entry.

Open question: why does D182 only scramble **after** navigating away and
back? Either the first import happens to have `line == width` and the
re-import doesn't, or the tile descriptor is stale on the second visit.

### 2. Briefing objectives are BLANK, not wrong

`GE_STARTMENU=10` briefing (Dam): header renders perfectly ("OHMSS / Agent:
James Bond / Mission 1: Arkangelsk / Part i: Dam / PRIMARY OBJECTIVES:").
Under it: just the bullet letter **"a."** with **no objective text**, and no
b/c/d lines. So D178 is the **D143/D151 blank-text family** (`langGet()`
returns NULL for the objective string slot → text silently blank), *not* a
propDef walk-desync or difficulty-decode problem. Fix = find which lang bank
the front-end briefing flow fails to load/map.

Caveat: this repro used the synthetic `GE_STARTMENU` boot (skips real
save/unlock state). The user's D178 report is from a live playthrough, and
D143 independently documents blank briefing text, so it is a real defect —
but re-confirm the exact "a." + blank shape in a real briefing.

---

## Family table

| Family | Bugs | Root cause (hypothesis) | Files | ROI |
|---|---|---|---|---|
| **A — fast3d texture line/pitch shear** | D176(b) Surface walls; D182(2) file-select bg | **HYPOTHESIS DISPROVEN (D183, `4b71435d`).** 0/166 texture loads on `-level_36` are strided; the wall texture is a 32×32 IA8 whose ROM bytes are genuinely a noise field, decoded correctly. A golden-safe de-stride + `GE_TEXRAW`/`GE_DTEX` diagnostics landed anyway (latent hardening). **D176(b)/D182(2) still OPEN** — next: ROM ground-truth decode of the wall texnum (`TEXTURE-GLITCH-ANALYSIS.md` §7) → likely a tiling-density / UV-scale / `shifts`/`shiftt`-LOD issue (RC3/D167 family) or a wrong-texture bind, not a decode bug. | `port/fast3d/gfx_pc.cpp` | was HIGH, **now needs re-scoping** |
| **B — front-end string bank not loaded** | D178 briefing objectives blank; D143 briefing text blank | **FIXED (D178, `cd1ed574`).** Not a bank-load problem — the `Ubrief*Z` segment is a raw 48-byte BE ROM image loaded with no converter; all 24 `u16` (string ids + difficulty gates) read byte-swapped on LE → `langGet()` NULL → blank, and `>=` difficulty gate always-false → objectives vanish. `romdataFixupBriefing()` swaps in place. Verified: all 4 Dam objectives render at 00 Agent with correct gating. **D143 was a symptom of this** — annotate it superseded. D151 (watch objective text) still owed a re-verify. | `port/src/romdata.c`, `src/game/front.c` (`#ifdef PORT`) | **DONE** |
| **C — D75 front-end / cutscene 3D models** | D75 Nintendo logo; D148 Dam rappel cutscene; D149 MISSION COMPLETE / mode-select models; D182(1) file-select Bond image "renders once"; D173 3rd-person Bond floats; 1P weapon viewmodel absent | mixed: compiled sub-DL corruption (D144/D146), model transform/geometry, `render_pos` arena lifetime, constructor-runs-once | `port/fast3d/gfx_pc.cpp`, `title.c`, `bondview*.c`, model path | **LOW** per fix, deep. **DEFER** (agreed COA). |
| **D — blood / particle** | D172 spray magenta/cyan; D174 no blood decals | D172: CC-mode / texture-format decode in the fast3d sprite path. D174: likely D120 `PointUsage[]` — **only takes effect after a full sidecar regen** (`d43 && d69 && d88 --regen`); verify that first. | `blood_animation.c`, fast3d sprite path; `tools_pc/d43_emit.py` | **MEDIUM**. D174 may already be fixed — regen + BUNKER1 firefight to confirm before touching code. |
| **E — transient hang / anim speed / clip** | D175 door-open stutter (Runway/Surface); D170 NPC flee runs not walks; D171 Silo grey-triangle sweep | D175: matches D155/D156/D134/D147 known classes (benign texture-import spike most likely). D170: D155/D156 wall-clock `deltaFrames` anim speed-up, or just scripted. D171: near-plane clip fan (D106 family). | frametiming clamp, `chrTick`/AI, portal near-plane | **LOW–MED** — needs interactive repro + N64 reference footage. **DEFER**. |
| **F — misc cosmetic** | D74 dead wrap-block (latent, harmless); D76 disclaimer partial (D164 fix owed an eyeball); D77 audio (Phase 3); **D176(a) black sky** | D176(a): sky DL not emitting / wrong transform — **separate from D176(b)**, own investigation (could be C-family or a sky-pass issue). | various | **LOW**, opportunistic. |

---

## Results (2 subagents, integrated + verified on branch HEAD)

- **Family B → FIXED** (`cd1ed574`). Byte-swapped raw briefing segment. Also
  explains D143. High confidence.
- **Family A → hypothesis disproven** (`4b71435d`, D183). D176(b) is not a
  texture decode bug. Golden-safe de-stride + diagnostics shipped for the
  latent case. **D176(b) / D182(2) remain open and need re-scoping** — start
  from the ROM ground-truth decode of the wall texnum; suspect
  tiling-density / UV / per-LOD `shifts`/`shiftt` (RC3/D167 family) or a
  wrong-texture bind.

Harness note: both agent worktrees were created from `def0e0ac` (the M-35
branch point, ~5 commits stale — the M-31 stale-worktree bug again). No harm
this round: the integrator re-applied both patches onto branch HEAD and
verified there. Check `git worktree list` HEADs on the next burst.

## Next

1. **D176(b) re-scope** — offline decode the Surface wall texnum from ROM
   (`TEXTURE-GLITCH-ANALYSIS.md` §7). Structured rock in ROM → wrong bind or
   UV/tiling; noise in ROM → not our bug (or a shift-LOD density issue).
2. **D176(a)** black sky — untouched, separate investigation.
3. **D182** — re-test the file-select round-trip now that `GE_TEXPITCH` /
   `GE_TEXRAW` exist; the "renders once" half (D182-1) is Family C.
4. Reassess C/D/E with the user. D174 (blood) is the cheapest — just needs a
   sidecar regen + BUNKER1 firefight to confirm the D120 fix.
