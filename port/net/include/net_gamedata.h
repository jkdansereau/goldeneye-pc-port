/*
 * net_gamedata.h -- GoldenEye multiplayer rule tables for the netplay core
 * (D413). Mirrors src/game/front.c / mp_weapon.c / bondconstants.h so the
 * matchmaking server (which never links the game) can validate settings and
 * the lobby UI can label them. If the game tables ever change, change these
 * with them; netgame.c cross-checks the counts against the game at runtime.
 */
#ifndef GE_NET_GAMEDATA_H
#define GE_NET_GAMEDATA_H

#include "net_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NG_NUM_SCENARIOS    8    /* MPSCENARIOS_MAX               */
#define NG_NUM_STAGES       12   /* MP_STAGE_SELECTED_MAX (0 = random) */
#define NG_NUM_LENGTHS      8    /* LEN_UNLIMITED .. LEN_LAST     */
#define NG_NUM_WEAPONSETS   14   /* mp_weapon_set_text_table[]    */
#define NG_NUM_AIMSIGHT     4    /* mp_sight_adjust_table[]       */
#define NG_NUM_HANDICAPS    11   /* MP_handicap_table[]           */
#define NG_NUM_CONTROLS     4    /* 1.1 .. 1.4 (2.x need 2 pads)  */
#define NG_NUM_CHARACTERS   64   /* mp_chr_setup[]                */

#define NG_SCENARIO_NORMAL  0
#define NG_SCENARIO_YOLT    1
#define NG_SCENARIO_TLD     2
#define NG_SCENARIO_MWTGG   3
#define NG_SCENARIO_LTK     4
#define NG_SCENARIO_2V2     5
#define NG_SCENARIO_3V1     6
#define NG_SCENARIO_2V1     7

#define NG_STAGE_RANDOM     0
#define NG_LENGTH_10MIN     2
#define NG_LENGTH_LAST      7
#define NG_WEAPONS_GOLDEN   13
#define NG_DEFAULT_HANDICAP 5
#define NG_DEFAULT_AIMSIGHT 3
#define NG_DEFAULT_WEAPONS  11   /* mp_weapon.c initial mp_weapon_set */

/* OPTION_* bits of save_data.options (src/game/file2.h). */
#define NG_OPT_INVERTLOOK    0x0001
#define NG_OPT_AUTOAIM       0x0002
#define NG_OPT_AIMCONTROL    0x0004
#define NG_OPT_SIGHTONSCREEN 0x0008
#define NG_OPT_LOOKAHEAD     0x0010
#define NG_OPT_DISPLAYAMMO   0x0020
#define NG_OPT_SCREENWIDE    0x0040
#define NG_OPT_SCREENRATIO   0x0080
#define NG_OPT_SCREENCINEMA  0x0800
/* DEFAULT_OPTIONS (file2.h): the N64 defaults. Screen/ratio/control-type
 * bits are never sent (netplay forces Full / Normal / per-player style). */
#define NG_DEFAULT_OPTIONS   (NG_OPT_AUTOAIM | NG_OPT_SIGHTONSCREEN | NG_OPT_LOOKAHEAD | NG_OPT_DISPLAYAMMO)
#define NG_OPTIONS_ALLOWED   (NG_OPT_INVERTLOOK | NG_OPT_AUTOAIM | NG_OPT_AIMCONTROL | NG_OPT_SIGHTONSCREEN | \
                              NG_OPT_LOOKAHEAD | NG_OPT_DISPLAYAMMO)

const char *ngScenarioName(int s);
int ngScenarioMinPlayers(int s);
int ngScenarioMaxPlayers(int s);
int ngScenarioIsTeam(int s);
const char *ngStageName(int st);
int ngStageMaxPlayers(int st);       /* random: 4 */
const char *ngLengthName(int l);
const char *ngWeaponsName(int w);
const char *ngAimSightName(int a);
const char *ngHandicapName(int h);
const char *ngControlName(int c);
const char *ngCharacterName(int c);

/* The lobby defaults (N64 front-end defaults). */
void ngDefaultSettings(NetSettings *s);

/* Clamp every field into range and apply GE's scenario rules exactly as
 * front.c reset_mp_options_for_scenario() does (YOLT forces "last alive",
 * Golden Gun forces its weapon set, length caps per scenario). */
void ngNormalizeSettings(NetSettings *s);

/* Validate a match about to start with `numPlayers` (players[] gives the team
 * picks). Returns 0 if OK, else -1 with a human-readable reason. */
int ngValidateMatch(const NetSettings *s, const NetPlayerInfo *players, int numPlayers, char *why, int whyLen);

/* Resolve the random stage (0) to a concrete one that fits numPlayers,
 * using `rnd` as the random source. Non-random stages are returned as is. */
int ngResolveStage(int stage, int numPlayers, uint32_t rnd);

/* Assign default teams for a team scenario (stable, by slot). */
void ngDefaultTeams(int scenario, NetPlayerInfo *players, int numPlayers);

#ifdef __cplusplus
}
#endif

#endif /* GE_NET_GAMEDATA_H */
