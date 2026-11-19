# D440

<!-- from the old section-F/H table row -->
| D440 | **"Original N64" / "Port defaults" one-press presets in the F10 overlay and front-end PC Options (+ `-n64preset`/`-portpreset`).** — full `## D440` entry at file tail | FIXED (port-only; headless-verified; user live accept owed). Follow-up: preset stretches 4:3 in non-4:3 windows (no pillarbox mode). |

## D440 — "Original N64" preset + "Port defaults" inverse (ROADMAP §5a, 2026-09-30)

**What:** two one-press action rows, "Original N64 preset" and "Port defaults preset", at the end of the GRAPHICS section (before its Reset row). They are rows in the shared `optionsoverlay.c` row table, so they appear in BOTH the F10 overlay and the front-end PC Options screen (`frontoptions.c` renders the same table; GRAPHICS now paginates to a second page there, the D406 paging). Activation reuses the D356 reset-row contract (edge-triggered arm -> "Confirm" -> apply within 3 s; held-repeat never fires it). The value column shows "Active" while every preset key already holds that preset's value. **An action, not a mode:** it writes the existing config keys (live-applied through `videoRequestLiveConfigForKey`, persisted on overlay close / clean exit like any F10 edit); no new ini key, so existing inis behave exactly as before unless a preset is pressed. D372's `Video.LowEndMode` flag no longer exists in tree (only the tree probe's "no M5" check references it), so there was no preset plumbing to reuse; the new generic hook is `configSetValue`/`configGetValue` (`port/src/config.c`, clamped like a file load).
**CLI route:** `-n64preset` / `-portpreset` apply the preset once at the top of `videoInit` (after `configLoad`, before any value reaches fast3d) and persist via the atexit save. Not added to `--help` (the help text lives outside this change's file partition). An "ini route" is just the keys below.

**Key audit** (every registered key; table lives in `port/src/video.c` `kVideoPresets`):

| Key | Port default | N64 value | In preset |
|---|---|---|---|
| Video.TextureFilter | 1 bilinear | 2 (3-point) | yes |
| Video.Anisotropy | 4 | 1 (RDP has none) | yes |
| Video.NativeWidescreen | 1 | 0 (4:3 projection) | yes |
| Video.WidescreenAuto | 1 | 0 (stock vertical FOV) | yes |
| Video.SafeAreaCrop | 1 | 0 (see below) | yes |
| Video.DrawDistance / LodDistance | 250 / 250 | 100 / 100 | yes |
| Video.DrawDistanceAutoFov / LodDistanceAutoFov | 0 / 0 | 0 / 0 | yes (identity guard) |
| Video.FovScale, Game.HudScale | 100, 100 | 100, 100 | yes (identity) |
| Game.ScreenShakeIntensity, Game.NoHitFlash | 1.0, 0 | 1.0, 0 | yes (identity) |
| Video.CrosshairHide | 0 | same | yes (identity) |
| Input.AimMode | 0 (N64) | 0 | yes (identity) |
| Input.AimRange | 0 (PC) | 1 (N64 limits, D338) | yes |
| Culling | — | — | no key: frustum planes / fog-LOD scale (D222) and the room pool (D294 `portRoomPoolScale`) derive from FOV, widescreen, draw and LOD distance and are identity at the values above |
| Video.MSAA, VSync, FpsCap | 2, 1, 60 | n/a | **no** — performance/pacing; FpsCap 30 throttles the sim (D186), not faithful |
| Video.Fullscreen, Window.* | — | n/a | **no** — display choice |
| Video.FixMipTextures, DetailBaseTile, WrapFix | 1, 1, 0 | — | **no** — accuracy fixes that move output toward N64 |
| Video.CrosshairRed/Green/Blue, DisplayFPS | — | — | no (only used with Custom colour / overlay) |
| Game.SkipIntro, Game.AllUnlocked | 0, 0 | 0, 0 | **no** — progression/convenience, not presentation (AllUnlocked has save implications, D387) |
| Input.* feel (sensitivities, deadzones, smoothing, mouse model AimAbsolute/MouseDirectLook/NaturalPitch, CrouchMode), Input.Bind.*, Bond.* (per-save), Audio.*, Debug.* | — | — | **no** — spec: bindings/volumes untouched; mouse modes have no N64 counterpart |
| Video.CrosshairColor/Size/Style, Video.CrosshairAlpha, Video.CrosshairHealthColor (D511) | 0/100/0/100/0 | same | **no** — removed/kept out 2026-10-07 (maintainer: presets are visual options; the crosshair look is a user aiming preference). They survive a preset press and never flip Display mode to Custom. `CrosshairHide` and `CrosshairPersistent` (D436) stay in: they decide when the sight is drawn, which is N64 behaviour |

**Overscan — which is faithful:** `SafeAreaCrop=0`. The N64 VI scans out the whole 640x480 framebuffer including GE's black safe-area border; a CRT's overscan hid part of it but by a set-dependent amount, and emulators/1964 show the full frame. The crop (`gfx_pc.cpp` `gfx_adjust_viewport_or_scissor`) is a port-side remap that stretches the inset gameplay viewport to the window (and trims the D246 1-unit side margin), so 0 = the signal the console output.
**4:3 limitation:** the preset gives a 4:3 *projection*; in a non-4:3 window it is stretched to the window (as the N64 image is on a 16:9 TV set to stretch). A pillarboxed gameplay mode does not exist in fast3d today — follow-up for the ROADMAP (lead).

**Verification (Windows, NTSC, 960x540 window, `-level_09`):**
- Port-default ini, `GE_PCDUMP=880-880` vs same with `-n64preset`: log `video: applied Original N64 preset (8 value(s) changed)`; the N64 frame shows the black safe-area bars + D246 side margin, the 4:3 projection stretched (narrower horizontal FOV than the Hor+ port frame) and 3-point-filtered textures.
- ini round-trip: after the `-n64preset` run the ini differs from the port-default ini in exactly the 8 keys (TextureFilter 2, WidescreenAuto 0, NativeWidescreen 0, DrawDistance 100, LodDistance 100, Anisotropy 1, SafeAreaCrop 0, AimRange 1); a following `-portpreset` run loaded those values (8 changed) and wrote an ini byte-identical to the original.
- UI dispatch: `GE_PRESETPROBE=1 GE_OPTIONSOVERLAY=4` drives both rows through the real arm -> confirm path: PASS (value "Confirm" when armed, "Active" after, other preset inactive).
- `GE_OPTIONTREEPROBE` reports FAIL both with and without this change (baseline build from the same commit) — pre-existing, not caused by the new rows (GRAPHICS is not in its page list).
**Not verified:** live mouse/pad press in either UI, draw-distance effect on an open level (DrawDistance is read at fog load, so it applies from the next level load), PAL/JP, Linux build.

**Status:** FIXED (port-only; headless-verified; user live accept owed).
