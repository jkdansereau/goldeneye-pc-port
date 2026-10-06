# GoldenEye 007 PC Port — v0.4.0

<p align="center">
  <img src="https://github.com/jkdansereau/goldeneye-pc-port/raw/v0.4.0/docs/media/goldeneye-gh-preview.gif" width="480"
       alt="~24 s gameplay montage from live v0.4.0 play sessions, opening on the Runway tank (no audio track)">
</p>

Platforms: **Windows x86-64** and **Linux x86-64 (including Steam Deck)**.
Region: **NTSC-U (US) only** — see [Caveats](#caveats).

v0.4.0 is a large feature and fidelity release. The full campaign runs at a
steady 60 fps with music and SFX throughout, and has been playtested end to
end (all 20 missions, Agent difficulty) on Windows and on Steam Deck with a
physical controller. This is a pre-1.0 release: the known issues below are
real, and some areas (multiplayer, PAL/JP, HD assets) are still ahead of us.

---

## New features

### Native widescreen *(on by default)*

The world now renders natively at your display's aspect ratio instead of
stretched 4:3: geometry is undistorted (wider view, same vertical FOV), the
in-level HUD keeps its shape and anchors to the screen edges, and the 4:3
front-end menus and ending-credits sequence are pillarboxed. Turn it off
with the **`Native widescreen`** toggle (F10 → Graphics) to restore the old
stretched frame. (D334/D335)

### A true, stable 60 fps

v0.3.0 could settle at 30 fps game-wide on some setups. The cause was a
pointer-width bug in the game's frame scheduler: a "present every frame"
flag was read back at a hardcoded byte offset that the 32→64-bit transition
silently moved, so the render path ran on the audio client's every-other-
frame cadence. Fixed at the root; the game now holds a rock-stable 60 fps
(measured with the in-game FPS readout) on both platforms. (D248)

### A complete aim system for mouse *and* controller

- **`Aim style`** — **N64** (default): the crosshair deflects and the
  camera follows once it reaches the screen edge. **Centred (PC)**: the
  crosshair is fixed at screen center and your mouse — or your right stick —
  moves the camera directly. In v0.3.0 the centred mode only worked with a
  mouse; it now works with a controller too, using the N64 original's own
  stick-response curve so full stick matches the original's top turn rate.
  (D332/D333/D337/D404)
- **`Aim range`** (shown while Centred is selected) — **PC** (default,
  full-screen feel) or **N64** (the original 65% stick limits). (D338)
- **Reticule jitter in aim mode is gone** — mouse aim now runs on the
  Perfect Dark PC port's aim model. (D332)
- Per-device sensitivity: `Mouse horizontal/vertical sensitivity`, and for
  controllers `X axis / Y axis look sensitivity (controller)` plus
  `Look smoothing (controller)`.

### Rebuilt options (F10 overlay + front-end PC Options)

- Both options screens are reorganized into functional sections — **Input,
  Gameplay, HUD, Graphics, Audio, Video** — each with its own *Reset to
  defaults*; per-profile settings scoping; and the old "save file" wording
  is now **profile**. (D353–D356)
- The **F10 in-game overlay** was refreshed GE-style: category headers,
  full controller support (value-adjust with hold-to-repeat), fixed pointer/
  keyboard navigation, and new rows: **Show FPS**, **Skip intro
  (EXPERIMENTAL)**, plus pad-only **Invert look / Southpaw / Deadzone /
  Trigger threshold**. (D237/D345–D347/D360–D370)
- The front end gained a **PC Options screen** beside the file-select bar,
  and save-file **Copy/Erase** moved into the bottom bar. Long sections
  (Input) page within the screen — a "Page 1/2" marker plus a bottom hint
  on every page — so every row stays on screen and stays reachable; the
  mouse wheel and W/S also page the list, and the selection clamps at page
  edges. (D343/D406/D407)
- Watch-only settings — **Auto-aim**, **Look ahead** and the rest — are now
  editable from the PC options and **persist correctly** (this closes the
  "auto-aim won't stay on" report, issue #103). (D330/D350/D352/D354)
- **`HUD scale`** (75–150%, default 100%): scales the ammo counter, pickup
  text and subtitle bars for smaller or larger displays. (D226)

### Crosshair customization *(defaults leave the N64 crosshair untouched)*

**Show crosshair** (on/off), **Crosshair colour** (default *Original
(red)*, or Green / Red / Blue / Yellow / Cyan / Magenta / White, or custom
RGB), **Crosshair size** (default 100% of the original drawing), and
**Crosshair style** (Original / Thin cross). (D373/D379/D381/D382)

### In-game key rebinding + new default layout

A two-group key editor (**Movement**, **Actions**, each with its own *Reset
to defaults*) lives in both options screens. The new default **Actions**
layout is the GEPD-style preset (e.g. Action/next-weapon on the keys you'd
expect from a modern layout); existing INI bindings are honored and older
ones migrate automatically. (D371/D374/D383/D385)

### Controller improvements

- **Sane pad defaults**: **A = use/interact, X = reload, Y = weapon cycle**
  (v0.3.0 mapped two buttons to crouch and had no in-game use button). (D393)
- Crouch uses the engine's real crouch input, with a **`Crouch mode`**
  toggle (Hold / Toggle). (D375)
- Xbox-variant controller parity, **Southpaw**, and per-stick
  **deadzone** + **trigger threshold** rows. (D237/D394)
- **The N64 Rumble Pak now drives real gamepad haptics** (Steam Deck
  included), scaled by a global **`Vibration`** slider (default 50%). (D401)
- The F10 overlay is fully gamepad-driven. (D347/D395/D396)

### `All unlocked` ships as a documented EXPERIMENTAL feature

F10 → Gameplay → **`All unlocked (EXPERIMENTAL)`** opens every mission, 007
mode and the full cheat menu. On a fresh install it now yields the game's
own fresh-slot layout instead of corrupting state (D281) — but enabling it
can still bake synthetic cheat/completion data into your save that switching
it off does not undo: **complete a level normally first, and back up
`data/ge007.eep` before enabling it.** (D387)

### Steam Deck / Linux

The first clean SteamOS/glibc build, plus a first-launch preset for decks
without a config (native 1280×800, 2× MSAA, vsync, 250% draw/LOD
distances). (D402/D283)

---

## Bug fixes

### Fidelity (verified against the N64 original / era-correct references)

- **Water** on the `IsWater` levels (Dam, Frigate, Surface 2, …) matches
  the original: the moving seam between two patterns and the pattern
  "resetting" as you moved are gone. (D245)
- The **particle "rainbow" effect** (intermittently recoloured sparks and
  explosion residue) no longer occurs. (D252)
- **Frigate: "every polygon breaks except the gun"** after turning — and
  the rare "Bond briefly out of place" quirk — were the same never-written
  fields read by the head-bob animation; both are fixed. (D336/D311)
- Surface 1/2: the tree backdrop that rendered as a solid wall of texture
  now renders as proper camera-facing tree cards. (D236)
- Ejected **shell casings** render again. (D331)
- The intro **gun-barrel blood drip** draws correctly. (D341)
- The extra erroneous **long muzzle flash** on some guns is gone. (D303)
- Occasional **z-fighting** fixed; **security-camera props** no longer face
  backwards; the front-end **Nintendo logo and copyright page** render
  correctly. (D308/D307/D403)
- The **train-intro soldier pose** is correct. (D392)
- **Tanks** (Runway, Streets) can be boarded and exited again — the new
  use/reload button split had removed the B-button tap the engine's tank
  handlers use; B is presented again in tank states, and use keeps its
  no-reload-fallback semantics everywhere else. A short input lockout during
  the sit-down animation means a double-tap can't cancel boarding, and in-tank
  mouse aim has its own **`Tank aim speed`** setting (`Input.TankAimScale`,
  default 100) so the turret matches your on-foot feel. (D407)
- File-select background / gun-barrel comb rendering fixed. (D397)
- Stale-texture artifacts after level transitions fixed. (D235)
- Distant-geometry dropout on the biggest open levels (Streets, Egyptian)
  substantially improved at default settings. (D249)
- The **Facility execution-scene watchdog** from v0.3.0 stays enabled as a
  safeguard; the animation-pinning that motivated it is root-caused (a tick
  granularity divergence) and fixed, so the scene plays out as authored.
  (D329/D318)

### Front end & UI

- Randomly **flickering F10 menu items** are gone. (D314)
- The watch's **controller-page graphic** renders again. (D290)
- File-select **folders and Bond photos** no longer vanish after backing
  out of a file. (D342)
- File-select bottom row (SELECT FILE / Copy / Erase / PC Options) aligned.
  (D398–D400)

### Audio

- **Gunshot SFX** cadence matches the N64 original's rate, and the
  intermittent silence of the PP7/AK47 under sustained fire near a looping
  sound is gone. (D240/D241)
- The silenced **PPK "slap"** and the broader real-time mixer corruption
  behind it are fixed. (D202)
- Windows audio now prefers the **DirectSound** backend over WASAPI (lower,
  more robust latency). (D322)
- In-stage music **re-synchronizes** correctly at stage transitions and
  level entry. (D355)
- Long-session audio degradation (issue #87): pool hardening shipped, plus
  an optional `GE_D322=1` telemetry probe if it ever recurs.

### Stability & renderer

- **Orderly game exit**: quitting no longer lands inside the graphics
  driver (a class of Windows driver bugchecks seen during testing). (D344)
- A **non-self-recovering Windows freeze** after the window sat idle and
  lost focus is fixed (mouse-mode changes now applied on the window
  thread). (D287)
- **MSAA above your driver's maximum no longer black-screens** (the request
  is clamped to `GL_MAX_SAMPLES`), and two framebuffer-state bugs in the
  render/resolve paths were fixed — community contribution, PR #106,
  security-reviewed. (D405)
- Port-wide code audit: missing returns in front-end menus, missing
  prototypes, undefined-behaviour shifts.

---

## Options reference

Every setting in v0.4.0's options screens (front-end **PC Options** and the
in-game **F10 overlay**), with the current default.

| Setting | Section | Default |
|---|---|---|
| `Aim style` | Input | N64 (N64 / Centred (PC)) |
| `Aim range` | Input | PC (shown while Centred is selected) |
| `Mouse horizontal sensitivity` / `Mouse vertical sensitivity` | Input | 100% (calibrated midpoint) |
| `Invert look (mouse)` / `Invert look (controller)` | Input | Off |
| `Southpaw` | Input | Off |
| `X axis look sensitivity (controller)` / `Y axis look sensitivity (controller)` | Input | 100% (native) |
| `Look smoothing (controller)` | Input | 0 (off) |
| `Tank aim speed` | Input | 100% (`Input.TankAimScale`) |
| `Deadzone (left stick)` / `Deadzone (right stick)` | Input | 70% |
| `Trigger threshold` | Input | 23% |
| `Vibration` | Input | 50% |
| `Crouch mode` | Input | Hold (Hold / Toggle) |
| `Bindings…` (`Movement…` / `Actions…`) | Input | GEPD-style preset |
| `Auto-aim` / `Look ahead` | Gameplay | Off (watch-backed) |
| `Skip intro (EXPERIMENTAL)` / `No hit flash` | Gameplay | Off |
| `All unlocked (EXPERIMENTAL)` | Gameplay | Off |
| `Sight on screen` / `Ammo on screen` | HUD | On (watch-backed) |
| `HUD scale` | HUD | 100% (75–150%) |
| `Show crosshair` | HUD | On |
| `Crosshair colour` | HUD | Original (red) |
| `Crosshair size` | HUD | 100% |
| `Crosshair style` | HUD | Original (Original / Thin cross) |
| `Native widescreen` | Graphics | On |
| `Widescreen auto FOV` | Graphics | On |
| `Crop overscan` | Graphics | On |
| `Anti-aliasing` | Graphics | 2× (1/2/4/8, clamped to driver max) |
| `Texture filter` | Graphics | Bilinear (Nearest / Bilinear / 3-Point) |
| `Anisotropic filtering` | Graphics | 4× (1–16) |
| `FOV scale` | Graphics | 100% |
| `Draw distance` / `LOD distance` | Graphics | 250% (100–400%) |
| `Music volume` / `FX volume` | Audio | 100% (watch-backed) |
| `Fullscreen` | Video | Off (windowed) |
| `Resolution` | Video | Auto windowed preset |
| `VSync` | Video | On |
| `Frame rate cap` | Video | 60 (30 / 60) |
| `Show FPS` | Video | Off |

---

## Caveats

- **NTSC-U (US) ROMs only.** PAL and JP ROMs are not supported in this
  version; asset repair for those regions is on the post-release roadmap.
  (D258)
- **The default Actions key layout is new** (GEPD-style). If you preferred
  the v0.3.0 defaults, rebind them in *Input → Bindings…* or reset the
  group. Existing INI bindings are auto-migrated and still honored.
- **Back up `data/ge007.eep` before using `All unlocked`** (see above).
- Steam Deck: the first launch in Game Mode applies the Deck preset only if
  no `ge007.ini` exists yet; F10 changes always win afterwards.

## Known issues

- On Facility, if gas leaks during Ourumov's monologue he can pause for up
  to ~10 s before resuming the scripted shootout — a latent race that exists
  in the N64 original too (where it softlocks permanently); the port detects
  and auto-recovers it. (D318)
- With *Native widescreen* on, the F10 options overlay stretches with the
  window instead of pillarboxing like the front-end menus (legible;
  cosmetic). (D335b)
- The front-end Rareware logo shows a subtle texture-filtering artifact
  (the Nintendo logo and legal page are clean). Cosmetic only. (D75)
- **`All unlocked` is experimental** (see above); the fresh-install
  corruption case is fixed, but saves made while it is on are not
  guaranteed recoverable by switching it off. (D387)
- **`Skip intro` is experimental — not recommended for regular use yet.**
  With it on, a failed or aborted mission skips the post-mission failure
  report screen and returns straight to the menus (the mission still records
  correctly). An older audio quirk on some saves is also still being chased.
  (D408/D216)

## Roadmap since v0.3.0 (research, not yet features)

- ROM-level **mod support** investigated and found viable (the game's file
  table lives in the ROM); HD texture packs and the drop-in-ROM flow are
  tracked post-1.0. (D325–D328)
- A measured CPU budget on low-end hardware (issue #92) reshaped the
  performance investigation toward the GPU. (D339)
- Controller options wave 2 (presets, pad reassignment UI) and PAL/JP
  support are planned post-v0.4.0.

---

## Downloads

| File | Platform |
|---|---|
| `goldeneye-pc-port-0.4.0-win64.zip` | Windows x86-64 |
| `goldeneye-pc-port-0.4.0-linux-x86_64.tar.gz` | Linux x86-64 (incl. Steam Deck) |

Each contains the engine executable, a README, license texts, and the
`prepare-assets` tool. **No ROM, no game assets.** The Windows bundle
carries its runtime DLLs; the Linux bundle carries SDL2, so on both
platforms nothing needs to be installed first.

## Running it

You supply your own **GoldenEye 007 N64 ROM** (`.z64`, big-endian) that you
legally own (NTSC-U only, as noted above). No Python, no toolchain, nothing
to install.

1. Unpack the archive.
2. Make a `data/` folder next to the executable and put the ROM in it, named
   `ge007.ntsc-final.z64`.
3. Run the executable **from that folder**. The first run takes a few extra
   seconds: it detects your ROM, generates the two derived asset folders
   (`data/pcmodels-ntsc-final/`, `data/pccg-ntsc-final/`) once, and saves
   them for every future run. (The generator is
   `prepare-assets/ge007-convert` inside the bundle; you can also run it
   manually; it prints what it's doing.)

**Steam Deck:** sideload the unpacked folder (SFTP, USB or a file manager), do
steps 2–3, then add the executable to Games → *Add Game* as a non-Steam
game. The first launch in Game Mode picks up the Deck preset (native
1280×800, 2× MSAA, vsync) automatically if no config exists yet.

**In-game settings on the Deck:** the options overlay is fully
gamepad-driven: it opens with **Select**, the D-pad or left stick (up/down)
moves between options, **A** steps the selected option forward, **B** steps
it back, and **Start** (or Select again) closes. No keyboard needed.

Full steps are in the bundled `README.md`.

## Verify the download

```
sha256sum -c goldeneye-pc-port-0.4.0-win64.zip.sha256
sha256sum -c goldeneye-pc-port-0.4.0-linux-x86_64.tar.gz.sha256
```

## Source & docs

<https://github.com/jkdansereau/goldeneye-pc-port>, built on the
[GoldenEye 007 decompilation](https://github.com/n64decomp/007),
architecture after the [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark).
Non-commercial fan preservation/research project; not affiliated with any
rights holder. **AI disclosure:** built through agentic AI coding (Claude
Code + a local open-weight model), directed by one person in their spare
time — as much a study of what agentic development gets wrong on a
game-sized codebase as it is a port. See the README's
[Background section](https://github.com/jkdansereau/goldeneye-pc-port#background)
for the full account.
