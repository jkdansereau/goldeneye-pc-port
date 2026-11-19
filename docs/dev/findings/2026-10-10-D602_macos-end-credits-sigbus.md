# D602

<!-- from the old section-F/H table row -->
| D602 | **macOS end-credits SIGBUS: `bondviewRenderCredits` -> `textMeasure` faulted on `langGet()` result = bank + 0x721E08F2 (garbage slot offset in a loaded text bank), at the START of the credits in a ~1 h natural session; the same Cuba stage booted fresh (`-level_54`) renders the whole cast roll correctly on the Mac. ROOT CAUSE NOT FOUND — mitigated with an impossible-offset guard + one-shot diagnostic.** — full `## D602` entry at file tail | **MITIGATED 2026-10-08, OPEN** (macos branch; `langGet` returns NULL for offset > 1 MiB and logs the bank state once as `D602:`; needs one natural-flow re-trigger to diagnose) |

## D602: end-credits crash on bad language-bank slot offset (macOS natural session) — MITIGATED, root cause OPEN (2026-10-08, macos branch)

**Symptom.** Maintainer's ~1 h natural macOS session (206,700 rendered frames), cleared the
campaign through Cradle, then SIGBUS at the start of the end credits, before the cast roll.
`ge007.crash.log`: SIGNAL 10, fault addr `0x1000e236b67a` (= window base + 0xE236B67A), leaf
callee of `bondviewRenderCredits.part.0+444` (the `textMeasure` read of the `langGet()` result).
Registers show a valid bank pointer (`0x10007018ad88`); the string pointer is bank +
`0x721E08F2`, i.e. the slot-table entry read from the loaded bank is not a plausible offset.

**What is known.**
- `langGet(slot)` = bank pointer + `table[slot & 0x3FF]`. The bank is mapped (it passes the D129 /
  D573 `portAddrIsMapped` check); the table entry is garbage. Real text banks are a few KiB.
- Bank 20 = `Llen` (the Cuba bank); the credits entries use text ids in that range. Table entries
  are byte-swapped in place after load by `romdataFixupLangBank`, which STOPS at the first entry
  that looks invalid (`v != 0 && (v <= i*4 || v >= blobSize)`) and trusts
  `resource_lookup_data_array[idx].poolRemaining` as the blob size — a partial fixup (or a
  slot id past the table, or a stale/overwritten bank) would give exactly this garbage.
- A fresh `-level_54` boot on the Mac (and Windows, 20000 frames) is clean: the Mac cast roll
  ("Starring 007 James Bond", "Guest Star Oddjob", "Also Featuring General Arkady Ourumov")
  renders with correct text, so the bank/fixup work on a cold start. The failing state is
  specific to a long session / the natural level flow (earlier stage banks loaded and freed in the
  same process).
- Not yet known: whether it is Mac-only, and which of the three mechanisms above it is.

**Mitigation (src/game/language.c, `#ifdef PORT`, same file/precedent as D129, D456, D573).**
`langGet` returns NULL (a missing string, which callers already handle) when the table entry
exceeds 1 MiB, instead of handing `textMeasure` a wild pointer; and it logs ONCE, as
`D602: langGet(0x...) bank N (name) slot S offset 0x... is impossible; bank=... poolRemaining=...
tbl[0..3]=... tbl[slot-1..slot+1]=...`. Valid lookups are unchanged (verified: bare Cuba cast roll
frames, no `D602` line). Worst case the affected credit lines are blank instead of a crash.

**Owed.** Re-trigger via the natural flow (finish a level, then the credits; or in a long session
pick Cuba from the unlocked mission list) and read the `D602:` line: `poolRemaining`
and `tbl[...]` say whether the fixup stopped early, the slot is past the table, or the bank was
overwritten. Then fix the real cause (and decide whether the guard stays).

**Follow-up (2026-10-08, maintainer, guarded build, ~16k-frame session):** the end credits
roll normally where the 1 h session crashed. The log has NO `D602:` line and no crash log, so
the guard was not exercised: the original fault did not recur on this shorter flow. This
neither confirms the guard nor explains the crash; the failing bank state still needs a
long-session / natural-flow repro. The guard stays as a safety net.

**Status: MITIGATED 2026-10-08, OPEN (root cause owed).**
