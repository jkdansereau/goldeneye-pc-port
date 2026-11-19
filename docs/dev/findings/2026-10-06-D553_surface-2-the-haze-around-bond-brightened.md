# D553

<!-- from the old section-F/H table row -->
| D553 | **Surface 2: the haze around Bond brightened/darkened as he moved — near-plane-clipped vertices lerped fog from a behind-the-eye corner that flips 0/255** — full `## D553` entry at file tail | FIXED 2026-10-06 (fast3d: fog recomputed on clip-created vertices by default, as the RSP; `GE_NEARCLIPFOG=lerp` = old A/B) |

## D553: Surface 2 haze around Bond popping dark/bright as he moves — near-clip fog now recomputed (RSP behaviour) — FIXED 2026-10-06

**Report (maintainer playtest 2026-10-06):** after D543 the lit area around
Bond on Surface 2 was dimmer (good) but changed with his position: dark,
step forward bright, step again dark. 1964 (HLE) shows a similar pulse;
the maintainer then compared against hardware-level emulation
(RetroArch Mupen64Plus-Next, cxd4 LLE RSP + angrylion LLE RDP): there the
light around Bond is static, apart from the game's own small shading
artefacts. So the pulse is a port deviation.

**Cause.** D543's CPU near-plane clip (`port/fast3d/gfx_pc.cpp`) gave each
clip-created vertex a fog value LERPED from its two parents. One parent is
behind the eye; `gfx_sp_vertex` computes its fog as `z * (1/w) * mul + off`,
which jumps between the 0 and 255 clamps as that corner's w crosses 0. The
near-edge fog therefore depended on where a big ground triangle's corners
fell relative to Bond, and flipped as he walked. The RSP's clipper runs
new vertices through its vertex-finish code (gmain.s clip routine ends in
the same finish call), i.e. fog is recomputed from the new vertex's own
clip-space position; at the near plane that is a constant.

**Fix.** The existing `GE_NEARCLIPFOG=recompute` path is now the default
(`GE_NEARCLIPFOG=lerp` keeps the old behaviour for A/B). Port layer only.
**Measured:** a scripted Surface 2 walk (`GE_INPUTSCRIPT="20:START;1400:UP"`,
`GE_RSEED` pinned) gives the same ground brightness in both modes (mean 86.8
vs 86.5 of 255; D543's "brighter feet" concern for recompute does not show),
step-to-step change <2/255 in both — that path does not trigger the pulse,
so the by-eye check where the maintainer saw it is the real test. Goldens:
see the D553 note in tools_pc/golden/README.md if any set moved.
