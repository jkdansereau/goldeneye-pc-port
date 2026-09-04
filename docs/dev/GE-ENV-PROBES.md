# GE_* environment probes — consolidated list

Every `getenv("GE_…")` in the tree. The table prose is hand-curated;
**`tools_pc/gen_env_probes.py`** re-greps the live sites and reports drift
(NEW / GONE vars + a fresh file:line map) — run it before trusting the
File:line cells. Last reconciled 2026-09-04 (M-51). All are **env-gated**: unset = zero behavior
change, so they are safe to leave in a release build (they only cost a
`getenv` on the guarded path). "Dead" below means *the finding it was built
for is closed* — the probe is strip-candidate scaffolding, not that it does
anything harmful.

Two classes:
- **Infra / live tooling** — deliberately kept, used by the playtest harness
  (`tools_pc/debug.ps1`, `playtest.sh`, `level_sweep.sh`) or by an
  investigation still in progress.
- **Dxx diagnostic** — added to chase one finding; most are now closed.

## Infra / live tooling

| Env var | File:line | What it does | Status |
|---|---|---|---|
| `GE_PCDUMP` | `port/fast3d/gfx_opengl.cpp:1079` (`is_pcdump`), `gfx_opengl.cpp:700`, `port/src/video.c:188`, `port/src/config.c:46` (`configGetFrameDump`) | Per-frame framebuffer PPM dump to `$PCDUMP` dir (verification-ritual golden captures, `tools_pc/framediff.py`/`pixcount.py`). Also settable via `[Debug] FrameDump` ini (env wins). **M-33/D168: the writer now emits rows top-to-bottom — captures before that fix are vertically flipped.** | **live** |
| `GE_INPUTSCRIPT` | `port/src/input.c:306` | Headless scripted controller-0 input: `"<frame>:<tok>,…;…"` (`SUP/SDOWN/SLEFT/SRIGHT/SNONE` sustain, buttons pulse). Sole input source when set. | **live** |
| `GE_INPUTLOG` | `port/src/input.c:329`, `port/src/config.c:53` (`configGetInputLog`) | Logs computed pad state (buttons + stick) each poll when non-zero. Also `[Debug] InputLog` ini. Also traces D165 `menuptr est/tgt/eff/stick`. | **live** |
| `GE_STARTMENU` (+ `GE_STARTMENU_PAGE`, `GE_STARTMENU_DIFF`) | `src/game/lv.c:398-402` (`#ifdef PORT`) | Boot straight into a front-end menu id (13=MISSION_COMPLETE, 10=BRIEFING, 7=MISSION_SELECT, 12=MISSION_FAILED, 6=MODE_SELECT); `_PAGE`=folder row (def 1=Dam), `_DIFF`=0..3. Crash-test a screen fast. | **live** |
| `GE_UNLOCK_ALL` | `src/game/debugmenu_handler.c:1098` (`get_debug_enable_all_levels_flag`, `#ifdef PORT`) | Mission-select shows every solo level (playtest jump-to-any-level aid). Cached in a `static int c`. | **live** |
| `GE_OPTIONSOVERLAY` | `port/src/optionsoverlay.c:231` | Force-enable the F10 port-layer options overlay (D184 / PR #10) regardless of ini. `atoi != 0`. | **live** |
| `GE_TEXPITCH` | `port/fast3d/gfx_pc.cpp:924` (`gfx_tex_pitch_fix`, cached) | `=0` disables the D183 defensive de-stride in `import_texture` (default on). RC2/RC3 texture-shear A/B knob. | **live** (RC3 not finished) |
| `GE_TEXRAW` | `port/fast3d/gfx_pc.cpp:1079` | With `GE_TEXDUMP`, also dumps the raw pre-import source bytes (`texdump/rNNN_f…_s…_WxH.bin`) so a decode bug can be told from a source-data bug offline (D183). | **live** (RC3 not finished) |
| `GE_WRAPFIX` | `port/fast3d/gfx_pc.cpp:3166` (`gfx_set_wrap_fix`) | RC3 test override for the `Video.WrapFix` D74/D167 wrap-block path (`atoi != 0` wins over the ini). | **live** (RC3 not finished) |
| `GE_SAVELOG` | `src/game/file.c:46`, `src/game/file2.c:16` (`SAVELOG` macro), `src/boss.c:749`, `src/game/objective_status.c:297,343` | Traces the campaign save/unlock chain (objective completion → `end_of_mission_briefing` → `fileWriteSave` → EEPROM). Used to confirm D157. `g_savelogObjOnce` gates the per-criterion dump to the `bossReturnTitleStage` call. | **live** (strip once a few more level boundaries are playtested) |
| `GE_D160` | `src/boss.c:744`, `src/game/bondview2.c:791`, `src/aicommands.def:10055,10181,10236,10242,10247` (all `#ifdef PORT`) | Dam exit-cutscene (D148/D160) diagnostic: trace points in `bossReturnTitleStage`, `bondviewSetCameraMode`, and AI cmds `EndLevel`/`exit_level`/`CameraLookAtBondFromPad`/`CameraSwitch`. **Investigation in progress** — user owes a `GE_D160=1` Dam run. | **live** |
| `GE_DTEX` | `port/fast3d/gfx_pc.cpp:1001` | Per-`import_texture` param log (dims / line / size / lod / gen_mipmaps). Used to root-cause RC2 and D159. Kept as an inert diagnostic. | **live** (RC2/RC3 not finished) |
| `GE_TEXDUMP` | `port/fast3d/gfx_pc.cpp:1020`, `port/fast3d/gfx_opengl.cpp:700` | PPM dump of every uploaded texture + a `fmt/siz/palfmt/palidx/pal[0..3]` line. Root-caused D161 (Depot ceiling). Kept as an inert diagnostic. | **live** (RC3 not finished) |
| `GE_D116` | `port/fast3d/gfx_pc.cpp:1710,2362`, `src/game/textrelated.c:265,541` (cached `ge_d116`) | HUD/text "X-mirror" (D114/D116) vbo/uv trace. **D114/D116 CLOSED — NOT A BUG (M-33/D168): the captures were upside-down, not mirrored.** Probe is now dead scaffolding — strip on the next cleanup pass. | dead (D114/D116 closed) |

## Dxx diagnostic probes — finding closed unless noted

| Env var | File:line | Finding / what it logged | Status |
|---|---|---|---|
| `GE_D51` | `port/src/libultra.c:496`, `src/game/model.c:125,244,544` | msgQ 32-slot overflow watch / ModelSlot layout | dead (D51 closed) |
| `GE_D54` | — (all blocks stripped M-32, commit `49ce620a`) | music seq-table ABI / endianness (`ALMidiHdr`) | dead (D54 closed) — fully removed (kept here as a tombstone; the generator flags it GONE) |
| `GE_D154` | `src/game/bg.c:3418,3447,3632` (capped 64 calls) | bg room-GDL call trace (room / gdlidx / vtxoff / op / raw hdr words) | dead (D154 closed) |
| `GE_D176` | `src/game/bgfog.c:459,471`, `src/game/sky.c:325,381` (all `#ifdef PORT`) | D176 sky/fog env-match + sky-vert trace (Path B, PR #18) | **live** (D176(a)/(b) OPEN) |
| `GE_D178` | `src/game/front.c:6586,6594` (`#ifdef PORT`) | briefing-data u16 decode trace (blank-objectives, D143/D178) | dead (D178 closed) |
| `GE_D56` | `src/game/model.c:227,830` | watch `Model` raw-offset reads | dead (D56 closed) |
| `GE_D60` | `port/src/libultra.c:521,735` | gfx frame msgQ delivery trace | dead (D60 closed) |
| `GE_D61` | `port/src/libultra.c:795` (`s_d61opened`) | opens a one-shot log file | dead (D61 closed) |
| `GE_D62` | `port/src/libultra.c:477` | osRecvMesg trace | dead (D62 closed) |
| `GE_D63` | remaining: `src/game/bg.c:2525,2908,2953`, `front.c:1410,1416,7814,7889,8083,8091,8099,8366`, `image.c:2453`, `language.c:431`, `model.c:4340`, `title.c:607` | gun-barrel sub-DL clobber hunt (D63/D64/D65/D66 — the labels drift within the blocks). Stripped M-30 (`d0789358`, `gfx_pc.cpp` trail). Stripped M-32 (`49ce620a`): `memp.c`, `bg.c:2880` (incl. the bare non-PORT `d63bgprimarycount` static + entry log), `blood_animation.c`, `dyn.c`, `rsp.c`. **Remaining blocks are all `#ifdef PORT` + getenv-guarded and inert — strip candidates for a later pass.** | dead (D63 closed) — partially removed |
| `GE_D69` | `src/game/ob.c:165,227` | stage-load (D69) object trace | dead (D69 closed) |
| `GE_D69BB` | `src/game/bg.c:2460,2479,3020` | D69 bg-binary layout trace | dead |
| `GE_D69STAN` | `src/game/stan.c:266,276` | D69 stan-tile trace | dead |
| `GE_D85DUMP` | `src/game/bg.c:2510` | D85 `bgWidenRoomGdl` 8→16B dump | dead (D85 closed) |
| `GE_D85TEX` | `src/game/image.c:2479`, `src/game/tex.c:859,1050` | D85 texpool-full events (Depot analysis) | dead (D85 closed) — was useful for §3 |
| `GE_D86` | `src/game/model.c:6295`, `src/game/objecthandler_2.c:143` | D86 model rwdata trace | dead (D86 closed) |
| `GE_D87` | `src/game/ramromreplay.c:296,363,381` | D87 `ramromfilestructure` endianness | dead (D87 closed) |
| `GE_D88` | `src/game/prop.c:1367`, `src/game/stan.c:3090` | D88 `Usetup*Z` propDef stream | dead (D88 family closed; D88.4 resolved) |
| `GE_D90` | remaining: `src/game/bondview2.c:2146`, `src/game/prop.c:1391` (bondview_r.c blocks stripped M-32, `49ce620a`) | D90 bondview / prop NULL trace | dead (D90 closed) — partially removed |
| `GE_D96` | `src/game/chrprop.c:436,507` (cached `probe`) | D96 chrprop trace | dead (D96 closed) |
| `GE_D104` | `src/game/bg.c:654` (rate-limited, `d104c`) | D104 depth-clear trace | dead (D104 closed) |
| `GE_D71LOG` | `port/fast3d/gfx_pc.cpp:645` | D71 fast3d trace | dead (D71 closed) |
| `GE_D75` | `src/game/title.c` `sub_GAME_7F007F30` (capped 8) | D75 Bug 2 gun-barrel model probe: logs `chrModelInstance`/`gunModelInstance` ptr+obj+numMatrices, `render_pos`, `renderData.mtxlist`, `g_GfxMemPos`, `osVirtualToPhysical(render_pos)` | **LIVE** (D75 Bug 2 OPEN — see §F "D75 Bug 2 — RUNTIME PROBE") |

## Notes

- The bare non-`#ifdef PORT` `GE_D63` block in `bgRenderRoomPrimary`
  (`d63bgprimarycount` static + entry log, was ~`bg.c:2876/2880`) was
  removed M-32 (`49ce620a`). Every remaining `GE_D63` block is
  `#ifdef PORT` / `#if defined(PORT)` wrapped and inert.
- `[Debug]` ini keys (`Debug.FrameDump`, `Debug.InputLog`) mirror
  `GE_PCDUMP` / `GE_INPUTLOG` as a fallback (env var wins) — M-26,
  `port/src/config.c`.
- Not GE_-prefixed but same category: `GE_DETERM` is **designed, not
  implemented** (fixed-tick determinism mode, §F D117).
