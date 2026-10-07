// Directory core tests: node --test "test/*.test.js"
import test from "node:test";
import assert from "node:assert/strict";
import {
  Directory, LOBBY_TTL_MS, MAX_BAD_MESSAGES, MAX_INTROS_PER_MIN, MAX_LOBBIES_PER_ADDR, QUICK_FRESH_MS, RATE,
  RESULT_GRACE_MS, SNAPSHOT_MAX_SESSIONS,
} from "../src/directory.js";
import { ANY, SC } from "../src/gamedata.js";
import { cleanCands, ipKey } from "../src/netaddr.js";
import * as P from "../src/protocol.js";

const BUILD = "abc1234|ntsc-final|x86_64-windows|p2|64bit";
const SECRET = Uint8Array.from({ length: 32 }, (_, i) => i * 7 + 1);

function harness(opts = {}) {
  let now = 1_700_000_000_000;
  let seed = 1;
  const changes = { n: 0 };
  const recorded = [];
  const dir = new Directory({
    now: () => now,
    randomBytes: (n) => Uint8Array.from({ length: n }, () => (seed = (seed * 1103515245 + 12345) >>> 0) >>> 16 & 255),
    onChange: () => changes.n++,
    recordMatch: (m, today) => recorded.push({ m, today }),
    secret: SECRET,
    ...opts,
  });
  return { dir, changes, recorded, advance: (ms) => (now += ms), now: () => now };
}

const ipNum = (s) => s.split(".").reduce((a, b) => ((a << 8) | Number(b)) >>> 0, 0);
const cand = (ip, port) => ({ ip: ipNum(ip), port });
const LAN = cand("192.168.1.20", 27007);

// A scripted client session: send(bytes) -> decoded replies; pushes collected.
// Its published addresses are its own (as the real game's are: STUN + LAN).
function client(dir, { ip = "198.51.100.1", name = "Bond", build = BUILD, transport = "ws", country = "GB", continent = "EU" } = {}) {
  const pushed = [];
  const s = {
    id: Math.random().toString(16).slice(2), ip, country, continent, transport, hello: false, build: "", name: "", hostedId: 0,
    push: transport === "ws" ? (b) => pushed.push(P.decodeService(b)) : null,
  };
  const send = (bytes) => dir.handle(s, bytes).map((b) => P.decodeService(b));
  const hello = send(P.encHello(build, name));
  const mine = ip.includes(":") ? [LAN] : [cand(ip, 51000), LAN];
  return { s, send, pushed, hello, cands: mine };
}

function hostLobby(h, c, extra = {}) {
  const [r] = c.send(P.encHost({
    name: c.s.name + "'s game", flags: P.F.PUBLIC, maxPlayers: 4, numPlayers: 1, cands: c.cands,
    players: [{ name: c.s.name, character: 0 }], ...extra,
  }));
  assert.equal(r.type, P.S.HOSTED, JSON.stringify(r));
  return r;
}

const QUICK = P.F.PUBLIC | P.F.QUICK;

// ------------------------------------------------------------------ basics

test("hello is required, once, and welcomes with counts", () => {
  const { dir } = harness();
  const s = { ip: "1.1.1.1", transport: "ws", hello: false, push: () => {} };
  const [e] = dir.handle(s, P.encList()).map(P.decodeService);
  assert.equal(e.type, P.S.ERROR);
  const c = client(dir);
  assert.equal(c.hello[0].type, P.S.WELCOME);
  assert.equal(c.hello[0].online, 1, "a browsing client counts as online");
  assert.equal(c.send(P.encHello(BUILD, "Again"))[0].code, P.E.BAD_REQUEST, "a second hello is refused");
});

test("old protocol versions are turned away", () => {
  const { dir } = harness();
  const s = { ip: "1.1.1.1", transport: "ws", hello: false, push: () => {} };
  const [e] = dir.handle(s, P.encHello(BUILD, "X", 2)).map(P.decodeService);
  assert.equal(e.type, P.S.ERROR);
  assert.equal(e.code, P.E.VERSION);
  assert.equal(s.closeAfter, true);
});

test("host, list (same build only), join by code, host is told", () => {
  const h = harness();
  const host = client(h.dir, { name: "Bond" });
  const hosted = hostLobby(h, host, { scenario: SC.MWTGG, stage: 7, weapons: 13, length: 4 });
  assert.match(hosted.code, /^[A-HJ-NP-Z2-9]{6}$/);
  assert.match(hosted.token, /^[0-9a-f]{32}$/);

  const other = client(h.dir, { name: "Alec", build: "other-build", ip: "198.51.100.2" });
  assert.equal(other.send(P.encList())[0].entries.length, 0, "other builds never see the lobby");

  const joiner = client(h.dir, { name: "Natalya", ip: "198.51.100.9" });
  const listed = joiner.send(P.encList())[0];
  assert.equal(listed.entries.length, 1);
  assert.equal(listed.entries[0].code, hosted.code);
  assert.equal(listed.entries[0].hostName, "Bond");
  assert.equal(listed.entries[0].country, "GB");
  assert.equal(listed.entries[0].weapons, 13);
  assert.equal(listed.entries[0].length, 4);

  const [info] = joiner.send(P.encJoin({ code: hosted.code.toLowerCase(), nonce: 77, cands: joiner.cands }));
  assert.equal(info.type, P.S.JOININFO);
  assert.equal(info.nonce, 77);
  assert.equal(info.lobbyId, hosted.lobbyId);
  assert.deepEqual(info.cands, host.cands);

  assert.equal(host.pushed.length, 1, "host got the join request");
  assert.equal(host.pushed[0].type, P.S.JOINREQ);
  assert.equal(host.pushed[0].name, "Natalya");
  assert.deepEqual(host.pushed[0].cands, joiner.cands);
});

test("private lobbies are unlisted, joinable by code only (never by id)", () => {
  const h = harness();
  const host = client(h.dir);
  const hosted = hostLobby(h, host, { flags: 0 });
  const j = client(h.dir, { name: "Xenia", ip: "198.51.100.2" });
  assert.equal(j.send(P.encList())[0].entries.length, 0);
  assert.equal(j.send(P.encJoin({ lobbyId: hosted.lobbyId, nonce: 1 }))[0].code, P.E.NOT_FOUND);
  assert.equal(j.send(P.encJoin({ code: hosted.code, nonce: 1, cands: j.cands }))[0].type, P.S.JOININFO);
});

test("join refusals: unknown, full, playing, other build, own game", () => {
  const h = harness();
  const host = client(h.dir);
  const hosted = hostLobby(h, host, { maxPlayers: 2, numPlayers: 2, players: [{ name: "Bond" }, { name: "Q" }] });
  const j = client(h.dir, { name: "Boris", ip: "198.51.100.3" });
  const err = (m) => j.send(m)[0];
  assert.equal(err(P.encJoin({ code: "ZZZZZZ", nonce: 5 })).code, P.E.NOT_FOUND);
  assert.equal(err(P.encJoin({ code: hosted.code, nonce: 5 })).code, P.E.FULL);
  hostLobby(h, host, { lobbyId: hosted.lobbyId, token: hosted.token, maxPlayers: 4, numPlayers: 2, state: P.ST.PLAYING });
  assert.equal(err(P.encJoin({ lobbyId: hosted.lobbyId, nonce: 5 })).code, P.E.IN_MATCH);
  const alien = client(h.dir, { build: "zzz-other", ip: "198.51.100.4" });
  hostLobby(h, host, { lobbyId: hosted.lobbyId, token: hosted.token, numPlayers: 1, state: P.ST.WAITING });
  assert.equal(alien.send(P.encJoin({ code: hosted.code, nonce: 5 }))[0].code, P.E.VERSION);
  assert.equal(host.send(P.encJoin({ code: hosted.code, nonce: 5 }))[0].code, P.E.BAD_REQUEST);
});

test("refresh needs the token; a wrong one is refused", () => {
  const h = harness();
  const host = client(h.dir);
  const hosted = hostLobby(h, host);
  const thief = client(h.dir, { ip: "203.0.113.66" });
  const [r] = thief.send(P.encHost({ lobbyId: hosted.lobbyId, token: "0".repeat(32), name: "pwned", maxPlayers: 4, numPlayers: 1, cands: thief.cands }));
  assert.equal(r.code, P.E.FORBIDDEN);
});

// ------------------------------------------------- hardening: addresses

test("anti-reflection: published addresses must be the sender's own", () => {
  const me = "198.51.100.1";
  const victim = cand("203.0.113.99", 27007);
  // public: only the sender's own address (any port); private LAN: up to 2;
  // loopback only from loopback; no low ports, no junk ranges
  assert.deepEqual(cleanCands([victim, cand(me, 51000), LAN, cand("10.0.0.5", 4000), cand("172.16.0.1", 5000)], me),
    [cand(me, 51000), LAN, cand("10.0.0.5", 4000)]);
  assert.deepEqual(cleanCands([cand("127.0.0.1", 5000)], me), []);
  assert.deepEqual(cleanCands([cand("127.0.0.1", 5000)], "127.0.0.1"), [cand("127.0.0.1", 5000)]);
  assert.deepEqual(cleanCands([cand(me, 53), cand(me, 80), cand("224.0.0.1", 5000), cand("255.255.255.255", 5000),
    cand("0.1.2.3", 5000), cand("169.254.1.1", 5000)], me), []);
  // IPv4-mapped sender address
  assert.deepEqual(cleanCands([cand(me, 51000)], "::ffff:" + me), [cand(me, 51000)]);
  // an IPv6 sender cannot be checked against its IPv4: one public address at most
  assert.deepEqual(cleanCands([victim, cand("203.0.113.98", 5000)], "2001:db8::1"), [victim]);

  // end to end: a host listing a victim's address publishes none of it...
  const h = harness();
  const evil = client(h.dir, { ip: me });
  const [r] = evil.send(P.encHost({ name: "x", maxPlayers: 4, numPlayers: 1, cands: [victim] }));
  assert.equal(r.code, P.E.BAD_REQUEST, "no usable address of its own");
  // ...and a joiner listing one never has it passed to the host
  const host = client(h.dir, { ip: "198.51.100.5" });
  const hosted = hostLobby(h, host);
  const j = client(h.dir, { ip: "198.51.100.6" });
  j.send(P.encJoin({ code: hosted.code, nonce: 1, cands: [victim, cand("198.51.100.6", 40000)] }));
  assert.deepEqual(host.pushed.at(-1).cands, [cand("198.51.100.6", 40000)]);
});

test("IPv6 senders are counted per /64 (one subscriber, one address)", () => {
  assert.equal(ipKey("2001:db8:1:2:aaaa::1"), ipKey("2001:db8:1:2:ffff:ffff:ffff:ffff"));
  assert.notEqual(ipKey("2001:db8:1:2::1"), ipKey("2001:db8:1:3::1"));
  assert.equal(ipKey("::ffff:10.0.0.1"), "10.0.0.1");
  const h = harness();
  let refused = 0;
  for (let i = 0; i < RATE.list + 5; i++) {
    const c = client(h.dir, { ip: `2001:db8:1:2::${(i + 1).toString(16)}` });   // a new address each time
    if (c.send(P.encList())[0].type === P.S.ERROR) refused++;
  }
  assert.equal(refused, 5, "rotating addresses inside one /64 does not escape the limit");
});

// --------------------------------------------------- hardening: identity

test("a restarted service re-adopts a refreshing lobby (signed token); forged ones get a new identity", () => {
  const h = harness();
  const host = client(h.dir);
  const hosted = hostLobby(h, host);
  const fresh = new Directory({ now: h.now, secret: SECRET });   // same secret: kept in storage
  const again = client(fresh);
  const [r] = again.send(P.encHost({ lobbyId: hosted.lobbyId, token: hosted.token, code: hosted.code, name: "x", maxPlayers: 4, numPlayers: 1, flags: P.F.PUBLIC, cands: again.cands }));
  assert.equal(r.type, P.S.HOSTED);
  assert.equal(r.lobbyId, hosted.lobbyId);
  assert.equal(r.code, hosted.code);
  assert.equal(r.token, hosted.token);

  // squatting: claiming someone's code (or any id) with a made-up token
  const other = new Directory({ now: h.now, secret: SECRET });
  const squatter = client(other, { ip: "203.0.113.7" });
  const [q] = squatter.send(P.encHost({ lobbyId: hosted.lobbyId, token: "a".repeat(32), code: hosted.code, name: "fake", maxPlayers: 4, numPlayers: 1, cands: squatter.cands }));
  assert.equal(q.type, P.S.HOSTED);
  assert.notEqual(q.code, hosted.code, "a forged token never gets the code");
  assert.notEqual(q.lobbyId, hosted.lobbyId);
});

test("per-address cap on concurrent lobbies", () => {
  const h = harness();
  const ok = [];
  for (let i = 0; i < MAX_LOBBIES_PER_ADDR + 1; i++) {
    const c = client(h.dir, { ip: "198.51.100.40", name: "H" + i });   // separate sessions, one address
    const [r] = c.send(P.encHost({ name: "g" + i, maxPlayers: 4, numPlayers: 1, cands: c.cands }));
    ok.push(r.type === P.S.HOSTED ? "ok" : r.code);
  }
  assert.deepEqual(ok, [...Array(MAX_LOBBIES_PER_ADDR).fill("ok"), P.E.BUSY]);
});

test("wrong codes are throttled per address", () => {
  const h = harness();
  const host = client(h.dir);
  const hosted = hostLobby(h, host, { flags: 0 });
  const g = client(h.dir, { ip: "198.51.100.77" });
  for (let i = 0; i < RATE.badcode; i++) assert.equal(g.send(P.encJoin({ code: "ZZZZZ" + "ABCDEFGHJK"[i], nonce: i }))[0].code, P.E.NOT_FOUND);
  assert.equal(g.send(P.encJoin({ code: hosted.code, nonce: 99 }))[0].code, P.E.RATE_LIMIT, "even the right code, until it cools down");
  h.advance(600_000);
  hostLobby(h, host, { lobbyId: hosted.lobbyId, token: hosted.token, flags: 0 });   // the host kept refreshing
  assert.equal(g.send(P.encJoin({ code: hosted.code, nonce: 100, cands: g.cands }))[0].type, P.S.JOININFO);
});

test("a game is sent a bounded number of join requests per minute", () => {
  const h = harness();
  const host = client(h.dir);
  const hosted = hostLobby(h, host);
  let busy = 0;
  for (let i = 0; i < MAX_INTROS_PER_MIN + 4; i++) {
    const j = client(h.dir, { ip: `198.51.101.${i + 1}` });
    const [r] = j.send(P.encJoin({ code: hosted.code, nonce: i, cands: j.cands }));
    if (r.type === P.S.ERROR && r.code === P.E.BUSY) busy++;
  }
  assert.equal(busy, 4);
  assert.equal(host.pushed.length, MAX_INTROS_PER_MIN);
});

test("a session that keeps sending garbage is closed", () => {
  const { dir } = harness();
  const c = client(dir);
  for (let i = 0; i < MAX_BAD_MESSAGES; i++) dir.handle(c.s, Uint8Array.of(250, 1, 2));
  assert.notEqual(c.s.closeAfter, true);
  dir.handle(c.s, Uint8Array.of(250));
  assert.equal(c.s.closeAfter, true);
});

// -------------------------------------------------------- quick matching

test("quick match: host when alone, join the fullest fresh quick lobby", () => {
  const h = harness();
  const a = client(h.dir, { name: "A", ip: "10.0.0.1" });
  assert.equal(a.send(P.encQuick({ nonce: 9 }))[0].type, P.S.QUICKHOST);
  const qa = hostLobby(h, a, { flags: QUICK });
  const b = client(h.dir, { name: "B", ip: "10.0.0.2" });
  const [ib] = b.send(P.encQuick({ nonce: 10, cands: b.cands }));
  assert.equal(ib.type, P.S.JOININFO);
  assert.equal(ib.lobbyId, qa.lobbyId);
  assert.equal(a.pushed.at(-1).type, P.S.JOINREQ);

  // a stale quick lobby is not offered
  h.advance(QUICK_FRESH_MS + 1000);
  const c = client(h.dir, { name: "C", ip: "10.0.0.3" });
  assert.equal(c.send(P.encQuick({ nonce: 11 }))[0].type, P.S.QUICKHOST);
});

test("quick match preferences: every variation matches only fitting games", () => {
  const h = harness();
  let n = 0;
  const host = (rules) => {
    const c = client(h.dir, { name: "H" + n, ip: `10.9.0.${++n}` });
    return hostLobby(h, c, { flags: QUICK, ...rules });
  };
  const golden = host({ scenario: SC.MWTGG, stage: 0, weapons: 13, length: 2, maxPlayers: 4 });
  const facility2 = host({ scenario: SC.NORMAL, stage: 7, weapons: 11, length: 1, maxPlayers: 2 });
  const team22 = host({ scenario: SC.T2V2, stage: 0, weapons: 3, length: 3, maxPlayers: 4 });
  const team21 = host({ scenario: SC.T2V1, stage: 9, weapons: 3, length: 3, maxPlayers: 3 });
  const yolt = host({ scenario: SC.YOLT, stage: 1, weapons: 12, length: 7, maxPlayers: 4 });
  const ask = (prefs) => {
    const c = client(h.dir, { name: "S" + n, ip: `10.8.0.${++n}` });
    const [r] = c.send(P.encQuick({ nonce: n, prefs, cands: c.cands }));
    return r.type === P.S.JOININFO ? r.lobbyId : "host";
  };
  assert.equal(ask({ scenario: SC.MWTGG }), golden.lobbyId);
  assert.equal(ask({ stage: 7 }), facility2.lobbyId);
  assert.equal(ask({ players: 2 }), facility2.lobbyId);
  assert.equal(ask({ scenario: SC.T2V2 }), team22.lobbyId);
  assert.equal(ask({ scenario: SC.T2V1, stage: 9 }), team21.lobbyId);
  assert.equal(ask({ length: 7 }), yolt.lobbyId, "last one standing means YOLT");
  assert.equal(ask({ scenario: SC.YOLT, length: 1 }), yolt.lobbyId, "YOLT ignores a length preference");
  assert.equal(ask({ scenario: SC.MWTGG, weapons: 0 }), golden.lobbyId, "Golden Gun ignores a weapons preference");
  assert.equal(ask({ weapons: 12 }), yolt.lobbyId);
  assert.equal(ask({ scenario: SC.LTK }), "host", "nothing fits: host one");
  assert.equal(ask({ scenario: SC.T2V2, stage: 11 }), team22.lobbyId, "Egyptian can't hold 2 vs 2: map dropped");
  assert.equal(ask({ stage: 7, players: 4 }), "host", "a 4-player Facility search never joins the 2-player game");
  assert.equal(ask({ scenario: 99, stage: 0, weapons: 99, length: 99, players: 9 }) !== "host", true, "garbage = any");
});

test("quick match prefers the searcher's own continent", () => {
  const h = harness();
  const eu = client(h.dir, { name: "EU", ip: "10.0.1.1", continent: "EU" });
  const qeu = hostLobby(h, eu, { flags: QUICK });
  h.advance(1000);
  const na = client(h.dir, { name: "NA", ip: "10.0.1.2", continent: "NA" });
  const qna = hostLobby(h, na, { flags: QUICK });
  const fromNA = client(h.dir, { name: "J", ip: "10.0.1.3", continent: "NA" });
  assert.equal(fromNA.send(P.encQuick({ nonce: 1 }))[0].lobbyId, qna.lobbyId, "same continent before older");
  const fromAS = client(h.dir, { name: "K", ip: "10.0.1.4", continent: "AS" });
  assert.equal(fromAS.send(P.encQuick({ nonce: 2 }))[0].lobbyId, qeu.lobbyId, "no local game: the usual order");
});

test("quick match: two lone quick hosts merge one way (newer into older)", () => {
  const h = harness();
  const a = client(h.dir, { name: "A", ip: "10.0.0.1" });
  const b = client(h.dir, { name: "B", ip: "10.0.0.2" });
  // both pressed Quick Match at once: both were told to host
  assert.equal(a.send(P.encQuick({ nonce: 1 }))[0].type, P.S.QUICKHOST);
  assert.equal(b.send(P.encQuick({ nonce: 2 }))[0].type, P.S.QUICKHOST);
  const qa = hostLobby(h, a, { flags: QUICK });
  h.advance(1500);
  const qb = hostLobby(h, b, { flags: QUICK });
  // the older host asking again is not sent into the newer lobby...
  assert.equal(a.send(P.encQuick({ nonce: 3, lobbyId: qa.lobbyId }))[0].type, P.S.QUICKHOST);
  // ...the newer one is sent into the older lobby
  const [ib] = b.send(P.encQuick({ nonce: 4, lobbyId: qb.lobbyId, cands: b.cands }));
  assert.equal(ib.type, P.S.JOININFO);
  assert.equal(ib.lobbyId, qa.lobbyId);
  // the lobby id alone is enough (HTTP polling: QUICK may precede HOST in a batch)
  const poller = client(h.dir, { name: "B", ip: "10.0.0.2", transport: "http" });
  assert.equal(poller.send(P.encQuick({ nonce: 5, lobbyId: qb.lobbyId }))[0].type, P.S.JOININFO);
  assert.equal(poller.send(P.encQuick({ nonce: 6, lobbyId: qa.lobbyId }))[0].type, P.S.QUICKHOST);
});

test("quick match: a game players cannot reach goes to the back, then is dropped; its host moves on", () => {
  const h = harness();
  const a = client(h.dir, { name: "A", ip: "10.0.0.1" });
  const qa = hostLobby(h, a, { flags: QUICK });   // behind a router nobody gets through
  h.advance(1000);
  const c = client(h.dir, { name: "C", ip: "10.0.0.3" });
  const qc = hostLobby(h, c, { flags: QUICK });   // newer, reachable

  // B is sent to the oldest game (A), times out, asks again excluding it
  const b = client(h.dir, { name: "B", ip: "10.0.0.2" });
  assert.equal(b.send(P.encQuick({ nonce: 1, cands: b.cands }))[0].lobbyId, qa.lobbyId);
  const [again] = b.send(P.encQuick({ nonce: 2, excludeId: qa.lobbyId, cands: b.cands }));
  assert.equal(again.type, P.S.JOININFO);
  assert.equal(again.lobbyId, qc.lobbyId, "never the excluded game again");

  // one failure already sends A to the back of the line, even though older
  const d = client(h.dir, { name: "D", ip: "10.0.0.4" });
  assert.equal(d.send(P.encQuick({ nonce: 3 }))[0].lobbyId, qc.lobbyId);

  // a failure report from an address never sent to A does not count
  const liar = client(h.dir, { name: "L", ip: "10.0.0.9" });
  liar.send(P.encQuick({ nonce: 30, excludeId: qa.lobbyId }));
  assert.equal(h.dir.lobbies.get(qa.lobbyId).fails.length, 1);

  // a second address that was sent there and failed: no longer offered at all
  hostLobby(h, c, { lobbyId: qc.lobbyId, token: qc.token, flags: QUICK, numPlayers: 4 });   // C is now full
  const e = client(h.dir, { name: "E", ip: "10.0.0.5" });
  assert.equal(e.send(P.encQuick({ nonce: 4, cands: e.cands }))[0].lobbyId, qa.lobbyId);   // only A left
  e.send(P.encQuick({ nonce: 5, excludeId: qa.lobbyId }));
  const f = client(h.dir, { name: "F", ip: "10.0.0.6" });
  assert.equal(f.send(P.encQuick({ nonce: 6 }))[0].type, P.S.QUICKHOST, "unreachable A not offered");
  const qf = hostLobby(h, f, { flags: QUICK });   // F hosts a fresh one

  // A, waiting alone in a game nobody can reach, is moved into ANY reachable
  // game (here the newer F) -- and F is never sent to A, so no swap cycle
  const [moved] = a.send(P.encQuick({ nonce: 7, lobbyId: qa.lobbyId, cands: a.cands }));
  assert.equal(moved.type, P.S.JOININFO);
  assert.equal(moved.lobbyId, qf.lobbyId);
  assert.equal(f.send(P.encQuick({ nonce: 8, lobbyId: qf.lobbyId }))[0].type, P.S.QUICKHOST);

  // someone getting into A clears its record
  hostLobby(h, a, { lobbyId: qa.lobbyId, token: qa.token, flags: QUICK, numPlayers: 2, players: [{ name: "A" }, { name: "Z" }] });
  const g = client(h.dir, { name: "G", ip: "10.0.0.7" });
  assert.equal(g.send(P.encQuick({ nonce: 9 }))[0].lobbyId, qa.lobbyId);
});

test("quick match: a lone host never merges into a game someone failed to reach", () => {
  const h = harness();
  const ghost = client(h.dir, { name: "Ghost", ip: "10.0.0.1" });
  const qg = hostLobby(h, ghost, { flags: QUICK });   // older, but one searcher failed to reach it
  const m = client(h.dir, { name: "M", ip: "10.0.0.2" });
  assert.equal(m.send(P.encQuick({ nonce: 1, cands: m.cands }))[0].lobbyId, qg.lobbyId);
  assert.equal(m.send(P.encQuick({ nonce: 2, excludeId: qg.lobbyId }))[0].type, P.S.QUICKHOST);
  h.advance(1000);
  const qm = hostLobby(h, m, { flags: QUICK });
  // M re-asks while alone (merge check): the older ghost game is not offered
  assert.equal(m.send(P.encQuick({ nonce: 3, lobbyId: qm.lobbyId }))[0].type, P.S.QUICKHOST);
  // and a new searcher goes to M's reachable game first
  const j = client(h.dir, { name: "J", ip: "10.0.0.3" });
  assert.equal(j.send(P.encQuick({ nonce: 4 }))[0].lobbyId, qm.lobbyId);
});

test("searching count: players waiting in open quick games, in WELCOME and LISTED", () => {
  const h = harness();
  const a = client(h.dir, { name: "A", ip: "10.0.0.1" });
  hostLobby(h, a, { flags: QUICK, numPlayers: 2, players: [{ name: "A" }, { name: "B" }] });
  const b = client(h.dir, { name: "B", ip: "10.0.0.2" });
  hostLobby(h, b, { flags: P.F.PUBLIC, numPlayers: 3, players: [{ name: "B" }, { name: "C" }, { name: "D" }] });
  const c = client(h.dir, { name: "C", ip: "10.0.0.3" });
  hostLobby(h, c, { flags: QUICK, numPlayers: 4, state: P.ST.PLAYING, players: [{ name: "1" }, { name: "2" }, { name: "3" }, { name: "4" }] });
  const viewer = client(h.dir, { name: "V", ip: "10.0.0.9" });
  assert.equal(viewer.hello[0].searching, 2);
  assert.equal(viewer.send(P.encList())[0].searching, 2);
  assert.equal(h.dir.snapshot().searching, 2);
  h.advance(QUICK_FRESH_MS + 1000);   // a quick game not refreshed lately is not "searching"
  assert.equal(viewer.send(P.encList())[0].searching, 0);
});

// ------------------------------------------------------- transports, life

test("HTTP-polling hosts get queued join requests", () => {
  const h = harness();
  const host = client(h.dir, { transport: "http" });
  const hosted = hostLobby(h, host);
  const j = client(h.dir, { name: "Jaws", ip: "198.51.100.8" });
  j.send(P.encJoin({ code: hosted.code, nonce: 3, cands: j.cands }));
  const polled = host.send(P.encPoll(hosted.lobbyId, hosted.token));
  assert.equal(polled.length, 1);
  assert.equal(polled[0].type, P.S.JOINREQ);
  assert.equal(polled[0].name, "Jaws");
  assert.equal(host.send(P.encPoll(hosted.lobbyId, hosted.token)).length, 0, "drained");
  assert.equal(host.send(P.encPoll(hosted.lobbyId, "b".repeat(32)))[0].code, P.E.NOT_FOUND, "token checked");
});

test("lobbies expire without refreshes; closing the host removes them", () => {
  const h = harness();
  const host = client(h.dir);
  hostLobby(h, host);
  assert.equal(h.dir.lobbies.size, 1);
  h.advance(LOBBY_TTL_MS + 1);
  h.dir.expire();
  assert.equal(h.dir.lobbies.size, 0);

  const host2 = client(h.dir, { ip: "198.51.100.10" });
  hostLobby(h, host2);
  h.dir.sessionClosed(host2.s);
  assert.equal(h.dir.lobbies.size, 0);
});

test("hibernation: serialize + adopt rebuilds the lobby", () => {
  const h = harness();
  const host = client(h.dir);
  const hosted = hostLobby(h, host);
  const rec = Directory.serialize(h.dir.lobbies.get(hosted.lobbyId));
  assert.equal(rec.host, undefined);
  const woken = new Directory({ now: h.now, secret: SECRET });
  const s = { transport: "ws", hello: true, build: BUILD, name: "Bond", push: () => {} };
  woken.adopt(JSON.parse(JSON.stringify(rec)), s);
  assert.equal(woken.lobbies.size, 1);
  assert.equal(s.hostedId, hosted.lobbyId);
  const j = client(woken, { ip: "198.51.100.11" });
  assert.equal(j.send(P.encJoin({ code: hosted.code, nonce: 1, cands: j.cands }))[0].type, P.S.JOININFO);
});

// --------------------------------------------------------------- results

function playMatch(h, c, hosted, players, ms = 600_000, extra = {}) {
  const roster = players.map((name) => ({ name }));
  hostLobby(h, c, { lobbyId: hosted.lobbyId, token: hosted.token, numPlayers: roster.length, players: roster, state: P.ST.PLAYING, ...extra });
  h.advance(ms);
  hostLobby(h, c, { lobbyId: hosted.lobbyId, token: hosted.token, numPlayers: roster.length, players: roster, state: P.ST.WAITING, ...extra });
}

test("results: only for a match the service saw, once, with roster names and sane numbers", () => {
  const h = harness();
  const host = client(h.dir);
  const pub = hostLobby(h, host);
  const report = (extra = {}) => host.send(P.encResult({ lobbyId: pub.lobbyId, token: pub.token, durationSec: 600, scenario: 0, stage: 7,
    players: [{ name: "Bond", character: 0, kills: 9, deaths: 2 }, { name: "Alec", character: 2, kills: 3, deaths: 9 }], ...extra }));
  report();
  assert.equal(h.recorded.length, 0, "no match was played: nothing recorded");
  playMatch(h, host, pub, ["Bond", "Alec"]);
  report({ players: [{ name: "Bond", character: 0, kills: 30000, deaths: -5 }, { name: "<script>", character: 99, kills: 1, deaths: 1 }],
    durationSec: 60000 });
  assert.equal(h.recorded.length, 1);
  const m = h.recorded[0].m;
  assert.equal(m.players[0].kills, 999, "clamped");
  assert.equal(m.players[0].deaths, 0, "clamped");
  assert.equal(m.players[1].name, "Agent", "a name not in the match is never shown");
  assert.equal(m.players[1].character, "?");
  assert.ok(m.duration <= 630, "no longer than the match the service saw");
  report();
  assert.equal(h.recorded.length, 1, "one result per match");
  // a result long after the match ended is refused
  playMatch(h, host, pub, ["Bond", "Alec"]);
  h.advance(RESULT_GRACE_MS + 1000);
  report();
  assert.equal(h.recorded.length, 1);
  // private games are anonymous; wrong tokens refused
  const host2 = client(h.dir, { ip: "198.51.100.12" });
  const priv = hostLobby(h, host2, { flags: 0 });
  playMatch(h, host2, priv, ["Secret", "Other"], 60_000, { flags: 0 });
  host2.send(P.encResult({ lobbyId: priv.lobbyId, token: priv.token, durationSec: 60, scenario: 1, stage: 1, players: [{ name: "Secret", character: 5, kills: 1, deaths: 0 }] }));
  assert.equal(h.recorded.length, 2);
  assert.equal(h.recorded[1].m.players[0].name, "Agent", "private game anonymised");
  assert.equal(h.recorded[1].m.lobby, "Private game");
  const [e] = host.send(P.encResult({ lobbyId: pub.lobbyId, token: "f".repeat(32), durationSec: 1, scenario: 0, stage: 1, players: [] }));
  assert.equal(e.code, P.E.FORBIDDEN);
  const snap = h.dir.snapshot();
  assert.equal(snap.today.matches, 2);
  assert.equal(snap.recent[1].stage, "Facility");
});

test("snapshot hides private details, only shows codes of open lobbies, and is bounded", () => {
  const h = harness();
  const a = client(h.dir, { ip: "10.1.0.1" });
  hostLobby(h, a, { flags: 0, name: "Hidden" });
  const b = client(h.dir, { ip: "10.1.0.2" });
  const pb = hostLobby(h, b, { name: "Open game", weapons: 13, scenario: SC.MWTGG });
  const c = client(h.dir, { ip: "10.1.0.3" });
  hostLobby(h, c, { name: "Busy game", state: P.ST.PLAYING, matchSec: 30 });
  h.advance(10_000);
  const snap = h.dir.snapshot();
  const priv = snap.sessions.find((s) => s.private);
  assert.ok(priv);
  assert.equal(priv.name, undefined);
  assert.equal(priv.players, undefined);
  const open = snap.sessions.find((s) => s.name === "Open game");
  assert.equal(open.code, pb.code);
  assert.equal(open.weapons, "Golden gun");
  const busy = snap.sessions.find((s) => s.name === "Busy game");
  assert.equal(busy.code, "");
  assert.equal(busy.state, "playing");
  assert.equal(busy.matchSec, 40, "match clock advanced since the host's refresh");
  assert.equal(snap.matches, 1);
  assert.equal(snap.lobbies, 2);

  const many = harness();
  for (let i = 0; i < SNAPSHOT_MAX_SESSIONS + 30; i++) {
    const x = client(many.dir, { ip: `10.${2 + (i >> 8)}.${i & 255}.9` });
    hostLobby(many, x);
  }
  assert.equal(many.dir.snapshot().sessions.length, SNAPSHOT_MAX_SESSIONS);
});

test("rate limits per address", () => {
  const h = harness();
  const c = client(h.dir, { ip: "203.0.113.200" });
  let refused = 0;
  for (let i = 0; i < RATE.host + 3; i++) {
    const [r] = c.send(P.encHost({ name: "spam" + i, maxPlayers: 4, numPlayers: 1, cands: c.cands }));
    if (r.type === P.S.ERROR && r.code === P.E.RATE_LIMIT) refused++;
  }
  assert.equal(refused, 3);
  h.advance(60_000);
  assert.equal(c.send(P.encHost({ name: "later", maxPlayers: 4, numPlayers: 1, cands: c.cands }))[0].type, P.S.HOSTED);
});

test("malformed input never throws", () => {
  const { dir } = harness();
  const c = client(dir);
  for (const bad of [new Uint8Array(0), Uint8Array.of(3), Uint8Array.of(5, 1, 2), Uint8Array.of(200), new Uint8Array(4000)]) {
    const r = dir.handle(c.s, bad).map(P.decodeService);
    assert.ok(r.length <= 1);
  }
  // a HOST with an absurd player count, and one with rules out of range
  const host = (players, weapons) => new P.Writer().u8(P.C.HOST).u32(0).str("", 32).str("", 6).str("x", 23).u8(1).u8(4).u8(0)
    .u8(1).u8(0).u8(1).u8(weapons).u8(2).u16(0).cands(c.cands).u8(players).bytes();
  assert.equal(c.send(host(9, 11))[0].code, P.E.BAD_REQUEST);
  assert.equal(c.send(host(0, 99))[0].code, P.E.BAD_REQUEST);
  void ANY;
});
