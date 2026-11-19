# D125

<!-- from the old section-F/H table row -->
| D125 | **crash classes C3+C6 (Aztec/Bunker2/Surface2):** — full `## D125` entry at file tail | **resolved (M-16b)** — line 374 → 8 NULs; all 21 sidecars regen'd; **Aztec `-level_28` now PASSES** (was C3 CRASH). Bunker2 falls through to a separate DOOR-tail `linkedDoor` layout bug (C3 residual) |

## D125 — crash classes C3+C6 (Aztec/Bunker2/Surface2):

crash classes C3+C6 (Aztec/Bunker2/Surface2): **root cause found (M-16)** — `tools_pc/d88_emit.py:374` assigns a 4-byte literal to an 8-byte slice (`out[dst_o+0x30:dst_o+0x38] = b"\x00…"`, the stan zero-fill in `emit_pad`); every pad/boundpad record silently shrinks the output bytearray by 4 B, and once the tail falls below the boundpad plink string blob, later verbatim leaf writes hit Python out-of-range slice semantics (insert at current end, not relocated offset) → boundpad names drift/truncate (Aztec pad33 `p138d2`→`8d2`) → `stanPackId()` reject → `getposstan()` NULL stan → `setupDoor` leaves door `model=NULL` → crash `propobj.c:13601`. M-14's "propDefs zeros in RAM" was a misread; sidecar propdefs were always correct (M-15) and post-load RAM matches the sidecar byte-for-byte (M-16)

**Status (from §F table):** **resolved (M-16b)** — line 374 → 8 NULs; all 21 sidecars regen'd; **Aztec `-level_28` now PASSES** (was C3 CRASH). Bunker2 falls through to a separate DOOR-tail `linkedDoor` layout bug (C3 residual)
