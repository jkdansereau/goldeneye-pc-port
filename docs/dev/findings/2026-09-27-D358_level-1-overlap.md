# D358

<!-- from the old section-F/H table row -->
| D358 | **D356 level-1 overlap: the front options save-file row (item 0) painted under the first content row, and `s_hl` (item-numbered) was indexed straight into `s_rowIdx` (content-numbered) -- wrong-row adjusts, a dead last row, a one-past read, wrong highlight (2026-09-27).** — full `## D358` entry at file tail | FIXED (port-only, `port/src/frontoptions.c`): content rows draw at `rowY(k+1)`; all `s_hl` -> `s_rowIdx` uses take `s_hl-1`; A-press guards `s_hl >= 1 && s_hl <= s_rowN`; hit-test/cursor untouched (already item-numbered). Companion: "(save)" tag -> "(profile)". |

## D358 — D356 level-1 overlap: the front options save-file row painted under the first content row; s_hl -> s_rowIdx off-by-one (2026-09-27)

**Problem.** D356's top save-file row (item 0 of the level-1 page) shared
the page's item numbering (0 = save row, k = content row k-1) but the
level-1 draw loop painted content row k at `rowY(k)` -- the save row's own
line -- so "Save file: File N" and the section's first row (e.g.
"Auto-aim") printed on top of each other (maintainer-reported overlap,
2026-09-27). The same item/content confusion ran through the input paths:
`s_hl` (item-numbered) was indexed straight into `s_rowIdx`
(content-numbered), so A-press / L-R adjusts hit the row *below* the
highlight, the last content row (`s_hl == s_rowN`) fell through both
handlers entirely, `s_rowIdx[s_hl]` could read one past the built rows,
and the highlight test (`k == s_hl`) lit the wrong row.

**Fix (port-only, `port/src/frontoptions.c`).** Content rows now draw at
`rowY(k + 1)` (one line below the save row, as `rowY`'s level-1 shift
intended) and every `s_hl` -> `s_rowIdx` use takes `s_hl - 1` (A-press,
L/R repeat, `optionsResetMaintain`); the A-press branch now guards
`s_hl >= 1 && s_hl <= s_rowN` so the last row is live. The hit-test and
cursor paths were already item-numbered (they use `rowY(k)` for item k)
and are untouched. Verified by build + headless boot smoke; visual check
of the screen owed (D357 plan gate 4).

**Companion change (same commit):** the per-file row tag now reads
"(profile)" (was "(save)", maintainer request), both UIs. (Full term
history: "(save)" -> "(profile)" -> "(game file)" -> the final
"(per profile)" set anchored on the end-user "Profile" term -- D357
plan section 4.)
