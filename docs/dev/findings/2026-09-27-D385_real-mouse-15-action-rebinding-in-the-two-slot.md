# D385

<!-- from the old section-F/H table row -->
| D385 | **Real mouse 1–5 action rebinding in the two-slot editor (2026-09-27).** full `## D385` entry at file tail | **CLOSED (2026-09-28).** Shipped in v0.4.0; accepted in the user's final review / campaign playtest. |

## D385 — Mouse 1–5 need to be real action bindings (2026-09-27)

**Status: PARTIAL (NTSC build and isolated probe pass; live acceptance owed).**
User correctly noted that Mouse 1/2/other buttons should be rebindable, not
merely described as fixed UI decorations. Previously `inputComputePad()`
unconditionally emitted Z on LMB and R on RMB even after Fire/Aim keyboard
slots were edited or cleared. The binding editor captured only keyboard
`SDL_KEYDOWN`; a click while capturing could instead lock the gameplay cursor.

**Port-only fix:** `Input.Bind.*` Primary/Secondary accept `Mouse 1` through
`Mouse 5` alongside SDL scancodes; held gameplay actions now read those
actual tokens rather than special-casing LMB/RMB for Fire/Aim. Fire defaults
to `Mouse 1`; Aim defaults to `Mouse 2,Left Shift`. Captured mouse-down events
are consumed before click-to-lock. The modal blocks through physical button
release so a binding click cannot also activate the editor or stage. Mouse
clicks stay fixed as *menu* accept/back, irrespective of their gameplay
assignment; wheel up/down still cycle weapons and are not remappable here.
Gamepad mappings remain untouched.

`Input.BindingsVersion=2` upgrades pre-mouse ini files on the next launch:
formerly-hardwired Mouse 1/2 are placed in Fire/Aim slots without losing an
existing single keyboard bind. For an action already using two keyboard
slots, the two user keys take priority: no hidden mouse bind is kept, a
warning says how to reassign a slot if the old mouse default is wanted.
Old third/fourth tokens follow D384's existing ignore-until-edit rule.
The change does **not** erase game saves or migrate other actions.

**Verification:** MINGW64 NTSC build/link passed. `GE_BINDPROBE=1` checks
Mouse 5 serialization, duplicate-slot clearing and keyboard/modal isolation,
restoring original strings in memory. In a private copy of the test data
(`AppData/Local/Temp/ge-d385`, not the user's ini), a version-1 config with
`Fire=F2` and `Aim=Left Shift` migrated to `Mouse 1,F2` and
`Mouse 2,Left Shift`; `GE_BINDCAPTUREPROBE=MOUSE5` reached the host event
pump, committed Fire Primary and persisted `Mouse 5,F2`. Quit after 120
frames was clean. **Owed:** physical Mouse 1/2/side-button capture and
stage-level behavior, and the 21-level visual gate.
