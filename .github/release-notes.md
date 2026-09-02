## GoldenEye 007 PC Port — alpha

> ⚠️ **Very early research alpha. Not a playable game.**
> The intro and menus render and all 21 solo missions load, but:
> - **no audio** (Phase 3, not started);
> - some front-end 3D models and some text are broken;
> - **ladders don't work** and can block progression on some levels;
> - expect crashes and glitches once past the level intro.
>
> This is a technical demo of the porting work. If you want to *play* GoldenEye
> on PC, use one of the Xbox 360 recompilation projects instead — see the
> [project README](https://github.com/jkdansereau/goldeneye-pc-port#how-this-differs-from-the-other-goldeneye-pc-projects).

### Download

`goldeneye-pc-port-<version>-win64.zip` — the engine executable, its runtime
DLLs, a README, and license texts. **Windows x86-64 only.** No ROM, no game
assets.

### Running it

You supply your own **NTSC-U GoldenEye 007 N64 ROM** (`.z64`, big-endian,
`SHA-1 abe01e4aeb033b6c0836819f549c791b26cfde83`). Unzip, put the ROM in a
`data/` folder next to the exe as `ge007.ntsc-final.z64`, run the exe from that
folder. Full steps in the bundled `README.md`.

### Verify the download

```
sha256sum -c goldeneye-pc-port-<version>-win64.zip.sha256
```

### Source & docs

<https://github.com/jkdansereau/goldeneye-pc-port> — built on the
[GoldenEye 007 decompilation](https://github.com/n64decomp/007), architecture
after the [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark).
Non-commercial fan preservation/research project; not affiliated with any
rights holder.
