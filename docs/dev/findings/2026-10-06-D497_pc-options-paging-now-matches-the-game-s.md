# D497

<!-- from the old section-F/H table row -->
| D497 | **PC Options paging now matches the game's dossiers: PREVIOUS tab/B turn back a page or leave, NEXT folder tab while a later page exists; D444's bottom-row page links removed** — full `## D497` entry at file tail | FIXED 2026-10-03, maintainer-verified in play |

## D497 — PC Options paging did not match the game's dossiers (2026-10-03)

**Report:** maintainer: only "previous" was a folder tab, and a redundant "Previous page / Next page" line sat under the rows (e.g. Graphics).
**Fix (port only, commit 31153ab3):** `frontoptions.c`, as the briefing screen (`front.c` `interface_menu0A_briefing`): the folder's PREVIOUS tab and B turn back a page, or leave the section/screen from its first page (`goPrevious`); a NEXT folder tab (`frontAddNextTabText` / `frontCheckCursorOnNextTab`) shows while a later page exists; D444's bottom-row links and `pageCtlAt` were removed. Up/Down still page.
**Verified:** build only.
**Owed:** none.
**Status:** FIXED, maintainer-verified 2026-10-03.
**Maintainer play check 2026-10-03:** PREVIOUS/NEXT tabs and B correct. Pad highlight/back-navigation issues found in the same check: D502.
