# D429

<!-- from the old section-F/H table row -->
| D429 | **Tank full pass: aim-mode (RMB) mouse behaviour and tank sticking forward/back (follow-up to D426).** — full `## D429` entry at file tail | CLOSED faithful (user 2026-09-29: the "sticking" was drive inertia; the original also drops driving input while the mouse aims; PC now matches GEPD/1964). |

## D429 — Tank full pass: aim-mode (RMB) mouse behaviour and tank sticking forward/back (user playtest 2026-09-29, follow-up to D426)

After D426, non-aim mouse turning in the tank is good. Still open: (1) right-mouse aim in the tank is "wonky"; (2) the tank can seemingly get stuck driving forward or backward. Candidates (unverified): the aim-held legacy velocity-stick path (`input.c` `tankAimStick`) feeding stick Y while the game treats stick Y as drive/turret pitch depending on `insightaimmode`; digital C-up/C-down (W/S) interplay in aim mode; latched button state across the aim toggle. Needs a dedicated pass against GEPD/1964 tank behaviour, then the input layer/QoL.
**Status:** CLOSED faithful (user 2026-09-29: tank controls now match GEPD/1964; the "sticking" was drive inertia and the original also drops driving input while the mouse aims).
