---
title: Input and aim
description: How the GoldenEye 007 PC port maps keyboard, mouse and modern controllers onto the N64's controller, including mouse aim.
---

## Input and aim

`port/src/input.c` maps **SDL2 keyboard, mouse and gamepad** events into the
N64's own controller structs, so the game's input code runs unmodified.

**Mouse aim** is modelled on the *GEPD* mouse-injector behaviour that the 1964
"GoldenEye/Perfect Dark Edition" bundle used — an **independent
reimplementation** of that behaviour (not derived code), credited to its
author in the README. It comes in two styles, the N64's own aim feel or a
centered FPS-style, with per-device sensitivity, smoothing and Y-inversion.

**Controllers:** modern pads get a dual-stick layout, deadzones,
sensitivity, southpaw, trigger thresholds, Rumble-Pak vibration, and
PlayStation/Nintendo button names. **Everything is rebindable** — keyboard,
mouse and each controller — in the F10 options overlay. Split-screen needs one
controller per extra player.

*More: [internals](internals.md) §7 (input & saves); the aim model's
provenance is the D194 lineage in `port/src/input.c`.*
