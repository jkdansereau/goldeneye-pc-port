# D434

<!-- from the old section-F/H table row -->
| D434 | **Raw `((u8*)&g_Textures[n])[0] & 0xf` hit-type reads are wrong on PC; latent OOB once D431 returned real texture ids (found by sub-agent review).** — full `## D434` entry at file tail | FIXED (`.hitTexture` at 3 sites, port-guarded); live check owed. |

## D434 — Raw `((u8*)&g_Textures[n])[0] & 0xf` hit-type reads are wrong on PC (found by sub-agent review of D431; latent OOB in `g_HitTypeSounds[13]`)

Under PORT `struct image_entry` puts `dataoffset:24` in bits 0-23 (D67), so byte 0 is the LOW byte of the data offset. On the N64 (big-endian) byte 0 is the MSB of word 0, the hit type. `chrprop.c` (impact_sounds ~1154, the explosion skip test ~1187) and `propobj.c` (~9685) used the raw byte, so on PC they indexed `g_HitTypeSounds[]` with an arbitrary nibble 0..15 (table has 13 entries; 13-15 read past it) and mis-tested the 5/6 spark skip. Harmless while `bghit.texturenum` was always -1 (D154); D431 now returns real ids for light fixtures, activating the path. (The Caverns playtest passed, so the light images' nibbles were presumably in range: luck, not design.) The comment in `image.h` claiming the N64 does the same was wrong and is corrected.
**Fix (`#ifdef PORT`, layout-driven, semantics-preserving):** use the `.hitTexture` bitfield at the three sites, as `chr.c` already does. **General rule (for porting-notes):** un-stubbing a `-1` sentinel (D154/D135/D431) re-activates every dormant consumer of the value -- grep all readers and check each for raw-layout reads and table-bounds assumptions first.
**Status:** FIXED; built. Live check owed (shoot light fixtures across levels; impact sound/sparks on lights).
