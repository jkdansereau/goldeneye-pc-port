# D593

<!-- from the old section-F/H table row -->
| D593 | **Windows x86_64 — an occasional render-thread stall fired the kernel's "no frame rendered for N ms" watchdog (observed once, 8001 ms, on an NVIDIA RTX 5090 during a `GE_TEXDUMP`/`GE_TEXRAW` heavy-I/O debug run; pre-existing, NOT the D591 x86_64 no-op). Tracked, not yet root-caused (probe I/O load vs a genuine render-thread hang).** — full `## D593` entry at file tail | OPEN 2026-10-08 (Windows; single observation; repro + root-cause owed) |

## D593: Windows x86_64 — an occasional render-thread stall fired the kernel's "no frame rendered for N ms" watchdog (observed 8001 ms) — OPEN 2026-10-08 (Windows; tracked from a single observation during the D591 verification; pre-existing, root-cause owed)

### Observation
During the D591 Windows verification (an `ntsc-final` build on an NVIDIA
GeForce RTX 5090, GL 4.6), one run with the heavy-I/O debug probes enabled
(`GE_TEXDUMP=1 GE_TEXRAW=1`, which `fopen`/`fwrite` hundreds of
`texdump/*.bin` / `.ppm` / `.rgba32` files) **stalled the render thread for
8001 ms** and the kernel's render-park watchdog logged **"no frame rendered
for 8001 ms"** before the run recovered / was cut. The same run family
otherwise exits 0 and N-1 of the batch were clean, so this is a flake, not a
deterministic hang.

### Context
- **Pre-existing / not D591.** The D591 fix (`gfx_tex_source_is_c_array`) is
  a no-op at `PORT_ADDR_BASE==0` (x86_64) — it cannot stall the render
  thread. The stall was observed while D591 code was present, but the
  mechanism is unrelated.
- **Suspected trigger: the probe I/O load.** The `GE_TEXDUMP` /
  `GE_TEXRAW` / `GE_TEXDUMPROW` family writes a large number of small files
  on the render thread; a page-fault / disk-flush burst on a fast-GPU host
  can exceed the watchdog threshold. If so, the stall is **probe-induced**
  (a debugging-tool cost), not a real game defect — normal play (no `GE_`
  probes) is unaffected.
- **Alternative: a genuine one-off render-thread / GPU-driver stall** (a
  D344-class `exit()`-during-GL-render interaction, or an NVIDIA driver
  hiccup). This would be the more important case.

### Next (owed)
- **Reproduce in isolation:** a plain (no-`GE_`-probe) Win loop vs a
  probe-heavy loop; record whether the "no frame rendered" watchdog only
  fires with the heavy I/O. That separates probe-induced cost from a real
  hang.
- If probe-induced: document it as a known cost of the `GE_` dump family
  (and, if cheap, batch the file writes or move them off the render thread).
  If a genuine hang: root-cause the render-thread stall (D344 / driver) and
  fix in the port layer.

### Cross-tag
D591 (observed during its Win verification; the fix is a confirmed x86_64
no-op), D344 (the `exit()`-during-GL-render memory class the render-park
watchdog guards). Not on the D589/D592 critical path (a Windows reliability
flake, independent of the arm64 GL-layer work).
