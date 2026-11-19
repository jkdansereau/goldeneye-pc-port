# D582

## D582: #152 v0.5.0 Linux crash in the ending credits (Cuba), textMeasure reading a garbage string pointer — ANALYSIS ONLY 2026-10-09

**Report:** issue #152 (Debian 13.7, v0.5.0 / 5c43e69, ntsc-final). SIGSEGV a few
seconds into the ending cutscene after left-clicking; not reproducible on a second
completion. FAULT ADDR = Rdx = 0xe236b67a, PC 0x200d53b9.

**Symbolization (high confidence).** The *actual* released tarball
(`gh release download v0.5.0`, CI gcc-14/sniper build, debug line info present)
was addr2line'd directly, not a rebuild (a local WSL gcc-13 rebuild put different
functions at every address and was discarded):
`textMeasure` textrelated.c:750 (`while (*text != '\0')`, first deref of `text`)
<- `bondviewRenderCredits` bondview2.c:9134/9136 <- `lvlRender` lv.c:931 <-
`bossMainloop` boss.c:710 <- `bossEntry` <- `portThreadWrapper`. Disassembly
(0x20013463..0x200134d6) shows `call langGet; mov %rax,%r15; mov %r15,%rdx;
call textMeasure`: the full 64-bit return value of `langGet(credits_pointer[i].TextId1)`
reaches textMeasure unmodified, so the caller/prototype are NOT truncating
(langGet is declared `u8 *langGet(s32)`; CreditsEntry is 12 bytes, u16 fields,
no widened member). The value 0xe236b67a is what langGet *returned*.

**Mechanism.** langGet returns `(u8*)g_LangBanks[bank] + textbank[slot]`. The bank
pointer passed D129's plausibility check (non-NULL, <16 GiB) but the slot offset
read from it is garbage, i.e. `g_LangBanks[bank]` points at memory that no longer
holds a (fixed-up) language bank. This is a stale-bank read, not an A1
pointer-width truncation. Credits only render on `LEVELID_CUBA` (`credits_state==1`).
Candidate for the stale pointer: `lvlUnloadStageTextData` (lv.c:1695) only calls
`langClearBank` when `g_CurrentStageToLoad != LEVELID_TITLE`, so when the ending
transitions out (click = skip/advance) the Cuba bank in the MEMPOOL_STAGE arena is
freed/reused while `g_LangBanks[]` keeps the pointer, and any credits frame still
rendered reads reused memory. N64 does not fault (KSEG0 address masking); the PC
build segfaults. Timing-dependent -> matches "did not reproduce". This ordering is
unverified (no runtime available).

**Not fixed.** Not an ABI/layout defect, so outside the AGENTS.md rule-2 exception;
no src/game edit made. Options for the maintainer: (a) port-layer guard in
`langGet` (`#ifdef PORT`, D129 precedent): reject `textslot_offset >= 0x10000`
(banks are < 64 KB per romdataFixupLangBank) and/or clear `g_LangBanks[]` on
stage-pool release in port code; (b) verify the ordering with a GE_PCDUMP/credits
click-skip run on Cuba. Confidence in symbolization: high; in the stale-bank
mechanism: medium-low.
