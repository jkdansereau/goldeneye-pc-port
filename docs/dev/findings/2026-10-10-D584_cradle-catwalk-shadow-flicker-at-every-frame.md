# D584

<!-- from the old section-F/H table row -->
| D584 | **Cradle catwalk shadow flicker at every frame rate (regression from D579).** D579 stopped cull-testing triangles with a vertex behind the camera; Cradle's translucent two-faced catwalk walls then drew both faces. Fix: mixed-w triangles get the homogeneous facing test det[x y w] (no division by w), identical in sign to the pre-D579 XOR flip but well-conditioned. #150 stays fixed (its real fix was the D543 camera-plane clip). | FIXED 2026-10-10 (maintainer Deck: Cradle clean, Dam wall-hug clean) |

## D584: Cradle catwalk shadow flicker at every frame rate (regression from D579) — FIXED 2026-10-10 (port only)

**Symptom:** on Cradle, shadow-like flicker over the whole level wherever the translucent 2D catwalk half-walls are, at 30/60/90 fps on the Deck. Not in v0.5.0.

**Bisect (Deck builds from known commits, maintainer by eye):** v0.5.0 clean; 2573f437 clean; 0886f413 (D579 diagnostics + no-colour-clear model) flickers. Note: the Deck backup named `bak-pre464f722b` was clean but was NOT built from 3c3191c0 (464f722b itself flickers and is diagnostics only), so backup names are not provenance; bisect only with builds made from a known commit. Live A/B on the current build: `nodegen` (D579 degenerate-scissor full-view fallback off) and `clearcol` (pre-D579 per-frame colour clear) did not help; `oldcull` (pre-D579 XOR behind-eye cull for mixed-w triangles) fixed it.

**Cause:** D579 made triangles with any vertex at w < 0 skip the face-orientation cull ("undefined across w = 0"). Cradle's catwalk walls are translucent and two-faced, large enough to straddle the camera plane; without the cull both faces drew, doubling the translucency and flickering as vertices crossed w = 0.

**Fix (`gfx_sp_tri1`):** mixed-w triangles are cull-tested with the homogeneous determinant det[x y w] (cross = -det). For all w > 0 its sign equals the shipped NDC cross (10,000/10,000 random triangles), so the all-in-front path is unchanged and still uses the NDC form verbatim; for mixed w it equals the old XOR flip exactly (17,561/17,561), without the x/w blow-up near w = 0. #150 stays fixed: its real fix was the D543 clip at the camera plane (3c3191c0); maintainer re-checked the Dam gate wall hug. `nohcull` in d578_live restores the D579 no-cull for A/B.

**Seen during the A/B, not chased:** a very brief Bond viewmodel glitch twice on the `oldcull` build only (not seen on the final build); Cradle turrets' explosions may draw wrongly (ROADMAP note, not investigated).
