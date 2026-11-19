# D380

<!-- from the old section-F/H table row -->
| D380 | **One PC keyboard layout; PD-style rebind design (2026-09-27).** full `## D380` entry at file tail | FIXED (follow-up D383): one PC layout and migration remain; keyboard capture UI is implemented separately, with live acceptance owed. |

## D380 — Remove the N64 keyboard preset; migrate to one honest PC layout (2026-09-27)

**User decision:** no N64 equivalent on PC; remove the N64-vs-GEPD key
layout selector and consolidate. Rebindable keys like the PD port should
be investigated next. The menu row used to hide the effective GEPD strings
behind persisted N64-era defaults (`Input.Bind.Fire = Left Ctrl` even though
Ctrl was actually crouch); it also made explicit unbinding of empty-default
Reload/Crouch impossible (D374/D376).

**Implemented (`port/src/input.c`, `port/src/optionsoverlay.c`):** remove
`Input.Layout` registration and F10 INPUT row. There is one PC layout.
`Input.Bind.*` now represents effective bindings directly: a fresh ini
contains Fire empty, Action Q, Cancel E, Reload R, Crouch Left Ctrl,
LeanLeft empty; `NONE` explicitly unbinds any action (PD convention).
`Input.BindingsVersion=0→1` performs a one-time migration of legacy ini
values equal to the old N64 defaults to the real PC values, preserving
non-default custom bind strings; then writes the updated ini and drops
the obsolete Layout key. There is no perfect way to distinguish a user's
explicit N64-default-valued custom bind from a stale auto-written default
in a pre-versioned ini: the one-time migration treats both as defaults,
matching D374's previous behavior. Once migrated, bindings with those
same strings can be intentionally set and retained. The crouch mode row
still invokes `inputLayoutApply` to drop the latch/rebuild parsed keys;
the exported function can also serve the later capture UI.

**Verification:** touched input/options objects compile and alternate exe
links. Backed up the user's ignored `data/ge007.ini` before a 90-frame
headless boot, observed the one-time migration: Layout removed;
BindingsVersion=1; Fire empty, Action=Q, Cancel=E, LeanLeft empty,
Reload=R, Crouch=Left Ctrl; normal quit. Then restored the original
user ini byte-for-byte (live game may still use it until next boot).

**PD-port analogue / next work:** `../pd_port/port/src/optionsmenu.c:1694–1885`
has a binding capture dialog (next key/button; ESC cancels, DEL clears)
and multiple slots per action; `port/src/input.c` there collects
SDL_KEYDOWN/MOUSEBUTTONDOWN/controller events into a last-key queue and
serializes `NONE`. GE already has multi-key ini strings but no UI; add
`ROW_BIND` to the shared F10/front-options row data with primary +
secondary slots, action labels and values, and a modal capture state.
`port/src/video.c:videoPumpEvents` owns SDL events on the host thread,
whereas `optionsOverlayHandleInput` runs on the scheduler thread, so
handoff must be synchronized (one-shot scancode/button event, ignore the
opening click/press). Escape cancels, Delete clears to `NONE`, and F10/
mouse-wheel/overlay navigation must be swallowed during capture. Use
`inputLayoutApply` to rebuild + persist on commit, expose only actions
that can actually be remapped (mouse LMB/RMB are currently hardwired).
No binding-capture UI is implemented yet; first verify the D379 colours
and D378 doors/reload with the user.
