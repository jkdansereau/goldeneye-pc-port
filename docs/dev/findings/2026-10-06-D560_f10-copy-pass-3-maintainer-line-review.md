# D560

<!-- from the old section-F/H table row -->
| D560 | **F10 copy pass 3 (maintainer line review)** — full `## D560` entry at file tail | FIXED 2026-10-06 (`optionsoverlay.c` tips/labels/value names; display text only) |

## D560: F10 copy pass 3 (maintainer line review)

Display text only (`port/src/optionsoverlay.c`); config keys, value-array order and count unchanged.
Every tip is now a complete sentence ending in a period. Changes: Display mode tip "Modern, original or custom graphics presets."; Aspect ratio tip shown only while Fill window is selected (`optionsRowHelp` checks the value; Fill window = `videoOutputAspect()` 0, the output rect is the whole window); tips removed for Native widescreen, Crop overscan, Draw/Fog/LOD distance and Look up/down; Explosion shake tip restored ("Sets how strongly explosions shake the screen.", it scales `viShake()` in `src/fr.c` via `portScreenShakeScale`); Control style, Layout presets (both the Extended `Input.PadPreset` row and the Original `Bond.Control` row now carry the label "Layout presets"), Auto-aim, Gameplay view area (D468), No hit flash, Skip intro ("Skips the intro and opens the file select screen." -- `lv.c` boots to MENU_FILE_SELECT, D408), Check for updates reworded.
Southpaw: swaps only the fire and aim TRIGGER actions (`input.c` `padMapPure`, `c->southpaw` G<->R swap), not the sticks (the stick swap belongs to the 1.3 Frost / 1.4 Elektra presets, D498); tip "Swaps the fire and aim triggers."
Look up/down values renamed Inverted/Normal (index 0 = the game's "Reverse", 1 = "Upright"); mapping from the game's own option strings and the options-screen preview (`options.c` ~3103: value 1 shows up for D-pad up). Note a fresh profile (value 0) therefore reads "Inverted"; compare against 1964 if in doubt.
Two tips were shortened to keep `tipSelfCheck` at 0 over 2 lines at HUD 150 (Control style: "Choose the port's modern controls or the original game's."; Gameplay view area: "...widescreen and any field of view."; layout presets drops "port"). Menu pointer row kept; analysis in the session report (Input.MenuPointerMode, D165, default Direct).
**Verified** by in-level captures (`-level_09`, 1280x720, AspectMode 0/1, HudScale 100): all whole, centred, punctuated; Gameplay view area wraps to two lines at HUD 100.
