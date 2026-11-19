# D120

<!-- from the old section-F/H table row -->
| D120 | **Blood-decal `PointUsage[]` chain — converter now emits it (M-30).** — full `## D120` entry at file tail | FIXED (`tools_pc/d43_emit.py`). Interactive verify pending — blood decals on a BUNKER1 firefight should now appear + no hang. |

## D120 — Blood-decal `PointUsage[]` chain — converter now emits it (M-30).

**Blood-decal `PointUsage[]` chain — converter now emits it (M-30).** `d43_emit.py` reserved the opcode-0x18 `PointUsage` region in the layout pass (`add_region(puo, 2*nv)`) but the **emit pass never wrote it** — every entry stayed zero. `chr.c`'s decal walk `index = PointUsage[index]` then cycled 0→PointUsage[0]→0 forever (capped by the `#ifdef PORT` guard at `chr.c:3328`, so decals were missing/wrong rather than a true hang). Fix: `op24_pointusage[puo] = nv` in layout + a byteswap loop (`put_s16(remap(puo)+2k, be16(src, puo+2k))` for `nv` = `numVertices` entries — a negative-terminated `s16` index chain, no remap) in the emit pass. Full regen chain re-run, round-trip validation passes. The `chr.c` guard stays as a safety net (SMALL-FIXES B3).

**Status (from §F table):** FIXED (`tools_pc/d43_emit.py`). Interactive verify pending — blood decals on a BUNKER1 firefight should now appear + no hang.
