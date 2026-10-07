// Robustness fuzzing (D412): node --test "test/*.test.js"
//
// The service talks to anyone on the internet. Every decoder must either
// return a message or throw ProtoError (nothing else); the directory must
// answer anything -- garbage, mutated real requests, many interleaved
// sessions, clock jumps -- without throwing, only ever reply with messages
// the game can decode, and keep its bookkeeping consistent and bounded.
// Deterministic: a failure names its seed and step.
import test from "node:test";
import assert from "node:assert/strict";
import {
  Directory, MAX_LOBBIES, MAX_LOBBIES_PER_ADDR, MAX_PENDING_JOINS, SNAPSHOT_MAX_SESSIONS,
} from "../src/directory.js";
import { ANY, LENGTHS, normalizePrefs, prefsMatch, SCENARIOS, STAGES, WEAPONS } from "../src/gamedata.js";
import { classifyV4, cleanCands, ipKey, ipv4Of } from "../src/netaddr.js";
import * as P from "../src/protocol.js";

function rng(seed) {
  let x = seed >>> 0 || 1;
  const u32 = () => {
    x ^= x << 13; x >>>= 0;
    x ^= x >>> 17;
    x ^= x << 5; x >>>= 0;
    return x;
  };
  return {
    u32,
    n: (k) => (k > 0 ? u32() % k : 0),
    chance: (pct) => u32() % 100 < pct,
    pick: (arr) => arr[u32() % arr.length],
    bytes: (len) => Uint8Array.from({ length: len }, () => u32() & 255),
  };
}

const INTERESTING = [
  0, 1, 2, 3, 4, 5, 7, 8, 11, 13, 31, 32, 33, 99, 101, 102, 127, 128, 200, 254, 255, 256, 1023, 1024, 2048,
  0x7fff, 0x8000, 0xffff, 0x10000, 0x7fffffff, 0x80000000, 0xffffffff,
];

function mutate(r, src, cap = 2100) {
  const b = Array.from(src);
  const edits = 1 + r.n(4);
  for (let k = 0; k < edits; k++) {
    const n = b.length;
    switch (r.n(9)) {
      case 0: if (n) b[r.n(n)] ^= 1 << r.n(8); break;
      case 1: if (n) b[r.n(n)] = r.u32() & 255; break;
      case 2: if (n) b[r.n(n)] = r.pick(INTERESTING) & 255; break;
      case 3: if (n >= 2) { const p = r.n(n - 1), v = r.pick(INTERESTING); b[p] = v & 255; b[p + 1] = (v >>> 8) & 255; } break;
      case 4: if (n >= 4) { const p = r.n(n - 3), v = r.pick(INTERESTING); for (let i = 0; i < 4; i++) b[p + i] = (v >>> (8 * i)) & 255; } break;
      case 5: if (n) b.length = r.n(n + 1); break;
      case 6: { const add = 1 + r.n(24); for (let i = 0; i < add && b.length < cap; i++) b.push(r.u32() & 255); break; }
      case 7: if (n < cap) b.splice(r.n(n + 1), 0, r.pick(INTERESTING) & 255); break;
      default: if (n) b.splice(r.n(n), 1 + r.n(8)); break;
    }
  }
  return Uint8Array.from(b);
}

const BUILD = "fuzz123|ntsc-final|x86_64-windows|p2|64bit";
const ipNum = (s) => s.split(".").reduce((a, b) => ((a << 8) | Number(b)) >>> 0, 0);

function rndStr(r, max) {
  const n = r.n(max + 2);
  let s = "";
  for (let i = 0; i < n; i++) s += String.fromCharCode(r.chance(85) ? 32 + r.n(95) : r.n(256));
  return s;
}

const ODD_IPS = ["", "garbage", "::", "::1", "127.0.0.1", "0.0.0.0", "255.255.255.255", "fe80::1", "2001:db8::", "1.2.3"];

function rndIp(r) {
  switch (r.n(6)) {
    case 0: return `2001:db8:${r.n(4).toString(16)}:${r.n(3).toString(16)}::${r.n(9)}`;   // shared /64s
    case 1: return r.pick(ODD_IPS);
    case 2: return `10.0.${r.n(3)}.${r.n(4)}`;
    default: return `198.51.${r.n(3)}.${r.n(30)}`;                                      // shared addresses
  }
}

function rndCands(r, ip) {
  const out = [];
  const n = r.n(7);
  const v4 = ipv4Of(ip);
  for (let i = 0; i < n; i++) {
    switch (r.n(4)) {
      case 0: out.push({ ip: v4 ?? r.u32(), port: 1024 + r.n(64000) }); break;          // its own public address
      case 1: out.push({ ip: ipNum(`192.168.${r.n(3)}.${r.n(50)}`), port: 27007 }); break;
      case 2: out.push({ ip: r.pick(INTERESTING) >>> 0, port: r.pick(INTERESTING) & 0xffff }); break;
      default: out.push({ ip: r.u32(), port: r.u32() & 0xffff }); break;
    }
  }
  return out;
}

function rndPrefs(r, anyPct = 40) {
  const f = () => (r.chance(anyPct) ? ANY : r.chance(85) ? r.n(16) : r.u32() & 255);
  return { scenario: f(), stage: f(), weapons: f(), length: f(), players: f() };
}

function rndPlayers(r) {
  return Array.from({ length: r.n(6) }, () => ({
    name: rndStr(r, 18), character: r.n(70), team: r.n(3), ready: r.n(2), kills: r.n(2000) - 500, deaths: r.n(2000) - 500,
  }));
}

// A well-formed client request, aimed at what this session (and others)
// have learned, so the deep paths -- re-hosting, joining, quick matching
// into real lobbies, results for real matches -- get exercised.
function rndRequest(r, sess, known) {
  const mine = sess.hosted;
  const other = known.length ? r.pick(known) : null;
  switch (r.n(10)) {
    case 0:
      return P.encHello(r.chance(90) ? BUILD : rndStr(r, 70), rndStr(r, 18), r.chance(90) ? P.NDP_VERSION : r.n(256));
    case 1:
      return P.encList();
    case 2:
    case 3: {
      const maxPlayers = r.chance(80) ? 2 + r.n(3) : r.n(256);
      return P.encHost({
        lobbyId: mine && r.chance(80) ? mine.lobbyId : r.chance(50) ? 0 : r.u32(),
        token: mine && r.chance(85) ? mine.token : rndStr(r, 33),
        code: mine && r.chance(70) ? mine.code : rndStr(r, 7),
        name: rndStr(r, 25), flags: r.n(256) & (r.chance(70) ? 3 : 255), maxPlayers,
        numPlayers: r.chance(80) ? r.n(maxPlayers + 1) : r.n(256), state: r.chance(80) ? r.n(3) : r.n(256),
        scenario: r.chance(85) ? r.n(SCENARIOS.length) : r.n(256), stage: r.chance(85) ? r.n(STAGES.length) : r.n(256),
        weapons: r.chance(85) ? r.n(WEAPONS.length) : r.n(256), length: r.chance(85) ? r.n(LENGTHS.length) : r.n(256),
        matchSec: r.u32() & 0xffff, cands: rndCands(r, sess.s.ip), players: rndPlayers(r),
      });
    }
    case 4:
      return P.encUnhost(mine && r.chance(70) ? mine.lobbyId : r.u32(), mine && r.chance(70) ? mine.token : rndStr(r, 33));
    case 5:
      return P.encJoin({
        lobbyId: other && r.chance(50) ? other.lobbyId : r.chance(50) ? 0 : r.u32(),
        code: other && r.chance(60) ? (r.chance(50) ? other.code.toLowerCase() : other.code) : rndStr(r, 7),
        nonce: r.u32(), cands: rndCands(r, sess.s.ip),
      });
    case 6:
    case 7:
      return P.encQuick({
        nonce: r.u32(), lobbyId: mine && r.chance(50) ? mine.lobbyId : 0,
        excludeId: other && r.chance(30) ? other.lobbyId : r.chance(80) ? 0 : r.u32(),
        prefs: rndPrefs(r, 75), cands: rndCands(r, sess.s.ip),
      });
    case 8:
      return P.encResult({
        lobbyId: mine && r.chance(80) ? mine.lobbyId : r.u32(), token: mine && r.chance(80) ? mine.token : rndStr(r, 33),
        durationSec: r.u32() & 0xffff, scenario: r.n(10), stage: r.n(14), players: rndPlayers(r),
      });
    default:
      return P.encPoll(mine && r.chance(80) ? mine.lobbyId : r.u32(), mine && r.chance(80) ? mine.token : rndStr(r, 33));
  }
}

function checkDecodesOrProtoError(fn, bytes, what) {
  try {
    const m = fn(bytes);
    assert.equal(typeof m.type, "number", what);
    return m;
  } catch (e) {
    if (!(e instanceof P.ProtoError)) {
      assert.fail(`${what}: threw ${e && e.name}: ${e && e.message} on [${Array.from(bytes).join(",")}]`);
    }
    return null;
  }
}

// ------------------------------------------------------------------ decoders

test("decoders return a message or throw ProtoError -- never anything else", () => {
  const r = rng(0x5eed1);
  const serviceSamples = [
    P.encWelcome({ online: 3, lobbies: 1, matches: 0, searching: 2, motd: "hi" }),
    P.encListed({
      online: 9, lobbies: 2, matches: 1, searching: 1,
      entries: [{ id: 7, name: "A", hostName: "B", code: "ABCDEF", flags: 3, state: 0, numPlayers: 1, maxPlayers: 4,
        scenario: 3, stage: 7, weapons: 13, length: 4, country: "GB" }],
    }),
    P.encHosted({ lobbyId: 5, token: "0123456789abcdef0123456789abcdef", code: "ABCDEF" }),
    P.encJoinInfo({ nonce: 1, lobbyId: 2, name: "N", hostName: "H", cands: [{ ip: 0x01020304, port: 5000 }] }),
    P.encError(P.C.JOIN, 3, P.E.NOT_FOUND, "nope"),
    P.encQuickHost(77),
    P.encJoinReq({ lobbyId: 9, name: "J", cands: [{ ip: 0x0a000001, port: 27007 }] }),
  ];
  let accepted = 0;
  for (let i = 0; i < 25000; i++) {
    const sess = { s: { ip: rndIp(r) }, hosted: null };
    let bytes;
    const kind = r.n(10);
    if (kind === 0) bytes = r.bytes(r.n(300));
    else if (kind < 7) bytes = mutate(r, rndRequest(r, sess, []));
    else bytes = mutate(r, r.pick(serviceSamples));
    if (checkDecodesOrProtoError(P.decodeClient, bytes, `decodeClient #${i}`)) accepted++;
    checkDecodesOrProtoError(P.decodeService, bytes, `decodeService #${i}`);
    // the HTTP batch framing, around valid and broken messages
    const batch = r.chance(50) ? P.frameBatch([bytes, r.bytes(1 + r.n(20))]) : mutate(r, P.frameBatch([bytes]));
    try {
      const msgs = P.unframeBatch(batch);
      assert.ok(msgs.length <= 16);
    } catch (e) {
      assert.ok(e instanceof P.ProtoError, `unframeBatch threw ${e && e.name}`);
    }
  }
  assert.ok(accepted > 1000, `only ${accepted} mutated requests decoded: the fuzzing is too shallow`);
  // unmutated requests always decode
  for (let i = 0; i < 2000; i++) {
    const req = rndRequest(r, { s: { ip: "198.51.100.1" }, hosted: null }, []);
    assert.ok(P.decodeClient(req), "a well-formed request was refused");
  }
});

// ------------------------------------------------------------------ directory

function checkDirectory(dir, label) {
  assert.ok(dir.lobbies.size <= MAX_LOBBIES, `${label}: ${dir.lobbies.size} lobbies`);
  const perKey = new Map();
  for (const [id, l] of dir.lobbies) {
    assert.equal(l.id, id, label);
    assert.equal(dir.byCode.get(l.code), id, `${label}: lobby ${id} code ${l.code} not indexed`);
    assert.ok(l.maxPlayers >= 2 && l.maxPlayers <= 4 && l.numPlayers <= l.maxPlayers,
      `${label}: lobby ${id} ${l.numPlayers}/${l.maxPlayers}`);
    assert.ok(l.pendingJoins.length <= MAX_PENDING_JOINS, `${label}: ${l.pendingJoins.length} pending joins`);
    assert.ok(Array.isArray(l.cands) && l.cands.length >= 1 && l.cands.length <= 4, `${label}: lobby ${id} cands`);
    for (const c of l.cands) {
      assert.ok(Number.isInteger(c.ip) && c.ip > 0 && c.port >= 1024 && c.port <= 65535, `${label}: cand ${JSON.stringify(c)}`);
      assert.notEqual(classifyV4(c.ip), "bad", `${label}: published a bad address`);
    }
    perKey.set(l.hostKey, (perKey.get(l.hostKey) || 0) + 1);
  }
  for (const [code, id] of dir.byCode) {
    const l = dir.lobbies.get(id);
    assert.ok(l && l.code === code, `${label}: stale code ${code}`);
  }
  for (const [k, n] of perKey) assert.ok(n <= MAX_LOBBIES_PER_ADDR, `${label}: ${n} lobbies from ${k}`);
  const c = dir.counts();
  for (const v of Object.values(c)) assert.ok(Number.isInteger(v) && v >= 0 && v <= 65535, `${label}: count ${v}`);
  const snap = dir.snapshot();
  const json = JSON.stringify(snap);
  assert.ok(json.length > 0 && snap.sessions.length <= SNAPSHOT_MAX_SESSIONS, label);
  assert.ok(dir.rate.size <= dir.maxRateKeys, `${label}: ${dir.rate.size} rate buckets`);
}

for (const seed of [1, 2, 3, 4]) {
  test(`the directory survives anything (seed ${seed})`, () => {
    const r = rng(0xd1ec7 * seed);
    let now = 1_700_000_000_000 + seed * 86_400_000;
    let rs = seed;
    const dir = new Directory({
      now: () => now,
      randomBytes: (n) => Uint8Array.from({ length: n }, () => (rs = (rs * 1103515245 + 12345) >>> 0) >>> 16 & 255),
      secret: Uint8Array.from({ length: 32 }, (_, i) => i + seed),
      maxRateKeys: 400,   // small, so the bound is exercised
    });
    const known = [];   // lobbies anyone learned about (id / code)
    const newSession = () => {
      const ip = rndIp(r);
      const transport = r.chance(75) ? "ws" : "http";
      const sess = { pushed: 0, hosted: null };
      sess.s = {
        ip, country: r.pick(["GB", "US", "DE", "", "XX"]), continent: r.pick(["EU", "NA", "AS", ""]), transport,
        hello: false, build: "", name: "", hostedId: 0,
        push: transport === "ws" ? (b) => { checkDecodesOrProtoError(P.decodeService, b, "push"); sess.pushed++; } : null,
      };
      if (r.chance(90)) dir.handle(sess.s, P.encHello(BUILD, rndStr(r, 16)));
      return sess;
    };
    const sessions = Array.from({ length: 40 }, newSession);
    let replies = 0, hosted = 0, joins = 0, quickHosts = 0;
    for (let step = 0; step < 15000; step++) {
      const i = r.n(sessions.length);
      const sess = sessions[i];
      if (sess.s.closeAfter || (r.chance(1) && r.chance(50))) {   // the socket closes (as index.js does after closeAfter)
        dir.sessionClosed(sess.s);
        sessions[i] = newSession();
        continue;
      }
      let bytes;
      const k = r.n(100);
      if (k < 4) bytes = r.bytes(r.n(200));
      else if (k < 30) bytes = mutate(r, rndRequest(r, sess, known));
      else bytes = rndRequest(r, sess, known);
      let out;
      try {
        out = dir.handle(sess.s, bytes);
      } catch (e) {
        assert.fail(`seed ${seed} step ${step}: handle threw ${e && e.stack} on [${Array.from(bytes).join(",")}]`);
      }
      assert.ok(Array.isArray(out), "handle returns a list of replies");
      for (const b of out) {
        const m = checkDecodesOrProtoError(P.decodeService, b, `seed ${seed} step ${step} reply`);
        assert.ok(m, `seed ${seed} step ${step}: the service sent something the game cannot decode`);
        replies++;
        if (m.type === P.S.HOSTED) {
          sess.hosted = m;
          hosted++;
          if (!known.some((x) => x.lobbyId === m.lobbyId)) known.push({ lobbyId: m.lobbyId, code: m.code });
          if (known.length > 64) known.shift();
        } else if (m.type === P.S.JOININFO) {
          joins++;
        } else if (m.type === P.S.QUICKHOST) {
          quickHosts++;
        }
      }
      // time: mostly small steps, sometimes long gaps (expiry, rate windows, a new day)
      now += r.n(3000);
      if (r.chance(1)) now += r.pick([61_000, 95_000, 700_000, 86_400_000]);
      if (step % 750 === 0) checkDirectory(dir, `seed ${seed} step ${step}`);
    }
    checkDirectory(dir, `seed ${seed} end`);
    assert.ok(hosted > 50 && joins > 10 && quickHosts > 10 && replies > 5000,
      `seed ${seed}: too shallow (hosted ${hosted}, joins ${joins}, quick hosts ${quickHosts}, replies ${replies})`);
  });
}

test("rate buckets are bounded under an address flood", () => {
  let now = 1_700_000_000_000;
  const dir = new Directory({ now: () => now, secret: new Uint8Array(32).fill(1), maxRateKeys: 100 });
  let limited = 0;
  for (let i = 0; i < 1000; i++) {
    const s = { ip: `2001:db8:${(i >>> 8).toString(16)}:${(i & 255).toString(16)}::1`, transport: "ws", hello: false, push: () => {} };
    dir.handle(s, P.encHello(BUILD, "Flood"));
    const [m] = dir.handle(s, P.encList()).map(P.decodeService);
    if (m.type === P.S.ERROR && m.code === P.E.RATE_LIMIT) limited++;
    assert.ok(dir.rate.size <= 100, `${dir.rate.size} buckets`);
    now += 10;
  }
  assert.ok(limited >= 850, `only ${limited} of the flood were held back`);
  // once the old buckets age out, newcomers are served again
  now += 300_000;
  const s = { ip: "198.51.100.200", transport: "ws", hello: false, push: () => {} };
  dir.handle(s, P.encHello(BUILD, "Later"));
  assert.equal(dir.handle(s, P.encList()).map(P.decodeService)[0].type, P.S.LISTED);
});

// ------------------------------------------------------------------ rules

test("preference and address rules hold for any input", () => {
  const r = rng(0xabcdef);
  const odd = [undefined, null, -1, 1.5, NaN, Infinity, "3", 256, 1e9, -0];
  for (let i = 0; i < 20000; i++) {
    const raw = rndPrefs(r);
    for (const f of Object.keys(raw)) if (r.chance(5)) raw[f] = r.pick(odd);
    const p = normalizePrefs(raw);
    for (const [f, v] of Object.entries(p)) {
      assert.ok(v === ANY || (Number.isInteger(v) && v >= 0 && v < 16), `${f} = ${v} from ${JSON.stringify(raw)}`);
    }
    assert.deepEqual(normalizePrefs(p), p, "normalisation is idempotent");
    // a lobby carrying exactly the asked-for (concrete) rules matches
    if (Object.values(p).every((v) => v !== ANY)) {
      assert.ok(prefsMatch(p, { scenario: p.scenario, stage: p.stage, weapons: p.weapons, length: p.length, maxPlayers: p.players }));
    }

    const ip = rndIp(r);
    const cands = cleanCands(rndCands(r, ip), ip);
    const sender = ipv4Of(ip);
    assert.ok(cands.length <= 4);
    let publics = 0;
    for (const c of cands) {
      const kind = classifyV4(c.ip);
      assert.ok(c.port >= 1024 && kind !== "bad", `kept ${JSON.stringify(c)} from ${ip}`);
      if (kind === "public") {
        publics++;
        // a sender on the internet publishes only its own public address
        if (sender !== null && classifyV4(sender) === "public") {
          assert.equal(c.ip, sender, `a public address that is not the sender's (${ip})`);
        }
      }
      if (kind === "loopback") assert.equal(classifyV4(sender ?? 0), "loopback", "loopback only from loopback");
    }
    // IPv6 senders (unverifiable) and local-network senders (tests): one at most
    if (sender === null || classifyV4(sender) !== "public") assert.ok(publics <= 1, `${publics} public addresses from ${ip}`);
    assert.equal(typeof ipKey(ip), "string");
  }
});
