# D174

<!-- from the old section-F/H table row -->
| D174 | **"No blood effect" (user QA report).** — full `## D174` entry at file tail | CLOSED — faithful (2026-09-26, user side-by-side on 1964: blood matches N64). Earlier: Blood renders (user, 2026-09-26); OPEN only for an N64-accuracy comparison on 1964. Earlier: OPEN — observed; first step = BUNKER1 firefight with regenerated sidecars |

## D174 — "No blood effect" (user QA report).

**"No blood effect" (user QA report).** Likely the unverified D120 decal fix: `d43_emit.py` now emits the opcode-0x18 `PointUsage[]` chain, but it was never interactively verified and requires a full sidecar regen to take effect (`debug.ps1` does not regen). Spray path is D172 (draws, wrong colour) — total absence would be new. See `GRAPHICS-BACKLOG.md` D174.

**Status (from §F table):** OPEN — observed; first step = BUNKER1 firefight with regenerated sidecars
