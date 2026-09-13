## GoldenEye 007 PC Port — <version> (pre-release)

> **Pre-release.** The full single-player campaign runs at a steady 60 fps
> with no known crashes, audio (music + SFX) playing throughout. This cut is
> for playtesting: a full end-to-end confirmation that all 21 missions are
> completable on this build is still owed, and the known issues below are real.

### What's new since v0.1.0

- **Steady 60 fps** in normal play (the software RSP runs off the presentation
  critical path); `Video.DisplayFPS` in the F10 overlay shows it.
- **Audio**: in-level music and sound effects throughout (the alpha was
  silent). A handful of tracks have wrong-sounding instruments (below).
- **Mouse**: click-to-lock capture (click to grab, ESC to release) with a
  proportional GEPD-style aim mode; sensitivity / Y-inversion / aim-turn split
  tunable in `ge007.ini` or the F10 overlay. The legacy always-grab mode is
  gone.
- **Rendering**: outdoor skies render correctly; water no longer renders
  green/pulsing; reflective surfaces (glass, chrome weapon skins) work.
- **Crash fixes**: the two v0.1.0-era crashing levels (Bunker ii, Statue) and
  the AI-pacing bug that broke Cradle are fixed and playtest-verified — all 21
  solo missions load and run crash-free.
- **QoL**: F10 in-game options overlay (fullscreen, resolution, frame cap,
  MSAA, texture filtering, FOV/draw distance, sensitivity), mute-on-focus-loss,
  F12 screenshot.
- **Modern dual-stick controller layout** (the scheme used by the console
  re-releases): left stick move/strafe, right stick look, right trigger fire,
  left trigger aim, A/X use, B/Y crouch/cancel, **RB/LB cycle weapons**.
- **Linux / Steam Deck**: the Linux bundle now ships its own SDL2 — it runs
  as-is on any distro, and sideloads onto a Steam Deck with nothing installed.

### Known issues

- **Cutscenes frequently glitch** — skipped beats, wrong camera, misplaced or
  hovering actors, wrong timing; the Dam level-end cutscene is racy (D243).
  The most visible gap in this release.
- **Music quality on some tracks** — a wrong-sounding bass instrument on a few
  elevator/level tracks (D230).
- **Particle colours cycle through a rainbow palette** — bullet-impact sparks
  and lingering smoke/explosion residue drift through the hues over time
  instead of holding their intended grey/orange palette (D252).
- Water on `IsWater` levels shows a moving seam between two patterns (D245);
  thin pixel strips at the left/right screen edges at non-integer window
  scales (D246); character face textures can wrap on Silo (D197).
- Some front-end 3D models are mispositioned or absent — the spinning
  Nintendo logo and the MISSION COMPLETE / mode-select models (D75).
- The F10 overlay occasionally shows a ghost repeat of the top row at the
  panel bottom in true 4K fullscreen with MSAA on (intermittent, display-
  specific; windowed is unaffected).
- **Not yet verified on real Steam Deck hardware** — the bundle is built for
  it and its prime crash suspect from v0.1.0 was fixed, but please report any
  Deck-specific faults (a `ge007.crash.log` next to the exe helps).

### Downloads

| File | Platform |
|---|---|
| `goldeneye-pc-port-<version>-win64.zip` | Windows x86-64 |
| `goldeneye-pc-port-<version>-linux-x86_64.tar.gz` | Linux x86-64 (incl. Steam Deck) |

Each contains the engine executable, a README, license texts, and the
`prepare-assets` tool. **No ROM, no game assets.** The Windows bundle carries
its runtime DLLs; the Linux bundle carries SDL2 — on both platforms nothing
needs to be installed first.

### Running it

You supply your own **GoldenEye 007 N64 ROM** (`.z64`, big-endian) that you
legally own — NTSC-U (US), PAL (EU) or NTSC-J (JP); the asset step detects the
region from the SHA-1. US is the best-tested.

1. Unpack the archive.
2. Make a `data/` folder next to the executable and put the ROM in it, named
   per region (`ge007.ntsc-final.z64` / `ge007.pal-final.z64` /
   `ge007.jpn-final.z64`).
3. Run the one-time asset step (needs Python 3.8+):
   `python3 prepare-assets/prepare-assets.py` — it reads your ROM and writes
   the two `data/pc*-<region>/` folders the engine needs. Standard library
   only; a few seconds.
4. Run the executable **from that folder**.

**Steam Deck:** sideload the unpacked folder (USB or a file manager), do steps
2–4, then add the executable to Games → *Add Game* as a non-Steam game.

**In-game settings on the Deck:** the options overlay is fully gamepad-driven
— it opens with **Select**, the D-pad or left stick (up/down) moves between
options, **A** steps the selected option forward, **B** steps it back, and
**Start** (or Select again) closes. No keyboard needed.

Full steps are in the bundled `README.md`.

### Verify the download

```
sha256sum -c goldeneye-pc-port-<version>-win64.zip.sha256
sha256sum -c goldeneye-pc-port-<version>-linux-x86_64.tar.gz.sha256
```

### Source & docs

<https://github.com/jkdansereau/goldeneye-pc-port> — built on the
[GoldenEye 007 decompilation](https://github.com/n64decomp/007), architecture
after the [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark).
Non-commercial fan preservation/research project; not affiliated with any
rights holder.
