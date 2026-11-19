# D614

<!-- from the old section-F/H table row -->
| D614 | **Deck Bunker fight texture-cache thrash: CI8 keys hashed leftover palette entries past a partial TLUT load, so the same image re-imported whenever draw order changed (1,000-4,000 evictions per 10 s).** A loaded-extent CI8 key cut re-imports 60-70% but changed neither import time (-7%) nor pacing, and 84-315 texels per run index past the extent (fidelity risk): reverted. Not the cause of the fight drops; D583 present scheduling remains the lead. | OPEN: measured, fix not shipped (2026-10-10) |

## D614: Deck Bunker fight texture-cache thrash — MEASURED, fix NOT shipped (2026-10-10, v0.6.0 prep; follow-up to D583/D585)

**Question.** D583's SLOW lines showed big `import` times in Bunker 1 fight frames on the Deck (e.g. 13.6 ms of a 15.5 ms draw, 166 misses). Is texture import the cause of the fight drops, and is it cheap to fix?

**Probe (temporary, removed):** per-10 s counters for new vs repeat misses (repeat = the address was imported before), LRU evictions, `G_INVALTEXCACHE` invalidations, `glTexImage2D` and `glGenerateMipmap` time, and which `TextureCacheKey` field differed from the address's previous key. Deck, `tools_pc/levelbench.sh` Bunker 1 replay (`bunker1.demo.orig2`), 100 s.

**Data.**
- The 1024-entry cache sits full in fights and evicts 1,000-4,000 entries per 10 s; ~95% of misses are repeats; invalidations 0. `glGenerateMipmap` is negligible (~0.3 us each); `glTexImage2D` is about half of import time.
- Raising `TEXTURE_CACHE_MAX_SIZE` to 4096 does not help: repeat misses happen with zero evictions (new keys for known addresses), and total import time doubled. `TextureCacheKey::Hasher` hashes only the address, so all variants of one address share a bucket and lookups slow down as variants pile up.
- Key-field split: ~95% of repeat misses differ in `palette_hash`; ~5% only in `palette_addrs`. By format: CI8 dominates (e.g. 3,436 CI8 vs 657 CI4 in one window), and every CI8 case followed a TLUT load of fewer than 256 entries. The D217 whole-table hash includes entries left over from earlier draws, so the same image gets a new key whenever draw order changes.
- Fix tried: hash CI8 keys over `palette[0, extent)` of the entries the material's TLUT load(s) wrote. Repeats fell from 9,952 to 3,067-4,122 per run and evictions from 9,284 to 2,418-3,488, **but import time fell only ~7% (2.36 s -> 2.10-2.20 s per 100 s) and pacing did not change beyond run-to-run noise** (uneven share 7.6% base vs 8.0-9.1%). An exactness check found 84 and 315 CI8 texels per run indexing past the loaded extent: those read leftover entries (stale TMEM on the N64 as well), and with the narrower key a cache hit could show a different leftover than a fresh decode. A small fidelity risk for no felt gain: **reverted.**

**Conclusion.** The thrash is real waste but not the cause of the Deck fight drops; D583's present-scheduling finding (a due present stuck behind the single worker's draw) remains the lead, with the parked present thread (D585) as the structural fix. If the thrash is revisited: key CI8 on the loaded extent only when the texture's max index is known to be inside it (e.g. memoise the max index per address+size with invalidation on tex-pool reuse), and give `Hasher` the full key so variants spread across buckets.

**Ops note.** Two `deckrunner.sh` instances (one from an earlier Game Mode launch) raced for the same job files: one claimed a job and the other ran it with empty arguments, clobbering results and running levels concurrently. Check `pgrep -af deckrunner.sh` before queueing.
