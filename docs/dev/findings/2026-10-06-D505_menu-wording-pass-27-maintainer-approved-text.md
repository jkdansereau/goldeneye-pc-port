# D505

<!-- from the old section-F/H table row -->
| D505 | **Menu wording pass: 27 maintainer-approved text edits across both PC options UIs** — Text only; D504 follow-up; full `## D505` entry at file tail | FIXED (port-only; by-eye pass owed) |

## D505: menu wording pass (text only)

Port-only (`port/src/optionsoverlay.c`, `frontoptions.c`, one release-notes sentence). Follows D504. A wording table of every user-visible string in both PC options UIs was reviewed by the maintainer; all 27 proposed edits were applied, nothing else (no layout, key, order or behaviour change).

**Renamed (old -> new).** Aspect -> Aspect ratio (value Window -> Fill window); Mouse horizontal / vertical sensitivity -> Horizontal / Vertical sensitivity; Invert look (mouse) and (controller) -> Invert look; Mouse smoothing -> Smoothing; Deadzone (left/right stick) -> Left/Right stick deadzone; X/Y axis look sensitivity (controller) -> Horizontal / Vertical look sensitivity; Look smoothing (controller) -> Look smoothing; Fire key / Aim key -> Fire / Aim; AI sees the wider view -> Guards see the wider view (release notes quote updated). Values: "ON (restart)" -> "On (restart)" (the front end's green cue now also matches "On ("); capture prompts "Press button..." / "Key/mouse..." -> "Press a button..." / "Press a key..."; resolution n/a -> "(n/a)"; the front-end "[ENTER]" and "for restart options" value texts removed. Tags: front end "(per profile)" -> "(profile)" (matches F10). Hint lines: front-end and F10 key/pad hints share one phrasing ("Keys and mouse only", no colons, "Esc"/"Del", `{BACK}` token).

**Deliberately not changed (maintainer decisions owed, in the table's flag rows):** Layout preset is NOT renamed "Control style" (the game's own term; T7 / #139); the Use / Action / Use-back naming of the A/B binds; the abstract `n/100` and `n/255` value formats; "No hit flash" polarity; Widescreen auto FOV naming. EXPERIMENTAL labels keep the capitals (red ink matches the literal).

**Verified (headless, `GE_QUITFRAME` on every run):** `./build-pc.sh ntsc-final` rc=0, no new warnings in touched files; `GE_OPTIONTREEPROBE` PASS; `GE_PRESETPROBE` PASS; `GE_WSPROBE_RESET` 6/6 sections verified, 0 failures, other-file isolation intact; `GE_FRONTOPTS_WIDTHS` widest label "Guards see the wider view" (175) shows with its value unclipped; contact sheet of every F10 dialog and front-end page showed no clipping or overlap. No config key changed, so the written ini is unchanged by construction (byte compare vs 23004d20 not run). **Owed (maintainer):** by-eye read of the new wording in both UIs.
