// GoldenEye multiplayer name tables and rules -- mirror of port/net/net_gamedata.c
// (which mirrors the game's own front.c tables) and of the quick-match
// preference rules in port/net/net_dirproto.c (ndpNormalizePrefs /
// ndpPrefsMatch). Used for validation, matchmaking and the status page.
// Indexes are the ones the game and the protocol use. Change both sides
// together; test/protocol.test.js checks them against the C build.

export const SCENARIOS = [
  "Normal", "You Only Live Twice", "The Living Daylights", "The Man with the Golden Gun",
  "Licence to Kill", "Team: 2 vs 2", "Team: 3 vs 1", "Team: 2 vs 1",
];
// players each scenario needs (front.c mp_player_counts)
export const SCENARIO_MIN = [2, 2, 2, 2, 2, 4, 4, 3];
export const SCENARIO_MAX = [4, 4, 4, 4, 4, 4, 4, 3];

export const STAGES = [
  "Random", "Temple", "Complex", "Caves", "Library", "Basement", "Stack",
  "Facility", "Bunker", "Archives", "Caverns", "Egyptian",
];
// most players each map allows (front.c multi_stage_setups); random: any
export const STAGE_MAX = [4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 2];

export const LENGTHS = [
  "No limits", "5 minutes", "10 minutes", "20 minutes",
  "First to 5 points", "First to 10 points", "First to 20 points", "Last one standing",
];

export const WEAPONS = [
  "Slappers only", "Pistols", "Throwing knives", "Automatics", "Power weapons",
  "Sniper rifles", "Grenades", "Remote mines", "Grenade launchers", "Timed mines",
  "Proximity mines", "Rockets", "Lasers", "Golden gun",
];

export const CHARACTERS = [
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
];

export const SC = { NORMAL: 0, YOLT: 1, TLD: 2, MWTGG: 3, LTK: 4, T2V2: 5, T3V1: 6, T2V1: 7 };
export const STAGE_RANDOM = 0;
export const LENGTH_LAST = 7;
export const ANY = 0xff;

export const nameOf = (table, i) => (Number.isInteger(i) && i >= 0 && i < table.length ? table[i] : "?");
export const isTeam = (s) => s === SC.T2V2 || s === SC.T3V1 || s === SC.T2V1;

// What a searcher wants, made consistent with GoldenEye's rules (same as
// ndpNormalizePrefs in C): out of range -> any; "last one standing" is
// YOLT; YOLT fixes the length and the Golden Gun its weapons (-> any); Flag
// Tag lengths stop at 20 minutes; team modes fix the game size and drop a
// map too small for it; a small map caps the size.
export function normalizePrefs(input = {}) {
  const p = { scenario: ANY, stage: ANY, weapons: ANY, length: ANY, players: ANY, ...input };
  const ok = (v, n) => Number.isInteger(v) && v >= 0 && v < n;
  if (p.scenario !== ANY && !ok(p.scenario, SCENARIOS.length)) p.scenario = ANY;
  if (p.stage !== ANY && (!ok(p.stage, STAGES.length) || p.stage === STAGE_RANDOM)) p.stage = ANY;
  if (p.weapons !== ANY && !ok(p.weapons, WEAPONS.length)) p.weapons = ANY;
  if (p.length !== ANY && !ok(p.length, LENGTHS.length)) p.length = ANY;
  if (p.players !== ANY && !(Number.isInteger(p.players) && p.players >= 2 && p.players <= 4)) p.players = ANY;
  // "last one standing" is YOLT's length: it picks YOLT when no mode was
  // chosen, and means nothing for any other mode
  if (p.length === LENGTH_LAST) {
    if (p.scenario === ANY) p.scenario = SC.YOLT;
    p.length = ANY;
  }
  if (p.scenario === SC.YOLT) p.length = ANY;
  else if (p.scenario === SC.MWTGG) p.weapons = ANY;
  else if (p.scenario === SC.TLD && p.length !== ANY && p.length > 3) p.length = ANY;
  if (p.scenario !== ANY && isTeam(p.scenario)) {
    p.players = SCENARIO_MIN[p.scenario];
    if (p.stage !== ANY && STAGE_MAX[p.stage] < p.players) p.stage = ANY;
  } else if (p.stage !== ANY && p.players !== ANY && STAGE_MAX[p.stage] < p.players) {
    p.players = STAGE_MAX[p.stage];
  }
  return { scenario: p.scenario, stage: p.stage, weapons: p.weapons, length: p.length, players: p.players };
}

// Does a game with these rules satisfy the (normalised) preferences?
export function prefsMatch(p, l) {
  return (p.scenario === ANY || l.scenario === p.scenario) && (p.stage === ANY || l.stage === p.stage) &&
    (p.weapons === ANY || l.weapons === p.weapons) && (p.length === ANY || l.length === p.length) &&
    (p.players === ANY || l.maxPlayers === p.players);
}

// The players a game with these rules may have (scenario, map), for
// validating a host's record.
export function sizeRange(scenario, stage) {
  const lo = SCENARIO_MIN[scenario] ?? 2;
  let hi = SCENARIO_MAX[scenario] ?? 4;
  if (stage !== STAGE_RANDOM && STAGE_MAX[stage] !== undefined && STAGE_MAX[stage] < hi) hi = STAGE_MAX[stage];
  return [lo, Math.max(lo, hi)];
}
