All of these items should be completed once the main original game is playable from start to finish on PC with no crashing or major issues.

### PC graphics settings

Optional video/rendering options, modelled on the PD port (baseline) with
Quake 1 remaster / Nightdive-style extras as stretch goals. **Every option
defaults to the current behavior** — zero visual delta at defaults.

**Starting point (verified):**
- `port/src/config.c` exists but is minimal (58 lines: Int + String only;
  PD's is 300 lines with Float/UInt, clamping, INI sections). Copy-and-adapt
  from `pd_port/port/src/config.c`.
- `port/src/video.c` is a trimmed port of PD's (250 vs 588 lines). Render path
  is architecturally identical to PD: native-res FBO (GE = 640×480 NTSC LAN1)
  → scaled blit to window, so all PD display options port over cleanly.
- No `#ifdef PC` blocks exist in `src/` yet; only the documented ABI/layout
  exception edits (D3x) are present. Anything below that changes *behavior*
  (FOV, HUD placement, menu contents) is a policy decision — see route notes.

**Baseline (PD parity)** — each item cites its PD source:
1. Config system: full `configRegister{Int,UInt,Float,String}` + clamps
   (`pd_port/port/src/config.c`).
2. Window/display (`video.c` + SDL): fullscreen checkbox, fullscreen mode
   (borderless/exclusive), maximize, center window, resolution dropdown
   (display-mode list + "Custom"), HiDpi toggle.
3. Frame pacing: vsync (Adaptive/Off/On/N-frames via `SDL_GL_SetSwapInterval`),
   framerate cap slider (PD caps FPS when vsync is off — keep that guard).
4. Quality: MSAA Off/2x/4x/8x/16x (FBO `samples` — **verify our FBO path
   accepts samples>0 first**; PD reads it back via `videoGetMSAA`), texture
   filter Nearest/Bilinear, anisotropic 1–16 slider, mipmap filter,
   on-screen FPS display toggle.
5. **FOV slider (baseline per owner decision).** PD does this game-side
   (`Game.PlayerN.FovY`, `g_PlayerExtCfg` in their `main.c` + decomp edits).
   Two routes — decide in a spike:
   - **(a) Port-layer:** override the camera projection in our fast3d where
     the view matrix/projection is consumed. No src changes; must find the
     exact injection point (camera math flows through game GBI setup).
   - **(b) PD-style port-only `src/` edit** reading a port-provided value.
     This would be the first *behavioral* src edit — exceeds the current
     ABI/layout exception in AGENTS.md #2; if chosen, expand the documented
     exception ("PC-port feature hooks, opt-in via config, default = original
     behavior") and record a Dxx finding.
6. **Widescreen handling:** non-4:3 window aspect → stretch vs pillarbox
   choice, plus HUD centering (None/4:3/Wide à la PD `Game.CenterHUD`). PD
   implements the HUD part with `G_ASPECT_*_EXT` extended GBI mods set
   game-side; for us that's either a fast3d viewport intercept of the HUD
   quads (route a) or a port-only src edit (route b). Same policy note as FOV.

**In-game video options menu:** v1 = config file (+ CLI flags if trivial).
PD's in-game menu (`pd_port/port/src/optionsmenu.c`, 2042 lines) hooks into
*their* decomp's menu tables with port-side handlers; GE's menu code differs,
so it's an adaptation, not a copy. Feasible under route (b) policy; revisit
after the config-file v1 works.

**Stretch goals (Quake 1 remaster / Nightdive):**
- Internal resolution scale: render at 2x/4x native then downscale (free
  supersampling AA, big fidelity win on modern displays).
- FXAA post-process toggle (cheap AA alternative to MSAA).
- SSAO + bloom / light-shaft post-processes — Quake Enhanced ships each
  enhancement as an individually-toggleable option; keep that pattern (one
toggle each, all default off, shared post-FBO stage with FXAA/colorblind).
- Integer-scaling option.
- Texture upscaling filter (ESRGAN-style or pre-generated upscales) — the
  biggest item; do last. Note GE textures are RZ-compressed at load
  (`port/src/rzdecomp.c`) — an upscale pass would hook there or post-decode.

**Acceptance:** every option persists via config file; toggles apply live
where PD's do; `tools_pc/golden/` dumps byte-identical at all-defaults;
no `src/` changes unless route (b) is explicitly chosen and documented.

### LAN deathmatch / multiplayer — the netplay foundation (do first)

**The idea:** play GE's *existing* competitive multiplayer (deathmatch /
mission-based, 2–4P) across up to **4 instances** of the PC port on a local
network. This is the **nettech proving ground**: the game logic already
exists in the decomp (original N64 feature — no new mode to invent), so this
item isolates and de-risks the entire networking layer. Co-op (below) bolts
onto whatever this produces.

**Milestone 0 (prerequisite, real port work):** verify GE's *existing*
split-screen multiplayer works locally on PC — two cameras, split viewport,
two independent input streams, full match lifecycle. Every code path netplay
will drive must first work on one box.

**Net model (research item — owner leans client-server):**
- Leading candidate: **host = server + player** (Source-engine style): host
  runs the authoritative sim, clients send inputs and receive state. Friends
  connect by manual IP entry on a LAN. Self-healing desync, one machine owns
  truth.
- Cost to size up: a full per-tick **state snapshot format** (players,
  entities/props, AI, doors/props) must be defined from scratch — no existing
  save/replay infra covers frame state (EEPROM saves are mission-level only).
- Alternative for the research phase: **lockstep/deterministic** (exchange
  ~10 bytes of input per 60Hz tick, both machines run the full sim). Far less
  code *if* the sim is bit-deterministic across hosts — PRNG is ported
  verbatim (`port/src/random.c`, good), but host-scheduling nondeterminism is
  a known risk (see D24-implications note below). Desync = hard stop.
  A per-tick state-hash desync detector is cheap insurance either way.
- PD port has **zero networking code** — nothing to copy; the net layer is
  new (`port/src/net.c`-style, SDL_net or plain sockets).

**Shape of the work under host-authoritative:** generalize to host + N clients
(N≤3), per-client input channels, state snapshots fanned out to all clients.
The host already runs the full sim, so scaling past 2P is mostly protocol
fan-out — which is why 2P is a natural intermediate milestone on the way to
the 4-instance bar.

**v1 bar:** LAN only, manual IP entry (host listens / client connects),
deathmatch playable; 2P first, then scale to 4 instances. No matchmaking,
discovery, reconnect, or internet play. Stretch: ping display, graceful
reconnect-on-drop, mission-based (non-deathmatch) multiplayer variants,
remote-player model interpolation between snapshots (Quake Enhanced ships
it; hides LAN jitter).

### Co-op (new mode — built on the deathmatch net layer)

**The idea:** two players progress the single-player campaign levels
together — locally on one PC, and over LAN with each player on their own PC.
Sequenced **after** LAN deathmatch: reuse its connection handling, snapshot
format, and input channels; this item adds the mode itself.

**Key fact (defines the scope): GE has no co-op mode.** Unlike PD, GoldenEye
only ships split-screen *competitive* multiplayer. Co-op on solo levels exists
in **PD only** — so this is a net-new game mode: mapping/level and GE game-
code feature work, on top of the (already-existing) networking plumbing.
Consequences:
- Non-negotiable #2 ("game logic unmodified") is about not altering *existing*
  behavior; adding a new mode is a separate, explicit owner decision. Record
  the policy call when work starts.
- `pd_port/` is the reference implementation: PD's decomp shows how two-player
  co-op was wired into this same Indy engine (shared objective progression,
  both players' AI/enemy interaction, win/lose conditions, split-screen
  presentation). Expect an adaptation study, not a copy — GE's mission/
  objective code differs file-by-file.
- Cheaper fallback worth considering during design: "freeplay" co-op (both
  players active on a solo level, but only P1's objective progress counts /
  or objectives are duplicated per player). Much less new logic; decide after
  the PD study.

**v1 bar:** LAN + local 2P co-op on solo levels over the deathmatch net layer.
No matchmaking, discovery, reconnect, or internet play.

### Fun features (remaster research: Quake 1 Remaster / Nightdive)

Feature mining from the Quake 1 Remaster (id, 2016), Nightdive's remaster
line (Duke Nukem 3D: Forever, DOOM, System Shock), and — round 2,
2026-08-29 — Quake Enhanced, Quake II Enhanced, Turok Resurrection, Doom 64
Definitive Edition, SiN Reloaded. Pattern across all of them: classic
fidelity as default + a shelf of opt-in QoL/accessibility/asset options.
Tiered by effort; all stay in `port/` unless noted.

**Cheap wins (config toggles over existing infra):**
- **Screenshot hotkey.** Productize the existing debug dump:
  `gfx_opengl_dump_bound_fbo()` (`port/src/video.c:199`) is already there —
  wire a keybind + configurable output dir, timestamped filenames. Every
  Nightdive title has this; ours is ~an afternoon.
- **Screen-shake intensity slider (0–10x).** All shake funnels through
  `viShake()` (`src/game/bondview2.c:8300` et al.) which lands in our port
  shim — scale amplitude there, zero src changes. PD precedent:
  `Game.ScreenShakeIntensity`.
- **No hit-flash / damage-indicator toggle.** The red full-screen flash on
taking damage is a top community-mod option; Nightdive ships it. Impl: find
how the flash overlay is drawn (likely a full-screen HUD quad) and intercept
in the port layer, or route-(b) one-liner.
- **Auto-pause / input-hold on window focus loss.** SDL focus events; trivial,
prevents drive-by deaths when alt-tabbing.
- **Controller polish (copy from PD `input.c`):** per-player gamepad
assignment (`Input.ControllerIndex`), per-stick deadzone/sensitivity sliders,
rumble scale + rumble mapping to GE's feedback moments. Note `osMotor*`
(Rumble Pak) is currently stubbed — accessory detection only
(PCPortResearch §4) — so wiring it to `SDL_GameControllerRumble` is part of
this item; Doom 64 DE shipped haptic feedback (a KEX first). We have basic
pad support (D118); this is the remaster-grade layer on top.
- **Tick-rate / framerate options.** PD ships `Game.TickRateDivisor` +
framerate cap. Flag: any tick-rate change must be reconciled with the future
netplay determinism requirement before shipping as a user option (lock it to
60Hz default, document).
- **Fog / draw-distance option.** Turok's KEX port removed the N64's heavy
  distance fog; Quake II Enhanced added height-based fog with custom colors.
  fast3d already decodes fog generically (PCPortResearch §5) — a config
  slider scaling/overriding fog density, default = original values. Zero src
  changes.

**Medium:**
- **High-res / user texture packs (the big goodwill item).** Quake 1 Remaster's
core feature is swappable asset sets (classic vs remastered toggle). GE
textures are RZ-compressed and decoded at load (`port/src/rzdecomp.c`) — a
per-texture override lookup against a user directory (fall back to ROM) hooks
there. Enables the community to make packs; also our escape hatch for the
blurry-asset complaints without touching fidelity defaults.
- **Accessibility: colorblind post-process filters** (protanopia/
deuteranopia/tritanopia full-screen shader pass) + the screen-shake slider
above. Nightdive includes these; cheap once we have a post-process stage
(shared infra with FXAA from the graphics item — sequence together).
  Quake II Enhanced also shipped high-contrast / readability UI options —
  add where the port layer can reach (bitmap HUD text limits scope;
  document what's not doable under route b).
- **Crosshair customization** (size/opacity at minimum; GE's crosshair is
game-drawn — check whether it's a textured quad we can scale in the port
layer before assuming route b).
- **Widescreen FOV compensation** — already baseline in the graphics item;
listed here because it's the feature every remaster ships and users will
expect.

**Big / stretch:**
- **Demo / replay record + playback.** Record the input stream (+ initial
state), play back headlessly. Quake tradition; Nightdive titles have
replay-ish tooling. **Strong synergy with netplay work:** a working replay
system *is* a determinism test harness — if a recorded demo replays
bit-identically, lockstep netplay's hardest assumption is proven. Consider
building it as the netplay pre-flight tool even before it's user-facing.
- **Photo mode.** Pause + free orbit camera + capture (System Shock
precedent). Free camera = port-layer camera override; pairs with the
screenshot hotkey.
- **In-game console.** Type commands at runtime. Less "remaster feature"
than dev tool, but for a WIP port it's arguably the highest-value item on
this list: toggle debug flags, dump state, force conditions without
restarting. PD has no equivalent; we'd own the design.
- **Benchmark mode.** Scripted auto-run sequence + FPS report (Quake
tradition); trivial once tick-rate options exist.
- **AI opponents in deathmatch.** Quake Enhanced ships bots in DM. GE's N64
  MP is human-only, so bots = net-new game logic — same policy class as
  co-op (record the owner decision when work starts). Stretch: lets a solo
  player exercise the netplay layer and makes MP approachable with 1–2 humans.
- **Animation smoothing ("wiggle elimination").** Quake II Enhanced restored
  high-res skeletal animations specifically to kill low-fps "wiggle". GE's
  model keyframes are N64-rate; interpolating (slerp) between keyframes in
  the port-layer model-transform path would visibly smooth guard/Bond motion
  on modern displays. Stretch — sequence with the graphics backlog, do late.
- **Dynamic shadows.** Quake Enhanced's headline graphical feature. For
  fast3d this means a shadow-map pass + per-light casting — deep stretch;
  park next to texture upscaling (both "do last" items).
- **Remastered audio option (Phase 3 note).** System Shock's remake added
  new sounds/music. Keep N64 ADPCM fidelity as the default; optionally offer
  a lossless re-encoded soundtrack toggle — owner decision + content work,
  park until Phase 3 lands.
- **Optional modern UI skin.** SiN Reloaded shipped upgraded 2D screens /
  menu art; System Shock an overhauled interface. For GE this is route (b)
  game-side territory (menu tables in `front.c` / `initmenus.c`) — park as
  an explicit owner decision; lowest priority on this list.
- **Stereo 3D** — both reference families shipped it; explicitly
deprioritized as anachronistic. Park unless someone asks.

Out of scope by the v1 bars above: online matchmaking, leaderboards,
anti-cheat, cross-play (Quake 1 Remaster's biggest features; revisit only
after LAN netplay is solid).

Also out of scope by policy (community port): online-platform integration —
Steam/GOG/Epic achievements, cloud saves, storefront add-on downloads. No
platform tie-in of any kind.

--Old is above here

---

## Technical cleanup (backlogged; not gated on playability)

- **Remove TEMP D63 debug scaffolding.** ~16 references in `port/src/` (`libultra.c`): the master-DL scanner and env-gated `GE_D63` logging inside `osSpTaskStartGo()`. D63 is closed; this is dead weight in the hottest function in the port.
- **Add a D24-implications bug class to `docs/PORT-LEARNINGS.md`.** "Host-scheduling nondeterminism / fake priority semantics": the pthread kernel does not enforce N64's 0–31 priorities (`osYieldThread` = `Sleep(0)`), so interleavings impossible on console can occur, and frame timing has host jitter. Consequence for future sessions: when a flaky timing bug appears (especially Phase 3 audio underruns), reach first for host thread priorities (`SetThreadPriority`: scheduler thread time-critical, tick normal) and the deferred `GE_DETERM` mode (D117) — not for kernel changes.
- **Watch item: `osYieldThread` = `Sleep(0)`.** The one place we fake cooperative behavior. If a level-sweep hot loop misbehaves under host load, this shim is the first thing to inspect. Keep as-is until then.

## Architecture decision (recorded; no action)

- **Keep the pthread + real `src/sched.c` model.** Compared against the PD port's actual design (verified in `pd_port/`: zero threads — thread API is pure no-op, `mainProc()` runs as the host main thread, `pdsched.c` is a gutted frame-callback wrapper, audio driven inline via `amgrFrame()`, M_AUDTASK dropped). PD could do that only because their *original N64* scheduler already used direct-call submission ("nothing writes to the cmdQ in PD") and they adapted game-side lib code (`audiomgr.c`). For GE — classic `osSendMesg(cmdQ)` protocol, real `__scMain` loop, `amMain` blocking on retrace/reply messages — that route means editing `src/audi.c`/submission points (non-negotiable #2 violation) and regressing Phase 3, which currently runs the full authentic audio scheduling protocol. No rewrite. Related: audio stays inline in the `M_AUDTASK` branch (`docs/AUDIO-PLAN.md`, Option A + sub-decision A3); no dedicated audio worker thread unless host thread priorities provably fail.