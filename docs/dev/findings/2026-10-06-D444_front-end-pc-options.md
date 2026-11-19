# D444

<!-- from the old section-F/H table row -->
| D444 | **Front-end PC Options: clickable Previous/Next page controls; hover no longer overrides the wheel/page selection; stale All-unlocked comments refreshed.** — full `## D444` entry at file tail | FIXED (build-verified; human mouse test owed) |

## D444 — Front PC Options paging controls + hover/scroll fight (2026-09-30)

**Scope:** port-only, `port/src/frontoptions.c` (the front-end screen has its own page model: `ROWS_PER_PAGE`=11, wheel/Up/Down step the highlight and turn the page at the ends). F10 (`optionsoverlay.c`) is NOT affected: it already has "no hover-to-highlight" (its hover code only hit-tests clicks) and scrolls its own window.

- **Bug:** every non-A-held frame the highlight `s_hl` was recomputed from the pointer position, so after the wheel/keys moved the selection any small mouse nudge re-selected the row under the pointer and discarded it.
- **Hover rule now:** `s_hl` is persistent. The pointer (cursor_h/v_pos differing from the previous frame, excluding our own keyboard/wheel snap in `cursorToItemRaw`) re-selects ONLY when it lands on a different row than the last hovered row (`s_lastHover`; reset on level changes). Hover never changes `s_pageno`. A click (A press) acts on the row actually under the pointer (`s_hoverHit`), not a stale wheel selection.
- **Paging controls:** on the bottom hint row of a multi-page section, "Previous page" (left, hidden on page 1) and "Next page" (right, hidden on the last page), highlight box on hover, click via `pageCtlAt()`. Up/Down still page at the ends (the old text hint "Up: previous page / Down: next page" is replaced by the buttons). Controller: unchanged nav (D-pad down/up crosses pages); the buttons are mouse-only. Named "Previous page" to avoid confusion with the game's bottom-left Previous (back) tab.
- **Comments:** stale D257/D259/D281/D387 text in `optionsoverlay.c` (kAllUnlocked) and `watchsettings.c` (two places) updated to D442 (pure query-time override; the boot-time BLANKSAVEDATA patch is gone, so a fresh eeprom now reaches the blank-file-1 fallback).

**Verified:** build green. No probe exercises mouse hover/clicks, so no game launch; a human must test with a mouse.

**Owed (human):** in a >11-row section (Input, Graphics): click Next/Previous; wheel down then nudge the mouse slightly (selection and page must stay); move onto another row (selects it); click a row after wheeling (acts on the clicked row); confirm the new hint row layout and that controls hide at first/last page.

**Confidence:** medium (logic reviewed, not exercised live; the pointer-motion detection assumes cursor_h/v_pos is delta-driven and only changes on real motion or our snap).
