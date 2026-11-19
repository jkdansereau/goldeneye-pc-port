# D974

<!-- from the old section-F/H table row -->
| D974 | **Switch controllers: SDL lists one joystick per npad (No1..No8) whatever is attached, so the game seated four players (START on four pads to leave a match): only npads with a controller are seated now (`nxPadConnectedMask`), the PC keyboard-as-player-1 routing is off on the Switch, `GE_PADS=<1..4>` forces N for tests.** | FIXED + CONSOLE-VERIFIED 2026-11-15 |

## D974: Switch controllers: seat only the npads with a controller behind them (`GE_PADS` forces N) -- FIXED + CONSOLE-VERIFIED 2026-11-15 (nxlink, 1 to 4 controllers plugged and unplugged live)

**Symptom.** Multiplayer always counted four players (Ryujinx with two configured controllers, console with one): the log showed four `Switch Controller` seats, and leaving a match asked for START on all four pads.

**Cause.** SDL's Switch joystick driver lists one joystick per npad No1..No8 (device index i = No(i+1); index 0 also reads the handheld Joy-Cons), whether or not a controller is attached, and sends no add/remove events for them. `inputOpenPads` (`port/src/input.c`) seated the first four, so `connectedMask` always had four bits. Once that was fixed, a second, PC-only rule surfaced: with exactly one pad, `mpRoute` makes the keyboard/mouse player 1 and the pad player 2 (`MPR_KBMP1`), so a single controller still counted as two. It never fired on the Switch before, because four pads were always seated.

**Fix (port layer only).** `nxPadConnectedMask` (`port/src/switch/nxmem.c`) reads libnx `hidGetNpadStyleSet` for No1..No8 (+ Handheld into bit 0). On the Switch, `inputOpenPads` skips SDL device indices whose npad has no style set and drops a seated pad whose npad went away (seat -> device index kept in `padDev[]`); `inputUpdate` re-reads the mask every 30 calls and rescans when it changes, since SDL reports nothing; `mpRoute` never routes keyboard/mouse to player 1 (there is none). `GE_PADS=<1..4>` forces the first N devices whatever is attached, for tests: co-op / split-screen test runs pass `GE_PADS=2`. The front end already greys out and blocks CO-OP and MULTIPLAYER below two controllers (`src/game/front.c`, `joyGetControllerCount() >= 2`), and the multiplayer player count is capped by the controller count, as on the N64. Other platforms unchanged.

**Verification.** Console over nxlink (`GE_PERFSTAT=1`, title screen): one controller -> `1 controller(s)`, mask 0x1; plugging a second, third and fourth controller seated them one by one within a second (mask 0x3, 0x7, 0xf), unplugging dropped them back (0x7, 0x3); no detach/reopen loop. A first build that filtered only indices 0..3 looped (devices 4..7 detached and reopened every rescan): the driver has eight devices, not four.

**Confidence.** High (direct console log of each step).
