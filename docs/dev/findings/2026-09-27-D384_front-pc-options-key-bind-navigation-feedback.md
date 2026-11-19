# D384

<!-- from the old section-F/H table row -->
| D384 | **Front PC Options key-bind navigation feedback + two-slot UI (2026-09-27).** full `## D384` entry at file tail | **CLOSED (2026-09-28).** Shipped in v0.4.0; accepted in the user's final review / campaign playtest. |

## D384 — Front PC Options used editable gameplay keys as its own controls (2026-09-27)

**Status: PARTIAL (NTSC built, isolated probes pass, user retest owed).** User
reported A/D could not be rebound and C caused menu glitches. Live log
`scratch/playtest_v040.log` shows captures succeeding but selection cycling
uncontrollably across slots 1–4 (e.g. `Input.Bind.StrafeRight` A → D → C → X,
then C across slots). The user's live ini now has `Forward=NONE,D` and
`StrafeRight=NONE,V,C,X` from that session: DO NOT modify or overwrite it
while testing. Root cause is precise and port-side: `inputComputePad()`
turns *newly assigned* A/D/C into live GE `GE_CONT_C/GE_CONT_F` strafe bits
or stick X/Y; `frontOptionsMenuInterface()` reads those same C buttons and
stick values as Left/Right slot selection and Up/Down row selection. The
editor therefore feeds its own rebind results back into its navigation on
the very next poll. F10 already swallows the virtual pad and did not have
this particular feedback path. In parallel, four visible slots and `1/4`
were confusing and produced unnecessary ghost mappings; user wants two.

**Fix, `port/` only:** while `current_menu==MENU_PC_OPTIONS`, the port
builds keyboard menu input from PHYSICAL arrows and Enter/Escape, not from
editable gameplay keys. Mouse LMB/RMB and gamepad still reach the menu; the
scripted-input test harness retains its exclusive source. While capture
is active or the captured key remains down, keyboard/mouse menu buttons
are neutral (`optionsBindingInputBlocked()` checks an atomic held-scancode
across host, scheduler and front threads); the front menu's capture modal
continues to suppress interface navigation. The same `Input.Bind.*` keys
still drive gameplay outside PC Options, unchanged. `GE_BINDPROBE` exercises
the menu conversion with A+D+C all pressed (zero menu input), arrows+
Enter (only their fixed signals), and a blocked modal (zero input).

`INPUT_BIND_SLOTS` is **2**: only Primary and Secondary are displayed and
active. Old third/fourth ini tokens are **preserved verbatim and ignored**
on boot (one warning per affected action); editing that action writes its
two displayed keys, removing only that action's extras. No automatic ini
rewrite/migration, no hidden active mappings. Reset to defaults still
restores the one-PC-layout binds. This also eliminates the 3rd/4th slot
selection loop; physical arrows and mouse/pad are the only editor controls.

**Verification:** MSYS2 MINGW64 NTSC link succeeded without new diagnostics.
Isolated `GE_BINDPROBE=1` reports PASS for two slots, sparse and comma-name
serialization, A/D/C no-nav, arrow/Enter and modal block, restoring
original bind state. In an isolated ini, `StrafeRight=NONE,V,C,X` produced
one `legacy extra keys (ignored)` warning and survived a no-op quit
unchanged; capturing F2 into Primary using the host-event/modal test
changed *only that action* to `StrafeRight=F2,V` (C/X discarded).
`GE_BINDCAPTUREPROBE` synthetic SDL_KEYDOWN must set `ev.key.repeat=0` and
`ev.key.keysym.mod=KMOD_NONE` explicitly; without initialization its
random repeat value caused the **test event** to be ignored. Real user
SDL key events were not implicated. Live physical-key feel check and
Turok Input→Bindings nesting remain open. No 21-level visual gate yet.
