# D280

<!-- from the old section-F/H table row -->
| D280 | **D236 wall, ninth pass — fog math VERIFIED N64-faithful; the IA8 noise class VISUALLY confirmed as the weave's painter (M-135).** No quick win for trees. — full `## D280` entry at file tail | CLOSED (2026-09-26 bookkeeping: sub-pass of D236, which closed in M-198). Earlier: REFRAMED again — no fix; next steps in detail section, see D236 |

## D280 — M-135: **D236 wall, ninth pass — fog math VERIFIED N64-faithful (owed step #1 closed), the noise class VISUALLY confirmed as the weave's painter, and the "trees" question reframed: `0x0c184b50` is in FRONT of the noise quad (depth-race read inverted), its fog is CPU-baked bimodal 25/255, and it barely draws at all under deterministic `-level_36` boot — no quick win for trees; the remaining work is CPU-side instantiation/room-streaming, not a port fix**

1. **Fog math verified (D276 owed step #1, closed).** Computed the expected per-vertex fog from the game's own constants: RSP fog ramp 0..154 → `fmul=32000`, `foff=-31744`; the noise class's z/w ∈ [0.992, 1.0] maps to expected fog ≈ 26–28. Compared against the port's actual per-vertex fog for all **2505 vertices** of `0xc81049d8`: mean abs deviation ±2 (integer truncation only). D276's "low fog" is therefore the *correct* N64 value — on hardware this quad would also reach the screen largely unfogged. **Fog is not a bug; no port-side fix exists here.**

2. **A/B visual confirmation of the painter (D276 owed step #3, closed).** `GE_D278C=1` (collapse `0xc81049d8` to a point) + `GE_PCDUMP=1500-1500:1`: the weave band's high-frequency energy drops **14.8 → 2.9**, mean 108 → 121; the band becomes a smooth bright haze (the fully-fogged CI8 cards behind it). The pixel-attribution-only evidence from D276 is now corroborated visually — `0xc81049d8`/IA8 is definitively what paints the visible weave. **No trees appeared with it removed** — the first clean test of that question, unconfounded by the noise quad (all prior A/Bs, D268/D273, were behind it).

3. **Full class census at the confirmed weave pixels** (`(0.05,0.10)`, `(0.25,0.15)`): only three real classes reach either point — `0xc81049d8` fmt=3 IA8 (the noise painter), `0x0c184b50` fmt=2 CI8 (720 tris; fog bimodal 25/255, **gfog=0** → CPU-baked per-vertex fog), and `0xc8102078` fmt=2 CI8 (69 tris). Two further "classes" in the logs are probe artifacts: `0x00552048` (z/w=0, dl=1 placeholder) and `0x00504340` (z/w=-1, behind camera).

4. **Depth-race correction — D276's "occluder" framing was inverted.** N64 z/w: *smaller = closer*. At pt0, **278/356** `0x0c184b50` tris are CLOSER than the nearest noise tri (z/w < 0.9908); at pt1, 310/356. The tree cards are IN FRONT of the noise quad, not behind it. The open question is therefore "why don't the closer textured cards show?", not "does the noise win the depth race?".

5. **`0xc8102078` ruled out as a tree layer.** `-level_36` + `GE_D270KEEPOML=c8102078`: **1.4M triangles** fill the lower half of the screen (y ≥ 0.35) — terrain/ground, not conifers.

6. **The tree class barely draws under deterministic boot.** `-level_36` + `GE_D270KEEPOML=0c184b50` + `GE_TEXRAW`: ~3–4 tris/frame at tick 1500, zero visible pixels, empty `texdump/` (no CI8 upload). Meanwhile the non-`-level_36` fog run logged 720 covering tris. **`0x0c184b50`'s instantiation is strongly run-to-run dependent and `-level_36` does not pin it** — the room-streaming state that feeds this class varies even with the boot flag.

**Conclusion: no quick win for trees.** The visible weave is the IA8 noise class (N64-faithful fog, confirmed painter — arguably N64-faithful *in isolation*). The discrete conifers of the reference come from `0x0c184b50`, which (a) barely instantiates in deterministic runs and (b), when it does instantiate, sits in front of the noise yet still doesn't visibly win. Making trees appear requires understanding this class's CPU-side placement/instantiation (room streaming / billboard emission) — multi-step game-logic investigation, not a port-layer fix.

**Owed / next.**
1. Single instrumented run: `-level_36` + `GE_D270PIX` (both weave points) + `GE_TEXRAW` + `GE_PCDUMP=1400-1600:1`; correlate per tick — does a fog=25 `0x0c184b50` tri cover the point AND is the pixel dark?
2. Log `curRoom`/`roomsDrawn` (D271 infra) alongside `GE_D270PIX` to correlate `0x0c184b50` presence with room-streaming state.
3. Decode `0x0c184b50`'s CI8 texture offline — needs a run in which it actually uploads (TEXRAW has not captured it yet).
