/*
 * net_gamedata.c -- GoldenEye MP rule tables for the netplay core (D409).
 * Sources (keep in sync): src/game/front.c multi_game_lengths[],
 * mp_player_counts[], multi_stage_setups[], mp_chr_setup[],
 * MP_handicap_table[], MP_controller_configuration_table[],
 * mp_sight_adjust_table[], reset_mp_options_for_scenario();
 * src/game/mp_weapon.c mp_weapon_set_text_table[].
 */
#include "net_gamedata.h"
#include "net_plat.h"

#include <string.h>

static const char *const kScenario[NG_NUM_SCENARIOS] = {
    "Normal", "You Only Live Twice", "The Living Daylights (Flag Tag)",
    "The Man with the Golden Gun", "Licence to Kill",
    "Team: 2 vs 2", "Team: 3 vs 1", "Team: 2 vs 1",
};
static const uint8_t kScenarioMin[NG_NUM_SCENARIOS] = { 2, 2, 2, 2, 2, 4, 4, 3 };
static const uint8_t kScenarioMax[NG_NUM_SCENARIOS] = { 4, 4, 4, 4, 4, 4, 4, 3 };

static const char *const kStage[NG_NUM_STAGES] = {
    "Random", "Temple", "Complex", "Caves", "Library", "Basement", "Stack",
    "Facility", "Bunker", "Archives", "Caverns", "Egyptian",
};
static const uint8_t kStageMax[NG_NUM_STAGES] = { 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 2 };

static const char *const kLength[NG_NUM_LENGTHS] = {
    "No limits", "5 minutes", "10 minutes", "20 minutes",
    "First to 5 points", "First to 10 points", "First to 20 points", "Last one standing",
};

static const char *const kWeapons[NG_NUM_WEAPONSETS] = {
    "Slappers only", "Pistols", "Throwing knives", "Automatics", "Power weapons",
    "Sniper rifles", "Grenades", "Remote mines", "Grenade launchers", "Timed mines",
    "Proximity mines", "Rockets", "Lasers", "Golden gun",
};

static const char *const kAimSight[NG_NUM_AIMSIGHT] = {
    "Sight off, auto-aim off", "Sight on, auto-aim off",
    "Sight off, auto-aim on", "Sight on, auto-aim on",
};

static const char *const kHandicap[NG_NUM_HANDICAPS] = {
    "Health -10 (Hero)", "Health -4 (Veteran)", "Health -3 (Veteran)",
    "Health -2 (Veteran)", "Health -1 (Veteran)", "Health normal",
    "Health +1 (Novice)", "Health +2 (Novice)", "Health +3 (Novice)",
    "Health +4 (Novice)", "Health +10 (Rookie)",
};

static const char *const kControl[NG_NUM_CONTROLS] = {
    "1.1 Honey", "1.2 Solitaire", "1.3 Kissy", "1.4 Goodnight",
};

static const char *const kCharacter[NG_NUM_CHARACTERS] = {
    "Bond", "Natalya", "Trevelyan", "Xenia", "Ourumov", "Boris", "Valentin",
    "Mishkin", "May Day", "Jaws", "Oddjob", "Baron Samedi",
    "Russian Soldier", "Russian Infantry", "Scientist", "Scientist (F)",
    "Russian Commandant", "Janus Marine", "Naval Officer", "Helicopter Pilot",
    "St. Petersburg Guard", "Civilian (F)", "Civilian 2", "Civilian 3",
    "Civilian 4", "Siberian Guard", "Arctic Commando", "Siberian Guard 2",
    "Siberian Special Forces", "Jungle Commando", "Janus Special Forces",
    "Moonraker Elite", "Moonraker Elite (F)", "Rosika", "Karl", "Martin",
    "Mark", "Dave", "Duncan", "B", "Steve E", "Grant", "Graeme", "Ken",
    "Alan", "Pete", "Shaun", "Dwayne", "Des", "Chris", "Lee", "Neil", "Jim",
    "Robin", "Steve H", "Terrorist", "Biker", "Joel", "Scott", "Joe",
    "Sally", "Marion", "Mandy", "Vivien",
};

#define TBL(arr, n, i) (((i) >= 0 && (i) < (n)) ? (arr)[(i)] : "?")

const char *ngScenarioName(int s) { return TBL(kScenario, NG_NUM_SCENARIOS, s); }
int ngScenarioMinPlayers(int s) { return (s >= 0 && s < NG_NUM_SCENARIOS) ? kScenarioMin[s] : 2; }
int ngScenarioMaxPlayers(int s) { return (s >= 0 && s < NG_NUM_SCENARIOS) ? kScenarioMax[s] : 4; }
int ngScenarioIsTeam(int s) { return s == NG_SCENARIO_2V2 || s == NG_SCENARIO_3V1 || s == NG_SCENARIO_2V1; }
const char *ngStageName(int st) { return TBL(kStage, NG_NUM_STAGES, st); }
int ngStageMaxPlayers(int st) { return (st >= 0 && st < NG_NUM_STAGES) ? kStageMax[st] : 0; }
const char *ngLengthName(int l) { return TBL(kLength, NG_NUM_LENGTHS, l); }
const char *ngWeaponsName(int w) { return TBL(kWeapons, NG_NUM_WEAPONSETS, w); }
const char *ngAimSightName(int a) { return TBL(kAimSight, NG_NUM_AIMSIGHT, a); }
const char *ngHandicapName(int h) { return TBL(kHandicap, NG_NUM_HANDICAPS, h); }
const char *ngControlName(int c) { return TBL(kControl, NG_NUM_CONTROLS, c); }
const char *ngCharacterName(int c) { return TBL(kCharacter, NG_NUM_CHARACTERS, c); }

void ngDefaultSettings(NetSettings *s)
{
    memset(s, 0, sizeof(*s));
    s->scenario = NG_SCENARIO_NORMAL;
    s->stage = 1;                      /* MP_STAGE_TEMPLE, front.c default */
    s->length = NG_LENGTH_10MIN;       /* LEN_10MIN, front.c default       */
    s->weapons = NG_DEFAULT_WEAPONS;
    s->aimsight = NG_DEFAULT_AIMSIGHT;
    s->delay = 0;                      /* auto */
    s->options = NG_DEFAULT_OPTIONS;
}

void ngNormalizeSettings(NetSettings *s)
{
    if (s->scenario >= NG_NUM_SCENARIOS) s->scenario = NG_SCENARIO_NORMAL;
    if (s->stage >= NG_NUM_STAGES) s->stage = 1;
    if (s->length >= NG_NUM_LENGTHS) s->length = NG_LENGTH_10MIN;
    if (s->weapons >= NG_NUM_WEAPONSETS) s->weapons = NG_WEAPONS_GOLDEN;
    if (s->aimsight >= NG_NUM_AIMSIGHT) s->aimsight = NG_DEFAULT_AIMSIGHT;
    if (s->delay > NET_MAX_DELAY) s->delay = NET_MAX_DELAY;
    s->options &= NG_OPTIONS_ALLOWED;
    s->flags &= NS_AUTOSTART;

    /* front.c reset_mp_options_for_scenario() */
    switch (s->scenario) {
    case NG_SCENARIO_YOLT:
        s->length = NG_LENGTH_LAST;
        break;
    case NG_SCENARIO_TLD:
        if (s->length > 3) s->length = NG_LENGTH_10MIN;
        break;
    case NG_SCENARIO_MWTGG:
        if (s->length > 6) s->length = NG_LENGTH_10MIN;
        s->weapons = NG_WEAPONS_GOLDEN;
        break;
    default: /* NORMAL, LTK, team scenarios */
        if (s->length > 6) s->length = NG_LENGTH_10MIN;
        break;
    }
}

int ngValidateMatch(const NetSettings *s, const NetPlayerInfo *players, int n, char *why, int whyLen)
{
    int lo, hi;
    if (s->scenario >= NG_NUM_SCENARIOS || s->stage >= NG_NUM_STAGES || s->length >= NG_NUM_LENGTHS ||
        s->weapons >= NG_NUM_WEAPONSETS || s->aimsight >= NG_NUM_AIMSIGHT) {
        netStrCopy(why, whyLen, "invalid match settings");
        return -1;
    }
    lo = ngScenarioMinPlayers(s->scenario);
    hi = ngScenarioMaxPlayers(s->scenario);
    if (n < lo || n > hi) {
        if (lo == hi) netStrFmt(why, whyLen, "%s needs exactly %d players", ngScenarioName(s->scenario), lo);
        else netStrFmt(why, whyLen, "%s needs %d-%d players", ngScenarioName(s->scenario), lo, hi);
        return -1;
    }
    if (s->stage != NG_STAGE_RANDOM && ngStageMaxPlayers(s->stage) < n) {
        netStrFmt(why, whyLen, "%s allows at most %d players", ngStageName(s->stage), ngStageMaxPlayers(s->stage));
        return -1;
    }
    if (players) {
        int i, ones = 0;
        for (i = 0; i < n; i++) {
            const NetPlayerInfo *p = &players[i];
            if (p->character >= NG_NUM_CHARACTERS || p->handicap >= NG_NUM_HANDICAPS ||
                p->control >= NG_NUM_CONTROLS || p->team > 1) {
                netStrFmt(why, whyLen, "invalid setup for player %d", i + 1);
                return -1;
            }
            if (p->team) ones++;
        }
        if (ngScenarioIsTeam(s->scenario)) {
            int want = (s->scenario == NG_SCENARIO_2V2) ? 2 : 1;
            if (ones != want) {
                netStrFmt(why, whyLen, "teams must be %s", s->scenario == NG_SCENARIO_2V2 ? "2 vs 2" :
                          s->scenario == NG_SCENARIO_3V1 ? "3 vs 1" : "2 vs 1");
                return -1;
            }
        }
    }
    return 0;
}

int ngResolveStage(int stage, int n, uint32_t rnd)
{
    int cand[NG_NUM_STAGES];
    int nc = 0, st;
    if (stage != NG_STAGE_RANDOM) return stage;
    for (st = 1; st < NG_NUM_STAGES; st++) {
        if (ngStageMaxPlayers(st) >= n) cand[nc++] = st;
    }
    if (nc == 0) return 1;
    return cand[rnd % (uint32_t)nc];
}

void ngDefaultTeams(int scenario, NetPlayerInfo *players, int n)
{
    int i;
    for (i = 0; i < n; i++) players[i].team = 0;
    if (scenario == NG_SCENARIO_2V2) {
        for (i = 0; i < n; i++) players[i].team = (uint8_t)(i >= 2);
    } else if (scenario == NG_SCENARIO_3V1 || scenario == NG_SCENARIO_2V1) {
        if (n > 0) players[n - 1].team = 1;
    }
}
