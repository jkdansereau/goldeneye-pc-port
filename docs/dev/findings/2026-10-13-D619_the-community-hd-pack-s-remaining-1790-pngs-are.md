# D619

<!-- from the old section-F/H table row -->
| D619 | **The community HD pack's remaining 1790 PNGs are blocked by four independent causes (recompile-side rect keys, wrong hash domain, rect-vs-whole-texture format, content outside the identity space) — #100 status written up** — ROADMAP #100; full `## D619` entry at file tail, current status in `docs/dev/TEXPACK-STATUS.md` | WRITTEN UP 2026-10-06 — brute-force rect join 14.6M positions / 0 hits (three stride models); pack taxonomy puts 822 PNGs in UI/Hacks classes the engine never registers; ceiling ~1200 of 2046 via an unmeasured level-scoped content join; 256 import today |

## D619: The community HD pack's remaining 1790 PNGs are blocked by four independent causes (recompile-side keys, wrong hash domain, rect-vs-whole-texture format, content outside the identity space) — feature status written up (ROADMAP #100)

**Status: WRITTEN UP 2026-10-06 — `docs/dev/TEXPACK-STATUS.md` is the
current-status read for #100; this entry is the evidence for the blocker
list.** Extends D618 (which stopped the rice route) with the brute-force
negative and the pack's own content taxonomy.

**Setup.** Question: can a *full* HD import be launched from the GoldenEye-007-HD
pack (2046 PNGs)? Measured, not assumed.

**Findings.**

1. **Brute-force rect join closes the non-CI route without a display-list
   walk.** The tdb gives each key's rect dims, so enumerate every
   sub-rectangle of every vanilla non-CI texture at those dims and hash the
   pool bytes with the GLideN64 recipe (`bpl` from the rect width, row
   advance = the source pool pitch, word-offset term rect-relative). Result:
   **14.6M positions, 0 hits**, and 0 for the two other plausible models
   (row advance = `bpl` contiguous-tile copy; absolute word-offset term).
   Combined with D618's ~100 whole-texture domain variants (0 hits), the
   non-CI keys are not derivable from vanilla-ROM bytes by any model tried.
2. **The pack's own folder taxonomy says the rest is not texture-replacement
   content at all:** `Effects` 734 (Explosion 339, Smoke 326, Death 40, …),
   `UI` 558 (Gunbarrel 299, Letters 208, Crosshair 27, Ammo 19), `Areas/<Mission
   NN - Name>` 481, `Hacks/<Mario Characters v3.17>` 264, `Objects/Weapons` 9.
   `UI` is in-exe C-array art the registry never sees (D615 limitation); `Hacks`
   are character-model swaps = model packs, the other half of #100. So the
   ceiling on this pack is ~1200 of 2046, and only through a level-scoped
   content join (per-level `GE_TEXNUMDUMP` active-texnum sets ∩ the tdb's
   `(fmt,siz,WxH)`, then content-match with a margin threshold) — not yet
   measured for discriminative power.
3. **Four independent blockers, in severity order** (full detail in
   `docs/dev/TEXPACK-STATUS.md`): **B1** keys name draw rects, not textures
   (`440x1` ×301 exceeds tmem's 128-byte pitch → recompile-side, unreachable
   by construction); **B2** the non-CI hash domain is the recompile's
   converted bytes, not the ROM's pool bytes; **B3** a rect key cannot be
   applied by the whole-texture `NNNN.png` format (needs a rect-keyed format
   extension + per-draw selection); **B4** most entries are outside the
   engine's identity space (HUD in-exe art, model swaps).

**Landed/verified for the record:** 256 of 2046 import (251 zlib-CI joined
by rice CRC + 9 coincidental non-CI); the texnum path itself (D615) is
A/B-verified. Owed on that path (shippable checklist): branch merge, F10
overlay row for `Video.TexturePack`, in-exe image registration, wrap period,
upload-buffer size ceiling, first-use stall, pack manifest, region-tagged
packs, user-facing authoring walkthrough.
