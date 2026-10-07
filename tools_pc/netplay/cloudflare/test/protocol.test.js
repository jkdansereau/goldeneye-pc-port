// Protocol codec tests, including byte-for-byte vectors produced by the C
// codec (netplay_selftest --dirproto-vectors > test/fixtures/c-vectors.txt).
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync, existsSync } from "node:fs";
import * as P from "../src/protocol.js";
import { normalizePrefs } from "../src/gamedata.js";

const fromHex = (h) => Uint8Array.from(h.match(/../g).map((b) => parseInt(b, 16)));
const toHex = (u8) => Array.from(u8, (b) => b.toString(16).padStart(2, "0")).join("");

test("client messages round-trip", () => {
  const host = P.decodeClient(P.encHost({
    lobbyId: 0x01020304, token: "a".repeat(32), code: "ABC234", name: "Bond's game", flags: 3, maxPlayers: 4,
    state: 2, numPlayers: 2, scenario: 5, stage: 7, matchSec: 321,
    cands: [{ ip: 0xcb007109, port: 51000 }, { ip: 0, port: 1 }],
    players: [{ name: "Bond", character: 63, team: 1, ready: 1 }],
  }));
  assert.equal(host.type, P.C.HOST);
  assert.equal(host.lobbyId, 0x01020304);
  assert.equal(host.name, "Bond's game");
  assert.equal(host.matchSec, 321);
  assert.deepEqual(host.cands, [{ ip: 0xcb007109, port: 51000 }], "junk address dropped");
  assert.deepEqual(host.players, [{ name: "Bond", character: 63, team: 1, ready: 1 }]);

  const join = P.decodeClient(P.encJoin({ code: "QWE987", nonce: 0xcafef00d, cands: [{ ip: 1, port: 2 }] }));
  assert.deepEqual(join, { type: P.C.JOIN, lobbyId: 0, code: "QWE987", nonce: 0xcafef00d, cands: [{ ip: 1, port: 2 }] });

  const res = P.decodeClient(P.encResult({ lobbyId: 3, token: "t", durationSec: 600, scenario: 2, stage: 4, players: [{ name: "A", character: 1, team: 0, kills: 12, deaths: -1 }] }));
  assert.equal(res.players[0].deaths, -1);
});

test("service messages round-trip", () => {
  const listed = P.decodeService(P.encListed({ online: 17, lobbies: 3, matches: 1, entries: [{ id: 7, name: "Quick Match", hostName: "Trev", code: "XYZ789", flags: 3, state: 0, numPlayers: 3, maxPlayers: 4, scenario: 0, stage: 0, country: "GB" }] }));
  assert.equal(listed.entries[0].code, "XYZ789");
  assert.equal(listed.online, 17);
  const err = P.decodeService(P.encError(P.C.JOIN, 99, P.E.FULL, "That game is full"));
  assert.deepEqual(err, { type: P.S.ERROR, reqType: P.C.JOIN, nonce: 99, code: P.E.FULL, text: "That game is full" });
});

test("strings are sanitised and capped like the C side", () => {
  const w = new P.Writer().u8(P.C.HELLO).u8(1).str("build-x", 63);
  w.u8(20);
  for (let i = 0; i < 20; i++) w.u8(i === 3 ? 0x07 : 0x41);
  const m = P.decodeClient(w.bytes());
  assert.equal(m.name, "AAA?AAAAAAAAAAA", "15 chars kept, control char replaced");
});

test("batch framing", () => {
  const a = P.encList();
  const b = P.encHello("b", "n");
  const back = P.unframeBatch(P.frameBatch([a, b]));
  assert.deepEqual(back.map((x) => Array.from(x)), [Array.from(a), Array.from(b)]);
  assert.throws(() => P.unframeBatch(Uint8Array.of(5, 0, 1)), P.ProtoError);
});

test("truncation is an error, never a partial read", () => {
  const full = P.encHost({ name: "x", maxPlayers: 4, numPlayers: 1, cands: [{ ip: 5, port: 6 }] });
  for (let n = 1; n < full.length; n++) assert.throws(() => P.decodeClient(full.subarray(0, n)), P.ProtoError);
});

// Cross-language: what the C codec wrote must decode identically here, and
// what we encode for the service->client direction is pinned so the C test
// (testDirProtoJsVectors) can check it.
const fixture = new URL("./fixtures/c-vectors.txt", import.meta.url);
test("C-encoded vectors decode", { skip: !existsSync(fixture) && "no fixture (run the C selftest with --dirproto-vectors)" }, () => {
  const lines = readFileSync(fixture, "utf8").trim().split(/\r?\n/);
  const v = Object.fromEntries(lines.filter((l) => !l.startsWith("prefs ")).map((l) => l.split(" ")));
  const host = P.decodeClient(fromHex(v.host));
  assert.equal(host.lobbyId, 0x01020304);
  assert.equal(host.token, "0123456789abcdef0123456789abcdef");
  assert.equal(host.code, "ABC234");
  assert.equal(host.name, "Bond's game");
  assert.equal(host.flags, 3);
  assert.equal(host.matchSec, 321);
  assert.deepEqual(host.cands, [{ ip: 0xcb007109, port: 51000 }, { ip: 0xc0a80114, port: 27007 }]);
  assert.deepEqual(host.players[0], { name: "Bond", character: 63, team: 1, ready: 1 });
  assert.equal(host.weapons, 13);
  assert.equal(host.length, 4);
  const join = P.decodeClient(fromHex(v.join));
  assert.equal(join.code, "QWE987");
  assert.equal(join.nonce, 0xcafef00d);
  assert.equal(join.cands.length, 2);
  const quick = P.decodeClient(fromHex(v.quick));
  assert.equal(quick.nonce, 0x11223344);
  assert.equal(quick.lobbyId, 0x0a0b0c0d);
  assert.equal(quick.excludeId, 0x01020304);
  assert.deepEqual(quick.cands, [{ ip: 0x01020304, port: 5 }]);
  assert.deepEqual(quick.prefs, { scenario: 3, stage: 7, weapons: 0xff, length: 4, players: 2 });
  const res = P.decodeClient(fromHex(v.result));
  assert.equal(res.players[0].kills, 12);
  assert.equal(res.players[0].deaths, -1);
  const hello = P.decodeClient(fromHex(v.hello));
  assert.equal(hello.version, P.NDP_VERSION);   // the C and JS sides agree on the version
  assert.equal(hello.name, "Agent");
  const poll = P.decodeClient(fromHex(v.poll));
  assert.equal(poll.lobbyId, 77);
  const unhost = P.decodeClient(fromHex(v.unhost));
  assert.equal(unhost.lobbyId, 77);
});

test("service vectors pinned for the C decoder", () => {
  const vectors = {
    listed: toHex(P.encListed({ online: 17, lobbies: 3, matches: 1, searching: 2, entries: [{ id: 7, name: "Quick Match", hostName: "Trev", code: "XYZ789", flags: 3, state: 0, numPlayers: 3, maxPlayers: 4, scenario: 1, stage: 2, country: "GB" }] })),
    hosted: toHex(P.encHosted({ lobbyId: 0x0a0b0c0d, token: "f".repeat(32), code: "HJK234" })),
    joininfo: toHex(P.encJoinInfo({ nonce: 5, lobbyId: 77, name: "Lobby", hostName: "Host", cands: [{ ip: 0x01020304, port: 5 }] })),
    joinreq: toHex(P.encJoinReq({ lobbyId: 77, name: "Joiner", cands: [{ ip: 0x09080706, port: 5432 }] })),
    error: toHex(P.encError(P.C.JOIN, 99, P.E.FULL, "That game is full")),
    quickhost: toHex(P.encQuickHost(0x11223344)),
    welcome: toHex(P.encWelcome({ online: 12, lobbies: 4, matches: 2, searching: 3, motd: "Hi" })),
  };
  // These exact strings are pasted into netplay_selftest.c; if the format
  // changes, both sides must change together.
  assert.equal(vectors.hosted, "670d0c0b0a20" + "66".repeat(32) + "06484a4b323334");
  assert.equal(vectors.quickhost, "6b44332211");
  assert.equal(vectors.joinreq, "6c4d00000006 4a6f696e6572 01 06070809 3815".replace(/ /g, ""));
  assert.equal(vectors.error, "6a0563000000041154686174206761 6d652069732066756c6c".replace(/ /g, ""));
  assert.equal(vectors.welcome, "650c0004000200030002 4869".replace(/ /g, ""));
  assert.ok(vectors.listed.startsWith("661100030001000200"));
  assert.ok(vectors.joininfo.startsWith("69050000004d000000"));
});

// The quick-match rules are implemented twice (ndpNormalizePrefs in C for
// the game, normalizePrefs here for the service): the C build prints 64
// pseudo-random inputs (out-of-range ones too) with its results.
test("quick-match preference rules agree with the C build", { skip: !existsSync(fixture) && "no fixture" }, () => {
  const rows = readFileSync(fixture, "utf8").trim().split(/\r?\n/).filter((l) => l.startsWith("prefs "));
  assert.equal(rows.length, 64);
  for (const row of rows) {
    const n = row.replace("prefs ", "").replace("-> ", "").split(" ").map(Number);
    const input = { scenario: n[0], stage: n[1], weapons: n[2], length: n[3], players: n[4] };
    const want = { scenario: n[5], stage: n[6], weapons: n[7], length: n[8], players: n[9] };
    assert.deepEqual(normalizePrefs(input), want, row);
    assert.deepEqual(normalizePrefs(want), want, "idempotent: " + row);
  }
});
