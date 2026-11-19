# D507

<!-- from the old section-F/H table row -->
| D507 | **Menu: one-line description of the focused option in both PC options UIs** — Port-only; D505/D506 follow-up; full `## D507` entry at file tail | FIXED (by-eye pass owed) |

## D507: option descriptions under the menus

Port-only (`optionsoverlay.c`, `optionsoverlay.h`, `frontoptions.c`). Maintainer ask: hints that explain the less obvious options (e.g. "Guards see the wider view"). `kRowHelp[]` maps a row key to a one-line description (45 rows; self-explanatory rows have none); `optionsRowHelp(i)` returns it. F10: drawn centred as the first line under the box for the focused row from any input source (mouse included); the two key-hint lines move down one line while it shows. `OV_HINT_H` 26 -> 53 reserves three lines (the safe-area crop eats the bottom margin, so 41 pushed the key hints off screen); a 240-line canvas loses about two rows of page height (Game and Video already scrolled). Front end: the description replaces the bottom hint line while a row is highlighted (page bottom has room for one line of about 44 characters; bind-capture hints still win during capture). Texts are written to stay within that width.

**Verified (headless):** build rc=0; `GE_OPTIONTREEPROBE` PASS; `GE_PRESETPROBE` PASS; `GE_WSPROBE_RESET` 6/6; F10 Game page dump shows description + both key hints; front-end Game page with "No hit flash" highlighted shows its description (scripted DOWN steps). **Owed (maintainer):** read all descriptions for accuracy (Look ahead, Skip intro, Aim style are my reading of the code, not the watch's wording); front-end line position at your window size; F10 row count on short windows.
