# GoldenEye 007 — game-behaviour reference

Community-documented account of how the retail N64 game is *supposed* to
behave: the combat/AI/stealth model, difficulty scaling, per-level objective
sets, timers, weapon data, and the original game's known quirks and glitches.

**Status of this document.** This is secondary source material, not ground
truth. The decompiled game code in `src/` is authoritative for behaviour (see
`AGENTS.md`). Use this doc to know *what correct looks like* when playtesting
the port — which objectives a level must expose, what a timer value should be,
whether a weird behaviour you just saw is an original-game quirk or a port
regression.

**Sources** (subjective commentary stripped; mechanical/factual data only):

- GameFAQs walkthrough by **Brett "Nemesis" Franklin**, v1.01, July 2001 —
  overview, mechanics, objective lists, weapon/gadget data, glitches.
- **marshmallow**, *Extensive 00 Agent Walkthrough* v3.0 (GameFAQs) — the
  00 Agent difficulty-delta list and the AI-exploit notes in §3.

Distilled from `goldeneye_007_reference.md` (the full raw walkthrough dump);
the walkthrough's per-stage tactical routes were dropped as not
behaviour-defining.

---

## 1. Difficulty modes

Three tiers. **00 Agent is a strict superset** of Agent and Secret Agent —
every objective required at a lower tier is also required at 00 Agent, so one
00 Agent run satisfies all three.

| Mode | Rank | Layout | Objectives |
|---|---|---|---|
| Agent | starting rank | small/simple | fewest |
| Secret Agent | +1 | average | more |
| 00 Agent | highest | big/complex | maximum (superset) |

### What 00 Agent changes mechanically

Relative to the lower tiers:

1. Guards are tougher — body-armoured guards can **survive a headshot** (a DD44
   headshot may not kill).
2. Better AI — enemies fire sooner, roll/dodge more often, and "hear" the
   player from further.
3. Picked-up guns start with only **10 rounds**.
4. Body armour is nearly absent (a few fixed locations only).
5. More objectives per level.
6. Low player health — roughly **8× KF7 Soviet hits** or **~5× AR33 hits** is
   lethal.
7. Higher enemy accuracy.
8. Explosions (crates, grenades, chairs) are near-instant-kill unless the
   player clears the radius fast.
9. Security cameras acquire the player in **~3 s** (vs ~10 s on Agent).

---

## 2. Combat, stealth & AI model

### Damage

- **Body-part damage system** — different hit locations do different damage;
  head shots are the fastest kill and are the intended way to conserve ammo.
- **Auto-Aim** (option) — snaps to the nearest enemy under the crosshair when
  firing; ~90% reliable at target selection.
- **Manual aim** (R / L-R) — zooms for precise body-part targeting.
- **Full-auto accuracy falls off** with sustained fire and with distance:
  "pecking" 1–2 rounds is more accurate than spraying; spray only wins against
  groups.

### Alerting

- A **loud** weapon fired within earshot of **2+ guards** triggers an area
  alert (they converge on the player's position and can call reinforcements).
  A lone guard may not call for help.
- **Silenced** weapons (PP7 Silenced, D5K Silenced, silenced pistols, knives)
  never alert.
- **Security cameras**: if a camera acquires the player it raises an alert
  that **spawns black-clad guards** armed with DD44s and KF7s. Shoot the
  camera centre for a one-shot kill.
- **Retreating** breaks pursuit — most guards stop following after a set
  distance, letting the player reload / heal / reposition.

### AI line-of-sight & animation quirks (exploitable, i.e. defining behaviour)

- A guard behind a **wall, low barrier or railing cannot shoot** the player,
  even point-blank — it must path around the obstacle. **Exception:** a guard
  behind a **crate** *can* shoot through/over it.
- Turrets / sentry guns **drop target beyond a set range**.
- Most guards play a **kneel animation before firing** (takes ~1–2 s). Circling
  a kneeling guard makes it fire at the player's previous position.
- **Point-blank**: a guard the player is pressed against **cannot fire** until
  it steps back.

---

## 3. Player state

| System | Behaviour |
|---|---|
| Health | restored by health packs; no regen. 00 Agent pool ≈ 8 KF7 hits (§1). |
| Body armour | separate absorbing layer; fixed pickup locations (e.g. by the Jungle drone guns, start of Antenna Cradle). Nearly absent on 00 Agent. |
| Explosive barrels/crates | shootable for area damage; can kill the player. In Water Caverns, detonating the barrels **before** using the radio destroys the radio and fails that objective. |

---

## 4. Timers (exact values — verify against decomp)

| Level | Trigger | Value | On expiry |
|---|---|---|---|
| Silo (M3) | level start | **8:30** | facility self-destructs; must reach the escape elevator |
| Bunker 2 (M5) | Natalya activates the main control panel | **~60 s** | alarm; must escort Natalya to the exit |
| Train (M6) | hostage scene, if Xenia is **not** killed | **4 s** | escape window before failure |

---

## 5. Companion AI — Natalya

- Present in Bunker 2, Jungle, Control Center, Water Caverns, Train
  (Missions 5–7).
- Carries a **Cougar Magnum**; engages enemies autonomously.
- Must survive — her death fails objectives.
- Scripted roles: hacks the mainframe in Control Center (player defends both
  staircases); freed from a cell and escorted out under the timer in Bunker 2;
  hacks a terminal in Train while the player laser-cuts the grate bearings.

---

## 6. Objective system

Each sub-level exposes a fixed set of **primary objectives** shown at level
start; completing the final one ends the sub-level. Objective *count* scales
with difficulty (§1). Some are **conditional / fail-state** objectives —
"Minimize scientist casualties", "Minimize civilian casualties" — tracked by a
running kill count against that class.

21 sub-levels across 9 missions:

| M | Location | Sub-levels |
|---|---|---|
| 1 | Aarkangelsk | Dam → Facility → Runaway |
| 2 | Severnaya | Surface → Bunker |
| 3 | Kirghizstan | Silo |
| 4 | Monte Carlo | Frigate |
| 5 | Severnaya (return) | Surface 2 → Bunker 2 |
| 6 | St. Petersburg | Statue Park → Military Archives → Streets → Depot → Train |
| 7 | Cuba | Jungle → Control Center → Water Caverns → Antenna Cradle |
| 8 | Teotihuacán | Aztec Complex |
| 9 | el-Saghira | Egyptian Temple |

### Per-level objective sets (00 Agent — the superset)

**Dam** — neutralize all alarms · install covert modem · intercept data backup ·
bungee jump from platform.
**Facility** — gain entry to lab · contact double agent (Dr. Doak) · rendezvous
with 006 · destroy all tanks in bottling room · minimize scientist casualties.
**Runaway** — find plane ignition key · destroy heavy gun emplacements ·
destroy missile battery · escape in plane.
**Surface** — power down comms dish · obtain safe key · steal building plans ·
enter base via ventilation tower.
**Bunker** — disrupt all surveillance equipment · copy Goldeneye Key and leave
original · get personnel to activate computer · download data · photograph main
video screen.
**Silo** — plant bombs in fuel rooms · photograph satellite · obtain telemetric
data · retrieve satellite circuitry · minimize scientist casualties.
**Frigate** — rescue hostages · disarm bridge bomb · disarm engine-room bomb ·
plant tracking bug on helicopter.
**Surface 2** — disrupt all surveillance equipment · break comms link to bunker ·
disable Spetznaz support aircraft · gain entry to bunker.
**Bunker 2** — compare staff/casualties lists · recover CCTV tape · disable all
security cameras · recover Goldeneye operations manual · escape with Natalya.
**Statue Park** — contact Valentin · confront and unmask Janus · locate
helicopter · rescue Natalya · find flight recorder.
**Military Archives** — escape interrogation room · find Natalya · recover
helicopter black box · escape with Natalya.
**Streets** — contact Valentin · pursue Ourumov and Natalya · minimize civilian
casualties.
**Depot** — destroy illegal arms cache · destroy computer network · obtain safe
key · recover helicopter blueprints · locate Trevelyan's train.
**Train** — destroy brake units · rescue Natalya · locate Janus secret base ·
crack Boris' password · escape to safety.
**Jungle** — destroy drone guns · eliminate Xenia · blow up ammo dump · escort
Natalya to Janus base.
**Control Center** — protect Natalya · disable Goldeneye satellite · destroy
armored mainframes.
**Water Caverns** — destroy inlet pump controls · destroy outlet pump controls ·
destroy master control console · use radio to contact Jack Wade · minimize
scientist casualties.
**Antenna Cradle** — destroy control console · settle score with Trevelyan.
**Aztec Complex** — reprogram shuttle guidance · launch shuttle.
**Egyptian Temple** — recover the Golden Gun · defeat Baron Samedi.

### Key scripted mechanics per level

- **Bunker / Surface 2** — cameras must be killed to avoid the black-clad
  alert spawn.
- **Silo** — Ourumov is killable here (he normally flees; Invisibility stops
  him); drops DD44 + briefcase. He still reappears later regardless.
- **Frigate** — hostages held at gunpoint; shoot the guard not the hostage.
  Bomb Diffuser used on two terminal-mounted bombs.
- **Streets** — tank vehicle; running over a **civilian** fails the mission;
  street land-mines apply damage-over-time to the tank.
- **Train** — kill Xenia + Ourumov simultaneously in the hostage scene; if
  Xenia survives, the 4 s timer starts and extra guards spawn.
- **Jungle** — Xenia boss on the bridge: uses RCP-90 + Grenade Launcher, rolls
  when hit, drops both weapons.
- **Control Center** — Boris pulls a gun ("I am Invincible!") and fades out
  rather than being killed.
- **Water Caverns** — contact Jack Wade by radio **before** detonating the
  barrel room; Code Cards A/B/C drop from guards in sequence.
- **Antenna Cradle** — Trevelyan final boss reappears alternately at the two
  sheds/catwalks; player kills him at each appearance, final fight point-blank
  in the shed under the cradle.
- **Aztec Complex** — first terminal starts the launch sequence + opens a
  door; a second terminal must be destroyed to reveal a hidden hallway before
  the launch finishes.
- **Egyptian Temple** — Golden Gun glass case lowers only after a fixed
  tile-path is walked; Baron Samedi appears twice, uses Lasers, dies only to
  the Golden Gun.

---

## 7. Weapons — data table

Clip / max-capacity / behaviour. (Balance and capacity numbers to cross-check
against the decomp's weapon tables.)

| Weapon | Type | Clip | Max | Behaviour notes |
|---|---|---|---|---|
| PP7 | pistol | 7 | 800 | default sidearm |
| PP7 (Silenced) | pistol | 7 | 800 | no alert on fire |
| DD44 Dostovei | pistol | 8 | 812 | high power, Magnum-like recoil |
| Cougar Magnum | pistol | 6 | 200 | high per-shot damage; Natalya's sidearm |
| Gold PP7 / Golden Gun | pistol | 7 / 1 | 800 / 100 | one-shot kill anywhere; Golden Gun = 1 round/clip |
| Silver PP7 | pistol | 7 | 800 | cosmetic |
| Klobb | SMG | 20 | 812 | weakest automatic |
| KF7 Soviet | auto rifle | 30 | 400 | ubiquitous early-game weapon |
| AR-33 | auto rifle | 30 | 400 | stronger than KF7; zoom |
| D5K Deutsche | auto rifle | 32 | 800 | zoom |
| D5K (Silenced) | auto rifle | 32 | 800 | no alert; one level |
| Phantom | auto rifle | 50 | 800 | very loud; one level |
| RCP-90 | auto rifle | 80 | 800 | fastest fire rate in the game |
| ZMG (9mm) | heavy auto | 32 | 800 | dual-wieldable |
| Automatic Shotgun | auto shotgun | 5 | 200 | Statue Park only (SP) |
| Shotgun | pump shotgun | 5 | 200 | slower reload; via All Guns cheat |
| Sniper Rifle | bolt rifle | 8 | 400 | longest zoom; best stealth weapon |
| Grenade Launcher | launcher | 6 | 12 | arcing grenades |
| Rocket Launcher | launcher | 1 | 3 | large blast, can kill player |
| Grenades | thrown | 12 | 12 | — |
| Throwing Knives | ranged melee | 12 | 12 | low damage |
| Hunting Knives | melee | — | — | fast silent takedowns |
| Proximity Mines | placed | 10 | 10 | triggers on proximity |
| Remote Mines | placed | 10 | 10 | detonated by Remote Watch; no timeout |
| Timed Mines | placed | 10 | 10 | detonate ~8 s after placement |
| Taser | gadget | — | — | non-lethal stun |
| Moonraker (Military) Laser | energy | — | — | fires through walls/doors; kills Jaws |
| Tank Shells | vehicle | — | — | Runaway, Streets |

Availability: weapons are level-scoped and do not carry between missions unless
noted. **All Guns** cheat unlocks everything.

---

## 8. Gadgets

| Gadget | Level(s) | Activation / effect |
|---|---|---|
| Covert Modem | Dam | toss onto a computer screen to install |
| Bomb Diffuser | Frigate | use on a terminal-mounted bomb |
| Camera | Bunker, Silo | photograph a target (video screen / satellite) |
| Key Analyzer | Bunker | copies a key and **destroys the original** |
| Data Thief | Bunker | download data from a terminal |
| Tracking Bug | Frigate | place on the Pirate helicopter |
| Watch Magnet | Bunker 2, Train | pull a key / metal object at range |
| Watch Laser | Train | cut metal (grate bearings) |
| Remote Mines + Remote Watch | Facility, Control Center, Water Caverns | place, then detonate at range |
| Door Decoder | Facility | from Dr. Doak; opens the locked bottling-room door |

---

## 9. World-interaction objects

| Object | Rule |
|---|---|
| Red alarm boxes | shoot to complete "neutralize all alarms"; a guard reaching one spawns reinforcements |
| Security cameras | destroy (centre = one-shot); acquisition raises the black-clad alert |
| Sentry guns | fixed turrets (Bunker 2 striped corridor, Depot); destroy to pass |
| Drone guns | remote turrets (Jungle, Control Center, Water Caverns, Aztec); some pairs must be destroyed together |
| Locks / grates | shootable (e.g. the 4-lock vent grate at the end of Surface) |
| Doors | B to open; some need keycard / key / code |
| Gate switches | red/green buttons control gates (Dam) |

---

## 10. Vehicles

| Vehicle | Level | Rules |
|---|---|---|
| Tank | Runaway, Streets | cannons + missiles (Runaway); may crush enemies, **must not crush civilians** (Streets); street mines = DoT |
| Plane | Runaway | final escape after the missile battery is destroyed |
| Train | Train | car-to-car traversal; a brake unit (red/yellow wires) in every car must be destroyed |

---

## 11. Controls / input options (relevant to `port/src/input.c`)

Default mapping (control style 1.1): **A** switch weapon/device · **B**
action / reload · **Z** fire / use gadget · **L/R** aim · **C-Up/Down** look ·
**C-Left/Right** strafe · **Stick** move · **D-Pad** mirrors C.

Four SP control styles (1.1–1.4) permute which of {aim, fire} is on Z vs A/L-R
and whether the stick moves or looks. Derived actions: **crouch** = Aim +
C-Down, **stand** = Aim + C-Up.

Options that change feel: **Reverse Pitch** (invert vertical aim),
**Look Ahead** (camera leads movement), **Head Roll** (full 360° look),
**Auto-Aim** (snap to nearest on fire), **Aim Control** = Hold or Toggle.

---

## 12. Original-game quirks & glitches

These are **retail N64 behaviours**. If the port reproduces them it is
faithful; if you see one while testing, it is probably *not* a port bug.

- **Guard AI freeze under Invisibility** — kill one guard in a group and the
  rest aim at the player's last known position but can never re-acquire (player
  is invisible); they hold the aim pose until the player moves or dies.
- **Ourumov / Jaws killable via Invisibility** — both normally can't be
  engaged (Ourumov flees, Jaws is near-indestructible); with Invisibility the
  AI can't see the player, so both become killable with any weapon.
- **Trevelyan respawn loop (Antenna Cradle, 00 Agent + Invisibility)** —
  killing Trevelyan on the catwalk can respawn him alive at the far shed,
  repeatable indefinitely. A pathing/AI bug, not intended.
- **Invulnerable 006 in Water Caverns** — with Invisibility, one of the
  "guards" leaving the elevator is actually a static, invulnerable Trevelyan
  model (his AI never detects the invisible player). An easter egg, not a
  spawn error.
- **Post-dialogue allies become killable** — after an ally's scripted
  conversation ends (e.g. Valentin in Statue Park) the player can kill them;
  no mechanical penalty.
- **Ourumov's key opens the Silo launch doors** — the briefcase/key dropped by
  Ourumov has no objective use, but "firing" the key (action button) near the
  rocket opens the giant launch doors. Otherwise both items are inert.
- **Cut content left in levels**: the Dam has the unused geometry of a
  cancelled boat mission out on the water; the Train's rear region past
  Alec/Trevelyan leads nowhere; the Facility's starting vent has a second path
  blocked purely cosmetically and shootable ceiling glass.

---

## 13. Multiplayer (brief)

Up to 4 local players. Deathmatch + variant modes. Level list = all SP levels
plus dedicated MP maps. Character/costume/face are swappable (the raw dump has
the full GameShark costume/character ID tables if ever needed for the MP
character system).
