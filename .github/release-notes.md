## GoldenEye 007 PC Port — v0.1.0 alpha

> ⚠️ **Research alpha. Playable, not polished.**
> The full single-player campaign runs. In a full-campaign playtest on this
> build, **18 of 21 missions were completable start to finish**. Known issues:
> - **no audio** (Phase 3, not started) — the game runs silent;
> - **two levels crash mid-mission** — Bunker ii and Statue (one root cause, D191);
> - **AI characters move too slowly** — guards and escorts lag well behind their
>   N64 pace; this also makes the final level (Cradle) impossible to finish, as
>   Trevelyan's scripted sequence never completes (D193);
> - **cutscenes frequently glitch** — skipped, wrong camera, misplaced actors (D148/D160);
> - the spinning Nintendo logo on the intro renders wrong;
> - outdoor levels render with a **black sky**;
> - mouse aim and some textures/transparency have rough edges.
>
> If you just want to *play* GoldenEye on PC today, use one of the Xbox 360
> recompilation projects instead — see the
> [project README](https://github.com/jkdansereau/goldeneye-pc-port#how-this-differs-from-the-other-goldeneye-pc-projects).

### Downloads

| File | Platform |
|---|---|
| `goldeneye-pc-port-<version>-win64.zip` | Windows x86-64 |
| `goldeneye-pc-port-<version>-linux-x86_64.tar.gz` | Linux x86-64 |

Each contains the engine executable, a README, license texts, and the
`prepare-assets` tool. **No ROM, no game assets.** The Windows bundle also
carries its runtime DLLs; the Linux bundle links against your distro's SDL2 /
zlib / libGL (`sudo apt install libsdl2-2.0-0 zlib1g libgl1`, or the equivalent).

Windows development and playtesting is the primary path; the Linux build boots
and renders (tested on WSLg) but has had far less exercise.

### Running it

You supply your own **NTSC-U GoldenEye 007 N64 ROM** (`.z64`, big-endian,
`SHA-1 abe01e4aeb033b6c0836819f549c791b26cfde83`). Only the US ROM is supported
in this alpha.

1. Unpack the archive.
2. Make a `data/` folder next to the executable and put the ROM in it as
   `ge007.ntsc-final.z64`.
3. Run the one-time asset step (needs Python 3.8+):
   `python3 prepare-assets/prepare-assets.py`
   — it reads your ROM and writes the two `data/pc*-ntsc-final/` folders the
   engine needs. Standard library only; a few seconds.
4. Run the executable **from that folder**.

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
