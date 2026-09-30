---
title: Self-hosted level sweep
description: How to run the ROM-gated 21-level sweep on your own self-hosted GitHub Actions runner (Windows or macOS), with your own ROM.
---

## Self-hosted level sweep (bring your own runner + ROM)

GitHub-hosted CI builds the port but cannot run it: the ROM is not
distributable, so no hosted job ever sees one. Level runs happen on a
**self-hosted runner that you own, with your own legally obtained ROM**,
using `.github/workflows/selfhosted.yml`. The workflow works unchanged on
a fork, and you can run it there to post results on a PR.

> **Status: scaffold.** The macOS path has not been run end to end yet. It
> needs the arm64 address model (#95) on the ref you run it against; until
> then it stops early with a clear message. Please report what breaks.

### What the workflow does

1. It copies the ROM and the extracted sidecars from a **seed folder on the
   runner** into `data/`, and checks the ROM's sha1 against the committed
   `ge007.u.sha1`.
2. It builds `ntsc-final`.
3. It runs the level sweep:
   - Windows: `tools_pc/verify.sh sweep`.
   - macOS: `tools_pc/level_sweep_mac.sh`, which boots each level, then
     checks for a crash log and a non-black frame.
4. It uploads **console logs and a JSON verdict only**. Rendered frames are
   ROM-derived and are never uploaded, and nothing is uploaded from `data/`.

### One-time setup

1. **Fork the repo** and enable Actions on the fork.
2. **Register a runner.** Go to the fork's *Settings → Actions → Runners →
   New self-hosted runner* and follow GitHub's steps.
   - Default labels on macOS: `self-hosted`, `macOS`, `ARM64`.
   - Default labels on Windows: `self-hosted`, `Windows`, `X64`.
   - Running it as a service is optional.
3. **Install the build dependencies** from [Building](building.md):
   - Windows: MSYS2 at `C:\msys64`. `tools_pc/check-selfhosted-deps.sh`
     lists the packages it needs.
   - macOS: `brew install cmake gcc sdl2 zlib python3`.
4. **Fill the seed folder.** Build and run the game once locally so the
   sidecars get extracted. Then copy these three items into the seed folder:
   - `ge007.ntsc-final.z64` (the US ROM);
   - `pcmodels-ntsc-final/`;
   - `pccg-ntsc-final/`.

   By default the seed folder is `_rom-seed/` inside the runner's install
   folder, next to `_work/`. To use a different folder, set a repository
   **variable** (not a secret, since it's only a path) named
   `GE_ROM_SEED_DIR`.

### Running it

From the fork's Actions tab, pick **Self-hosted runtime → Run workflow**. Or
use the command line:

```sh
gh workflow run selfhosted.yml --repo <you>/goldeneye-pc-port \
  --ref macos -f platform=macos
```

Inputs:
- `platform`: `windows` (default) or `macos`.
- `runner_labels`: an optional JSON array that overrides the runner labels,
  for example `["self-hosted","macOS","ARM64"]`.
- `levels`: a sweep subset in `Name:NN` form. **Windows only.** The macOS
  sweep always runs all 21 levels.

Share results by linking the run, or by pasting `sweep-verdict.json` into the
PR. Maintainers treat fork results as evidence rather than a merge gate, and
test firsthand before merging.

### Rules (please don't relax these on your fork)

- **Never add a `pull_request` trigger** to this workflow. It must never run
  code from someone else's PR on a machine that can read your ROM. The only
  trigger is `workflow_dispatch`.
- **Don't put the ROM in `actions/cache`.** Cache entries are stored by
  GitHub, and pull-request workflows can restore them, which would make the
  ROM readable to any PR's workflow. The macOS path only uses the local seed
  folder.
- Under *Settings → Actions → General*, set **"Require approval for all
  outside collaborators"** on the fork.
