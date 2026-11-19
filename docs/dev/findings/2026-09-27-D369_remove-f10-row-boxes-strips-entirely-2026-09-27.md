# D369

<!-- from the old section-F/H table row -->
| D369 | **Remove F10 row boxes/strips entirely (2026-09-27).** — full `## D369` entry at file tail | FIXED (awaiting live acceptance): no row backgrounds, control rectangles or hover paint; selected setting distinguished by brighter text, slider tracks remain. Input rule from D368 unchanged. Accepted in use: the maintainer used the F10 overlay extensively on 2026-10-01 (D469/D470/D472 testing) with no complaint about row styling. |

## D369 — Remove F10 per-row paint (2026-09-27)

User still saw boxes in D368 because the tinted per-row change strips and
selected-row rectangles were still painted. Removed **all** of those from
`optionsOverlayEmit()` (including hover paint). Selected text remains
brighter, sliders retain only their track and knob, and the existing dark
panel/Controls footer remain. Mouse hit behavior from D368 is unchanged.
NTSC compile and staged link pass; a running playtest still holds the old
exe, so the local launcher will install `scratch/d369-test/ge007-d369.exe`
only after the game exits. No save/ini touched.
