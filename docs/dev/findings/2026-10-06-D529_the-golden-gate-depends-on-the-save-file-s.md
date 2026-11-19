# D529

<!-- from the old section-F/H table row -->
| D529 | **The golden gate depends on the save file's CONTENT — the D523 "presence, not which save" claim is refuted; the gate must pin a canonical save** — Golden gate; D523 follow-up (D525/D524); full `## D529` entry at file tail | FIXED 2026-10-05 (canonical save in-tree at `tools_pc/golden/ge007.eep`; `verify.sh` installs it like the ini pin; all 21 win sets re-captured under it — three consecutive clean 21/21 sweeps are the valid green record; the no-arg `verify.sh` path now exits 1; the box linux re-round under it landed as D530) |

## D529: The golden gate depends on the save file's CONTENT — the D523 "presence, not which save" claim is refuted; the gate must pin a canonical save (golden gate; refines D523; follow-up to D525/D524)

**2026-10-05 (pre-push session).** The D523 pin installed *whichever* local save existed ("presence, not which save — Archives measures worst_cell 0.175625 on each of the four candidate saves and 21.557% with none"). That claim had only ever been measured **on Archives**. The 2026-10-05 sweep refutes it: the local `data/ge007.eep` had been rewritten by the live Dam-ending playtest sessions (md5 `a41d7ab…`, mtime 2026-10-05 07:27), and the honest sequential sweep under it produced **6/21 REGRESSION** on a *byte-identical binary* (the 2026-10-04 run had been 21/21).

**The A/B (facility, window 900-1500:300):**

| save (2 KB each) | facility worst_cell |
|---|---|
| `ge007 copy.eep` (2026-09-02, the pre-live-sessions save) | **0.0–0.25 — PASS** |
| `ge007 - Copy.eep` (2026-09-16) | 0.0–0.25 — PASS |
| `build-pc/ge007.eep` (2026-09-28) | 0.25 (f2) / 0.25 (f3) — PASS |
| `../007/data/ge007.eep` (2026-09-15) | 0.25–0.5 — PASS |
| `gamefaqs_dexdrive_2001.eep` (external) | **177.47 (f2) / 177.22 (f3) — REGRESSION** |
| `data/ge007.eep` local (a41d7ab, the 2026-10-05 sessions) | **177.47 (f2) / 177.22 (f3) — REGRESSION** |

Two distinct classes in the 6-failure sweep, proven apart by the A/B:
1. **Save-content dependent (facility, streets, jungle, cradle):** same failure signature (177.2–177.5 at the 1200/1500 stems — a *different scene* in-window, not a jitter class) only on the two saves with that content (the 2001 dexdrive save and the 10-05 local save share it; 2001-vintage saves apparently hold state that routes facility's intro differently). The other four candidates pass. **This is what D529 fixes: the gate installs ONE canonical save, so "which save" can never vary.**
2. **D117 wall-clock flyby (dam@1500 4.9%, frigate@1000 3.2–8.3%, streets@1000 2.5–25.7%):** fails *identically on every save* — the window's first stem is inside the real-time-paced intro flyby, so the content at a given sim frame varies with machine load (D117, long-known, previously masked because the Oct-4 goldens happened to be captured the same way).

**Resolution (FIXED 2026-10-05, same session):**
- **Canonical save in-tree: `tools_pc/golden/ge007.eep`** (2 KB, = `ge007 copy.eep`, the stable pre-live-sessions save the Oct-4 gate was green on). In-tree = a fresh clone or CI can run the gate with zero external state, and the D523 "fresh clone has no save → exit 2" failure branch is gone for good.
- **`verify.sh` installs it like the ini pin** (snapshot the local `data/ge007.eep` if any → install the canonical → restore-or-remove on exit). The local playtest save is never the gate's input.
- **All 21 win golden sets re-captured under the canonical save** (`scratch/golden-recapture-win-v050.sh`; 21/21 rc=0). The first four flaky levels (dam/frigate/streets/jungle — the D117 flyby stems) were re-captured again **clean** (no concurrent processes) after the first interleaved capture.
- **Green protocol held (final, clean):** the first "double-green" pair ran `verify.sh` **with no arguments — which at the time only printed the usage and exited 0** (a no-op, not a sweep — that pair does not count) and was also interleaved with the in-flight re-capture. The valid record is **three consecutive clean full sweeps (`verify.sh sweep`) all 21/21 PASS** after the clean 4-level re-capture (`scratch/verify-v050-flaky3-run{1,2,3}.log`). **`verify.sh` with no arguments must never be cited as a green run.** CODE-PATCHED the same day (independent-reviewer follow-up): the no-arg path now prints the usage and **exits 1** with an explicit "never cite a no-arg run as a gate result" error — the failure class is closed in code, not just doc.
- **Linux side:** the box set was re-rounded 2026-10-05 under a *cleared* eep (D525) — a cleared eep is a *specific content state* (empty save), not the canonical one. The box re-round under `tools_pc/golden/ge007.eep` is owed in parallel on the release branch (off the critical path; the box pixel probe fails closed there anyway — D244 — so it is a second-surface check, not a gate).

**Why this is not a renderer drift:** the binary is byte-identical to the 2026-10-04 21/21 run; the only variable that changed is `data/ge007.eep`, and the A/B is save-keyed (same binary, four passing saves, one failing save). No renderer code changed.

**Never do:** (1) run the golden gate against a local playtest save — it is exactly this class of drift; always the in-tree canonical eep. (2) claim "the frame depends on save *presence*" again — it depends on *content*; the D523 claim survives only as the (superseded) measurement on Archives. (3) treat the linux 63-frame set as valid under the canonical eep before the box re-round lands — it is the D525 cleared-eep round, and **no linux number may be cited (e.g. in the README pass) until that re-round lands**. (4) cite `verify.sh` with **no arguments** as a green run — a sweep is `verify.sh sweep` (the no-arg path now exits 1, closing the trap in code). (5) run a re-capture concurrently with a sweep (contention moves the D117 flyby stems). (6) treat **same-session** sweeps as proof the four D117 flaky levels (dam/frigate/streets/jungle) are stable across wall-clock: the first-stem content is time-dependent and the pin was fixed to "now" — the cross-machine test is the box re-round; **if those four go red there, the fix is to exclude or widen the first-stem cell for those levels, NOT to re-capture again** (an independent reviewer's 2026-10-05 verification flagged this; same-session worst_cell swings were streets 8.38→4.41, jungle 1.42→3.87). (7) trust a `capture_p7.sh` round's provenance claim without checking WHICH save state the loop actually installed — the D524-era per-level re-pin restored the local save and silently overwrote D529's pre-loop canonical install (D531); the linux set is only "canonical" if the loop line says `cp "$CANON_EEP"`.
