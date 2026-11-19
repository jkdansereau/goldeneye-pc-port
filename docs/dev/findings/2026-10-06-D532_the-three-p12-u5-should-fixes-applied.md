# D532

<!-- from the old section-F/H table row -->
| D532 | **The three P12 U5 should-fixes applied — the gate's ini pin now fails closed, the seed is hard-pinned (stray `GE_RSEED` warns), and `audiodebug.ps1`'s dead A/B switches are gone** — golden gate + tooling; D531/U5 follow-up; full `## D532` entry at file tail | FIXED 2026-10-05 (tooling-only: `pin_ini_640x480` fails closed like `pin_eep`; `GOLDEN_SEED` literal + warning, capture `eval` unaffected; `-AB`/`-Old`/`-OldMode` removed with a note) |

## D532: the three P12 U5 should-fixes applied — the golden gate's ini pin now fails closed, the seed is hard-pinned (a stray `GE_RSEED` warns instead of re-seeding), and `audiodebug.ps1`'s dead A/B switches are gone (golden gate + tooling; D531/U5 follow-up)

**2026-10-05 (local session).** Three tooling-only fixes, no game logic:

1. **`verify.sh` `pin_ini_640x480` failed OPEN** — the three call sites (`single`/`sweep`/`parity`) were `pin_ini_640x480 2>/dev/null || true`, so a failed ini write (missing `data/`, read-only tree, failed snapshot) ran the gate under an **unpinned ini** — the frame depends on a defaults-only 640x480 ini exactly like the save pin (D529), so that was a spurious-verdict source, not a skipped nicety. The pin now fails closed (snapshot and write both `error + exit 2`, matching `pin_eep`'s style) and the call sites are plain.
2. **`GOLDEN_SEED` hard-pinned** — it was `"${GE_RSEED:-0x0123456789abcdef}"`, so a stray `GE_RSEED` in the environment silently re-seeded the documented-as-pinned gate (frames shift → spurious verdicts). It is now the literal, with a warning when a differing env var is present (verified: `GE_RSEED=0xdead` warns, the pinned value is silent). `capture_p7.sh`'s `eval grep -m1 '^GOLDEN_SEED='` still picks up the same line, so capture and gate stay consistent.
3. **`audiodebug.ps1` dead switches removed** — `-AB`/`-Old` were documented no-ops (`GE_D204_OLD` no longer exists in the game); the switches, the A/B block, and the `-OldMode` plumbing are gone, with a one-line removal note (D532).

PS parse: 0 errors; `bash -n` clean; the warning path verified. **Owed (unchanged):** the pixel-gated box verify in a WSLg GPU-display window (D531).
