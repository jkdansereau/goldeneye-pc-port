# CUT-CONTENT-BACKLOG — TCRF "cutting room floor" findings vs decomp/port

Research branch: `research/cut-content-backlog`. Source of truth: the local TCRF
export at `scratchpad/backlog-scrape/restore-cut-content-mod/` (gitignored).

> **Superseded by the verified cross-reference.** All six article *bodies* were
> recovered and every major claim was checked against the decomp source and asset
> tables. The full verified findings (with file:line references, corrections to
> TCRF, and updated mod tiers) live in `backlog/cut-content/RESEARCH.md`
> (gitignored; article markdowns in `backlog/cut-content/articles/`). This doc is
> the first pass (image-filename based) — read it for the per-item imagery index,
> but trust RESEARCH.md where they disagree. Key corrections: (1) "dest" is the
> *shipped* Frigate geometry, not an early version (`bg.c` levelinfotable);
> `cryp`/`dish` are shipped Egypt/Temple; (2) Library/Stack share Basement's bg
> file — they have geometry; (3) all 29 "unused items" exist in the ROM object
> table as pickup+prop pairs except heroine; (4) TCRF's 11 unused cheats, music
> track index, MP character table and build dates all verify 1:1 against
> `bondconstants.h`/`cheat.c`/`front.c`/`compiletime.c`.
>
> **Round 2 (decomp-only discoveries — not in TCRF's documentation):**
>
> 1. **The shipped game still calls the M16 an "AR33"** — pickup text
>    `"an AR33 assault rifle."` (`LpropobjE.c` PROPOBJ_STR_2A, used at
>    `propobj.c:10471`), plus unused strings `"US AR33 Assault Rifle"`/
>    `"US AR33"` (`LgunE.c` GUN_STR_74/75) and `PROP_CHRM16 /* AR33 Assault
>    Rifle */` comments. TCRF's prerelease page mentions the AR33 name in early
>    footage but not that it survived into retail text.
> 2. **Klobb rename trace verified**: weapon-name strings in `LgunE.c` are
>    alphabetical except GUN_STR_6B `"Klobb"` (between "Spy File" and "Staff
>    List") — exactly the last-minute Skorpion→Klobb scar TCRF describes.
> 3. **Two cut weapons TCRF's unused-items page doesn't list**: `cartrifle` and
>    `wristdart` — full pickup+prop resources (`GcartrifleZ`, `GwristdartZ`,
>    `PchrwristdartZ /* (BETA) */`) but no `ITEM_*` entry and no WeaponStats,
>    i.e. unusable as weapons. The wrist dart is the hardware behind TCRF's
>    `AMMO_DARTS` ($0E).
> 4. **All four Bonds, fully mapped**: `CUFF_CONNERY/DALTON/MOORE` +
>    `BOND_BROSNAN/CONNERY/DALTON/MOORE` enums; the actor-switch code
>    (`bondview2.c:391-460`) survives with every branch flattened to Brosnan;
>    the save struct keeps the per-dossier actor field. **No Connery/Moore/
>    Dalton models exist in the retail ROM** (80 `C*Z` chr models enumerated —
>    none for the former actors); only enum slots + the `SW_CONNERY`/`SW_DINNER`
>    switches on the watch-arm model remain. TCRF's "four dossiers, four
>    actors" was pre-release state.
> 5. **Cuba is the one orphan level with recoverable content**: `LlenE.c` holds a
>    cut Natalya dialogue exchange, the "C U B A N J U N G L E" title card, and a
>    full alternate credits cast. The other 12 unused level text files are empty
>    stubs.
> 6. **Orphan-level geometry IS in the ROM**: `assets/obseg/obseg.h` declares all
>    nine text-only orphans' bg segments (`bg_sho_all_p_seg[]` …) — a
>    byte-matching decomp can't fabricate them. What's missing is mission setup
>    (briefings exist only for the 21 solo levels), not geometry.
> 7. **Moneypenny's two risqué Frigate-briefing lines are gone from the ROM**
>    (final `LdestE.c` keeps only the toned-down "thighs of steel" paragraph);
>    recoverable only from TCRF's transcription. "Destroyer"→"Frigate" rename is
>    complete; the Manticore yacht mission has zero ROM trace.
> 8. **Music enum ↔ Notes-page GS index fully aligned**: indices 0–48 match
>    slot-for-slot; four local mismatches past 0x2C (49 `M_CUBA` vs "Ending
>    theme", 52 `M_MPTHEME3` vs "Surface 2 X", 54 `M_GUITARGLISS` vs "MP Death
>    (Alt)", 59 `M_SURFACE2X` vs "Surface 1 X") — in-game `DEB_MUSIC` audition
>    needed to resolve.

## 1. Findings

### A. Debug menu & dev tools — fully in the ROM, fully compiled into the PC build

TCRF main page images: `GEDebugMenu`, `GECheatOptions`, `GoldenEye-displayspeed`,
`GoldenEye-moveview`, `GoldenEye-LineMode`, `GoldenEye-testingmanpos`,
`GoldenEye-gunwatchpos`.

All of these exist in decompiled code:

- **Debug menu** — `src/game/debugmenu_handler.c` (1202 lines). Opened by holding
  **U + D C-buttons** (`debug_menu_processor`, ~line 451; wired into `boss.c:551`
  and the main game loop). Full option list at lines 116–185: `move view`,
  `stan view`, `bond view`, `level` / `region` / `scale` (level switching),
  `play title`, `bond die`, `select anim`, `gun pos`, `flash colour`,
  `hit colour`, `music`, `sfx`, `invincible`, `visible`, `collisions`,
  `all guns`, `max ammo`, `display speed`, `background`, `props`,
  `stan hit` / `stan region` / `stan problems`, `print man pos`,
  `port close` / `port inf` / `port approx`, `pr room loads`,
  `show mem use` / `show mem bars`, `grab rgb` / `grab jpeg` / `grab task`
  (screenshots), `rnd walk`, **`record ramrom` / `replay ramrom` /
  `save ramrom` / `load ramrom`** (input recording/replay!), `auto y aim`,
  `auto x aim`, `agent` / `all` / `fast` (unlock flags), `objectives`,
  `marg top/bot/left/right/reset`, `screen size`, `screen pos`,
  `show patrols`, `intro` / `intro edit` / `intro pos`, `world pos`,
  `gun key pos`, `vis cvg`.
- **Free camera** — `src/game/debug_camera.c` (the "move view" option).
- **Load-all-models tool** — `src/game/deb_loadallmodels.c` (used by
  `initguards.c`; dumps every model — how the TCRF screenshots of unused
  models were likely made).
- **Line mode** — `CHEAT_LINEMODE` in `src/game/cheat.c:685` (wireframe toggle,
  cheat-menu unlockable; `front.c:1017`).
- Screenshot options (`grab rgb`/`grab jpeg`) call `indyGrabRgb32bit()` /
  `indyGrabJpg32bit()` — N64 VI grabs; on PC these are the natural hook for the
  port's existing screenshot QoL (see `feat/qol-mute-screenshot` lineage).

**Status: Tier 0.** Everything compiles into the PC build (`CMakeLists.txt`
`GLOB_RECURSE src/game/*.c`). The debug menu should already work in the PC port
if U+D C-buttons can be produced by `port/src/input.c`. *Verify in-game* (open
menu, toggle `display speed`, use free cam). Mod value: document it; consider a
config/keybind that synthesizes the U+D c-button hold, and mapping `grab rgb`
to the port's screenshot path.

### B. Cut weapons — fully functional in the ROM, grantable via existing cheat codes

TCRF images (main + prerelease pages): `GoldenEye-Taser[PR]`, `GoldenEye-KF7[PR]`
(+`KF7Watch`), `GoldenEye-GrenadeLauncher[PR]`, `GoldenEye-RocketLauncher[PR]`
(+`RocketLauncherWatch`), `GoldenEye-RifleLauncher` (+`RifleLauncherCheat`),
`GE64_Shotgun1`, watch variants `MagnumWatchPR`, `PP7WatchPR`, `KlobbWatchPR`,
`TaserWatch`.

Evidence in code/assets:

- Model + stat files exist for every cut gun under `assets/obseg/gun/`:
  `taser/`, `rocketlaunch/`, `grenadelaunch/`, `fnp90/`, `spectre/`,
  `autoshot/`, `ak47/`, `m16/`, `skorpion/` … each with
  `gunWeaponStat.inc.c` + `ModelFileHeader.inc.c`.
- `assets/obseg/gun/gunWeaponStats.inc.c`: complete `WeaponStats` for
  `taser_stats` (line ~365), `rocketlaunch_stats` (~305),
  `grenadelaunch_stats` (~295), plus `watchlaser_stats`, `tank_stats`,
  `bombcase_stats`, etc. These are data-driven — the weapon engine
  (`src/game/gun.c`, `gunfire.c`) will run them.
- First-person view cases exist: `src/game/bondview_r.c:38` handles
  `ITEM_GRENADELAUNCH`, `:41` `ITEM_ROCKETLAUNCH`.
- **Cheat codes grant them**: `src/game/cheat.c` — `CHEAT_2X_ROCKET_LAUNCHER`
  (line 711), `CHEAT_2X_GRENADE_LAUNCHER` (712), `CHEAT_2X_RCP90` (713),
  plus unlock sequences (`CHEAT_UNLOCK_2XGL/2XRL/2XFNP0`, lines ~695–734) and
  the grant logic at lines 1277–1310: `bondinvAddInvItem(ITEM_ROCKETLAUNCH)` /
  `give_cur_player_ammo(AMMO_ROCKETS, …)` etc. Note the `#if defined(BUGFIX_R1)`
  guards — check which region macros the PC build defines (non-negotiable #3).
- Taser SFX exist: `GUN_TASER_SFX` / `GUN_TASER_LOOP_SFX` (`src/bondconstants.h:2271`).
- `ITEM_TASER` is in the item enum and used as an inventory bound in
  `bondinv.c:854` (JP-text-trigger branch) — the taser was closest to shipping.

**Status: Tier 0/1.** Rocket launcher, grenade launcher and FN P90 are
playable today via the game's own cheat codes (single-player). Taser/Spectre/
AutoShot have no cheat path but everything needed is in the binary; a
port-layer toggle calling the same `bondinvAddInvItem` +
`give_cur_player_ammo` pair the cheats use would be a clean mod (no game-logic
change — it replicates an existing code path). Watch-mounted variants
(KF7/Magnum/PP7/Taser watch) are *not* in the item enum — likely cut before
data landed; **unverified** whether any model exists beyond what TCRF shows.

### C. "Unused items" page (intact imagery) — objects with full logic, never placed

The 20 intact images correspond exactly to ROM object entries
(`assets/obseg/file_resource_table.inc.c`, 781 entries):

| Image | Resource ID | Notes |
|---|---|---|
| `GE007-AudioTape` | `AUDIOTAPE` / `CHRAUDIOTAPE` (lines 125, 265) | pickup + prop variants |
| `GE007-BombCase` | `BOMBCASE` (129) | has full `bombcase_stats` (throwable explosive) |
| `GE007-Controller` | N64 controller model (`joypad/` dir in gun table) | desk prop |
| `GE007-WatchMagnetRepel` | `ITEM_WATCHMAGNETREPEL` (`bondconstants.h:4190`) | watch function, use-logic present |
| `GEMicroCamera` | `MICROCAMERA` (176) + `AMMO_MICRO_CAMERA` | |
| `GoldenEye-Weapon_Case` | `ITEM_WEAPONCASE` (`gunfire.c:3066` use-case) | |
| `GE007-PolarizedGlasses` | `POLARIZEDGLASSES` (185) + `ITEM_POLARIZEDGLASSES` | `darkglasses/` also present |
| `GoldenEye-Plans` | `PLANS` (183) | `blueprints/` variant too |
| `GoldenEye-Watch_Identifier` | `WATCHIDENTIFIER` (208) + `ITEM_WATCHIDENTIFIER` (`gunfire.c:3076`) | 4th watch function with use-anim, never spawned |
| `GoldenEye-Safecracker` | `SAFECRACKERCASE` (190) | |
| `GoldenEye-Watch_Communicator` / `Watch_Geiger_Counter` | `ITEM_WATCHCOMMUNICATOR` / `ITEM_WATCHGEIGERCOUNTER` (`bondconstants.h:4188-89`) | shown for contrast — these *are* used (Aztec) |
| `GEGasKeyring` | `GASKEYRING` (157) + `ITEM_GASKEYRING` | |

All have `GUN_ANIM_STATE_USE_ITEM` handling in `gunfire.c:3060-3084` — i.e. the
game can *use* them; level data just never places them.

**Status: Tier 1/2.** Granting any of these to Bond is a port-layer item-spawn
(same mechanism as §B). Actually *placing* them in levels requires ROM/level-data
editing (a level editor or hand-patched object tables) — Tier 2, out of scope
for this repo but viable as a data mod.

### D. Cut multiplayer maps — 8 commented-out menu entries with dev notes

`src/game/front.c:556-573` `multi_stage_setups[]`: the shipped MP list is
Random/Temple/Complex/Caves/Library/Basement/Stack (default) + Facility/Bunker2/
Archives/Caverns/Egypt (progression-unlocked). Immediately below, **commented
out with in-source dev annotations**:

```c
  //{... LEVELID_CITADEL ...}, //Citadel (old format setup)
  //{... LEVELID_FRIGATE ...}, //dest (has xbla setup)
  //{... LEVELID_STATUE ...},  //stat (works)
  //{... LEVELID_CRADLE ...},  //crad (works)
  //{... LEVELID_AZTEC ...},   //azt (needs setup)
  //{... LEVELID_RUNWAY ...},  //runway (xbla setup)
  //{... LEVELID_DAM ...},     //dam (xbla setup)
  //{... LEVELID_DEPOT ...},   //depot (xbla setup)
```

TCRF imagery matches: `GECitadel*` (5 screenshots, main page),
`GEStatueMulti`, `GECradleMulti`, the whole Depot image set (`DepotCrateStack`,
`DepotRockets`, `DepotSafeTable`, `DepotSiloPipe[Ed]`, `DepotTrailers`,
`DepotPhoto`), and `GoldenEye-FrigateMap`.

The port already boots every one of these standalone (`-level_XX`; see
`docs/dev/LEVEL-STATUS.md` — Statue `-level_22`, Train `-level_25`, etc. all
PASS). So the geometry, AI setup and briefings
(`assets/obseg/brief/UbriefdepoZ.c`, `UbrieftraZ.c`, …) are intact; only the MP
menu entry was removed.

**Status: Tier 3 (mod fork).** Re-enabling = editing a game data table in
`src/game/front.c` — that's a game-logic change, disallowed in this repo
(non-negotiable #2). Viable as a separate mod branch/fork ("8 more MP maps"),
or as a documented cheat-menu route (the debug menu's `level`/`region` options
may already jump to any level ID — verify).

### E. Per-level unused props & geometry ("Unused stuff by level" page)

~150 intact screenshots, grouped by level. Cross-referenced against the object
table and level IDs:

- **Streets** (campaign stage 12; `LEVELID_STREETS`): an entire prop kit was
  never placed — `StreetsCars`, `StreetsLamp[Base/Big]`, `StreetsSign[2/3]`,
  `StreetsTrafficLight`, `StreetsShovel`, `StreetsCrossing`, plus two cut set
  pieces `GE64-StreetsArea1/2` and Ourumov's car driving/crashing
  (`StreetsOuruGo`, `StreetsOuruCrash`, `GEOurumovsCar`). Suggests a cut
  street-chase scene.
- **Silo**: `SiloVentShaft`, `SiloLadder`, `SiloLaunchDoors`,
  `SiloWalkway2Door/Guard`, `SiloMissingHandrail` (a bug/oddity screenshot),
  `GoldenEye_UnusedStartingPoint`, and prerelease-only `PRSiloElevator`,
  `PRSiloLamps`, `SiloCutWalkway`, **`ThirdSiloEditor`** (editor view of a
  third silo that didn't ship).
- **Facility**: PR-vs-final wall sets (`FacilityWalls[Final]`,
  `PRFacilityWalls`, `ArchivesWalls`), plus cut rooms: `FacilityBathroom`,
  `FacilityConveyor`, `FacilityGasTanks`, `FacilityLabTanks`,
  `FacilityWalkway`, `FacilityArmedScientistPR`, `FacilitySatBoxTex`,
  `GoldenEye007-FacilityPhoto`.
- **Archives**: `ArchivesOverview`, `ArchivesLamp[Scan]`, `ArchivesPhoto`,
  `PRArchivesNoWalls` (wall-less build view).
- **Bunker**: `BunkerExtraMons[Side]` (extra monster/guard spawns),
  `BunkerPIP`, PR `BunkerLamp`.
- **Caverns**: `CavernsPursueObj` (a cut "pursue" objective marker), PR
  `CavernsWalkway`.
- **Dam**: `DamIsland`, `DamPlatform`, `DamDockDoors`, `DamEndDuct`,
  `DamTruckDoor`; region-diff shots `DamCompUS/EU`.
- **Frigate**: `FrigateHarpoonScan` (scan of a harpoon model),
  `PRTrainTargetGlass`, `Pdest_harpoonZ[2]`, `Ptorpedo_rack`,
  `FrigateMoneypennyPR` (a Moneypenny model in the Frigate PR build!),
  region-diff `FrigateIntroUS/JP`.
- **Aztec/Temple**: `AztecConsolePads1-4`, `GE-AztScreen2781/2789`,
  `AztExtraMainframe`, `AztDronePadsLabel`, `unused-Aztec-Path` (a whole
  unplaced path), `EgyptianRed/WhiteClouds` (two skybox variants),
  `TempleSkySolo/Multi`, PR: `AztBlueSuitPR` (cut enemy suit),
  `AztHeavyFogPR`, `AztTableScreensPR`.
- **Surface**: `SurfaceBookCrate`, `SurfaceObsPad`, `SurfaceTower`,
  `SurfaceJerryCan`, `SurfaceHutTable`, `SurfacePropHut`, PR
  `SurfaceOverviewPR`, `SurfaceVent[PR]`, `SurfaceDish[PR]`.
- **Train** (campaign stage 14): `GE007-TrainSecretRoom` (a hidden room),
  `TrainTargetGlass`.
- **Misc props/vehicles**: `GEMotorbike`, `GERedTruck`, `GESovietBoat`,
  `GEHelicopterPilot`, `GEBiker`, `GoldenEye-moonfemale` (`moonfemale/` chr
  model exists), `OurumovKey/Briefcase`, `GEDeskMat`, `GoldenEye-Crypt`
  (confirmed orphaned map — see note below),
  `GE64-BridgeRooms`, `GEJunglePicture`, `Runwaypicture`,
  `Controlicon`, `Cactus`.
- **Orphaned level geometry** — `assets/obseg/bg/` holds per-level dirs for
  `ame arch arec ark azt cat cave crad cryp dam depo dest dish len oat ref run
  sev sevb sevx silo stat tra`. Notable: `cryp` (Crypt) has full geometry +
  briefing (`UbriefcrypZ.c`) but **no `LEVELID_CRYPT` entry** in
  `bondconstants.h` — fully orphaned. `len` (Lenin statue area, cf.
  `StatueLeninPR`) and `dish`/`cat`/`oat`/`arec`/`ame`/`ark`/`ref` are further
  level dirs beyond the shipped set. No `citadel` dir — its geometry was
  either removed or shared (TCRF's 5 Citadel screenshots show a finished map,
  so it existed in some build).

**Status: Tier 2.** All models are in the ROM's object table (spot-checked:
`PchrtesttubeZ`, `Pdest_harpoonZ`, `Pdoor_roller1-4Z`, `PhatfurryZ`,
`Pletter_tray1Z`, `Pmissile_rackZ`, `PsilencerZ`, `Psteel_door2[ b]Z`,
`Ptorpedo_rack`). Placing them needs level-data editing (object spawn tables) —
a data-mod / level-editor project, not a port change. The PR-vs-final texture
pairs (`*PR__1vt.jpg`) are interesting for a "pre-release textures" cosmetic mod
via the `tools_pc` asset pipeline — but the PR ROM itself isn't available here,
so source assets would have to come from TCRF's screenshots (low-res) or a
donated PR dump.

### F. Unused characters & faces

TCRF main-page imagery: `UnusedFace1/2/3` (each with F/S1/S2 views),
`GE64-FemHead1-4`, `KenFaceUnused/Used`, `Rosika[Compare]`, `SciCompare`,
`FemaleSci`, `GEBonds`, `Terrorists`, `TerroristBiker`, `StPetersburgGuard`,
Bond-suit sets named after actors: `Connery*`, `Dalton*`, `Moore*` (each with
HeadFront/Back/Left/Right + Jacket pieces), `GEArmor[our]`.

Cross-reference:

- The MP character table (`front.c:601+` `mp_chr_setup[]`) is huge — ~40 named
  entries beyond the 8 selectable, including `ROSIKA`, `KARL`, `MARTIN`,
  `MARK`, `DAVE`, `DUNCAN`, … `TERRORIST`, `BIKER`, `STPETERSBURGGUARD`,
  `HELICOPTERPILOT` — Brosnan-body + swapped-head combos. The "unused faces"
  are almost certainly extra head models in this pool that no body/entry uses.
- Head model dirs exist under `assets/obseg/chr/`: `headken` (the Ken face),
  `headbrosnan*` (4 costume variants), `moonfemale`, plus ~30 named heads.
  The actor-named suits (Connery/Dalton/Moore) do **not** appear as chr dirs —
  **resolved in round 2**: the ROM holds exactly 80 `C*Z` character models and
  none is a former-Bond actor; only the enum slots + the flattened actor-switch
  code (`bondview2.c:391-460`) survive (see banner, item 4).

**Status: Tier 2/3.** Cosmetic mod (extra MP faces) = asset-level work if the
models are in the ROM; verify by dumping `assets/obseg/chr/*/Model.c` headers.

### G. Prerelease / dev-build evidence

Prerelease page (body blocked; filenames + embeds only):

- PR-vs-final comparison pairs: Taser, KF7, GrenadeLauncher, RocketLauncher
  (+watch versions), SurfaceVent/Dish, `StatueLeninPR`, Facility walls/rooms
  (§E), ArchivesWalls, CavernsWalkway, SiloCutWalkway, ThirdSiloEditor,
  PRSiloElevator, BunkerLamp, OutletConsoles. Pattern: the PR build had more
  weapons (watch-mounted guns!) and different Facility/Silo/Aztec geometry.
- **Dev-build videos** (YouTube embeds captured in the export):
  - "Goldeneye 007 - Shoshinkai/Space World 1996"
  - "Nintendo of America E3 1996 Take Away Video (VHS Rip)"
  - "Goldeneye Beta WalMart Video"
  - "Goldeneye 007 Promotional Trailer 1996"
  - "N64 vous n'en reviendrez pas (VHS RIP 1997)"
  - "64History - N64 B-Roll (1995)"
  - "Escape (Banjo-Kazooie, GoldenEye 007)" — engine sibling footage.
  Useful for dating PR features; not actionable in-repo.

### H. Misc oddities (main page, unverified)

- `GoldenEye007MickeyMouse` (by-level page, 2 KB image) — no "mickey" object in
  the resource table; likely a texture/model easter egg spotted in a level.
- `Unused_bond_walk.gif` — an unused Bond walk animation (check
  `assets/animationtable_entries.c` for orphaned anim IDs).
- `GoldenEye007-MovieLogoUnused` / `-MovieNoLogoUnused` — alternate intro logo
  frames (title sequence data; `src/game/brief*`? verify).
- `GE_UnusedAlarm`, `GoldenEye-BloodImpact` / `BoxBlood` / PR `OpaqueBlood` —
  blood-effect variants (cf. `src/game/blood_animation.c`).
- `GoldenEye007-BarrelUnused` / `-SmallBarrelUnused` — barrel props never placed.
- `GoldenEye-OddDay`, `Sight[Alt]`, `MountainTex`, `background`, `Pbin1Z`,
  `PchrtesttubeZ[Explode]`, `PitonGunBungee`, `Psev_door[3_wind]Z` — props in
  the table, placement unverified.
- Bugs & Notes articles: **nothing recoverable from this export** — re-scrape.

## 2. Feasibility summary

| Tier | Item | Effort | Where it lands |
|---|---|---|---|
| **0 — already works** | Debug menu (U+D C-hold), free cam, line mode, ramrom record/replay, cheat-grantable rocket/grenade/P90 | verify in-game; document keybinds | docs + maybe `input.c` keybind for U+D c-buttons |
| **1 — port-layer hook** | Grant taser/spectre/autoshot + unused items (watch identifier, bomb case, …) via config toggle replicating the cheat grant path (`bondinvAddInvItem` + `give_cur_player_ammo`) | small; follow F10-toggles pattern (`feat/f10-game-toggles`) | `port/src/` only |
| **2 — data/asset mod** | Place unused props per level; extra MP faces; PR texture swap | needs level-editor or object-table patching; separate project | data mod, not this repo's code |
| **3 — game-logic change (mod fork)** | Re-enable 8 cut MP maps (`front.c` table), campaign reordering | one-line-per-map table edits + setup fixes ("xbla setup" = unknown editor flag) | separate mod branch/fork only |

## 3. Next steps

1. **In-game verification pass** (PC build): open debug menu; confirm free cam,
   `display speed`, level switch; enter the 2X rocket/grenade/P90 cheat codes;
   screenshot via `grab rgb`. Record results here + findings.md if anything new
   breaks (Dxx).
2. Confirm which region macros the PC build defines vs the `BUGFIX_R1` guards in
   the weapon-grant cheats (non-negotiable #3) — the grants may be dead code in
   our build.
3. Dump `assets/obseg/chr/` model headers to identify the UnusedFace1-3 /
   FemHead1-4 models concretely.
4. ~~Re-scrape TCRF article bodies~~ — **done**; all six bodies recovered into
   `backlog/cut-content/articles/` and cross-referenced (see RESEARCH.md §0).
5. If Tier-1 item granting is wanted: spec it as an F10 toggle set, port-layer
   only, each entry citing the cheat-code precedent in `cheat.c`.
