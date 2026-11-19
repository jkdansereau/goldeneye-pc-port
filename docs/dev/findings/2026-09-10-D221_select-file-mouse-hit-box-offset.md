# D221

<!-- from the old section-F/H table row -->
| D221 | **SELECT FILE mouse hit-box offset — compiler stack-layout dependency, not coordinate mismatch (M-87 QA / M-91 fix).** — full `## D221` entry at file tail | **FIXED (M-91)** — explicit `{min,max}` `coord2d` pairs under `#ifdef PORT`; human mouse-playtest confirmed. Distinct from D192 (mode-0 clamp range, still open). |

## D221 — M-91: SELECT FILE mouse hit-box offset — root cause is a compiler stack-layout dependency, not a coordinate/origin mismatch; fixed by passing explicit `{min,max}` pairs (`#ifdef PORT`)
**Symptom (M-87 user QA):** on the SELECT FILE screen (default
`MouseCaptureMode=1`, free OS cursor), clicking directly over a visible
file wallet does nothing; you have to click *below* it to select.

**The hit-test.** `interface_menu05_fileselect` (`front.c`) loops the four
folders, gets each wallet model's world-space extents
(`modelGetXYExtents(walletinst[n], &xmax, &xmin, &ymax, &ymin)` — four
separate f32 locals) and projects them with
`projectRectCornersTo2D(&folderpositions_camspace[n], &xmin, &ymin,
&folderbbox.right, &folderbbox.left)`. The function's signature takes
`struct coord2d *` **pairs** — `{f[0]=min, f[1]=max}` — and projects
`arg1->f[0]`, `arg1->f[1]` (horizontal) and `arg2->f[0]`, `arg2->f[1]`
(vertical). Passing `&xmin` therefore *silently reads the stack float
physically after `xmin`* as "xmax", and `&ymin` reads the float after
`ymin` as "ymax". The decompiled C only reproduces the N64 binary's
behavior if the N64 compiler happened to lay the four locals out with each
max adjacent to its min (reverse declaration order) — which it evidently
did, since file select works on N64.

**The PC build breaks that assumption.** Probe (`GE_D221`, temp,
removed before commit) logged the actual adjacency in the MinGW build:
`((f32*)&xmin)[1] == ymax` (declaration-order locals: `xmax,xmin,ymax,ymin`) and
`((f32*)&ymin)[1] == 2.25734`, later `nan` — an unrelated stack slot. So the
projected hit box used `(x=xmin, x=ymax-as-X)` horizontally (a valid but
over-wide band) and `(y=<garbage/NaN>, y=ymin)` vertically — a ~14 px strip
at the *bottom edge* of each wallet (f=0: `[164.8, 179.0]` vs the true
folder extent `[36.7, 179.0]`), and NaN in other frames (the band check
`down <= v && v <= up` is false against NaN → unselectable that frame).
A ~14 px bottom-edge strip is exactly "hovering over the file does nothing;
click below it works." The horizontal over-width is why left/right felt
fine. The other caller, `objective_status.c:525`, passes genuine
`struct coord2d` locals (`sp7C`/`sp74`) and was never affected.

**The fix (game code, ABI/layout exception per AGENTS.md rule 2).** In
`interface_menu05_fileselect`, under `#ifdef PORT`: declare
`struct coord2d xminmax, yminmax;`, fill them from the four extents, pass
those to `projectRectCornersTo2D`. The N64 path keeps the original call
byte-for-byte. This is the same class as the D3x pointer-width edits:
the decompiled source encodes a memory-layout invariant the PC toolchain
does not honor; the edit makes the C express what the N64 binary already
did, changing no logic or values on either platform.

**Verification (headless, controlled).** Title screen → any button reaches
file select (`GE_INPUTSCRIPT="300:A;900:A"`). The default cursor sits at
(154.7, 127.8), inside folder 0's *correct* box but **outside** its broken
band (y=127.8 ∉ [164.8, 179.0]). Pre-fix: A-press there never selected
(the probe kept printing for the whole 60 s run — 4392 lines). Post-fix:
same script, folder 0's box is `[x 96.5..214.9] × [y 36.7..179.0]`, the
A-press selects folder 0 and the file-select loop exits (probe output
stops after ~59 frames; no crash/BUG lines). Identical cursor position and
button press on both sides — only the box changed.

**Status:** fixed in `front.c` (PR pending), probe stripped. Human
eyeball owed: normal mouse play through file select (hover highlight,
select/copy/delete options) — the headless test proves the hit-test fires
where the visible wallet is, but the highlight/selection *feel* is a human
check. **Human mouse-playtest (M-91 session, from the §F row, appended 2026-09-15 ctx-opt):** confirmed
clicking directly over each visible file wallet selects it immediately, no
aiming low required; consistent across all slots and the Copy/Erase icons;
no crash. D192 (mode-0 clamp range) is a separate bug and remains open.
