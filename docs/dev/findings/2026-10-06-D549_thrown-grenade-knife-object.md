# D549

<!-- from the old section-F/H table row -->
| D549 | **Thrown grenade/knife/object: `s32 sp94` receives a 12-byte `coord3d` from `bullet_path_from_screen_center`** — D24 class (N64 overflow hit a dead slot, PC can hit live locals); full `## D549` entry at file tail | FIXED 2026-10-06 (AVOID_UB coord3d, gun.c x3; build-verified) |

## D549: thrown grenade / knife / object -- `s32 sp94` receives a 12-byte coord3d — FIXED 2026-10-06 (D545 follow-up triage)

`generate_player_thrown_grenade`, `generate_player_thrown_knife` and
`generate_player_thrown_object` (`src/game/gun.c`) declare `s32 sp94` and pass
`&sp94` to `bullet_path_from_screen_center(coord3d *, ...)`, which writes x/y/z.
On the N64 (sp94 at 0x94) the extra 8 bytes ended at `spA0_a` on a slot that is
dead at that point (the mine path's `if (wor == NULL)` after the call proves
`wor` was not hit there); with GCC's layout they can hit live locals. Fix:
`struct coord3d sp94` under `#ifdef AVOID_UB`, original under `#else`; nothing
else reads sp94. Same class as D545 / D490 (porting-notes §D24). Possibly
related to older "thrown items through walls" reports; not demonstrated.
