# D206

<!-- from the old section-F/H table row -->
| D206 | **PC played every SFX one bank slot too high — pointer-width layout shift in `struct ALInstrumentAlt_s` (M-76).** Root cause of D205 (3)/(4) + D202's "slap". — full `## D206` entry at file tail | **FIXED (M-76, `src/snd.c:1001`, `#ifdef PORT`)** — ABI/layout-only (D3x class); user by-ear A/B vs N64 = PASS (M-77c) → D202 closed. |

## D206 — **PC played every SFX one bank slot too high: `ALInstrumentAlt_s.soundArray` shifts offset 12→16 at 64-bit** (M-74 found, M-76 FIXED)

**This is the root cause of D205's melee→Klobb and armor-pickup symptoms, and
of D202's original "silenced PPK plays a slap" report.** It was invisible for
eight sessions because *the verification tooling shares the bug* — see
"Why M-68/M-70 passed" below.

### The evidence

The user supplied an independently-produced, verified rip of GE's sound
effects (`B00I00S<hex>.wav`, 186 files). Filenames are **hexadecimal** bank
indices. Cross-checked by PCM frame count against `scratchpad/rom_sfx_decode.py`
decoding the ROM's `sfxctl` bank:

```
offset -2:   0/184 matches
offset -1:   0/185 matches
offset  0: 186/186 matches   <-- rip index == bank index, exactly
offset +1:  21/185 matches
offset +2:  16/185 matches
```

So the rip is ground truth for *what sound lives at each bank index*, and our
decoder's indexing agrees with it. The user then identified three by ear:

| user's statement | rip file | = bank index | game requests (from `audiotrace.log`) |
|---|---|---|---|
| correct body-armour sound | `S50` | **80** | — |
| what the PC port actually plays for armour | `S51` | **81** | **81** (`ARMOUR_COLLECT_SFX`) |
| the actual Klobb sound | `S69` | **105** | **105** (`PUNCHING_AIR_SFX`, punch whiff) |

Both are exactly `requested index → played bank[index]`, and in both cases the
**correct** sound sits at `index - 1`.

### The structural confirmation

- ROM `sfxctl` bank: `bankCount=1`, `instCount=1`, **`soundCount=261`**,
  `percussion=NULL` → valid `soundArray` indices are **0…260**.
- `SFX_ID` (`src/bondconstants.h`) has **262** members, beginning
  `NOTHING_SFX = 0`, which `snd.c:992` explicitly treats as "no sound"
  (`if (soundIndex == 0) return NULL`).
- **262 enum members − 1 sentinel = 261 real IDs (1…261), mapping exactly onto
  bank slots 0…260.** The arithmetic only closes with a `-1`.
- Corollary bug: the last ID, `BIG_CLANK_SFX` = 261, currently indexes
  `soundArray[261]` — **one past the end of a 261-entry array.**

`src/snd.c:1001` is unmodified decomp code:

```c
sound = (soundBank->instArray[0]->soundArray[soundIndex]);   /* needs - 1 */
```

### Independent confirmation from D202's own reference clip

`scratchpad/B00I00S2D.wav` — the silenced-PPK reference clip that drove D202
from M-60 to M-67, and whose provenance the user could only describe as "not
sure / found online" — **is a file from this same rip**: `S2D` = **bank 45**
(3264 frames, identical). And `wppksil_stats.Sound = 0x2E` = **46**.

So the silenced PPK's ROM data value 46 must resolve to bank **45**, and the
user's clip *is* bank 45. **M-61 found precisely this** ("index 45 is the only
strong match for the user's reference clip, envcorr +0.999; index 46 is the
'slap'") and it was argued away over four sessions — M-67 "refuted" the clip as
a genuine capture on a waveform-correlation test. The +0.999 was right. The
"slap" heard on every silenced PPK shot is `bank[46]`, which under the correct
mapping is `PUNCH1_SFX` — **literally a slap.**

### Why M-68/M-70's "exact match" passed anyway

`scratchpad/exact_match.py` verifies a runtime voice against
`rom_sfx_decode.py`'s decode of **the requested index**, and that decoder walks
`soundArray[i]` with the same missing `-1`. `scratchpad/banktest/diff_bank.py`
likewise compares an N64-layout walk to a PC-layout walk using the same
indexing on both sides. **Every tool built to check the mapping reproduced the
mapping.** M-68/M-70 proved the port plays *the sample it asked for, at the
right pitch* — self-consistency — and never tested whether the index asked for
was the right one. Method note added to `porting-notes.md` §E.

### What it explains

- **D202** — "silenced PPK plays a slap": requests 46, plays `bank[46]` =
  `PUNCH1`; correct is `bank[45]`. ✔
- **D205 (4)** — melee plays the Klobb gun: whiff requests 105, plays
  `bank[105]` = **the Klobb sample**; correct is `bank[104]`. ✔ user-confirmed
- **D205 (3)** — armour pickup wrong sample: requests 81, plays `bank[81]`;
  correct is `bank[80]`. ✔ user-confirmed
- **D205 (1)** — slap/glass layered over gunfire: consistent (a gunshot ID
  landing on the adjacent impact/punch sample), not yet individually confirmed.
- **D205 (2)** — explosion → scream: **NOT explained.** Explosion IDs 169–183
  map to bank 168–182, all still explosions. Stays open; M-73's reading
  (guards killed by the blast screaming, ±1 s) remains the likely answer.

### RESOLVED (M-76) — pointer-width layout shift in `ALInstrumentAlt_s`

The M-75 reframe below was on the right track (candidate B — a PC-side layout
mismatch, not a code or data `-1`). The exact mechanism:

`sndPlaySfx` (`snd.c:1001`) reads the on-disk sound bank — a **standard**
`ALBankFile`/`ALInstrument` (verified: `rom_sfx_decode.py` finds `soundCount`
= 261 at instrument offset 14, standard `ALSound*` table at offset 16) —
through GE's cast alias:

```c
struct ALInstrumentAlt_s {      /* src/snd.h:165 */
    s32 unk0;   /* off 0  */
    s32 unk4;   /* off 4  */
    s32 unk8;   /* off 8  */
    ALSound *soundArray[1];     /* N64: off 12 (4-byte ptr) | PC: off 16 (8-byte ptr, 8-aligned) */
};
```

- **N64:** `soundArray` at struct offset **12** — it overlaps the on-disk
  instrument's `bendRange`(12) + `soundCount`(14) words. So `soundArray[0]`
  is those two `s16`s (a bogus pointer, but `SFX_ID 0 = NOTHING_SFX` is
  short-circuited at `snd.c:992` and never dereferenced), `soundArray[1]` is
  on-disk table entry `[0]`, and in general **`soundArray[N]` == on-disk
  entry `[N-1]`**. GE's `SFX_ID` enum and every `WeaponStats.Sound` byte are
  therefore **1-based** into the sound table, by design.
- **PC:** 8-byte pointers + 8-byte alignment insert 4 bytes of padding after
  `unk8`, so `soundArray` sits at offset **16**. `port/src/romdata.c`
  `afFixupInst` rebuilds the bank to exactly this layout (16-byte header,
  `8*soundCount` bytes of 64-bit pointers from offset 16, entry `i` = on-disk
  entry `i`). So PC `soundArray[N]` == on-disk entry `[N]` — **one slot
  higher than N64** for every SFX.

This is the D3x class (a ROM-serialized struct whose pointer-width field
shifts the layout at 64-bit). **Fix:** `src/snd.c:1001`, `#ifdef PORT` branch
→ `soundArray[soundIndex - 1]`, N64 line kept verbatim under `#else`. The one
read site covers both the initial lookup and the retrigger chain (the loop
recomputes `soundIndex` from `keyMap->velocityMin`, same 1-based space). No
game-logic change; `romdata.c` untouched (its layout is internally correct —
it just wasn't the layout the N64 struct alias assumes).

### Open before fixing — where the fix belongs (M-75 reframe) — SUPERSEDED by M-76 above

**M-75:** upstream `n64decomp/007` `src/snd.c` byte-matches the real ROM,
carries no `NON_MATCHING`/`GLOBAL_ASM` guard on `sndPlaySfx`, and its index
line is identical — `soundArray[soundIndex]`, **no `-1`**. Blame puts
`snd.c:1001` in the 2022 upstream import, not this fork. So the earlier
hypothesis "(i) real ROM does `soundArray[soundIndex-1]`, decomp dropped it"
is almost certainly **dead**: a matching decomp would contain the `-1`. The
N64 runs `soundArray[N]` for ID `N` and that game sounds correct.

That forces a reconciliation. Candidates, cheapest test first — full decision
gate and per-candidate byte-level experiments in
`docs/dev/notes/D206-IMPL-BRIEF.md`:

- **(A)** the rip `B00I00S<hex>` is keyed by `SFX_ID`, not by 0-based bank
  slot → there is **no engine bug**, the PC audio path is faithful, and
  D205 / D202's slap need another cause. Re-audit whether
  `scratchpad/rom_sfx_decode.py`'s 0-based `sound_offsets` walk actually
  models the runtime `soundArray` (leading NULL? offset-table vs `soundCount`
  mismatch? sfxctl not a plain `ALBankFile`?). Cheapest — do first.
- **(B)** PC-only off-by-one in the *converted* bank — `port/src/romdata.c`
  `afFixupInst` drops/adds one `soundArray` entry vs the ROM. Test: dump PC
  runtime `soundArray[81]` wavetable `base`/`len` (existing `GE_AUDIOTRACE`
  probe at `snd.c:1002`) and byte-compare to `rom_sfx_decode.py` bank 80 vs
  81. PC-side fix, rule-#2 clean.
- **(C)** PC-only off-by-one in the `soundIndex` reaching `sndPlaySfx`
  (request path / miscompiled or region-mismatched enum). Test: `GE_AUDIOTRACE`
  already logs `soundIndex=` at the call — confirm armour logs `81`, not `82`.
- **(D)** genuine data-side: ROM `SFX_ID` constants + `WeaponStats.Sound`
  bytes are each +1 vs a 0-based bank. Only reachable if A/B/C all fail.
  ~532 enum refs + debug-name table + weapon-stats asset bytes — **not** a
  legal port edit under AGENTS.md rule #2; stop and escalate to the user
  (possible "wontfix / faithful").

M-62's caveat still applies: this repo never builds or byte-verifies the N64
target, so "it byte-matches" cannot by itself settle (D).

### Verification done (M-76)

- Build clean (`build-pc.sh ntsc-final`).
- BUNKER1 (`-level_09`) `GE_AUDIOTRACE=1`, 60 s: crash-free, 446 `sndPlaySfx`
  calls. Every one of ~90 distinct requested `soundIndex` values resolves to a
  wavetable whose `len` matches `rom_sfx_decode.py` slot **`soundIndex - 1`**
  (0/90 match slot `soundIndex`). Includes `ARMOUR_COLLECT_SFX`=81 → slot 80,
  `GUN_B4_BOLTACTION_SFX`(AK47)=109 → slot 108, `GUN_SILPPK_A`=46 → slot 45.
- `-level_09` golden framediff (`200-440:120`): **3/3 within threshold** on two
  consecutive runs (an audio-path change cannot affect rasterisation; ritual).

### Still owed

User by-ear A/B vs N64: armour pickup, unarmed melee whiff, silenced PP7 fire.
If clean → **D202 closes outright**, D205 shrinks to symptom (2) (explosion→
scream) alone. Then remove the temporary D202 `GE_AUDIOTRACE` / `[VOL]` /
`[DISTVOL]` probe set (checklist in HANDOFF).


**M-77c (from the §F row, appended 2026-09-15 ctx-opt):** user by-ear A/B vs N64 = **PASS** — armour / melee / silenced PP7 all correct now → D202 closed, D205 down to symptom (2). Committed `378386d1`.
