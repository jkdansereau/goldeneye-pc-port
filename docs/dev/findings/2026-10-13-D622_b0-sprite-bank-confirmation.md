# D622

<!-- from the old section-F/H table row -->
| D622 | **B0 sprite-bank confirmation — the `Effects`/`UI` sprite-bank texnums register and draw (explosion/smoke banks 2081–2196, crosshair 2236, front-end art 2194–2628), so those PNGs stay in the join scope; the unregistered residue is exactly the known in-exe art (font glyphs + fire particles); Phase B continues to B1–B3** — ROADMAP #100 Phase B0 (`docs/dev/TEXPACK-PLAN.md`); full `## D622` entry at file tail | PASS 2026-10-07 — two human-driven `GE_TEXNUMDUMP`+`GE_TEXDUMP` runs (front end 377 unique texnums / 521 dumps, `-level_09` combat 321 / 316; evidence `scratch/b0/`): Effects/Explosion+Smoke banks and UI/Crosshair registered at `texLoad` and drawn; `UI/Gunbarrel` PNGs are gun model textures (not a sprite bank), `UI/Letters`+`UI/Ammo` = in-exe fonts (out of plan); `GE_TEXNUMDUMP` log now flushes per line (was lost on force-kill); next: B1–B3 |

## D622: B0 sprite-bank confirmation — the `Effects`/`UI` sprite-bank texnums register and draw, so those PNGs stay in the join scope; the unregistered residue is exactly the known in-exe art; Phase B continues to B1–B3

**Setup.** Phase-B step B0 of `docs/dev/TEXPACK-PLAN.md` (ROADMAP
#100, plan D621): confirm the HD pack's `Effects/`
(Explosion/Smoke/Death) and `UI/` (Gunbarrel/Crosshair/Ammo)
sprite-bank texnums register through the texnum hook and draw, so the
external-signal join (D620) can reach them — otherwise those ~1290 PNGs
move out of scope and the ceiling drops. Two `GE_TEXNUMDUMP=1
GE_TEXDUMP=1` runs, human-driven (the `GE_INPUTSCRIPT` headless attempt
burned most of its level time in the intro cutscene — scripted combat is
not reliable yet, human played the runs): (a) no level flag — front-end
menus (title → mode select → 0100 → 0200, then the Dam level loaded);
(b) `-level_09` with combat (firing + kills). Evidence in
`scratch/b0/` (gitignored, regenerable): `texnumd_frontend.log`
(445 lines / 377 unique texnums, 521 texture dumps),
`texnumd_level09.log` (335 lines / 321 unique, 316 dumps), the PPM/RGBA
dump sets, labeled contact sheets (`fe_*.png`, `lvl09_*.png`) and zoom
sheets.

**Findings.**

1. **The `Effects` sprite-bank texnums register and draw.** The
   explosion/smoke sprite banks on `-level_09` are texnums
   2081–2196: grey smoke puffs (2086–2098), white flash discs / smoke
   billows (2108–2112, 2176–2196), yellow/orange spark and flash
   sprites (2119, 2128, 2157–2160), white crescents/dots (2171,
   2177–2180). Every one of these appears in
   `scratch/b0/texnumd_level09.log` (registered at `texLoad`) and was
   drawn during the combat run (texnum-named dumps exist).
2. **The `UI` crosshair is a registered texnum.** 2236 = the red
   targeting reticle — registered and drawn on `-level_09`.
3. **Front-end art is registered.** Mode-select icons 2194–2196,
   mission-select photos 2552–2553 (128x64) and 2578–2598 (68x44),
   agent portraits 2504–2508 (32x64 CI8), the "CLASSIFIED /
   CONFIDENTIAL / EYES ONLY" roll 2618–2628 (95x32 CI8 — the pack tdb's
   `95x32` rect keys now read as this game image geometry, D618),
   waveform/cube/arrow sprites 2214–2225, 2188–2193.
4. **The unregistered residue is exactly the in-exe C-array art
   already out-of-plan (D619):** the echardata font glyphs (unnamed
   `tNNN_8x7`–`tNNN_8x12` dumps, a–z/digits) and the 15 fire-particle
   DL images (`fireNN_16x14`, D219). Nothing else unregistered was
   observed.
5. **`UI/Gunbarrel` is not a sprite-bank class.** GE renders
   first-person weapons as textured 3D models, so the pack's 299
   `Gunbarrel` PNGs are gun *model* textures — ordinary registered
   texnums scoped to the level(s) that equip the weapon, joinable by
   the same level-scoped route as `Areas`/`Objects`. Same for the
   in-HUD art: `UI/Letters` (208) and `UI/Ammo` (19) are the in-exe
   font/glyph art (finding 4) and stay out of plan. The
   `Effects/Death` sub-class (40) was not visually isolated but is the
   same sprite-bank mechanism (chrobj effect banks load via `texLoad`).

**Verdict — B0 PASS.** The `Effects`/`UI` classes stay in the join
scope; the ~1200 ceiling stands until B3 measures it. Along the way:
the first dump run produced a 0-byte `texnumd.log` — the log's `FILE*`
was buffered and the force-killed dump run lost it on exit;
`port/src/texpack.c` now flushes after every line (port-layer fix, no
game-code touch).

**Next.** Phase B1 (folder→level table), B2 (per-level GDL texnum
census), B3 (candidates-per-PNG ambiguity report + gate), per the plan.
