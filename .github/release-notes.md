# GoldenEye 007 PC Port — v<version>

<p align="center">
  <img src="https://github.com/jkdansereau/goldeneye-pc-port/raw/v<version>/docs/media/goldeneye-gh-preview.gif" width="480"
       alt="~24 s gameplay montage from live play sessions, opening on the Runway tank (no audio track)">
</p>

Platforms: **Windows x86-64** and **Linux x86-64 (including Steam Deck)**.
Region: **NTSC-U (US) only**. See [Known issues](#known-issues).

<!-- MAINTAINER: after the play session, add one line here saying what this
     build was played on (e.g. "Played on Windows and Steam Deck: campaign
     spot-checks, 2-4P split-screen, a controller-only session"). -->

The headline is **2–4 player split-screen multiplayer**, the largest feature
the N64 original had that this port was still missing. Alongside it come
controller presets and rebinding, audio output options, a steady 60 fps on
low-end hardware, and a round of fidelity and stability fixes checked against
the N64 game.

---

## New features

### 2–4 player split-screen multiplayer (#99)

- Local multiplayer on every MP map, with the N64's own setup flow,
  pause/watch menus and per-player HUD.
- **Who controls which player:** with one controller, keyboard/mouse is
  player 1 and the controller is player 2; with two or more controllers, the
  controllers take players 1–4. `Input.MPMode` in `ge007.ini` picks Auto
  (default), PadsOnly or KbmP1.
- A controller unplugged and replugged mid-match keeps its player (thanks to
  dolent, #122). Each controller's LED shows its player number.

### Controller presets and rebinding (D469, D498)

F10 → Input → **Controller** (also in the front-end PC Options):

- **Layout preset:** the Xbox release's four styles: "1.1 Jinx" (the
  default, identical to before), "1.2 Christmas", "1.3 Frost" and
  "1.4 Elektra" (they swap which stick moves, looks, strafes and turns), or
  **Custom**.
- **Custom:** rebind every action per controller, up to two buttons each.
  Tap B or Back to cancel a capture, hold Back to clear. Menus always keep
  A/X accept and B/Y cancel, so you can't lock yourself out.
- Builds on ideas from italoarruda's #109 (credited as co-author).
- In the tank, the pad follows your layout too: the weapon button switches
  the tank's weapon and the use button gets out (D495).

### PlayStation and Nintendo button names (D471)

PlayStation and Switch controllers show their own button names in the
menus (Cross/Circle, ZL/ZR, …). Xbox and Steam Deck text is unchanged.

### Audio output options (D470)

- **Master volume**, applied after the music and FX volumes.
- **Output device:** pick your audio device. A missing or unplugged device
  falls back to the system default.

### Presets, filtering and the original look (D440, D447, D499)

- **Original N64 preset** (F10 → Graphics) sets every value the port changes
  back to the N64's; **Port defaults preset** restores the port's modern
  defaults.
- **Aspect → Original** shows the exact 4:3 frame with bars (16:9 while the
  game's own Ratio setting is 16:9).
- **Texture filter → Trilinear**, next to Bilinear: smooths distant
  textures that the game has no mipmaps for.

### Options polish (D443, D472, D497, D502)

- **Restart game** row: applies restart-only settings such as anti-aliasing
  with one click.
- Clearer values: mouse sensitivity as ×, deadzone, vibration and volumes in
  whole %, field of view in horizontal degrees, anti-aliasing up to 16×.
- Front-end PC Options pages like the game's own dossiers: PREVIOUS and
  NEXT folder tabs, and B goes back a page. With a controller, the first
  item is highlighted when you open a section and you return to where you
  were when you leave it.
- The F10 overlay is pillarboxed in widescreen (instead of stretched),
  experimental options are shown in red, an unknown `ge007.ini` key shows a
  warning (#90), and with several controllers the one that opened the
  overlay drives it.

### `All unlocked` no longer touches your save (D442)

It's now a memory-only override: switch it off and your real progress is
back. Fake unlocks written by older builds are not repaired, so back up
`data/ge007.eep` if you used it before.

---

## Performance

- **A steady 60 fps on low-end hardware (D481, #92).** Rendering ran on the
  same thread that delivers the game's 60 Hz timing signal, so slower GPUs
  regularly missed their frame slot and dropped to 30–45 fps. Rendering now
  runs on its own thread, like the N64's graphics chip. A 2011 laptop (Intel
  HD 3000) went from ~45 to a steady 60 fps at every setting.
- **Lighter first-launch defaults on Atom/Celeron-class GPUs (D482, #92).**
  On Intel HD 400–605 / UHD 600–605 class GPUs and software renderers, the
  first launch lowers draw distance, LOD distance and anti-aliasing. Settings
  you've changed yourself are never touched.
- **No more in-play shader stutter (D480):** shaders the game has used are
  compiled at startup on the next launch.
- **Render-thread CPU roughly halved in typical scenes (D473),** plus fewer
  redundant texture lookups and palette re-imports (D474, D476).

---

## Fidelity fixes (checked against the N64 game)

- **Draw distance no longer changes AI behaviour (D466, #125).** The
  extended draw distance (default 250%) let guards "see" you from beyond the
  N64's fog, so Xenia's fight on Jungle could start early. AI awareness now
  uses the level's original distance; you can still see, shoot and use
  everything that's drawn.
- **Ultrawide and high field of view (D468):** the AI keeps the N64's widest
  (16:9) view. Turn on *Guards see the wider view* if you prefer the old
  behaviour.
- **Sniper rifle (#136):** the scope zooms all the way in again (it was
  capped at about a third of the original magnification, D484); zoom speed
  matches the N64 (it was about twice as fast at 60 fps, D485); the crouch
  key now crouches and stands with it out, as it does in GEPD (D494).
- **Turrets and explosions at 60 fps (D486, #126):** turret fire rate,
  tracers, explosion density and screen-shake length now match the N64.
  Damage is unchanged. (Explosions that sometimes don't appear in heavy
  firefights are the N64's own behaviour: it has six explosion slots.)
- **Weapons (#114–#118):** automatic-fire cadence follows the N64's rules at
  any frame rate; grenade launcher recoil is back; the gun sways with mouse
  look again; the tank turns with the mouse; the run-over death sound no
  longer loops.
- **Light fixtures go dark when shot, with the right impact effects
  (#119).**
- **Dam/Caverns water (D465, #127):** the water animation no longer slides
  side to side, and it scrolls at the N64's rate.
- **Kneeling guards** use their crouched-rifle firing animation (D445,
  thanks dolent, #120).
- **Dam:** the first-tower guard walks his whole patrol instead of a small
  circle (D490).
- **The Rareware logo** in the intro renders correctly (D75).
- Props near some doorways no longer count as being in extra rooms, and
  ejected casings spin as authored (D491).

---

## Stability & fixes

- **Knife-slash crash fixed (D454).**
- **64-bit memory-layout fixes** (D441, D453, D456, D457, D461, D462): two
  item/vertex tables were sized with N64 strides, and several pointer fields
  were truncated.
- A split-screen zero-size texture crash is guarded (D463); stale
  anisotropic filtering on recycled textures is fixed (D446, thanks dolent,
  #121).
- *Skip intro* no longer skips the failure dossier after a failed mission
  (D408).
- Adjusting the music volume with a controller in F10 no longer drops the
  frame rate (D489).
- Steam Deck: the Deck preset now also applies after a first launch in
  Desktop Mode (D283).
- Aztec: the area by the shuttle with the four turrets no longer breaks
  into stray triangles. The per-frame vertex buffer, sized for what the
  N64 could see, could overflow with the port's wider view (D500).
- In 3- and 4-player games, weapon pickup messages no longer show stray
  characters (D496).
- A controller no longer keeps vibrating after you quit mid-firefight
  (D493).
- *Aspect → Original* no longer shows a one-pixel strip of flickering
  pixels at the left and right edges (D493).
- Crash logs now include resolved stack frames, which makes reports far
  easier to act on.

---

## Known issues

- **NTSC-U (US) ROMs only.** PAL and JP ROMs convert, build and boot from
  source on this release's code, but the release packages don't support them
  yet (#85).
- **No macOS or ARM builds yet.**
- The first frame of a level takes a little longer while its textures
  upload: a brief dip in the FPS counter.
- **`Skip intro` and `All unlocked` are still experimental.**
- Far objects almost hidden in fog can show as a faint outline with
  straight edges (e.g. the satellite dish seen from Surface's start area).
  The N64's 16-bit colour hid it (D503).
- The full list, with workarounds, is the
  [known-issues table](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/ROADMAP.md#known-issues).

---

## Downloads

| File | Platform |
|---|---|
| `goldeneye-pc-port-<version>-win64.zip` | Windows x86-64 |
| `goldeneye-pc-port-<version>-linux-x86_64.tar.gz` | Linux x86-64 (incl. Steam Deck) |

Each contains the engine executable, a README, license texts, and the
one-time asset tool. **No ROM, no game assets.** The Windows bundle carries
its runtime DLLs and the Linux bundle carries SDL2, so nothing needs to be
installed first.

## Running it

You need your own **GoldenEye 007 N64 ROM** (US version, `.z64`).

1. Unpack the archive.
2. Make a `data/` folder next to the executable and put the ROM in it, named
   `ge007.ntsc-final.z64`.
3. Run the executable from that folder. The first start takes a few extra
   seconds while it generates its asset files from your ROM.

**Updating from an earlier version:** unpack into a new folder and copy your
ROM, `ge007.eep` (your progress) and `ge007.ini` (your settings) into its
`data/` folder. Don't copy the old `pcmodels-*` / `pccg-*` folders: the new
version makes its own.

**Steam Deck:** do the steps above on the Deck, then add the executable as a
non-Steam game. The options overlay opens with **Select** and is fully
controller-driven.

Full steps, controls and troubleshooting are in the bundled `README.md`.

## Verify the download

```
sha256sum -c goldeneye-pc-port-<version>-win64.zip.sha256
sha256sum -c goldeneye-pc-port-<version>-linux-x86_64.tar.gz.sha256
```

## Thanks

dolent (#120, #121, #122), italoarruda (#107, #109), MST246 (the #125
investigation and handoff used for the fix), JosephAHK (#133), MistaEcho
(#119 via #87), and everyone who filed #92, #114–#119, #125, #126, #127,
#129, #130 and #136.

## Source & docs

<https://github.com/jkdansereau/goldeneye-pc-port>, built on the
[GoldenEye 007 decompilation](https://github.com/n64decomp/007), with its
architecture modelled on the [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark).
Non-commercial fan preservation/research project; not affiliated with any
rights holder. **AI disclosure:** built through agentic AI coding (Claude
Code + a local open-weight model), directed by one person in their spare
time. See the README's
[How it was made](https://github.com/jkdansereau/goldeneye-pc-port#how-it-was-made)
section for the full account.
