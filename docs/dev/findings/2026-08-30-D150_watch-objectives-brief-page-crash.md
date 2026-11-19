# D150

<!-- from the old section-F/H table row -->
| D150 | watch OBJECTIVES / BRIEF page crash: `strcat(buf, langGet(id))` with `langGet` → NULL (unloaded bank) → NULL deref; `str.c` str* builtins elide a plain param NULL check, so guard via asm-laundered `GE_IS_NULL` | FIXED (`src/str.c` `#ifdef PORT`); interactive re-verify pending Owed re-verify considered cleared by the 2026-09-28 v0.4.0 campaign sign-off (ROADMAP §3 bookkeeping, 2026-10-01); reopen if seen. |

<!-- from the old section-F/H table row -->
| D150 | **Watch objective/briefing page crash (M-28).** — full `## D150` entry at file tail | FIXED (`src/str.c`, `#ifdef PORT`). Build green (`ntsc-final`); `-level_09` 600+ frames @ 91.67% unregressed. Watch-page repro is interactive — user to re-verify. porting-notes.md §C. Owed re-verify considered cleared by the 2026-09-28 v0.4.0 campaign sign-off (ROADMAP §3 bookkeeping, 2026-10-01); reopen if seen. |

## D150 — Watch objective/briefing page crash (M-28).

**Watch objective/briefing page crash (M-28).** Opening the watch → BRIEF (any subpage) or OBJECTIVES page → AV in `strcat` (`str.c:25`, `Rdi/src = 0`). The pages assemble their display text with `strcpy`/`strcat(buf, langGet(id))` (`options.c:3940-4068`, `front.c:3538-3562`, `propobj.c`). `langGet()` returns **NULL** on PC for a string bank the menu flow never loaded (D129/D143 guards — briefing/objective banks in the in-level watch flow), so `strcat(buf, NULL)` derefs NULL. On N64 those ids always resolve so the decomp never guards. Twist: `str.c`'s `strcpy/strncpy/strcat` are `__nonnull__` **builtins** to GCC, so a plain `if (src == NULL)` on the parameter is optimised away as provably-dead (verified: `-Og` elided it, no `test` in the disassembly). Fix: `#ifdef PORT` NULL guards that launder the pointer through an empty `__asm__` (`GE_IS_NULL()` / `ge_launder_ptr()`) so the check survives — NULL src → treat as empty string (blank text, not a crash), NULL dst → return. Same philosophy as the D143 textRender/textMeasure NULL guards, one layer down. Inert on N64.

**Status (from §F table):** FIXED (`src/str.c`, `#ifdef PORT`). Build green (`ntsc-final`); `-level_09` 600+ frames @ 91.67% unregressed. Watch-page repro is interactive — user to re-verify. porting-notes.md §C.
