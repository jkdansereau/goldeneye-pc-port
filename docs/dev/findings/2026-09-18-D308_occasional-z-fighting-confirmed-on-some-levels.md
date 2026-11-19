# D308

<!-- from the old section-F/H table row -->
| D308 | **Occasional z-fighting confirmed on some levels' geometry, general/broader than the D306 truck-wheel instance (user QA, 2026-09-18).** — full `## D308` entry at file tail | CLOSED — faithful (2026-09-26, user: Statue ground z-fighting reproduces on 1964 GEPD bundle). Earlier: OPEN (user, 2026-09-26: possibly faithful; validate Statue ground z-fighting on 1964 vs port). Earlier: OPEN — investigated: PC depth precision == N64 (24-bit both sides; RSP writes full 16.16), so level-authored coincident geometry is the prime suspect; GE_ZF flicker probe in-tree. |

## D308 — Occasional z-fighting confirmed on some levels' geometry, general/broader than the D306 truck-wheel instance (user QA, 2026-09-18).

**Reported/confirmed by the user, not investigated, explicitly not for v0.3.0.** User: "Confirmed occasional z-fighting in some levels due to the level geometry and current state of code" — framed as a general, known-class issue (overlapping/coincident geometry at the current renderer's precision) rather than a single specific bug site. D306 (Dam truck wheels) may be one visible instance of this broader class, not confirmed as the same mechanism.

**Investigation (2026-07-19, with D306):**

- **Depth pipeline precision compared against N64 — equivalent.** GE's RSP (`rsp/graphics/gmain.s` vertex transform) writes post-transform x/y/z/w back to the vertex buffer as **full 32-bit 16.16 fixed-point** (`sdv` stores at +0/+8/+18), NOT s16 8.8 — so the RDP receives full-precision NDC on N64. The port's `gfx_sp_vertex` keeps float (≥ as precise). Both sides write into a **24-bit integer depth buffer** (port: `GL_DEPTH24_STENCIL8`; N64 RDP: 24-bit z-image) with the same near/far from the game's projection matrix. Net: the PC has *no less* depth precision than the N64 — coincident-geometry fighting that exists in the level data fights on both platforms.
- **Probe.** TEMP env-gated `GE_ZF=1` flicker detector in `port/fast3d/gfx_pc.cpp`: a 96×54 screen-cell grid remembers recent per-cell 24-bit depth values; a hit requires non-monotonic oscillation (a-b-a within `GE_ZFTOL`, default 2 units) that **recurs ≥5 times at the same cell** — filters out tessellation gradients and one-off camera-motion coincidences. Optional region filter `GE_ZFBOX="x0 y0 x1 y1"`.
- **Result (Dam, `-level_33`, 40 s unattended, intro plays automatically):** sustained hits in the lower-left screen region (ndc ≈ x∈[-0.1,+0.1], y≈-0.57 — the truck) with per-cell oscillation counts reaching ~970: genuine near-equal depths alternating across hundreds of frames.
- **Open question — port bug or faithful?** Given equivalent precision on both sides, the most likely explanation is **level-authored coincident geometry** (the user's own framing: "due to the level geometry") that fights on N64 too. Proving/disproving requires an N64 reference of the same spot (footage or side-by-side), which is not available in this environment. Secondary suspects if it turns out PC-only: exact `glDepthFunc`/z-range edge cases, or MSAA depth behavior — none evidenced so far.
- **Fix posture if confirmed PC-only:** no game-code lever exists (depth values are the game's); a port-side mitigation would be a precision change (e.g. 32-bit float depth) which would *reduce* fighting but diverge from N64 behavior — needs a rule-2-style decision, not a silent fix.

**Status:** OPEN — real z-fighting confirmed by probe; port-vs-faithful blocked on an N64 reference of the same spot.

**Current status (2026-09-29 bookkeeping):** the Status line above is historical; the authoritative verdict is the §F row — CLOSED — faithful (2026-09-26, user: Statue ground z-fighting reproduces on 1964 GEPD bundle). Earlier: OPEN (user, 2026-09-26: possibly faithful; validate Statue ground z-fighting on 1964 vs port). Earlier: OPEN — investigated: PC depth precision == N64 (24-b…
