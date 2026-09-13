---
title: GoldenEye 007 PC Port
description: >-
  A native PC port of the original Nintendo 64 GoldenEye 007, built from its
  decompiled source with a software RSP. v0.2.0 pre-release is out for Windows
  and Linux (including Steam Deck) — download it, drop in your own ROM, and
  play; or dig into the code and the engineering record behind it.
---

# GoldenEye 007 PC Port

A native PC port of the original 1997 Nintendo 64 _GoldenEye 007_, compiled
from the game's [decompiled source](https://github.com/n64decomp/007) with
the N64's graphics coprocessor (RSP) running in software. It follows the
architecture of the
[Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark), the same
Rare "Indy" engine family, one hardware generation apart.

It's also a case study in AI-agent collaboration on a large, low-level
codebase: two coding agents (a local open-weight model on a single RTX 5090,
and Claude / Claude Code), driven by one person part-time through ~230
translation units of unmodified big-endian MIPS game code, handing work back
and forth through shared written notes.

**Status: v0.2.0 pre-release.** The full single-player campaign runs at a
steady 60 fps with no known crashes, and audio (music + SFX) plays
throughout. Known rough edges: cutscenes still glitch, a few music tracks
sound wrong, and a handful of cosmetic rendering defects remain — the
[README's Status section](https://github.com/jkdansereau/goldeneye-pc-port#status)
is the honest list.

## Download

| Platform | Bundle | Notes |
|---|---|---|
| **Windows** (x86_64) | [win64.zip](https://github.com/jkdansereau/goldeneye-pc-port/releases) | Engine + runtime DLLs + the one-time asset tool. |
| **Linux** (x86_64) / **Steam Deck** | [linux tarball](https://github.com/jkdansereau/goldeneye-pc-port/releases) | SDL2 is bundled — it runs as-is on any distro, and sideloads onto a Deck with nothing installed. |

Both bundles contain **no ROM and no game assets** — you supply your own
GoldenEye 007 N64 ROM (the
[Requirements table](https://github.com/jkdansereau/goldeneye-pc-port#requirements)
has the three region filenames and SHA-1s). Then: unpack, drop the ROM in
`data/`, run `prepare-assets` once, launch. The full four steps are in the
[Quick start](https://github.com/jkdansereau/goldeneye-pc-port#quick-start);
pre-built releases are legal to distribute precisely because they're useless
without a ROM you already own.

## Play it — or take it apart

- **Try it yourself** — grab a bundle above, bring your own ROM, and play the
  campaign. It's a pre-release cut for exactly this: if something breaks, an
  [issue](https://github.com/jkdansereau/goldeneye-pc-port/issues) with what
  you were doing is genuinely useful.
- **Read the code** — the game logic in `src/` is unmodified decompilation;
  every hardware surface (video, audio, input, save storage, and the
  software-RSP renderer) is shimmed in the MIT-licensed `port/` layer.
  [Internals](internals.md) is the map; [Porting notes](porting-notes.md) is
  the catalogue of N64→PC bug classes hit along the way — a good read even if
  you never touch the code.
- **Mod it** — the port layer, build system and `tools_pc/` helpers are MIT
  licensed and yours to extend: new video options, input tweaks, your own
  asset sidecar. [CONTRIBUTING](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/CONTRIBUTING.md)
  has the ground rules that keep it faithful to the original game.
- **The research angle** — [the two-agent development case study](dev/agentic-development.md):
  goal, setup, timeline, the handoff workflow, and an honest assessment of
  what did and didn't work.

## In-engine

<p align="center">
  <img src="img/attract-bunker1.png" width="45%" alt="Bunker 1 intro camera, rendered by the port">
  <img src="img/attract-dam.png" width="45%" alt="Dam intro camera, rendered by the port">
</p>

<p align="center">
  <img src="media/goldeneye-demo.gif" width="60%" alt="~15 s of the port running: mission dossier, Facility, Silo, Jungle, Archives">
  <br><em>~15 s of the port running: mission dossier &rarr; Facility &rarr; Silo &rarr; Jungle &rarr; Archives (the clip has no audio track).</em>
</p>

<details>
<summary><strong>Full playtest gallery</strong> — 19 in-engine captures from the v0.2.0-pre Windows playtest, across the campaign</summary>

<p align="center">
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-01.jpg" width="100%" alt="In-engine — Dam (v0.2.0-pre)">
    <div><small>Dam</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-02.jpg" width="100%" alt="In-engine — Facility (v0.2.0-pre)">
    <div><small>Facility</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-03.jpg" width="100%" alt="In-engine — Runway (v0.2.0-pre)">
    <div><small>Runway</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-04.jpg" width="100%" alt="In-engine — Surface (v0.2.0-pre)">
    <div><small>Surface</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-05.jpg" width="100%" alt="In-engine — Surface (v0.2.0-pre)">
    <div><small>Surface</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-06.jpg" width="100%" alt="In-engine — Bunker 1 (v0.2.0-pre)">
    <div><small>Bunker 1</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-07.jpg" width="100%" alt="In-engine — Silo (v0.2.0-pre)">
    <div><small>Silo</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-09.jpg" width="100%" alt="In-engine — Frigate (v0.2.0-pre)">
    <div><small>Frigate</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-10.jpg" width="100%" alt="In-engine — Frigate (v0.2.0-pre)">
    <div><small>Frigate</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-11.jpg" width="100%" alt="In-engine — Surface 2 (v0.2.0-pre)">
    <div><small>Surface 2</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-13.jpg" width="100%" alt="In-engine — Bunker 2 (v0.2.0-pre)">
    <div><small>Bunker 2</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-16.jpg" width="100%" alt="In-engine — Statue (v0.2.0-pre)">
    <div><small>Statue</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-19.jpg" width="100%" alt="In-engine — Statue (v0.2.0-pre)">
    <div><small>Statue</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-20.jpg" width="100%" alt="In-engine — Archives (v0.2.0-pre)">
    <div><small>Archives</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-21.jpg" width="100%" alt="In-engine — Cradle (v0.2.0-pre)">
    <div><small>Cradle</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-23.jpg" width="100%" alt="In-engine — Cradle (v0.2.0-pre)">
    <div><small>Cradle</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-24.jpg" width="100%" alt="In-engine — Aztec (v0.2.0-pre)">
    <div><small>Aztec</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-25.jpg" width="100%" alt="In-engine — Aztec (v0.2.0-pre)">
    <div><small>Aztec</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-26.jpg" width="100%" alt="In-engine — Frigate (v0.2.0-pre)">
    <div><small>Frigate</small></div>
  </div>
</p>
</details>

More captures land here as playtesting continues (Steam Deck session next).

## Documentation

- [The two-agent development case study](dev/agentic-development.md) — goal, setup, timeline, the handoff workflow, and an honest assessment of what did and didn't work.
- [Development process](dev-process.md) — how work was scoped, partitioned, and budgeted across agents; the finding-log discipline.
- [Internals](internals.md) — architecture, the software RSP-emulation approach, GoldenEye-vs-Perfect-Dark engine differences, the phased plan.
- [Porting notes](porting-notes.md) — the recurring Nintendo 64 → PC bug classes hit during the port, with fixes.
- [Building](building.md) — full build and asset-extraction guide.
