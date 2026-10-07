// The online directory's core (D414-D416): lobby registry, codes, joins,
// random matchmaking with preferences, match results, live counts.
// Transport- and runtime-agnostic: the Cloudflare Durable Object
// (src/index.js) and the local test server (test/mock-server.js) both drive
// it through Directory.handle().
//
// A session is a plain object the caller owns:
//   { id, ip, country, continent, transport: "ws" | "http", push(bytes) | null,
//     hello, build, name, hostedId, closeAfter }
// push() delivers an unsolicited message (a join request to a host) on a
// live WebSocket; HTTP-polling hosts get theirs queued and returned by their
// next HOST / POLL instead.
//
// The directory never relays gameplay. It introduces a joiner to a host:
// each side learns the other's candidate addresses (public via STUN + LAN)
// and they connect to each other directly over UDP.
//
// Hardening (D416):
//   - every address a peer publishes is checked against where its request
//     came from (netaddr.js cleanCands): the directory cannot be used to aim
//     game traffic at third parties;
//   - lobby tokens are signed (HMAC of id + code): after a restart a host can
//     re-adopt its own lobby, nobody can claim a code they were not given;
//   - match results must follow a match the directory saw, once, with names
//     from that match's roster and numbers in range;
//   - per-address caps (IPv6 counted per /64): lobbies, rate of every
//     request kind, wrong codes, failure reports only from addresses that
//     were actually sent to that game, join requests a game is sent;
//   - a session that keeps sending garbage is closed.

import * as P from "./protocol.js";
import {
  CHARACTERS, LENGTHS, SCENARIOS, STAGES, WEAPONS, nameOf, normalizePrefs, prefsMatch,
} from "./gamedata.js";
import { cleanCands, ipKey } from "./netaddr.js";
import { ctEqual, hmacSha256, toHex } from "./sha256.js";

export const LOBBY_TTL_MS = 90_000;      // no HOST refresh for this long -> gone
export const QUICK_FRESH_MS = 45_000;    // quick-match only into recently refreshed lobbies
export const MAX_LOBBIES = 2000;
export const MAX_LOBBIES_PER_ADDR = 4;   // a LAN party may host a few
export const MAX_PENDING_JOINS = 8;
export const MAX_INTROS_PER_MIN = 20;    // join requests one game is sent per minute
export const UNREACHABLE_AFTER = 2;      // addresses that failed to reach a quick game
export const RECENT_MAX = 20;
export const SNAPSHOT_MAX_SESSIONS = 200;
export const RESULT_MAX_STAT = 999;
export const RESULT_GRACE_MS = 120_000;  // a result may follow its match's end by this much
export const MAX_BAD_MESSAGES = 20;      // then the session is closed
export const MAX_RATE_KEYS = 50_000;     // address/kind rate buckets held at once (memory bound)
const PENDING_JOIN_TTL_MS = 30_000;
const INTRO_MEMORY_MS = 60_000;          // a failure report counts only this soon after
const MAX_INTRO_MEMORY = 32;
const CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   // as net_host.c makeCode

// Per address and window (most per minute; wrong codes per 10 minutes). A
// waiting quick host re-asks every 5 s, and a LAN party shares an address.
export const RATE = { host: 12, join: 30, quick: 60, list: 60, result: 12, badcode: 10 };
const RATE_WINDOW_MS = { badcode: 600_000 };

export const dayKey = (ms) => new Date(ms).toISOString().slice(0, 10);

const keyOf = (s) => s.key || (s.key = ipKey(s.ip));

export class Directory {
  constructor(opts = {}) {
    this.now = opts.now || (() => Date.now());
    this.rand = opts.randomBytes || ((n) => crypto.getRandomValues(new Uint8Array(n)));
    this.onChange = opts.onChange || (() => {});
    this.recordMatch = opts.recordMatch || (() => {});
    this.motd = opts.motd || "";
    this.secret = opts.secret || this.rand(32);   // the DO keeps one in storage
    this.lobbies = new Map();      // id -> lobby
    this.byCode = new Map();       // code -> id
    this.rate = new Map();         // `${key}|${kind}` -> { start, count }
    this.maxRateKeys = opts.maxRateKeys || MAX_RATE_KEYS;
    this.browsing = new Set();     // live sessions that are not hosting
    this.recent = [];              // finished public matches, newest first
    this.today = { day: dayKey(this.now()), matches: 0, players: 0, peak: 0 };
  }

  // ------------------------------------------------------------ dispatch

  handle(s, bytes) {
    let m;
    try {
      m = P.decodeClient(bytes);
    } catch {
      return this.bad(s, 0, 0, "Malformed request");
    }
    if (m.type !== P.C.HELLO && !s.hello) return this.bad(s, m.type, m.nonce || 0, "Say hello first");
    switch (m.type) {
      case P.C.HELLO: return this.hello(s, m);
      case P.C.LIST: return this.list(s);
      case P.C.HOST: return this.host(s, m);
      case P.C.UNHOST: return this.unhost(s, m);
      case P.C.JOIN: return this.join(s, m);
      case P.C.QUICK: return this.quick(s, m);
      case P.C.RESULT: return this.result(s, m);
      case P.C.POLL: return this.poll(s, m);
      default: return this.bad(s, m.type, 0, "Unknown request");
    }
  }

  // A malformed or out-of-order message: answer, and close a session that
  // keeps doing it.
  bad(s, type, nonce, text) {
    s.bad = (s.bad || 0) + 1;
    if (s.bad > MAX_BAD_MESSAGES) s.closeAfter = true;
    return [P.encError(type, nonce, P.E.BAD_REQUEST, text)];
  }

  sessionClosed(s) {
    this.browsing.delete(s);
    if (s.hostedId) {
      const l = this.lobbies.get(s.hostedId);
      if (l && l.host === s) {
        this.removeLobby(l.id);
        this.onChange();
      }
    }
  }

  // ------------------------------------------------------------ messages

  hello(s, m) {
    if (s.hello) return this.bad(s, P.C.HELLO, 0, "Already said hello");
    if (m.version !== P.NDP_VERSION) {
      s.closeAfter = true;
      return [P.encError(P.C.HELLO, 0, P.E.VERSION, "This game version is not compatible with the online service")];
    }
    if (m.build.length < 3) {
      s.closeAfter = true;
      return [P.encError(P.C.HELLO, 0, P.E.BAD_REQUEST, "Missing game build id")];
    }
    s.hello = true;
    s.build = m.build;
    s.name = m.name;
    if (s.transport === "ws" && !s.hostedId) this.browsing.add(s);
    this.notePeak();
    return [P.encWelcome({ ...this.counts(), motd: this.motd })];
  }

  list(s) {
    if (!this.allow(keyOf(s), "list")) return [this.rateError(P.C.LIST, 0)];
    this.expire();
    const entries = [...this.lobbies.values()]
      .filter((l) => l.build === s.build && l.flags & P.F.PUBLIC)
      .sort(byJoinability)
      .slice(0, P.LIM.LIST)
      .map((l) => ({
        id: l.id, name: l.name, hostName: l.hostName, code: l.code, flags: l.flags, state: l.state,
        numPlayers: l.numPlayers, maxPlayers: l.maxPlayers, scenario: l.scenario, stage: l.stage,
        weapons: l.weapons, length: l.length, country: l.country,
      }));
    return [P.encListed({ ...this.counts(), entries })];
  }

  host(s, m) {
    const now = this.now();
    const key = keyOf(s);
    if (m.maxPlayers < 2 || m.maxPlayers > 4 || m.numPlayers > m.maxPlayers || m.players.length > m.maxPlayers ||
        m.scenario >= SCENARIOS.length || m.stage >= STAGES.length || m.weapons >= WEAPONS.length ||
        m.length >= LENGTHS.length || m.state > P.ST.PLAYING) {
      return [P.encError(P.C.HOST, 0, P.E.BAD_REQUEST, "Lobby details out of range")];
    }
    const cands = cleanCands(m.cands, s.ip);
    if (cands.length === 0) {
      return [P.encError(P.C.HOST, 0, P.E.BAD_REQUEST, "No usable address to publish -- check your network")];
    }
    let l = m.lobbyId ? this.lobbies.get(m.lobbyId) : undefined;
    if (l) {
      if (!ctEqual(l.token, m.token)) return [P.encError(P.C.HOST, 0, P.E.FORBIDDEN, "Not your lobby")];
    } else {
      if (!this.allow(key, "host")) return [this.rateError(P.C.HOST, 0)];
      if (this.lobbies.size >= MAX_LOBBIES) {
        return [P.encError(P.C.HOST, 0, P.E.BUSY, "The online service is full, try again later")];
      }
      if (s.hostedId && s.hostedId !== m.lobbyId) this.removeLobby(s.hostedId);
      let fromHere = 0;
      for (const o of this.lobbies.values()) if (o.hostKey === key) fromHere++;
      if (fromHere >= MAX_LOBBIES_PER_ADDR) {
        return [P.encError(P.C.HOST, 0, P.E.BUSY, "Too many games from your address")];
      }
      // A refresh for a lobby we no longer know (the service restarted):
      // its signed token proves we issued that id and code, so the lobby is
      // re-adopted under both and players invited by code can still join.
      // Anything else -- an unknown id, a forged token, a taken code --
      // becomes a new lobby with a new identity.
      const wanted = (m.code || "").toUpperCase();
      const readopt = m.lobbyId !== 0 && validCode(wanted) && !this.byCode.has(wanted) &&
        ctEqual(m.token, this.tokenFor(m.lobbyId, wanted));
      const id = readopt ? m.lobbyId : this.newId();
      const code = readopt ? wanted : this.newCode();
      l = {
        id, token: this.tokenFor(id, code), code, build: s.build, hostName: s.name || "Agent", hostKey: key,
        createdAt: now, stateSince: now, state: m.state, numPlayers: 0, pendingJoins: [], host: null, fails: [],
        intro: { start: now, count: 0 }, introKeys: {}, match: null,
      };
      this.lobbies.set(id, l);
      this.byCode.set(code, id);
    }
    // The match lifecycle, which a result must follow (one per match).
    if (l.state !== m.state) {
      l.stateSince = now;
      if (l.state === P.ST.WAITING && m.state !== P.ST.WAITING) {
        l.match = { start: now, end: 0, roster: m.players.map((p) => p.name), reported: false };
      } else if (m.state === P.ST.WAITING && l.match && !l.match.end) {
        l.match.end = now;
      }
    }
    // Someone got in: whoever failed to reach this game before may have been
    // unlucky -- give it a clean slate.
    if (m.numPlayers > (l.numPlayers || 0)) l.fails = [];
    Object.assign(l, {
      name: m.name, flags: m.flags & (P.F.PUBLIC | P.F.QUICK), maxPlayers: m.maxPlayers, state: m.state,
      numPlayers: m.numPlayers, scenario: m.scenario, stage: m.stage, weapons: m.weapons, length: m.length,
      matchSec: m.matchSec, cands, players: m.players, country: s.country || "", continent: s.continent || "",
      updatedAt: now, host: s.push ? s : null,
    });
    s.hostedId = l.id;
    this.browsing.delete(s);
    this.notePeak();
    this.onChange();
    const out = [P.encHosted({ lobbyId: l.id, token: l.token, code: l.code })];
    if (!s.push) out.push(...this.drainJoins(l));
    return out;
  }

  unhost(s, m) {
    const l = this.lobbies.get(m.lobbyId);
    if (l && ctEqual(l.token, m.token)) {
      this.removeLobby(l.id);
      if (s.hostedId === l.id) s.hostedId = 0;
      if (s.transport === "ws") this.browsing.add(s);
      this.onChange();
    }
    return [];
  }

  join(s, m) {
    const key = keyOf(s);
    if (!this.allow(key, "join")) return [this.rateError(P.C.JOIN, m.nonce)];
    const byCode = !m.lobbyId;
    // Codes are the only key to a private game: guessing them is throttled.
    if (byCode && this.exhausted(key, "badcode")) {
      return [P.encError(P.C.JOIN, m.nonce, P.E.RATE_LIMIT, "Too many wrong codes -- wait a few minutes")];
    }
    this.expire();
    const id = m.lobbyId || this.byCode.get(m.code.toUpperCase());
    const l = id ? this.lobbies.get(id) : undefined;
    if (!l || (!byCode && !(l.flags & P.F.PUBLIC))) {   // ids of private games are never handed out
      if (byCode) this.allow(key, "badcode");
      return [P.encError(P.C.JOIN, m.nonce, P.E.NOT_FOUND, byCode ? "No game with that code" : "That game is no longer open")];
    }
    const refusal = this.refuse(l, s, P.C.JOIN, m.nonce);
    if (refusal) return [refusal];
    if (!this.introAllowed(l)) {
      return [P.encError(P.C.JOIN, m.nonce, P.E.BUSY, "That game is getting a lot of join requests -- try again shortly")];
    }
    return [this.introduce(s, l, m.nonce, cleanCands(m.cands, s.ip))];
  }

  // Random matchmaking. Each searcher is put into the best open quick game
  // of their build that fits their preferences (scenario, map, weapons,
  // length, size -- "any" fits all): one nobody failed to reach, then one on
  // their continent, then the fullest, then the oldest. When there is none
  // they are told to host one with their preferences as its rules, and the
  // next fitting searchers are sent there.
  quick(s, m) {
    const key = keyOf(s);
    if (!this.allow(key, "quick")) return [this.rateError(P.C.QUICK, m.nonce)];
    this.expire();
    const now = this.now();
    const prefs = normalizePrefs(m.prefs);
    // The asker could not reach the game it was sent to last time (strict
    // routers on one side or the other). Noted once per address, and only
    // from an address we actually sent there: such a game goes to the back
    // of the line, and stops being offered after UNREACHABLE_AFTER.
    if (m.excludeId) this.noteUnreachable(m.excludeId, key, now);
    // The asker may already host a quick game of its own (two searchers at
    // the same moment both hosted; it asks again while it waits alone). Only
    // OLDER games nobody failed to reach are offered then, so the newer host
    // moves into the older game and never both ways at once -- unless nobody
    // can reach its own game: it takes any, and since unreachable games are
    // never offered, nothing can swap back.
    const mine = this.lobbies.get(m.lobbyId || s.hostedId);
    const anyGame = !mine || unreachable(mine);
    const fits = [];
    for (const l of this.lobbies.values()) {
      if (l.build !== s.build || (l.flags & (P.F.QUICK | P.F.PUBLIC)) !== (P.F.QUICK | P.F.PUBLIC)) continue;
      if (l.state !== P.ST.WAITING || l.numPlayers >= l.maxPlayers || now - l.updatedAt > QUICK_FRESH_MS) continue;
      if (l.id === s.hostedId || l.id === m.lobbyId || l.id === m.excludeId || unreachable(l)) continue;
      if (!prefsMatch(prefs, l)) continue;
      if (!anyGame && (!olderThan(l, mine) || l.fails.length)) continue;
      fits.push(l);
    }
    fits.sort((a, b) => quickOrder(a, b, s.continent));
    for (const l of fits) {
      if (this.introAllowed(l)) return [this.introduce(s, l, m.nonce, cleanCands(m.cands, s.ip))];
    }
    return [P.encQuickHost(m.nonce)];
  }

  // A finished match of a lobby, for the public page. Only for a match the
  // directory saw start (and end, at most RESULT_GRACE_MS ago), only once;
  // names must be from that match's roster, numbers in range.
  result(s, m) {
    const l = this.lobbies.get(m.lobbyId);
    if (!l || !ctEqual(l.token, m.token)) return [P.encError(P.C.RESULT, 0, P.E.FORBIDDEN, "Not your game")];
    if (!this.allow(keyOf(s), "result")) return [];
    const now = this.now();
    const mt = l.match;
    if (!mt || mt.reported || (mt.end && now - mt.end > RESULT_GRACE_MS)) return [];
    mt.reported = true;
    const elapsed = Math.floor(((mt.end || now) - mt.start) / 1000);
    const roster = new Set(mt.roster);
    const pub = !!(l.flags & P.F.PUBLIC);
    const stat = (v) => Math.max(0, Math.min(RESULT_MAX_STAT, v | 0));
    const match = {
      ended: now,
      duration: Math.max(0, Math.min(m.durationSec, elapsed + 30)),
      scenario: nameOf(SCENARIOS, m.scenario),
      stage: nameOf(STAGES, m.stage),
      lobby: pub ? l.name : "Private game",
      quick: !!(l.flags & P.F.QUICK),
      country: l.country,
      players: m.players.slice(0, l.maxPlayers).map((p) => ({
        // Private games stay anonymous on the public page; a name that was
        // not in the match is never shown.
        name: pub && roster.has(p.name) ? p.name : "Agent",
        character: nameOf(CHARACTERS, p.character),
        team: p.team ? 1 : 0,
        kills: stat(p.kills),
        deaths: stat(p.deaths),
      })),
    };
    this.recent.unshift(match);
    if (this.recent.length > RECENT_MAX) this.recent.length = RECENT_MAX;
    this.rollDay();
    this.today.matches++;
    this.today.players += match.players.length;
    this.recordMatch(match, { ...this.today });
    this.onChange();
    return [];
  }

  poll(s, m) {
    const l = this.lobbies.get(m.lobbyId);
    if (!l || !ctEqual(l.token, m.token)) return [P.encError(P.C.POLL, 0, P.E.NOT_FOUND, "Lobby not registered")];
    l.updatedAt = this.now();
    return this.drainJoins(l);
  }

  // ------------------------------------------------------------ helpers

  tokenFor(id, code) {
    return toHex(hmacSha256(this.secret, `${id >>> 0}:${code}`)).slice(0, 32);
  }

  refuse(l, s, type, nonce) {
    if (l.build !== s.build) {
      return P.encError(type, nonce, P.E.VERSION, "That game runs a different version of GoldenEye 007 PC");
    }
    if (l.id === s.hostedId) return P.encError(type, nonce, P.E.BAD_REQUEST, "That is your own game");
    if (l.state !== P.ST.WAITING) return P.encError(type, nonce, P.E.IN_MATCH, "That game is already playing");
    if (l.numPlayers >= l.maxPlayers) return P.encError(type, nonce, P.E.FULL, "That game is full");
    return null;
  }

  introAllowed(l) {
    const now = this.now();
    if (!l.intro || now - l.intro.start >= 60_000) l.intro = { start: now, count: 0 };
    return l.intro.count < MAX_INTROS_PER_MIN;
  }

  // Tell the host who is coming (so it punches towards them) and give the
  // joiner the host's addresses. Both address lists were checked against
  // their senders (cleanCands).
  introduce(s, l, nonce, cands) {
    const now = this.now();
    if (!l.intro) l.intro = { start: now, count: 0 };
    l.intro.count++;
    if (!l.introKeys) l.introKeys = {};
    l.introKeys[keyOf(s)] = now;
    const keys = Object.keys(l.introKeys);
    if (keys.length > MAX_INTRO_MEMORY) {
      keys.sort((a, b) => l.introKeys[a] - l.introKeys[b]);
      for (const k of keys.slice(0, keys.length - MAX_INTRO_MEMORY)) delete l.introKeys[k];
    }
    const req = P.encJoinReq({ lobbyId: l.id, name: s.name, cands });
    if (l.host && l.host.push) {
      try {
        l.host.push(req);
      } catch {
        l.pendingJoins.push({ at: now, bytes: req });
      }
    } else {
      l.pendingJoins.push({ at: now, bytes: req });
    }
    if (l.pendingJoins.length > MAX_PENDING_JOINS) l.pendingJoins.shift();
    return P.encJoinInfo({ nonce, lobbyId: l.id, name: l.name, hostName: l.hostName, cands: l.cands });
  }

  noteUnreachable(id, key, now) {
    const l = this.lobbies.get(id);
    if (!l || !l.introKeys || !(now - (l.introKeys[key] || -Infinity) <= INTRO_MEMORY_MS)) return;
    if (!l.fails) l.fails = [];
    if (!l.fails.includes(key) && l.fails.length < 8) l.fails.push(key);
  }

  drainJoins(l) {
    const now = this.now();
    const out = l.pendingJoins.filter((j) => now - j.at < PENDING_JOIN_TTL_MS).map((j) => j.bytes);
    l.pendingJoins = [];
    return out;
  }

  removeLobby(id) {
    const l = this.lobbies.get(id);
    if (!l) return;
    this.lobbies.delete(id);
    if (this.byCode.get(l.code) === id) this.byCode.delete(l.code);
  }

  expire() {
    const now = this.now();
    let changed = false;
    for (const l of this.lobbies.values()) {
      if (now - l.updatedAt > LOBBY_TTL_MS) {
        this.removeLobby(l.id);
        changed = true;
      }
    }
    this.pruneRate(now);
    if (changed) this.onChange();
  }

  pruneRate(now) {
    for (const [k, v] of this.rate) {
      const kind = k.slice(k.lastIndexOf("|") + 1);
      if (now - v.start > 2 * (RATE_WINDOW_MS[kind] || 60_000)) this.rate.delete(k);
    }
  }

  bucket(key, kind) {
    const now = this.now();
    const k = `${key}|${kind}`;
    let b = this.rate.get(k);
    if (!b || now - b.start >= (RATE_WINDOW_MS[kind] || 60_000)) {
      if (!b && this.rate.size >= this.maxRateKeys) {
        // A flood of distinct addresses (rotating IPv6 prefixes, say): the
        // buckets are bounded, so newcomers wait until old ones age out
        // (pruning at most once a second: it walks the whole map).
        if (!(now - this.lastRatePrune < 1000)) {
          this.lastRatePrune = now;
          this.pruneRate(now);
        }
        if (this.rate.size >= this.maxRateKeys) return { start: now, count: Infinity };
      }
      b = { start: now, count: 0 };
      this.rate.set(k, b);
    }
    return b;
  }

  allow(key, kind) {
    const b = this.bucket(key, kind);
    b.count++;
    return b.count <= RATE[kind];
  }

  exhausted(key, kind) {
    return this.bucket(key, kind).count >= RATE[kind];
  }

  rateError(type, nonce) {
    return P.encError(type, nonce, P.E.RATE_LIMIT, "Too many requests -- wait a minute and try again");
  }

  newId() {
    for (;;) {
      const b = this.rand(4);
      const id = ((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]) >>> 0;
      if (id !== 0 && !this.lobbies.has(id)) return id;
    }
  }

  newCode() {
    for (;;) {
      const b = this.rand(P.LIM.CODE);
      let c = "";
      for (let i = 0; i < P.LIM.CODE; i++) c += CODE_ALPHABET[b[i] % CODE_ALPHABET.length];
      if (!this.byCode.has(c)) return c;
    }
  }

  counts() {
    const now = this.now();
    let inLobbies = 0;
    let open = 0;
    let matches = 0;
    let searching = 0;   // players in open quick games, waiting to be matched
    for (const l of this.lobbies.values()) {
      inLobbies += l.numPlayers;
      if (l.state === P.ST.PLAYING) matches++;
      else open++;
      if (l.state === P.ST.WAITING && (l.flags & (P.F.QUICK | P.F.PUBLIC)) === (P.F.QUICK | P.F.PUBLIC) &&
          now - l.updatedAt <= QUICK_FRESH_MS) {
        searching += l.numPlayers;
      }
    }
    return {
      online: Math.min(65535, inLobbies + this.browsing.size), lobbies: Math.min(65535, open),
      matches: Math.min(65535, matches), searching: Math.min(65535, searching),
    };
  }

  rollDay() {
    const d = dayKey(this.now());
    if (d !== this.today.day) this.today = { day: d, matches: 0, players: 0, peak: 0 };
  }

  notePeak() {
    this.rollDay();
    const online = this.counts().online;
    if (online > this.today.peak) this.today.peak = online;
  }

  // ------------------------------------------------- persistence helpers

  // A lobby as stored in its host's WebSocket attachment (survives the
  // Durable Object hibernating); rebuilt with adopt().
  static serialize(l) {
    const { host, pendingJoins, ...rest } = l;
    return rest;
  }

  adopt(rec, s) {
    if (!rec || !rec.id || this.lobbies.has(rec.id)) return;
    if (rec.code && this.byCode.has(rec.code)) return;
    const l = {
      fails: [], intro: { start: 0, count: 0 }, introKeys: {}, match: null, ...rec,
      pendingJoins: [], host: s && s.push ? s : null,
    };
    this.lobbies.set(l.id, l);
    if (l.code) this.byCode.set(l.code, l.id);
    if (s) {
      s.hostedId = l.id;
      this.browsing.delete(s);
    }
  }

  // ------------------------------------------------- the public status page

  snapshot() {
    this.expire();
    const c = this.counts();
    const now = this.now();
    const sessions = [...this.lobbies.values()].sort(byJoinability).slice(0, SNAPSHOT_MAX_SESSIONS).map((l) => {
      const base = {
        state: ["waiting", "starting", "playing"][l.state] || "waiting",
        numPlayers: l.numPlayers,
        maxPlayers: l.maxPlayers,
        scenario: nameOf(SCENARIOS, l.scenario),
        stage: nameOf(STAGES, l.stage),
        weapons: nameOf(WEAPONS, l.weapons),
        length: nameOf(LENGTHS, l.length),
        since: l.stateSince,
        // as of now, not as of the host's last refresh
        matchSec: l.state === P.ST.PLAYING ? l.matchSec + Math.floor((now - l.updatedAt) / 1000) : 0,
        country: l.country,
        quick: !!(l.flags & P.F.QUICK),
      };
      if (!(l.flags & P.F.PUBLIC)) return { ...base, private: true };
      return {
        ...base,
        name: l.name,
        host: l.hostName,
        code: l.state === P.ST.WAITING && l.numPlayers < l.maxPlayers ? l.code : "",
        players: (l.players || []).map((p) => ({ name: p.name, character: nameOf(CHARACTERS, p.character), team: p.team })),
      };
    });
    this.rollDay();
    return {
      now,
      online: c.online,
      lobbies: c.lobbies,
      matches: c.matches,
      searching: c.searching,
      today: { ...this.today },
      sessions,
      recent: this.recent,
    };
  }
}

function validCode(c) {
  if (c.length !== P.LIM.CODE) return false;
  for (const ch of c) if (!CODE_ALPHABET.includes(ch)) return false;
  return true;
}

const unreachable = (l) => (l.fails ? l.fails.length : 0) >= UNREACHABLE_AFTER;
const olderThan = (l, mine) => l.createdAt < mine.createdAt || (l.createdAt === mine.createdAt && l.id < mine.id);

// Quick match ranking: fewer failed attempts first, then the searcher's own
// continent (lockstep feels every millisecond), then fuller (a match starts
// sooner), then older.
function quickOrder(a, b, continent) {
  const fa = a.fails ? a.fails.length : 0;
  const fb = b.fails ? b.fails.length : 0;
  if (fa !== fb) return fa - fb;
  if (continent) {
    const ca = a.continent === continent ? 0 : 1;
    const cb = b.continent === continent ? 0 : 1;
    if (ca !== cb) return ca - cb;
  }
  if (a.numPlayers !== b.numPlayers) return b.numPlayers - a.numPlayers;
  return a.createdAt - b.createdAt;
}

// Waiting + open first (fullest first), then starting, then playing; newest
// first within a group.
function byJoinability(a, b) {
  const rank = (l) => (l.state === P.ST.WAITING ? (l.numPlayers < l.maxPlayers ? 0 : 1) : l.state === P.ST.STARTING ? 2 : 3);
  return rank(a) - rank(b) || b.numPlayers - a.numPlayers || b.createdAt - a.createdAt;
}
