Project Dam @VERSION@ -- GoldenEye 007 on the Nintendo Switch (homebrew)
=========================================================================

A Switch port of the GoldenEye 007 PC port by jkdansereau and contributors
(https://github.com/jkdansereau/goldeneye-pc-port). Unofficial; not affiliated
with Nintendo, Rare, Microsoft or MGM. No game content is included: you need
your own GoldenEye 007 ROM.

Requirements
------------
- A Switch running custom firmware with the homebrew menu.
- Launch through title takeover (hold R while starting a game), not from the
  album applet: the game needs the full application memory.
- Your own GoldenEye 007 ROM, US (NTSC-U) version, big-endian .z64.

Install
-------
1. Copy the switch/ folder of this archive to the root of the SD card.
2. Copy your ROM to the SD card as
       switch/project-dam/data/ge007.ntsc-final.z64
3. Start "Project Dam" from the homebrew menu. The first launch converts
   the ROM (a few seconds, black screen) and writes the converted model and
   level files into switch/project-dam/data/; later launches skip this.

Settings, saves and logs live in switch/project-dam/data/ (ge007.ini,
ge007.eep, ge007.log; ge007-crash.log after a crash).

Controls
--------
The buttons follow the labels on the controllers. Hold ZL to aim; in aim
mode the right stick is slower and more precise near the centre (Options:
"Aim-mode sensitivity", "Aim-mode precision curve"). Quit Game, in the
options menu, returns to the HOME menu.

Source and changelog: the project-dam repository on GitHub.
License: MIT for the port code (LICENSE); third-party components in NOTICE.
