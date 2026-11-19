# D306

<!-- from the old section-F/H table row -->
| D306 | **Dam intro/truck wheels render with odd transparent-looking areas, possibly z-fighting (user QA, 2026-09-18).** — full `## D306` entry at file tail | CLOSED — faithful, confirmed on 1964 (M-201): all PC-specific leads refuted (FOV, depth precision, depth state, near plane, wheel transforms); it is the authored truck geometry seen from intro camera 5 under the truck. Absorbs D291. Earlier GE_ZF "real fighting" was a probe-quantisation artefact. |

## D306 — Dam intro/truck wheels render with odd transparent-looking areas, possibly z-fighting (user QA, 2026-09-18).

**Reported only, not investigated, explicitly not for v0.3.0.** User playtest: the truck's wheels on Dam (seen during the level intro) render oddly, showing what look like transparent areas where they shouldn't be — user's own suspicion is z-fighting. No code read done. May be an instance of the broader z-fighting issue logged separately as D308, or a distinct texture/alpha bug specific to this prop — not distinguished yet.

**Investigation (2026-07-19, with D308):** see §D308's investigation block — the two were worked together. The `GE_ZF` flicker probe confirms **real, persistent depth-fighting in the Dam intro's lower-left screen region** (the truck area) — cells there accrue oscillation counts to ~1000 over a 40 s unattended run, i.e. near-equal depths genuinely alternating frame-to-frame, not tessellation noise. The "transparent-looking areas" are consistent with the wheel geometry losing the depth test against coincident surfaces in alternating frames.

**Status:** OPEN — confirmed as real z-fighting by probe; port-vs-faithful question open (see D308).

**Current status (2026-09-29 bookkeeping):** the Status line above is historical; the authoritative verdict is the §F row — CLOSED — faithful, confirmed on 1964 (M-201): all PC-specific leads refuted (FOV, depth precision, depth state, near plane, wheel transforms); it is the authored truck geometry seen from intro camera 5 under the truck. Absorbs D291. Earlier GE_ZF "real fightin…

**2026-09-26 (v0.4.0, M-201): D291 is this same bug; FOV ruled out.** User captured the D291 "Dam truck" glitch with F12: it happens only on the Dam level intro (also the attract demo), in the shot where the truck drives over the low walkway camera (~frames 280–440 from `-level_33` boot). Wheels and tyre treads draw through the mudguards/body. It reproduces at **FovScale 100** (user re-test, `scratch/d291_shots/fov100_full.png`) as well as at 150 + native widescreen (`shot_000..003.png`), so the port's wider cutscene FOV is not what exposes it. Headless A/B at default vs user settings: `scratch/d291_AB.png` (runs not frame-locked; `GE_ZF` hit its 600-line cap in both). Occlusion-state path checked in passing: fast3d honours `G_ZBUFFER`/`G_ZS_PRIM`/`Z_CMP`/`Z_UPD` per triangle (`gfx_pc.cpp` depth_mode), so no obvious state gap. **Still gated on an N64/emulator reference of this exact shot** (porting-notes D12): if the N64 shows clean wheels there, the fight is PC-specific and the next suspect is the vehicle model's per-part transforms at extreme close range; if it shows the same, close as faithful.

**2026-09-26 (M-201, user: "investigate the PC-specific leads"): every PC-specific lead refuted; the artifact is the authored truck geometry seen from under the truck.**

- **Repro pinned:** Dam has 6 intro cameras and picks the first at random (`bondview_r.c`, `randomGetNext() % g_SetupIntroCameraCount`); the truck-over-walkway shot is **index 5**. New TEMP `GE_INTROCAM=5` forces it (PRNG draw still consumed). `GE_RSEED` does not reliably select it. With it pinned the truck passes at ~frames 280–320 (60 fps), and frame 280 at **default settings** (640x480/1280x720, FovScale 100) shows the same tread-slab look as the user's captures.
- **FOV:** refuted (user re-test at FovScale 100).
- **Depth precision / DrawDistance:** refuted. DrawDistance does move the far plane (`bgfog.c` → `viSetZRange`), but perspective depth resolution near the camera is set by the near plane; at the truck's range (w ≈ 280–790) 24-bit depth resolves far below the tyre↔mudguard separation, so these non-coplanar surfaces cannot precision-fight at any setting.
- **Depth state:** refuted. TEMP `GE_D306C="f0 f1 [wmax]"` census (per emitted triangle: GL depth mode, geometry_mode, other_mode_l, tile-0 texture, NDC, z/w, min w): every truck triangle is `dm=07` (test+compare+update, ZMODE_OPA, `G_ZBUFFER` on). The only no-compare near geometry (`dm=01`, `oml=c8102048`) is the walkway/ground under the camera, and 2D text is `dm=00`.
- **Near plane:** refuted. Truck triangles have w ≥ 278, no eye-plane crossings; only the ground under the camera crosses w=0, and GL clips it in homogeneous space.
- **Wheel transforms:** refuted. TEMP wheel-state log (removed): `wheelyrot` = 0 through the straight run, `wheelxrot` advances normally, radius/scale constant; the one ~20° steering blip (t≈530, `turnrot60`=0.0026) is the original formula's response and is outside the overhead pass. The wheel code uses named fields only (no A1 raw-offset reads).
- **Conclusion:** the rendered image is a depth-correct depiction of the geometry (correct depth state, sane z/w, sane transforms, ROM model data). The camera sits under the truck, where the model's wheel/mudguard construction (tyres passing through the arches, open-backed panels) is visible; it is not seen from normal gameplay angles. **Faithful with high confidence**; an N64/emulator frame of intro camera 5 would make it certain.
- **Probe-quality correction:** `GE_ZF` quantises NDC z to ±255 (`z24 = ndc_z*255`, not 24-bit as its comment says) with tolerance 2, so it flags unrelated nearby surfaces as "oscillating". The 2026-09-18 "confirmed real depth-fighting" was over-read and should not be cited as evidence of a fight.

**N64 reference (user, 2026-09-26):** the same shot on the 1964 emulator (GEPD bundle) shows the same wheel/body fighting as the port. Confirmed faithful.

**Status: CLOSED — faithful, confirmed against 1964 (authored geometry; no PC-specific cause). D291 closes with it.**
