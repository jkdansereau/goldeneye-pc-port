# Changelog

## [Unreleased] - minor

### Added
- GE+ sky (Switch, on by default; Video > Sky): the original clouds in three layers at different heights on a curved sky, a sun with a halo on day and dusk levels, stars and a moon on night levels, and a haze band at the horizon that the scenery fades into; Original (or the N64 display mode) restores the game's own sky, and Frigate keeps it
- Co-op on the mode menu (Switch, between Select Mission and Multiplayer; needs a second controller): player 2 picks a character, then a solo mission starts for two players in split screen; a downed player is revived by the partner standing at the body and pressing A for 10 seconds, and the mission fails when both are down; Start opens that player's own watch in a full-screen view and pauses both players; scripted end-of-mission cameras show player 1's view in both halves; dropped weapons fall and can be picked up; ammo, armour and weapons lying in the level exist once per player, so a pickup one player takes stays for the other; no multiplayer radar; Co-op enemy health (100, 150 or 200 percent) on the Game page makes guards tougher
- Look ahead (Extended) on the Controller page (Switch; Off, Low, Medium or High speed, High by default): walking forward with the look stick at rest eases the view back to level; the game's own Look ahead never fired with the Extended controls because its walk is digital, and stays on the Game page
- Anti-aliasing on the Switch: MSAA 2x handheld and 4x docked, plus FXAA, a final pass that smooths polygon and rail edges without blurring flat textures; the Switch display mode turns them on
- Aim zoom and Aim assist on the Controller page (Switch): handheld only, the picture zooms 2x while you aim (1.5x available) so a head is big enough to hit on the small screen, and the aim stick slows down while the crosshair is on or near an enemy; centered aim only, and the aim stick slows with the zoom
- Aim entry on the Controller page (Switch, Move view by default): when you start aiming, the crosshair no longer slides away from the enemy it was on; the view turns with it to the centre in a few frames (or the crosshair centres at once)
- Stop on aim on the Controller page (Switch, on by default): aiming with the move stick released stops your character at once; the game lets him glide for about a second (the head-bob filter keeps pushing him), so the enemy the auto-aim had picked drifted out of the crosshair
- Blood on hit (Game page, on by default): a bullet in a guard's body makes a dark-red puff instead of the wall's pale-yellow one, so hits read as hits; off restores the original game
- Look precision and Turn acceleration on the Controller page: a stick curve you can steepen (2.0 by default, the original game's) for fine aim, and a turn that speeds up when the stick is held at the edge (High by default) for wide turns
- Centered aim style, the default on the Switch: the crosshair stays at the centre and the weapon trails the camera when it turns, then settles back at the weapon's own pace (there is no separate Centered + sway: it is part of Centered)
- Aim sensitivity on the Controller page: how fast the right stick turns while aiming (0.75x by default)

### Changed
- The options entry on the file select screen and the title of the options menu read "Options" instead of "PC Options"
- The Switch controller defaults are tuned values: both stick deadzones 10%, look sensitivity 0.85x, vertical look inverted, look smoothing 2, trigger threshold 10%, Aim sensitivity 0.75x, Turn acceleration High, Aim zoom 2x, Aim assist High, Look ahead High
- The Controller page hides the aim rows that need the Centered aim style (Aim zoom, Aim assist, Aim entry, Stop on aim, Look ahead, Look precision, Turn acceleration) while Aim style is Original, and the aim group and these rows with the Original control style: they only work with the Extended controls
- Display mode on the Switch has three choices: Switch (the game's own settings), N64 (the original look) and Custom; Switch and N64 only show Show FPS, and Custom shows every other graphics option (aspect, widescreen, VSync, frame cap, anti-aliasing, FXAA, texture filter, anisotropy, draw, fog and LOD distance) to tune the picture
- The right stick deadzone is 10% by default on the Switch instead of 25%, so small aim movements register; settings that still had the old default move to it once
- The options no longer show the Mouse and Key Bindings pages, which need a mouse or a keyboard; Aim sensitivity replaces the aim-mode sensitivity and precision curve options
- Aim style moves to the Controller page; the Centered styles turn the camera with the right stick, and the view stays where it is when the stick is released (Original keeps the N64 crosshair)
- Faster rendering of busy scenes: vertex data goes to the GPU through one streaming buffer instead of a new buffer per draw, about three times cheaper per draw on the console
- Quitting the game on the Switch now returns to the launcher it was started from (sphaira or the homebrew menu) instead of closing the whole application and dropping to Home; if the game's threads do not stop in time it falls back to the old exit

### Fixed
- Docking or undocking the console while the game runs no longer crashes it: the picture is resized at the start of a frame on the render thread, not behind its back
- Restart Game no longer shows at the bottom of the options menu on the Switch, where it could only quit the game
- About one launch in fifty no longer stops at boot on the Switch: the game's memory windows used to sit at fixed addresses that the system's own stack could land on, and they now move to a free spot chosen at each launch

## [0.1.0] - 2026-11-11

### Added
- Project Dam: the GoldenEye 007 PC port as a Nintendo Switch homebrew (NRO), started by title takeover from the homebrew menu
- The release archive carries prepare-assets, which converts your own US (NTSC-U) .z64 ROM on a PC; no game content is included
- Renders at 1920x1080 docked and 1280x720 handheld, and follows the console when it is docked or undocked
- The buttons follow the labels printed on the controllers (A confirms, B goes back)
- Aim-mode sensitivity and precision curve for the right stick (Options), set for the short Joy-Con stick: fine aim for headshots, and a trigger press no longer moves the crosshair
- Quit Game in the options menu returns to the HOME menu
- After a crash, data/ge007-crash.log on the SD card records where it happened

### Changed
- Options that have no meaning on the console are hidden: window mode, fullscreen, resolution, MSAA, Restart Game and the update check

### Fixed
- No more stutter on zone changes: textures are no longer rebuilt every time the game reloads them
