# D983

## D983: Jungle rooms were scissored to their portal boxes (the start's crashed plane lost a wing) -- `specialportalarray` walk ended at a linker neighbour -- FIXED + MAINTAINER-CHECKED 2026-11-16 (Ryujinx, Jungle start: the plane stays whole) [porting-notes D5]

**Symptom.** Jungle start: from room 2, the crashed plane (room 1 geometry) was cut by a vertical line, a wing missing. Present since the original port (0.1.0 Switch, and the Windows build). ares draws it whole from every point.

**Ruled out.** D980 foliage depth (0.1.0 has it too). Display presets, FMA contraction (Windows x86-64 shows it too), `controlbytes2` (0 in the ROM for every Jungle portal), the portal projection itself (`GE_NOROOMSCISSOR` probe: the wing is room 1 geometry, the scissor cuts it; the box matches the portal opening).

**Cause.** An ares RDRAM dump at the same view (room list `dword_CODE_bss_8007FFA0`, US 0x8007FFA0, 0x1C-byte `s_bound_info` entries): N64 has the same rooms in the same order, but every box full screen and `next = 2`, i.e. reached through portals with `PORTALFLAG_SPECIAL`, whose branch in `sub_GAME_7F0B7F84` draws the room full screen. The PC never set the flag: `sub_GAME_7F0B37EC` walks `specialportalarray` (per level index: portal ranges to flag; 0x03 Control a few ranges, 0x11 Jungle 0x00-0x3A, i.e. all) up to `&g_BgCurrentRoom`, the variable the N64 linker placed right after the table, comparing `(u32)` pointers. On PC the walk stopped after the first entry, so only Control was flagged (the D128 fix of the write itself only mattered there).

**Fix.** `#ifdef PORT`: end at `specialportalarray + sizeof(specialportalarray)` and compare full-width pointers. Layout-only (porting-notes D5); no logic change. It applies always, not under Video.Visuals: it restores the N64 behaviour.

**Also affects.** Every Jungle room (all portals are special there): boxes are now full screen as on N64, so props and room geometry that cross a portal are no longer cut anywhere in the level, and slightly more is drawn (each room full screen instead of its portal box). Control's flagged portals were already right.
