# D275

<!-- from the old section-F/H table row -->
| D275 | **D236 wall, eighth pass — probe upgrades (`GE_D270PIX` list, `GE_D270KEEPOML` counter) (M-133).** — full `## D275` entry at file tail | CLOSED (2026-09-26 bookkeeping: sub-pass of D236, which closed in M-198). Earlier: done — probes live, uncommitted; see D276 for what they found |

## D275 — M-133: probe upgrades for the eighth pass — `GE_D270PIX` extended from one point to a list, `GE_D270KEEPOML` gained a "kept N tris" counter (an external review's step 1/2 groundwork)

**Context.** A second external review of the D274 write-up proposed a four-step eighth pass: (1) multi-point pixel attribution + fog state inside the weave to name the actual front-most painter and close D269's never-done "log per-vertex fog" step; (2) an aligned class-isolation A/B with a visible "kept" counter so a black capture can be told apart from a genuine render bug; (3) an LOD-quality A/B; (4) stay alert to the framing risk that the confirmed wall-card content (a gradient wedge, D274) cannot by itself explain the N64 reference's discrete conifer silhouettes — a second, unattributed draw class is likely responsible for those, and D236 may split into two separate defects. This entry covers the mechanical probe work for steps 1–2; D276 covers what step 1 actually found.

**Changes (`port/fast3d/gfx_pc.cpp`, `#ifdef PORT`, both still env-gated/opt-in).**
- `GE_D270PIX="x0,y0[;x1,y1;...]"` (was single-point `"x,y"`): up to 8 normalized screen points, semicolon-separated. Each `D270P[ptN][...]` line now also carries `pix=(x,y)` (which point) and `addr=%p` (D274's reliable per-slot address, not `seq`).
- `GE_D270KEEPOML`: was silent. Now logs `D270KEEPOML: kept tri #N (oml24=0x...)` for the first 4 kept tris and every 5000th thereafter — a black capture with a nonzero kept-count is a genuine render-interaction bug; a black capture with `kept==0` at that tick is tick misalignment or an empty keep-set, not a probe failure.

**Offline groundwork before the live run:** picked 4 candidate weave pixels directly from `scratch/d273_base_1500.png` (the confirmed reproduction of the user's reported wall) by eyeball + sampled RGB to confirm they land in the gray weave band, not the snow floor or the black letterbox bars: `(0.05,0.10)`, `(0.25,0.15)`, `(0.85,0.12)`, `(0.50,0.06)` (the last one turned out to be sky, not wall — a miss, harmless, see D276).

**Attempted, still open:** the class-isolation A/B itself (review's step 2) was not completed this pass — D276's step-1 finding reframed the whole "which class is the wall" question first, which the review itself said should gate everything else ("step 1 first because it's the only step that can invalidate the current framing"). Re-run step 2 with the *correct* class identified in D276, not the D269-named OPA/XLU pair.
