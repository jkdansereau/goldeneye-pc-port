# Switch: the N64 address window under Horizon

How the Switch build hosts the port's N64 address window, and why. Code:
`port/src/switch/nxmem.c` (`port/include/nxmem.h`), `port/include/portaddr.h`,
`port/src/dram.c`, `port/src/romdata.c`, `port/src/libultra.c`
(`portAllocLowStack`). History and console checks: findings D958 (run-time
base, quit to the launcher) in `docs/dev/findings.md`.

## The constraint

The port maps the N64's 32-bit space onto the host window
`[PORT_ADDR_BASE, +4 GiB)` (`portaddr.h`): cart at `+0x10000000`, DRAM V1 at
`+0x70000000`, its KSEG0 mirror at `+0x80000000`, game-thread stacks at
`+0xA0000000`. The game truncates host pointers to `u32`/`s32` and converts
them back (`PORT_N64PTR(T, (s32)ptr + off)`), so **`(u32)ptr` must equal the
N64 address**: the base must be 4 GiB-aligned. Windows and Linux use base 0,
macOS a fixed 16 TiB base.

Horizon makes a fixed base impossible:

- 39-bit address space; the heap, alias, stack and ASLR regions are placed at
  random per process.
- No `mmap(MAP_FIXED)`. `svcMapMemory` maps only into the stack region and
  makes the source inaccessible.
- The main thread's stack is placed at random in the stack region
  (`0x08000000–0x80000000`) when the process is created. With the windows at
  base 0 it sat on the cart or DRAM range in about 2 % of launches (the first
  Switch release had this boot failure). An automatic relaunch is not possible
  from sphaira / hbmenu.

## Mapping methods (console test, 2026-11-11)

`tools/switch/memprobe`, title takeover. Regions of the hbloader process: heap
`0x1a1600000` (8 GiB), alias `0xbd8800000` (6 GiB), stack
`0x08000000–0x80000000` (1920 MiB), ASLR `0x08000000–0x1000000000` (64 GiB).

| Method | Result |
|---|---|
| A. `svcMapMemory` heap → stack region | works, only below `0x80000000` |
| B. `svcMapPhysicalMemory` → alias region | refused, `0xfa01` (InvalidState) |
| C. code memory (`svcMapProcessCodeMemory` on the own process handle + `svcSetProcessMemoryPermission` RW) → ASLR region | works for all blocks at any free 4 GiB-aligned base |

Method C needs the own process handle, which hbloader provides; Ryujinx and
other launches do not.

## What the build does

**Base chosen at boot.** On the Switch `PORT_ADDR_BASE` is the variable
`g_portAddrBase`. `nxChooseAddrBase()` runs first in `portAddrInit`, before any
thread exists, and picks a 4 GiB slot in the ASLR region:

- slot 0 is skipped, so every launch takes the code-memory path;
- the search starts at a random slot and wraps;
- a slot is taken when the cart (128 MiB reserved), DRAM V1 and KSEG0, and
  the 512 MiB stack region are unmapped, **outside the kernel's heap and alias
  regions** (`svcQueryMemory` reports them unmapped but code memory fails
  there with `rc=0xd401`), and the slot does not hold the executable;
- no free slot aborts the boot with a message.

Without the own process handle the base stays 0 and the blocks go into the
stack region with method A (the old layout, with its collision risk).

`nxMapFixed(addr, size)` places zeroed heap memory at an exact address, with
method A inside the stack region and method C elsewhere.

| Block | Address | Placement |
|---|---|---|
| Cart (ROM + converted sidecars) | base + `0x10000000` | `nxMapFixed`; failure aborts boot (the game cannot run from a heap copy) |
| DRAM KSEG0 | base + `0x80000000` | `nxMapFixed` |
| DRAM V1 | base + `0x70000000` | `nxMapAlias` (`svcMapProcessMemory`), a second view of the KSEG0 block, checked by a write/read |
| Game-thread stacks | base + `0xA0000000` (512 MiB) | `nxMapFixed` per thread, 16 KiB guard between stacks, 8 MiB each (real memory: Horizon does not overcommit) |

If the V1 alias is refused, DRAM falls back to one block at V1 and
`g_nxDramK0Folded` makes `portN64ToHost()` fold KSEG0 addresses onto V1 in
software.

**What used to bake the base in:**

- The three DRAM symbols of `dram_syms.S` (`cfb_16`, `_bssSegmentEnd`,
  `animations_frame_buffer`) are run-time lvalues on the Switch
  (`PORT_DRAM_SYM`). The game-file edits for this are ABI/layout only, under
  `#if defined(PORT) && defined(__SWITCH__)` (`src/fr.h`, `src/bondgame.h`,
  `src/game/initanitable.c`, `src/crash.c`).
- ROM labels (`romassets_*.s`) stay link-time absolutes at their N64 value;
  code that reads through a label goes through `romdataCartPtr()`.

**Executable below 4 GiB.** hbloader places the NRO anywhere in the ASLR
region, below 4 GiB in about one launch in seven. fast3d used to treat any
value under 4 GiB as an N64 address, so a pointer to a static display list in
the image was misread. The `gSP*` macros now tag pointer-typed arguments
(`GBI_W1`, bit 62, `include/PR/gbi.h`); fast3d reads a tagged value as a host
pointer when it is at least 4 GiB or inside the image (`g_portImageEnd`).
Residual: a pointer-typed argument holding an N64 value that falls inside an
image placed below 4 GiB would still be misread (none seen).

**Teardown.** Quitting returns to the launcher: the port's threads stop
(`g_portQuit`) and are joined, then `nxReleaseAll()` unmaps aliases first,
then the blocks. libnx keeps a thread's stack mapped until the thread is
joined, and a block still mapped on return makes hbloader abort
(`rc=0xd401`).

## Debugging

- The startup log lists the regions, the chosen slot and every mapping
  (`nxmem:` lines).
- `-DNX_LINKSTDIO=ON` test builds send the log and crash dump over nxlink and
  quit by themselves, for unattended launch series:
  [nxlink-stdio.md](nxlink-stdio.md).
- `PORT_ADDR_STRICT` catches invalid conversions, including code that
  compares V1 and KSEG0 addresses directly.
