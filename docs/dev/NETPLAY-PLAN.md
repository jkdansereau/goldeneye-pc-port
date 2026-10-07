# Online multiplayer (netplay) — design plan

Status: **design + first implementation (v1)**, 2026-10-07. Finding label: **D413**
(see `docs/dev/findings.md`). User-facing guide: [`docs/netplay.md`](../netplay.md).
Server operations: [`tools_pc/netplay/README.md`](../../tools_pc/netplay/README.md).

**Verification state (2026-10-07).** The network core (`port/net/`), the
matchmaking server and the self-test are built and verified (MSVC `/W4` zero
warnings; `netplay_selftest` all green, incl. a real `ge007-netserver` over
UDP loopback). The game-side glue (`netgame.c`, `netui.c`, the `input.c` /
`libultra.c` / `video.c` / `watchsettings.c` hooks) was type-checked with
MSVC against stub SDL headers, and the fast3d presentation math was
unit-tested by extracting the code verbatim (23/23). **Not yet done:** a
MinGW/SDL2 build of the game and the in-game runs of §10 step 2 — none of
that could run in the authoring environment (no toolchain, no ROM).

## 1. Goal

Online matchmaking for GoldenEye's **4-player multiplayer**, where every player
is on their own PC and sees **only their own view, full-window** (not split
screen), with the full N64 multiplayer feature set: every scenario (Normal,
YOLT, Flag Tag, Golden Gun, Licence to Kill, 2v2 / 3v1 / 2v1 teams), every MP
stage, every game length, every weapon set, all 64 MP characters, handicaps,
control styles, aim/sight modes.

Non-goals for v1: spectators, rollback, mid-match join, local split-screen
mixed with online players, cross-platform (Windows <-> Linux) matches, IPv6.

## 2. Approach: deterministic lockstep over an input relay

**Every peer runs the complete, unmodified N64 multiplayer simulation for all
players. Only controller input crosses the network.** This is the model N64
emulator netplay uses, and it is the only model compatible with AGENTS.md
rule 2: the game logic is untouched because the game simply sees four
controllers. A state-sync / client-server model (what the Perfect Dark port's
`port-net` branch does) would require pervasive game-code edits.

### 2.1 Evidence the GE simulation is deterministic given its inputs

GE ships a RAMROM demo-replay system (`src/game/ramromreplay.c`) that replays
recorded input on real hardware. It records exactly what determines a stage:

| Recorded state (`copy_current_ingame_registers_before_ramrom_playback`) | Per frame (`record_player_input_as_packet`) |
|---|---|
| `g_randomSeed`, `g_chrObjRandomSeed` | `speedgraphframes` (60 Hz ticks this frame) |
| `gamemode`, `selected_num_players`, `scenario`, `MP_stage_selected`, `game_length`, MP weapon set | every controller sample (stick x/y, buttons) |
| `player_char[4]`, `player_handicap[4]`, `controlstyle_player[4]`, `aim_sight_adjustment`, team flags | a `randseed` check byte (desync detector) |
| the save file (options) via a RAM-only save slot (folder 100) | |

So: identical stage-start state + identical per-frame tick counts + identical
controller samples ⇒ identical simulation. Netplay reproduces exactly those
three things at the port boundary.

### 2.2 What the port must make identical (and how)

| Input to the sim | Normal PC behaviour | Netplay mechanism |
|---|---|---|
| Ticks per frame (`waitForNextFrame` → `updateFrameCounters`) | wall-clock `osGetCount()` (D117) | **virtual clock**: on the game thread during a match, `osGetCount()` releases exactly one 1/60 s quantum per consumed frame ⇒ `deltaFrames == 1` every frame on every peer (`port/src/libultra.c`) |
| Controller samples | scheduler-thread `joyPoll` → variable samples/frame | the game's own **joy playback hook** `joySetPlaybackFunc()` + `joySetContDataIndex(1)`: called once per frame on the game thread; we supply exactly one sample (all 4 pads) from the network bundle |
| Controller count | real pads | playback mode reports `playbackcontcount` = player count |
| PRNG seeds | seeded once at boot from wall clock (`boss.c`) | host-chosen seeds written at stage load (`g_randomSeed`, `g_chrObjRandomSeed`) |
| MP setup globals | whatever each local front end holds | host's settings applied before `lvlStageLoad` (and the game's own validators re-run) |
| Save options (auto-aim, aim mode, sight, look-ahead, ammo, screen, ratio, invert) | each player's selected folder, reloaded at every stage start | RAM save slot (folder 100) via the RAMROM path `set_selected_foldernum_and_copy_demo_eeprom()` with agreed option bits |
| Active cheats | front-end cheat toggles (`g_CheatActivated[]`) | cleared for the match, restored afterwards |
| Frame counters, player perm data | accumulate since boot | reset at the match start hook |
| Port knobs that change *which game code runs* | per-user ini / window aspect | **pinned** to N64-faithful values during a match (table in §4) |
| Port input paths that write game state directly (`input.c`) | scheduler thread, `g_CurrentPlayer`, any time | **captured** into the local player's network record and re-applied identically on every peer at the playback point (§5) |

## 3. Architecture

```
            +---------------------------- game process ---------------------------+
            |  game thread (bossMainloop)            scheduler thread (fast3d)    |
            |   waitForNextFrame -> osGetCount ----.   gfx_run: present only the  |
            |   joyConsumeSamplesWrapper           |   local player's viewport    |
            |     -> netgamePlaybackFunc ---------.|   + netui overlay            |
            |   lvlRender (sim + DL)             | |                              |
            |                                    v v                              |
            |                  port/src/netgame.c  (game glue, pins, capture)     |
            |                                    |                                |
            |                  port/net/  ge007net static lib (clean includes)    |
            |   net thread:    net_runtime -> net_client  [+ net_host if hosting] |
            +-------------------------------------|-------------------------------+
                                                  | UDP
                   +------------------------------+------------------------------+
                   |                                                              |
     direct host (a player's game: net_host)          ge007-netserver (net_host, many lobbies,
     LAN / port-forwarded IP                          matchmaking: list / create / code / quick)
```

* **Star topology, host-authoritative input relay.** Every client sends its
  local input records to the host; the host assembles *frame bundles* (all
  players' records for frame *f*) and broadcasts them; every peer simulates
  only from bundles. One authority means disconnects resolve deterministically
  (the host decides from which frame a dropped player's input is neutral).
* The **host never simulates the game.** It only shuffles ~10-byte input
  records, so the same `net_host` code runs inside a player's game (direct /
  LAN hosting) and inside the standalone **`ge007-netserver`**, which hosts any
  number of lobbies and is the matchmaker. Server-hosted lobbies need no NAT
  traversal (everyone connects out to the server).
* The host player's own game connects to its in-process host over loopback, so
  game-side code is always "a client".
* The network stack runs on its own thread; the game thread only pushes local
  records and pulls bundles (lock-protected), so stage loads (seconds) never
  stall the protocol.

### 3.1 Code layout

| Path | Contents | Depends on game? |
|---|---|---|
| `port/net/` (`ge007net` static lib, own CMakeLists with a clean include path — the game's `-I` list shadows libc with N64 stubs, D324 class) | `net_plat` (time/threads/mutex/CSPRNG), `net_sock` (UDP + transport vtable), `net_wire` (LE serializer), `net_proto` (messages), `net_session` (handshake, keepalive, RTT, reliable ordered channel), `net_host` (sessions, lobbies, matchmaking, lockstep relay, desync compare), `net_client` (connection, lobby replica, lockstep consumer, timesync), `net_runtime` (net thread) | no |
| `port/src/netgame.c` | match start/stop, virtual clock coupling, playback func, settings application, pins, input record apply, state hash, presentation config | yes |
| `port/src/netui.c` | in-game overlay: matchmaking + lobby screens, in-match HUD line | yes (fonts/DL) |
| `tools_pc/netplay/` | `ge007-netserver` (matchmaking + relay server), `netplay_selftest` (simulated-network lockstep harness) | no |

### 3.2 Game-code touch points

Exactly **one** line of game code, a seam with no effect unless a match is
starting: `src/boss.c` calls `netgameOnStageLoad(g_StageNum)` at the top of the
per-stage loop body (before the stage's first PRNG draw in
`init_player_data_ptrs_construct_viewports` and before `lvlStageLoad`). Same
pattern as the existing `#ifdef PORT` seams in that loop (D121, D294, D235).
Everything else is in `port/`: `osGetCount` (`libultra.c`), the knob
functions (`video.c`), `portMouseAimPdGetTurn` + capture (`input.c`), the
watch-settings drain (`watchsettings.c`), overlay + presentation (`fast3d`).

## 4. Determinism hazard register

Found by auditing every `port*`/`input*`/`video*` symbol the game calls and
every port write into game state (study notes in D413).

| # | Hazard | Why it diverges | Mitigation (v1) |
|---|---|---|---|
| H1 | Variable timestep (D117) | `deltaFrames` from wall clock | virtual clock, 1 tick/frame |
| H2 | Samples per frame vary | `joyPoll` runs per retrace on the scheduler thread | playback hook, exactly 1 sample/frame |
| H3 | PRNG stream position | menus/attract demos draw from `g_randomSeed` | seeds set at stage load |
| H4 | `portNativeAspect()` (window aspect) | projection aspect → crosshair↔world mapping, auto-aim, rocket viewmodel offset | returns 0 (N64 projection) in a match |
| H5 | `portScaleFovY()` (FovScale, WidescreenAuto) | frustum → visible rooms/props → `propsTick`, auto-aim targets | identity in a match |
| H6 | `portDrawDistanceMultiplier()` | far clip / chr fade → which props render & tick | pinned to the port default (250 %) in a match |
| H7 | `portLodDistanceMultiplier()` | model LOD selection | pinned to the port default (250 %) in a match |
| H8 | `portRoomPoolScale()` / `GE_ROOMPOOL` | room pool size → allocation failures | the D294 formula at the pinned distances (√2.5), env override ignored |
| H9 | `input.c` direct writes (`vv_theta/vv_verta`, crosshair, crouch, use/reload/gadget calls, control-type override) | scheduler thread, local peer only, `g_CurrentPlayer` = whichever player | captured into the local record, applied on all peers at the playback point to `g_playerPointers[slot]` |
| H10 | `portMouseAimPdGetTurn()` (PD mouse aim, D332) | returns local mouse for whatever player is current | returns the current player's networked turn |
| H11 | F10 watch settings (`watchSettingsGameTick`) | writes global options mid-match | drain deferred while a match runs |
| H12 | Crosshair hide/style, no-hit-flash | change which game code runs (texture loads, colour state) | pinned to defaults in a match |
| H13 | Save options per folder | reloaded at every stage start | RAM save slot (folder 100) with agreed bits |
| H14 | Cheats | front-end toggles applied at first tick | cleared for the match |
| H15 | Leftover joy playback ring | edge detection vs stale sample | ring zeroed on the first playback call |
| H16 | `g_playerPlayerData` leftovers (stats, team flags) | not all fields reset per stage | zeroed + re-seeded at match start |
| H17 | Audio-thread state (`sndGetPlayingState`) | async | audited: only gates sound start/stop; no PRNG/sim effect |
| H18 | Floating point across machines | | same binary ⇒ same SSE2 code; game math (`sinf/cosf/atan2f/acosf`) is the game's own code, not libm; matches restricted to identical build id |
| H19 | Unknown unknowns | | **desync detector**: every 30 frames each peer hashes PRNG seeds + per-player position/angles/health/weapon; the host compares and warns |

Not hazards (verified): screen shake (VI v-offset only), HUD scale, crosshair
tint/scale, texture filtering/MSAA, D318 watchdog (solo AI lists only,
tick-counted), scheduler `frameCount` parity (audio task phase only).

## 5. Input

Each peer samples its **local** input once per netplay frame on the game
thread (`inputComputePad(0)` with the local player made current), so input.c's
existing tuned mapping (keyboard, mouse modes, gamepad) is reused unchanged.
While capturing, every direct game write in `input.c` is redirected into a
`NetInputRec`:

```
NetInputRec { u16 buttons; s8 stick_x, stick_y;   // native N64 pad
              u8 flags; u8 actions;                // RELOAD, GADGET, CROUCH_DOWN/UP
              f32 look_dtheta, look_dverta;        // hip/edge-scroll mouse look deltas
              f32 cross_x, cross_y;                // GEPD absolute crosshair (aim mode)
              f32 gun_az, gun_turn;                // ...and the gun pose it writes with it
              f32 pdturn_x, pdturn_y; }            // PD mouse aim turn
```

The dedicated Use key becomes a native `B` tap in `buttons` (the interact
list is resolved inside each player's own render pass, so a direct call
would act on the wrong list). On the wire, each float group travels only when
its flag is set and sticks / actions only when non-zero, so an idle record is
3 bytes.

Floats travel as raw IEEE bits, so every peer applies bit-identical values.
A record sampled at frame *f* is scheduled for frame *f + D* (input delay).
Frames `0 .. D-1` are neutral by definition. Records are applied at the
playback point, i.e. once per frame before `lvlManageMpGame` and every
player's input processing, identically on every peer.

The **control style** of each player (1.1 Honey ... 1.4 Goodnight) is a lobby
setting applied through the game's own `controlstyle_player[]`; input.c's
per-poll override is suppressed in matches. On joining a lobby the overlay
proposes the style the PC bindings are tuned for (`inputPreferredControlStyle`:
1.2 Solitaire with Input.NaturalPitch=1, else 1.1 Honey — what input.c
enforces offline) and the player's remembered character (`Net.Character`).

Disconnected players get neutral input, except on the game-over screen where
an `A` tap is synthesized from (identical) game state so the results screen
can still be exited (`mpwatchMenuTick` waits for every player).

Rumble is unavailable online (`joyRumblePakStart` is a no-op in playback mode).

## 6. Presentation: your own view, full window

The game renders all players' views (it must — `lvlRender` interleaves
`propsTick`, auto-aim target selection and room visibility per player, so
every peer runs every view identically). fast3d then **presents only the
local player's viewport**:

* the local viewport rect `S` (e.g. 159×109 at (0,10) for player 1 of 4) is
  mapped to the largest window rect `T` of the same display aspect, centred;
* every viewport / scissor / 2D rectangle passes through the existing
  `gfx_adjust_viewport_or_scissor()` choke point with the S→T remap applied;
  scissors are intersected with `T`; HUD aspect modes are neutral inside the
  remap; draws whose logical viewport/rect lies outside `S` are skipped
  (other players' views cost DL interpretation only, no GL work);
* the port overlays (F10, netplay UI) are drawn after the remap is switched
  off, at normal full-window mapping.

3–4 players: quadrants are 159×109 (1.46:1) — practically the single-player
320×220 (1.45:1) frame, so the result looks like normal single-player.
2 players: GE's 2P views are 320×109 strips (2.9:1); v1 presents the strip
letterboxed (faithful N64 geometry). A full-height 2P layout needs a
game-side viewport change (`bondview2.c` viewport functions) and is listed in
§11 as a rule-2 candidate rather than done silently.

## 7. Protocol (UDP, little-endian, ≤ 1200-byte packets)

Header: `"GE7N"` magic, protocol version, packet type, payload length,
connection id. Packet types: `JOIN` (padded to 256 B so it is never smaller
than any reply — no amplification), `JOIN_ACCEPT`, `JOIN_REJECT`, `SESSION`,
`QUERY`/`QUERY_REPLY` (LAN discovery), `DISCONNECT`.

`SESSION` packets carry acks (`u16` cumulative + 32-bit selective bitfield) and
a list of messages; **reliable** messages are sequenced, retransmitted after
~1.5×RTT, delivered in order; **unreliable** ones are fire-and-forget.

| Message | Dir | Rel. | Purpose |
|---|---|---|---|
| `PING`/`PONG` | both | no | RTT (smoothed), keepalive |
| `LOBBY_STATE` | H→C | yes | full lobby snapshot (players, settings, code, leader) |
| `PLAYER_SET` | C→H | yes | own character / handicap / control / team / ready |
| `SETTINGS_SET` | leader→H | yes | match settings |
| `START_REQ` | leader→H | yes | start the match |
| `MATCH_START` | H→C | yes | match id, seeds, resolved stage, delay, slot map, settings snapshot |
| `MATCH_LOADED` / `MATCH_GO` | both | yes | start barrier (frame 0 only after everyone finished loading) |
| `INPUTS` | C→H | no | all unacked local records (redundant until acked) |
| `FRAMES` | H→C | no | all unacked bundles + per-slot latest-input frames (timesync) |
| `HASH` / `DESYNC` | both | yes | desync detection |
| `MATCH_END` / `MATCH_LEAVE` | both | yes | abort / leave |
| `LIST_REQ`/`LIST`, `CREATE`, `JOIN_CODE`, `QUICK`, `JOINED`, `FAIL`, `LEAVE_LOBBY` | C↔server | yes | matchmaking |
| `CHAT`, `NOTICE` | both | yes | lobby chat / system messages |

**Input delay** `D` (frames): auto = `clamp(ceil(maxRTT / 16.7 ms) + 2, 2, 12)`
measured at start, or fixed by the lobby (1–12).

**Timesync:** each `FRAMES` packet carries the host's latest input frame per
slot; a client that runs > 1 frame ahead of the slowest player sleeps up to
4 ms per frame, so the fast peer yields smoothly instead of hitting periodic
full-frame stalls.

**Disconnects:** the host times a client out (8 s in a match, 15 s in a lobby)
and marks the slot disconnected from the first frame it has no record for;
bundles carry the mask. Host loss aborts the match on every client.

## 8. Matchmaking server (`tools_pc/netplay/ge007-netserver`)

Single-file-per-module portable C (Windows/Linux), one UDP port (default
27008). Hosts lobbies itself (relay host, no game simulation, negligible CPU),
lists public lobbies, joins by 6-character code, quick match (join any open
public lobby with a compatible build, else create one; quick lobbies
auto-start 5 s after ≥ 2 players are all ready). Build-id gating: only
identical builds/protocol versions are matched. Limits: sessions, lobbies,
per-address join rate.

## 9. UI (`F9`)

Port overlay drawn with the game's own fonts (same approach as F10, D184;
`port/src/netui.c`). Screens: **Online** (Quick match / Browse / Create
public or private / Join by code / LAN games / Host on this PC / Join by
address / Settings) → **Lobby** (players with character, team, ready and
ping; your character / health / control style / team / ready; the match
settings — scenario, stage, length, weapons, aim/sight, input delay and the
six save options — editable by the leader; chat; Start / Leave) → **Match**
(F9 in a match: resume / leave / end for everyone; closed: a one-line net
HUD with ping, delay, waits and desync warnings). Keyboard (arrows / Enter /
Esc, typing and Ctrl+V into text rows), mouse (click, right-click, wheel)
and gamepad. While open it swallows controller 0 like F10 (in a match the
player idles; the match keeps running).

Safety rules the UI enforces: **Ready** is withdrawn automatically after 2 s
away from an interactive front-end menu (a start found anywhere else — a solo
mission, the PC options screen — is declined and the match would run without
that player); the leader's **Start** needs everyone ready and the leader on
the menus. A start that arrives mid menu-transition is held up to 15 s
(`netgame.c ngFrontEndSettled`). The window title carries the online status,
because it stays live while the game thread waits for a peer; **Shift+F9**
leaves a match at once, also during such a wait.

CLI: `--net-host [port]`, `--net-join addr[:port]`, `--net-server addr[:port]`,
`--net-quick`, `--net-name name`. Config `[Net]`: `Name`, `Server`, `HostPort`,
`ShowStats`, `Character` (the input delay is a lobby setting, 0 = auto).

## 10. Verification plan

1. **Selftest (runs anywhere, no ROM):** `netplay_selftest` wires a host and
   N clients through a simulated network (latency, jitter, loss, reordering,
   duplication), drives a toy deterministic "game" from the bundles, and
   asserts: every client sees byte-identical bundles; the toy state hash
   matches on all peers every frame; desync injection is detected; a dropped
   client is cut over to neutral input at the same frame everywhere; host loss
   aborts. Also a real-UDP loopback run against `ge007-netserver`.
2. **In game (needs the ROM):** two instances on one PC
   (`--net-host` + `--net-join 127.0.0.1`), 10-minute match on every stage
   with the desync detector on — zero desync reports is the acceptance bar;
   then a LAN pair; then internet via a server.
3. **Regression:** with netplay never opened, the port is byte-identical
   (all new code paths gated on an inactive match): single-frame `GE_PCDUMP`
   vs golden, plus the usual 60 s `-level_09` run.

## 11. Phases / roadmap

* **v1 (this change):** everything above, including LAN discovery.
* **v1.1:** 2P full-height views (needs a rule-2 sign-off: `bondview2.c`
  viewport width for 2 players in netplay), MP cheats in the lobby,
  per-player FOV, rumble from local damage events, chat polish.
  ~~An *Online* entry in the F10 overlay / the front-end menus~~ -- **done
  in D414** (file-select label, PC Options + F10 root entries).
* **D414 (landed): the online service.** Player-hosted lobbies brokered by a
  central directory with UDP hole punching. The directory is a free
  Cloudflare Worker + Durable Object (`tools_pc/netplay/cloudflare`): lobby
  list, codes, quick match with a one-way merge for simultaneous requests,
  join introductions, and a live public status page. Peers learn public
  addresses from Cloudflare STUN from their own game sockets; the host
  punches towards each joiner; joiners try every published address. A
  waiting-for-players screen is drawn by the scheduler thread while the game
  thread is parked (start barrier, late input). Full record: findings D414.
* **D415 (landed): random matchmaking.** Quick Match pairs whoever is
  searching; unreachable games are skipped and reported (deprioritised, then
  dropped); after three, the searcher hosts; quick games use standard rules
  with a random stage and a 12 / 8 / 3 s fill window; a searching screen.
* **D416 (landed): every variation, hardened.** Quick Match Settings cover
  mode (team sizes included), map, weapons, length and players, each "any",
  under GoldenEye's own rules. A searcher who finds no fitting game hosts one
  with its preferences as the rules. Protocol v3.
  * **Service hardening:** anti-reflection address rules, signed lobby
    tokens, results only for matches it saw, per-address and edge rate
    limits, HTTPS only.
  * **Game side:** peer input floats are sanitised, since a crafted packet
    could hang every PC in the game's angle-wrap loop.
  * **netfuzz under ASan** found the host's net thread spinning forever at
    the ordinary end of every match (`matchAssemble`), latent since D413.
    Fixed and regression-tested.
* **v2:** relay fallback for routers that cannot be hole-punched (TURN, or
  ge007-netserver as a relay for player-hosted lobbies); rollback (needs full
  game save/restore -- large); mixed local split-screen + online;
  spectators; cross-platform matches (build-id relaxation once
  desync-tested).

## 12. Risks

* **Undiscovered determinism leaks** — mitigated by H19 (detect, report,
  log the frame) and by pinning conservatively; first in-game runs are the
  real test.
* **Input feel at higher delay** — lockstep adds `D` frames; LAN ≈ 2 frames,
  typical internet 3–6.
* **Performance** — every peer still interprets all players' display lists
  (as split-screen does today); GL work for other views is skipped.
* **Mixed builds** — gated by build id; a modified local build reports a
  different id only if its version hash differs (non-git source trees report
  `unknown`; desync detection is the backstop).
