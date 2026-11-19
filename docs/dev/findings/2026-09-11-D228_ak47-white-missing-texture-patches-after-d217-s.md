# D228

<!-- from the old section-F/H table row -->
| D228 | **AK47 white missing-texture patches after D217's texpool fix — separate failure mode (M-97, `-level_34` FACILITY).** — full `## D228` entries at file tail | **FIXED (`port/fast3d/gfx_pc.cpp`, `palette_to_rgba32`)** — byte-order bug in the G_TT_IA16 branch; user-verified live. Fix write-up: second `## D228` section. |

## D228 — M-97: AK47 white missing-texture patches survive D217's texpool-alignment fix — split out as a distinct defect (white ≠ wrong-colour)

**Method.** PR #47's branch (`fix/d217-texpool-alignment`) merged up to date
with `main`, rebuilt clean, launched directly into FACILITY (`-level_34`) —
the level the D217 log names as the sharpest repro. Human eyeball across the
M-87 weapon matrix.

**Result — a clean split:**

| Symptom (M-87 matrix) | After PR #47 |
|---|---|
| PP7/PPK grip + hammer green | **FIXED** — correct colour |
| Bond watch face green | **FIXED** |
| Green cast on hands / 3rd-person world model | **FIXED** — none seen |
| **NPC-held AK47 white patches (magazine/receiver)** | **STILL PRESENT** |
| General world/HUD textures | fine, no regressions, no crash |

This is the second independent human playtest to land on exactly this split
(M-92 was the first) — and it is the split M-90's write-up predicted in
advance, which is good evidence it is a real boundary between two mechanisms
rather than an artifact of one incomplete fix.

**Why these are different mechanisms, not degrees of one.** D217's root
cause (M-90) is *a wrong palette being bound*: the texpool arena base landed
`%8==4`, so every palette in the affected pools sat 4 bytes early and
`G_LOADTLUT` read the neighbouring tenant's header bytes as colour — the
tile decodes fine, it just decodes against the wrong 512-byte TLUT, which
necessarily produces a **wrong colour** (the documented green `0x1A00`).
A **white** patch is a different signature entirely: it means the tile is
sampling a texture that is missing, never uploaded, or cleared — not one
that is present with wrong contents. Aligning the arena base cannot fix a
texture that was never decoded into the arena in the first place, which is
precisely what the playtest shows.

**Why the label split.** Leaving this inside D217 would mean the row can
never close cleanly: its own stated mechanism *is* fixed and sweep-verified,
and carrying an unrelated open failure mode under the same label is how a
finding log rots (cf. the D170→D193 subsumption that had to be untangled
later). D228 takes the white-texture residual; D217 closes on the green
mechanism it actually describes.

**Next (not started).** Run `-level_34` with `GE_D217TEX=1` / `GE_TEXDUMP`
while an AK47-carrying guard is on screen and determine which of two cases
holds:

1. the AK47's affected tiles **never reach `import_texture`** — a decode
   that never ran, pointing at the weapon-model texture *load* path rather
   than anything palette-related; or
2. they reach it with **empty/zeroed source bytes** — a decode that ran
   against a cleared or exhausted texpool slot, pointing back at the
   two-ended `leftpos`/`rightpos` allocator in `image.c` `texLoad`.

Useful control: the AK47 is not a level-start weapon on Facility, so its
textures load later than the PP7's — compare against a level where the AK47
*is* an early/starting weapon. If it renders clean there, load ordering /
pool exhaustion is implicated directly. D217 · D161 lineage.

## D228 — FIXED: `palette_to_rgba32` G_TT_IA16 byte order (fix write-up from the §F row, appended 2026-09-15 ctx-opt)

**FIXED (`port/fast3d/gfx_pc.cpp`, `palette_to_rgba32`) — a real byte-order bug, not a texpool/decode-never-ran issue as originally guessed.** `GE_TEXDUMP` on Facility (`-level_34`) found ~14% of LOD-textured uploads (fmt=CI, `palette_fmt=G_TT_IA16`) rendering solid near-white; a `GE_D228` correlation probe ruled out the texpool/RC2-clip hypothesis (the decoded size exactly matched the tile's base image every time — the data reaches `import_texture` fine, sized correctly). The actual bug: `palette_to_rgba32`'s `G_TT_IA16` branch read `intensity = palentry & 0xff` (low byte) and `alpha = palentry >> 8` (high byte) — backwards relative to the already-correct direct-IA16 decode two functions above it in the same file (`import_texture_ia16`: `intensity = addr[2*i]` (first/high byte after `gfx_dp_load_tlut`'s `PD_BE16` swap), `alpha = addr[2*i+1]`). A dim, fully-opaque palette entry (e.g. intensity=0x58, alpha=0xff) decoded backwards as bright and mostly-transparent (intensity=0xff, alpha=0x58) — exactly the white-patch symptom, on any CI-format material whose TLUT happens to be IA16 rather than RGBA16 (weapon metal/grip materials commonly are, since IA is a natural fit for grayscale metal with a separate opacity channel). Fix: swap the two lines to match the working convention. **User-verified live:** AK47 and other metallic guns render correctly (no more white patches). Verified crash-free with a `-level_34` headless run before the live check; a full 21-level sweep is still owed (not run this session — the user was using their own game instance concurrently, `verify.sh`/`taskkill` would have killed it). D217 · D161 lineage — same general "CI/LUT format-assumption" bug class, distinct specific bug.
