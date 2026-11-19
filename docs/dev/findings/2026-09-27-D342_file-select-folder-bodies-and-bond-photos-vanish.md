# D342

<!-- from the old section-F/H table row -->
| D342 | **File select: folder bodies and Bond photos vanish after selecting a file and backing out (user, v0.4.0, 2026-09-26).** — full `## D342` entry at file tail | FIXED (M-201, user-verified): scratch placed at the N64 offset +0xA000 of `ptr_logo_and_walletbond_DL` landed inside the larger PC wallet model; moved past its 0x17000 reservation. |

## D342 — File select: folder bodies and Bond photos vanish after selecting a file and backing out (M-201, 2026-09-26)

**Report:** select a file, back out to file select: the folders draw only their outlines; bodies and Bond photos are gone. Reproduced headless (`SkipIntro=1`, `GE_INPUTSCRIPT="400:A;700:B"`).

**Cause (layout-only, A1-adjacent):** the file-select folders are `PROP_WALLETBOND` model instances loaded at the start of `ptr_logo_and_walletbond_DL`. On PC that model expands to 0x1664C bytes and was given a 0x17000 reservation (D45) instead of N64's 0xA000, but two users of the space AFTER it kept the N64 offset `4096*10` (+0xA000): `init_menu05_fileselect`'s scratch DL (budget 0x6e000) and `load_briefing_text_for_stage` (briefing data). On PC both write into the middle of the wallet model. `load_walletbond` only reloads the model when the instances were freed (coming from the legal screen), so after main menu → back, the overwritten model was drawn: outlines intact, body/photo geometry corrupt.

**Ruled out first:** GL texture-cache staleness on pool reuse (a queued texLoad-address invalidation made no difference; reverted).

**Fix (`src/game/front.c`, `#ifdef PORT`):** both sites use `ptr_logo_and_walletbond_DL + 0x17000`. The budget is unchanged: 0x85000 (PC buffer) - 0x17000 == 0x78000 - 0xA000 == 0x6e000.

**Verified:** user playtest (select/back out repeatedly, folders and photos intact); golden gate 3/3.

**Status: FIXED (M-201, user-verified 2026-09-27).**
