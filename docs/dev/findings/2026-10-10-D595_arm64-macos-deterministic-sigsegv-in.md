# D595

<!-- from the old section-F/H table row -->
| D595 | **arm64 macOS — deterministic SIGSEGV in `modelSetAnimFrame` when an AI plays a hit/stagger/death animation (fire → `triggered_on_shot_hit` → `modelSetAnimation2`); the anim-table resolver `initResolveAnimTable` read the live `ptr_animation_table` host pointer as a truncated `(s32)`, dropping the 16 TiB `PORT_ADDR_BASE` on arm64 macOS → every `struck_anim` resolved to bogus low memory. §A1 live-host-pointer-truncation class (D588/D590/D591).** — full `## D595` entry at file tail | **FIXED 2026-10-08** (macos branch; read the base at full pointer width — identity on x86_64; verified no crash on the Mac) |

## D595: arm64 macOS — deterministic SIGSEGV in `modelSetAnimFrame` when an AI plays a hit/stagger/death animation — the anim-table resolver truncated the live `ptr_animation_table` host pointer to `(s32)`, dropping the 16 TiB base — FIXED 2026-10-08 (macos branch; §A1 cross-tag; unblocked by D594)

### Symptom
Once mouse fire worked (**D594** fixed), the first time a shot hit a guard and
the guard played a hit / stagger / death-reaction animation, the game SIGSEGV'd:
```
#02 modelSetAnimFrame + 164
#03 modelSetAnimation2 + 168
#04 triggered_on_shot_hit + 1172
#07 chraiDefaultWeaponFireHandler
```
arm64 macOS only — the identical fire → AI-anim path is clean on Windows and
Linux (x86_64).

### Root-cause
`initResolveAnimTable()` (`src/game/initactorpropstuff.c`) resolves the
`ModelAnimation *struck_anim` pointers of the `StruckAnim` reaction tables
(`death_stagger`, and the `g_HitReactionTable[*].deathAnims / flinchAnims` via
`initWeaponAnimGroups`). Each entry holds a small N64 ROM offset
(`PTR_ANIM_*`, e.g. `0x1C`); the resolver adds it to the base of the loaded
anim table, `ptr_animation_table` (`mempAllocBytesInBank(…, MEMPOOL_PERMANENT)`
— a **live host pointer**).

The PORT path read that base **as a truncated `(s32)`**:
```c
entry[-1].struck_anim = (ModelAnimation *)(uintptr_t)(u32)((*((s32 *)entries)) + (0, address));
```
On every `PORT_ADDR_BASE == 0` platform (Windows / Linux x86_64) the PERMANENT
bank is s32-low (`0x70xxxxxx`), so the `(s32)` read is identity. On **arm64
macOS** (`PORT_ADDR_BASE = 16 TiB`), `ptr_animation_table` is `0x10007xxxxxx`;
reading it as `(s32)` keeps only the low 32 bits, so every resolved
`struck_anim` landed in bogus low memory (`0x00007xxxxxx`). The first time
`modelSetAnimation2` handed that garbage `ModelAnimation` to
`modelSetAnimFrame` → `modelConstrainOrWrapAnimFrame` dereferenced
`anim->unk07`/`anim->unk04` → SIGSEGV.

This is the **§A1 live-host-pointer-truncation** class (D588 / D590 / D591):
a stored/live host pointer fed through a `(s32)` (truncating) rebase. The other
anim-resolution sites (`bondhead.c`, `bondview2.c`) already use a full-width
`(uintptr_t)&ptr_animation_table->data` base; `initResolveAnimTable` was the
one site still on the `(s32)` form.

### Fix (2026-10-08, macos branch) — §A1 narrow exception (pointer-width misread; no logic/behavior change)
Read the base at full pointer width and add the small N64 ROM offset as `u32`
(the offset is never widened, per the §A1 rule):
```c
entry[-1].struck_anim =
    (ModelAnimation *)((uintptr_t)(*entries).struck_anim + (u32)address);
```
`(*entries).struck_anim` reads the 8-byte `ptr_animation_table` value at
full width. **Identity on every `PORT_ADDR_BASE == 0` platform** (the value is
s32-low there, so the 8-byte read == the old 4-byte read); correct on arm64
macOS.

### Verification (2026-10-08, Mac)
Rebuilt; `-level_33` at the Dam; LMB fire (now that D594 works) into a guard →
the guard plays its hit/death animation, **no SIGSEGV**, no `ge007.crash.log`,
frames advance. (Win/Linux unaffected by the change — identity read.)
Plus the **maintainer's live pass (Launch E, 2026-10-08)**: real LMB fire at
Dam guards (the E1 ground-truth hit-anim check) with **no crash and no
`ge007.crash.log`** for the whole session; the `GE_KEYLOG` log shows the
real `MOUSEBT-DOWN button=1` events on the main-thread side *and* `LMB=1`
on the scheduler-poll side — the struck-animation path was exercised by hand,
not just by the D597 scripted sweep-fire.

### Status
**FIXED 2026-10-08 (macos branch).** §A1 cross-tag: see the `porting-notes.md`
§A1 catalogue (D595 entry — the `initResolveAnimTable` site of the
`ptr_animation_table`-truncation class). Unblocked by D594 (mouse fire); this
was the last input-blocker to a playable arm64 macOS build.
