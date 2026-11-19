# D557

<!-- from the old section-F/H table row -->
| D557 | **New/blank profiles start with Look ahead OFF on PC (Rule-2 sign-off, maintainer 2026-10-06)** — full `## D557` entry at file tail | FIXED 2026-10-06 (`src/game/file2.h` DEFAULT_OPTIONS under `#ifdef PORT`; existing saves untouched; win gate 21/21) |

## D557: new/blank profiles start with Look ahead OFF on PC — RULE-2 SIGN-OFF (maintainer, 2026-10-06) — FIXED 2026-10-06

**Decision.** Defaults review against the Perfect Dark PC port
(`docs/dev/notes/DEFAULTS-VS-PD.md`, local): Look ahead (per-profile
`OPTION_LOOKAHEAD`) is the one N64 default that is clearly hostile on PC —
its pitch auto-centring on slopes fights mouse and right-stick look; PD's
port keeps the equivalent on only under `PLATFORM_N64`. Maintainer approved
turning it OFF for new profiles, with the explicit Rule-2 sign-off this
`src/game` edit needs (it is a deliberate PC default, not an ABI/layout
fix).

**Why src/game.** New profiles are built from `BLANKSAVEDATA`
(`src/bondconstants.h`, using `DEFAULT_OPTIONS`) at nine sites inside
`src/game/file2.c` (new save, erase/clear, copy-to-blank, ...); there is no
port-side "profile created" hook, and inferring "new" from save contents
risks flipping existing profiles. So the change is one macro:
`src/game/file2.h` `DEFAULT_OPTIONS` drops `OPTION_LOOKAHEAD` under
`#ifdef PORT` (`#else` keeps the original for the byte-matching N64 build).
The port's F10 "Reset to defaults" mirror (`kBlankSave`,
`port/src/watchsettings.c`) is built from the same macro, so it follows.

**Scope.** Existing profiles keep their stored option bits — no migration.
Only profiles initialised from `BLANKSAVEDATA` afterwards (new/erased/cleared)
start with Look ahead off; players can still turn it on per profile. Same
batch: `Input.CrouchMode` default -> Toggle (D556; ini default, new installs
and Reset to defaults only). **Verified:** build rc 0; Windows gate sweep
21/21 PASS (the canonical golden save's profiles keep their bits).
