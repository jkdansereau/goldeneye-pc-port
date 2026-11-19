# D566

<!-- from the old section-F/H table row -->
| D566 | **Steam Deck Game Mode showed the on-screen keyboard at every launch (SDL text input left on)** — full `## D566` entry at file tail | FIXED 2026-10-06 (port only: `gfx_sdl2.cpp` SDL_ENABLE_SCREEN_KEYBOARD=0 + SDL_StopTextInput; build-verified Win/Linux; Deck Game Mode check owed) |

## D566: Steam Deck Game Mode showed the on-screen keyboard at every launch — FIXED 2026-10-06 (port only)

**Report (maintainer, Deck, Game Mode, 2026-10-06):** the Steam virtual keyboard
popped up every time the game started. **Cause:** SDL2 starts with text input
enabled and the port never turned it off (it reads keys only as `SDL_KEYDOWN`;
nothing in `port/` consumes `SDL_TEXTINPUT`/`SDL_TEXTEDITING`). With text input
active, Steam's Game Mode treats the window as wanting its keyboard. Observed with
the Deck's system SDL (`sdl2-compat` on SDL3; the Steam shortcut pointed at the
exe, so the bundled SDL2 2.32 next to it was not loaded); not checked whether the
release tarball's `$ORIGIN` SDL2 shows it too, and the fix covers both.
**Fix (`port/fast3d/gfx_sdl2.cpp`):** set the `SDL_ENABLE_SCREEN_KEYBOARD` hint to
`0` before `SDL_Init` (literal name; ignored where unknown) and call
`SDL_StopTextInput()` right after `SDL_ShowWindow`. No game code touched.
**Verify:** builds on Windows and Linux (X220); deployed to the Deck play folder
2026-10-06 21:00; maintainer Game Mode launch owed (expect no keyboard).
