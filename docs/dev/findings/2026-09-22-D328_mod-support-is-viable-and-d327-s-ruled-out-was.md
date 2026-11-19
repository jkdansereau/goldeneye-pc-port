# D328

<!-- from the old section-F/H table row -->
| D328 | **Mod support is viable and D327's "ruled out" was wrong: GE's file table IS in the ROM, inside the 1172-compressed data segment, and locating it needs no hardcoded offsets (PD dabs fork review + verification on vanilla + 2 mods, 2026-09-22).** — full `## D328` entry at file tail | CLOSED as a question — table at `0x252c4` inside the segment inflated from ROM `0x21990` (727 rows; 681 exact offset+size matches vs filelist, 0 mismatches). Hardcode-free scan reads Random Eye's own table -> **704/726 files byte-identical** (vs 0/803 at vanilla offsets in D326) and GF64's 24 MB table correctly. PD's architecture is an offline importer + mod-dir override, not a runtime ROM loader; its MIT `rompatch.c` removes the GPL xdelta3 constraint. Remaining work is the mod-dir override layer. |

## D328 — Mod support is viable and D327's "ruled out" was wrong: GE's file table is in the ROM inside the 1172-compressed data segment, and locating it needs no hardcoded offsets (verified on vanilla + 2 mods, 2026-09-22)

### Source

The Perfect Dark PC port fork at `perfect-dark-dabs-mod-dabs-mod` (**MIT**,
Copyright 2022 Ryan Dwyer — same license as this tree; `AGENTS.md` already names
the PD port a standing reference). Two files matter:

- `port/src/modimport.c` — in-game console-mod import. Its header states our own
  constraint verbatim: *"The port cannot run the patched ROM — it runs decomp
  code and reads the ROM at fixed offsets — but it can use the mod's content."*
- `port/src/geconvert.c` — converts a **GoldenEye 007 US ROM** into PD arenas, so
  it parses *our* ROM and carries GE's constants.

### The architectural flip (why D325–D327 asked the wrong question)

D325–D327 scoped mod support as "make the port load a modded ROM", concluded
that needed a runtime-parsed file table, and stalled on whether that table was
recoverable. PD does not do that at all: it **diffs the patched ROM against
stock, extracts only what differs, and writes it out as a mod directory**
(`files/`, `segs/`) in the layout its loader already reads. The game keeps
running its own stock ROM at fixed compiled offsets; mod content arrives as loose
files. Mod support is an **offline import step plus a file-override layer**, not
a loader rewrite.

Supporting pieces in that fork, all MIT:

- `port/src/rompatch.c` — its own VCDIFF (xdelta3: RFC 3284 + app header +
  per-window adler32), BPS and IPS decoder, verified byte-identical against
  xdelta3 on 40 patches. **This removes the GPLv2 xdelta3 dependency D325 flagged
  as a licensing constraint.**
- `findFileTable()` (`modimport.c:321`) scans the decompressed data segment for a
  run of ≥`MIN_FILES` (500) records; the data segment is found by scanning the
  first 4 MB. No hardcoded offsets, so it survives relocation.
- `tblStartBySamples()` locates audio tables by searching for stock waveform
  prefixes and voting — the content-addressed idea D327 proposed, in production.

### GE's file table: where it actually is

`geconvert.c` gives `DATA_ROM 0x21990`, `DATA_VRAM 0x80020d90`, `FILES_AT
0x252c4`, and reads the table as 12-byte records with size by differencing
adjacent addresses — **exactly `obInit` (`src/game/ob.c:126`)**. Verified here
against our own `data/ge007.ntsc-final.z64`:

- `1172` magic present at `0x21990`; `zlib` raw-inflate (`-15`) after the 2-byte
  header yields a **247,120-byte** data segment. This is the same inflate
  `port/src/rzdecomp.c` already wraps.
- Table at `0x252c4`, **727 rows**, first word counting `0..726`. Names resolve
  via `vaddr − 0x80020d90` to `bg/bg_sev_all_p.seg`, `bg/bg_silo_all_p.seg`, …
  matching `assets/obseg/file_resource_table.inc.c` order.
- **Cross-checked against ground truth:** derived `addr`/`size` pairs vs
  `scripts/filelist.u.csv` — **681 exact offset+size matches, 0 mismatches**
  (44 unmatched are artifacts of the basename matcher used, not data errors).

**D327's approach (a) is therefore NOT ruled out.** Its scans were run over
*uncompressed* ROM bytes, so it was right about the mechanism ("lives inside the
compressed code image") and wrong about the consequence: the segment inflates in
one call with a decompressor we already ship.

### Hardcode-free discovery, verified on two real mods

Implemented the PD strategy independently (scan for `1172` in the first 4 MB,
inflate, scan for a ≥500-row run whose first word counts from 0) and ran it on
all three ROMs. **No constant from `geconvert.c` was used.** Then validated by
comparing vanilla file *content* against each mod ROM at the offsets its **own**
table gives:

| ROM | data seg | table | rows | content identical to vanilla at extracted offsets |
|---|---|---|---|---|
| vanilla NTSC-U | `0x21990` | `0x252c4` | 727 | — (reference) |
| Random Eye v1.1 | **`0x219f0`** | `0x25304` | 727 | **704 / 726 (97.0%)** |
| Goldfinger 64 v1.0 (24 MB) | `0x21990` | `0x252c4` | 727 | 19 / 726 (2.6%, as expected for a total conversion) |

Random Eye's non-identical entries, fully accounted for:

- **2 real changes** — `UsetupcaveZ` and `UsetuptraZ` (setup files; the
  randomizer's actual edits).
- **1 artifact** — index 0, the `NULLFILE` slot, whose "size" is the differencing
  of `addr 0` against the first real file.
- **19 zero-size slots** — unused `bg/` entries (`bg_sho_all_p.seg`,
  `bg_eld_all_p.seg`, `bg_imp_all_p.seg`, …), i.e. cut-level table padding, not
  failures.

704 + 2 + 1 + 19 = 726. **Complete.**

Contrast with D326, which found **0 of 803** slices matching when read at
*vanilla* offsets: the same ROM, read through its own table, is 97% recognisable.
That is the whole mod story in one number, and it also confirms D326's shift
measurement (`+7392`) was the data segment and file area moving together.

### Revised viability

From "architectural project gated on unresolved research" (D327) to **known-good
architecture with an MIT reference implementation**. Remaining work is real but
bounded, and none of it is research:

1. **File-table extraction** — the scan above, in C, reusing
   `port/src/rzdecomp.c`. Proven.
2. **Patch decoding** — VCDIFF/BPS/IPS. `rompatch.c` is MIT and adaptable with
   attribution; no GPL dependency needed.
3. **Mod-directory override layer** — the genuinely new piece. PD has
   `romfile.moddir` file slots; **we have no analogue** — `port/src/romdata.c`
   reads the ROM only. This is where the effort sits.
4. **Converter integration** — `d43`/`d69`/`d88` taking an extracted table
   instead of `scripts/filelist.u.csv`.

**Unchanged limits:** mods that *add levels* still need new stage-table entries in
`src/game` (AGENTS.md rule 2, not the ABI/layout exception); ASM/code mods remain
impossible since ROM code is never executed here.

**Status:** CLOSED as a question — viability established, mechanism verified end
to end on two real mods, no research blocker remains. Amends **D327** (approach
(a) un-ruled-out) and supersedes its keystone framing. No code changed; backlog
updated in the local notes backlog §5. Cross-ref: D325 (GF64 probe + taxonomy),
D326 (Random Eye repack measurement this re-reads correctly), D327 (the scans
whose conclusion this corrects), D43/D69/D88 (converters), D179.

---

### M-199 (2026-09-22): adversarial review of the proposed D318/D320 fix plan (frame-data audit → runtime trace → conformance sweep → watchdog excision). **Verdict: SOUND-WITH-CORRECTIONS** — direction and constraint posture are right, but step 1 mischaracterizes the data path and is ~80% already done, the plan's "unhandled branch" is actually the *expected* outcome of step 1 (with no decision rule for it), and step 2 as written hunts for a mechanism that cannot exist in byte-matched code. No code changed; review only.

**Pre-flight (mechanism re-verified against source this session, all links hold):**
- `chrlvInitActAttack` (chraction.c ~1258–1376): `next_anim = randomGetNext() % len`, `modelSetAnimation(panim_float->anim, flip, panim_float->start_frame, …)`; `unk36`/`unk32`/`unk34` set from weapon props + PRNG. ✓
- `chrlvAttackActionRelated` (chraction.c:1126–1163): **only ever calls `modelSetAnimEndFrame`** — never `modelSetAnimation`. E-selection branches verified verbatim: AIM_ONLY → `H = (0 ≤ recoil < shoot_start) ? recoil : shoot_start`; live-fire → `E = recoil (if ≥ 0, both unk36 arms) else shoot_start / end_frame / −1`. ✓
- Re-init block (chraction.c ~7126+): `modelGetAnimEndFrame <= frame` gates everything; the unk31 path (~7209) is the only `modelSetAnimation` re-init and is unreachable while E > H. ✓
- `modelSetAnimEndFrame` (model.c:2936): accepts `endframe` only if `endframe < ModelAnimation->unk04 − 1`, else stores −1 (unbounded). Named fields throughout. ✓

**New finding 1 — there is no conversion step for this data; step 1's premise is wrong.** The firing-animation groups are byte-matched decompiled C in `src/game/chr.c` (`pistol_firing_animation_groupN[]`, named-field access → A1-immune; `len = −1` resolved at runtime by `initResolveAnimGroups`/`initResolveAnimGroupTable`, initactorpropstuff.c:82–158, counting to the sentinel row). The `ModelAnimation` records come from **raw ROM via `romCopy`** (D34, port/src/romdata.c; `g_pc_animdata_base`) — the PC reads the *same bytes* the N64 does, and `ModelAnimation` has no pointer members (bondtypes.h:629; D32 already u32-ized `bitDescriptors`/`bitStream`) so the layout is stable. "Decode from raw ROM and compare against the port's converted assets" compares two things that are identical by construction; no ROM-decode tooling is needed or in `tools_pc/`.

**New finding 2 — the audit was effectively pre-run, and its outcome is knowable now.** Applying `chrlvAttackActionRelated`'s exact selection logic to `pistol_firing_animation_group1` (the group matching both probe signatures):

| variant | start | shoot_start | recoil_start | H (pre-aim) | E (post-derail) | outcome |
|---|---|---|---|---|---|---|
| fire_hip_…_slow | 12 | 19 | 24 | **19** | **24** | **PIN** — exact match to frozen-run probe (H=19.00/E=24.00) |
| fire_hip_…_fast | 17 | 25 | 30 | 25 | 30 | PIN |
| fire_standing_… | 12 | 58 | 60 | 58 | 60 | PIN |
| fire_hip_forward_… | 4 | 22 | 22 | **22** | 22 | ESCAPE (E==H → `endframe <= frame` re-init) — exact match to escape-run probe (H=22.00) |

Group1 is **3/4 pin-capable** — consistent with the observed ~2/3–3/4 forced-repro rate. Other pistol groups: g2 1/2, g3 3/4, g4 1/1, g5 1/1, g6 3/4 — **no all-escape group exists**. Consequence: byte-matched code + ROM-identical data ⇒ under identical ticks the N64 should pin on ≥½ (typically ¾) of derails. The user's spec (0 pins in ~30 y / all 1964 trials escape) is therefore **in direct conflict with the byte-matched data** — this is the plan's "unhandled branch," and it is the *expected* outcome of step 1, not a corner case. The plan has no decision rule for it.

**New finding 3 — the pin sits exactly ON the fire-window boundary; D318/D320's stated condition is imprecise.** In every E>H variant above, **H == shoot_start exactly** (19/25/58) — not "H < shoot_start" as D318/D320 state. The pinned frame therefore sits at the edge of the fire gate `shoot_start <= frame < shoot_end` (chrlvTickAttackCommon), and whether c78 fires from that state hinges on **sub-frame settling**: `modelTickAnim`'s s32 `framea/frameb` clamp (`modelConstrainOrWrapAnimFrame`) + the `if (vb == va) animframe1 = va` sub-progress discard. The D318 probe printed `%.2f` ("19.00") — 18.99x would display identically. Working hypothesis: on N64 the frame settles at exactly shoot_start → gate opens → every "pin" variant fires (N64 pin rate = 0%, matches spec); on PC it settles just below (or a downstream gate in the fire loop — `unk38[i]` weapon slots, `unk44`/`attack_time` priming — stays closed) → ¾ pin (matches observation). **A single deterministic per-derail divergence explains the full 0%-vs-~66% rate gap with no PRNG argument** — which also confirms the brief's claim that a stream difference cannot explain it. Note this makes "find what re-inits the animation on N64 that the port misses" (plan step 2) misdirected: there is **no** re-init path for E>H in byte-matched code, so if data+code are identical, nothing can re-init on N64 either — the N64 escape must come from the fire gate opening at the boundary (or from the derail never landing), not from a missed re-init.

**Verdict per attack surface:**
1. **Candidate completeness.** {data, runtime} is exhaustive only if "runtime" is expanded to include (a) fire-gate boundary settling in `modelTickAnim`/`modelConstrainOrWrapAnimFrame` (new finding 3 — the prime suspect) and (b) the upstream derail window (hypothesis i). The §A1 concern (E/H misread at runtime from correct data) is *ruled out by construction* for this chain — compiled named-field access + raw-ROM `ModelAnimation` with no pointer members — but the plan should record that ruling explicitly instead of running a ROM decode.
2. **The unhandled branch.** Expected outcome, not corner case (finding 2). "The user's spec is wrong" is **not** an acceptable first conclusion: byte-matched code + identical data vs a 30-year community record means one link in the pin chain carries an unverified platform assumption; hunt runtime/reachability first (boundary settling is cheap and port-probeable). If the boundary hypothesis fails, one *bounded* emulator artifact — a single `g_randomSeed` memory read feeding `GE_RSEED_LV`, or one trial with frame-precision observation — is a different cost class from the 10-trial series the user ruled out; needs user sign-off before use.
3. **Ordering/feasibility.** Data-audit-first is right but ~80% complete after this review (finding 2). Remainder: extend the H/E table to the ai_19/ai_9 groups and confirm which group index c78 actually draws (`anim_index` from `chrlvDistanceToChrRelated`, distance-bucketed over the 32-entry `ptr_pistol_firing_animation_groups[]`). Effort: ~1–2 h, no new tooling.
4. **ai_19 / ai_9.** Different anim groups (kneel/crouched tables; probes pinned at frames 30.00/23.00 — neither matches a group1 row). Fixing ai_22's root cause implies theirs only if the cause is shared (boundary settling would be; per-group data is not). Step 4's "re-verify first" must be an explicit **per-entry H/E table + boundary check + forced-repro sweep**, and each watchdog entry excised on its own evidence — not on ai_22's result. Depot ai_12 (clean) and the untested ai_13/14/15 (LOW-MED) stay out of scope for excision.
5. **Emulator A/B.** Cutting the trial series is correct (given identical data it can only re-test "does the derail happen on N64," which the spec's +3.5 s fire signature already answers — the monologue-completion path takes ~22 s, so a +3.5 s shot is itself evidence the derail landed). Preserve `GE_RSEED_LV` + DET logging armed as the fallback oracle for an upstream stream hunt (cost: one memory-viewer read + an env var); do not burn it until the boundary hypothesis fails.
6. **Constraint compliance.** All steps port-side; no Rule-2 exposure *as written*. Two cautions: (a) watchdog excision changes shipped v0.4.0 behavior mid-release — keep the watchdog on `release/v0.4.0` until step 3 passes and land the excision post-release (M-192 item 7 precedent); (b) pre-decide the fix bucket for the boundary root cause: if it turns out to be FMA contraction / float-rounding in compiled game code, a **build-flag** change (CMakeLists, e.g. `-ffp-contract=off`) is port-side, but any arithmetic edit in `src/game/model.c` requires Rule-2 sign-off.

**Corrected plan sketch:** (1) finish the static H/E table for all three confirmed entries' groups + c78's group index — ~1–2 h; (2) env-gated probe: full-precision (`%f`/bit-exact) `animframe1` + fire-gate operands (`shoot_start`, `phi_f20`, `unk38[i]`, `unk3a[i]`, `unk44`) in a frozen forced-repro run, plus an A1/float pass over `modelTickAnim`/`modelConstrainOrWrapAnimFrame` — ~half a day; (3) decision gate: boundary divergence found → port-side fix + per-entry conformance (static table + existing ai_19/ai_9 forced sweeps; c78 headless materialization is NOT available today — d320repro cannot spawn the proximity-spawned c78, so a "headless forced-derail seed sweep" of the Trevelyan beat as written in plan step 3 is infeasible without a ~1-day harness extension; cost it or drop it) → excision post-release; boundary hypothesis dead → upstream hunt with `GE_RSEED_LV` fallback (user sign-off for the single bounded emulator artifact); (4) D318/D320/D321 rewrite only on pass, per-entry.

**Status:** review recorded; plan amended per above. No code changed. Cross-ref: D318 (mechanism + 2026-09-22 correction + probe evidence), D320 (sweep + forced repros), D321 (trigger chain), D34 (animationtable ROM-copy data path), D32 (ModelAnimation u32 fields), D94 (firing-table pointer arithmetic), porting-notes §A1 (ruled out for this chain by construction).
