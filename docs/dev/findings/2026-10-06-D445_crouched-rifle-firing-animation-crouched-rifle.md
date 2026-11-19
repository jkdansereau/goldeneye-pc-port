# D445

<!-- from the old section-F/H table row -->
| D445 | **Crouched-rifle firing animation (`crouched_rifle_firing_animation_groupA`) never resolved on PC -> SIGSEGV in modelConstrainOrWrapAnimFrame when an MP player crouch-fires a rifle (2026-09-28).** — full `## D445` entry at file tail | FIXED — layout only (§D5 linker adjacency, D164 class): `crouched_rifle_firing_animation_group1` has no zero terminator and on N64 falls through into `...groupA` (source-order .data); GCC places groupA first, so groupA[0] kept the raw anim offset 0xB84. `#ifdef PORT` one array {right, left, terminator} + `groupA` alias (chr.c/chr.h); N64 resolution restored. |

## D445 — crouched-rifle firing animation never resolved on PC (linker adjacency) -> MP crash (2026-09-28)

**Symptom.** SIGSEGV in `modelConstrainOrWrapAnimFrame` from `playerTick`
(bondview2.c ~11021) with `anim = 0xb84`, when a multiplayer player
crouch-fires a rifle (found by the 5PLUS bot runs: Complex, 4 seats, M16,
frame 5030; present in the 0.9.x friend bundles). Found on
`exp/5plus-multiplayer` as "D341" (renamed D420 in PR #120, then D445 on merge: D341 and D420 are already used here).

**Root cause (layout, porting-notes §D5 "linker adjacency", D164 class).**
`firing_animation_groups[1][5]` points at `crouched_rifle_firing_animation_groupA`
(chr.c). The tables hold anim *offsets*; `initWeaponAnimGroups` ->
`initResolveAnimGroupTable` rebases each table up to its zero terminator.
`crouched_rifle_firing_animation_group1` has ONE entry and **no terminator**:
on N64 .data is laid out in source order, so resolving group1 walks on into
groupA (resolving it too, len 2). GCC placed groupA *below* group1, so
groupA[0] kept the raw offset `PTR_ANIM_fire_kneel_left_leg` (0xB84) and
`modelSetAnimation` received it as a `ModelAnimation*`. Side effect before
the fix: crouching rifle guards saw len 1 instead of the N64's 2.

**Not covered upstream:** upstream D392 (chraction.c firing-table fields)
and D336 (bondhead.c) are different sites; chr.c/chr.h were unchanged on
09278407.

**Fix (8595a06e, `#ifdef PORT`, `#else` = decomp):** chr.c one array
`{kneel_right_leg, kneel_left_leg, terminator}` (exactly the N64 layout);
chr.h `#define crouched_rifle_firing_animation_groupA
(&crouched_rifle_firing_animation_group1[1])`. Resolution and len are the
N64's; no logic change. Upstream candidate (a real port bug).

**Verified (programmer, 5plus + main):** repro (4 seats, bots, Complex,
pinned seed, 18 078 frames x3) no crash, SH identical across runs; 8 seats
remote mines no crash; every firing/struck anim table in chr.c scanned --
this was the only unterminated one; gdb scan of all 24
`firing_animation_groups` entries at run time: 1 bad pointer before, 0 after
(also 0 on the main-line cherry-pick); P1 gate (1 324 recordings, frame
dumps) unchanged; netplay 2P/4P identical.
