# D359

<!-- from the old section-F/H table row -->
| D359 | **Settings UI file terminology settled on the end-user term "Profile": the screen mixed five terms ("Save file"/"File N"/"(profile)"/"Select file"/"(no file)"), and the manual's "game file" reads as the program's own files in modern PC usage (2026-09-27).** — full `## D359` entry at file tail | **CLOSED (2026-09-28).** Shipped in v0.4.0; accepted in the user's final review / campaign playtest. |

## D359 — Settings UI file terminology settled on the end-user term "Profile" (2026-09-27)

**Reported:** maintainer English/terminology review of the settings UI:
the screen mixed five terms for the same thing ("Save file" label,
"File N" values, "(profile)" tag, "Select file" fallback, "(no file)"
state), and the instruction manual's "game file" was questioned for
clarity.

**Decision:** the end-user term is **"Profile"** — a per-player bundle
of progress + per-file settings. "Game file" rejected: in modern PC
usage it reads as the program's own files (Steam's "Verify integrity of
game files"), not the user's save. "Save" was the fallback but reads as
a verb in the row tag and implies progress-only. Rationale + the full
string table: `docs/dev/D357-SETTINGS-VALUES-PLAN.md` §4.

**Landed (port-only, `frontoptions.c` + `optionsoverlay.c`, both draw
paths + comments):** top row `Profile` / value `N` (was `Save file` /
`File N`); section-title annotation `(Profile N)` / `(none)` (was
`(File N)` / `(no file)`); per-file row tag `(per profile)` (was
`(save)` -> `(profile)` -> `(game file)`); F10 unavailable value
`Select a profile` (was `Select file`). Code identifiers and ini keys
(`Bond.*` rows, `watchSettings*`, ini) are unchanged — terminology is a
display-layer concern only (principle 3).

**TODO:** update the instruction manual's "game file" to "profile"
(port-owned doc, not in this repo).
