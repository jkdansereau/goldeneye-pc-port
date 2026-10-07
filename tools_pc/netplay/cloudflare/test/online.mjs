// End-to-end online play through the game's own network runtime (D414).
// Each netonline_test process is one player's runtime, exactly as the game
// runs it: it finds the others through the service (the local mock), learns
// its "public" address from a STUN server (a local fake that reports the
// source address it sees), publishes it, and the players connect to each
// other directly over UDP -- host punching towards the joiner included.
//
//   NETONLINE_TEST=path/to/netonline_test node test/online.mjs
//
// What this cannot show: traversal of real home routers (everything here is
// one machine). That part is the STUN codec + hole punching logic, covered
// by the C selftest's simulated network and, finally, by people playing.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import dgram from "node:dgram";
import { startMockServer } from "./mock-server.js";
import * as P from "../src/protocol.js";

const BIN = process.env.NETONLINE_TEST || process.argv[2];
if (!BIN) {
  console.error("set NETONLINE_TEST to the netonline_test binary");
  process.exit(2);
}
const BUILD = "netonline-test-build|ntsc-final|x86_64-windows|p2|64bit";
const ipNum = (s) => s.split(".").reduce((a, b) => ((a << 8) | Number(b)) >>> 0, 0);

// A STUN server (RFC 5389 Binding only) that answers with the address the
// request came from -- what a real one on the internet does.
function startFakeStun() {
  return new Promise((resolve) => {
    const sock = dgram.createSocket("udp4");
    const seen = new Set();
    sock.on("message", (msg, rinfo) => {
      if (msg.length < 20 || msg.readUInt16BE(0) !== 0x0001 || msg.readUInt32BE(4) !== 0x2112a442) return;
      seen.add(rinfo.port);
      const res = Buffer.alloc(32);
      res.writeUInt16BE(0x0101, 0); // Binding success
      res.writeUInt16BE(12, 2);
      res.writeUInt32BE(0x2112a442, 4);
      msg.copy(res, 8, 8, 20); // transaction id
      res.writeUInt16BE(0x0020, 20); // XOR-MAPPED-ADDRESS
      res.writeUInt16BE(8, 22);
      res.writeUInt8(0, 24);
      res.writeUInt8(1, 25); // IPv4
      res.writeUInt16BE(rinfo.port ^ 0x2112, 26);
      res.writeUInt32BE((ipNum(rinfo.address) ^ 0x2112a442) >>> 0, 28);
      sock.send(res, rinfo.port, rinfo.address);
    });
    sock.bind(0, "127.0.0.1", () => resolve({ addr: `127.0.0.1:${sock.address().port}`, seen, close: () => sock.close() }));
  });
}

function runC(args, onLine) {
  return new Promise((resolve, reject) => {
    const p = spawn(BIN, args, { stdio: ["ignore", "pipe", "pipe"] });
    const lines = [];
    let buf = "";
    let err = "";
    p.stdout.on("data", (d) => {
      buf += d.toString();
      let i;
      while ((i = buf.indexOf("\n")) >= 0) {
        const line = buf.slice(0, i).trim();
        buf = buf.slice(i + 1);
        if (!line) continue;
        lines.push(line);
        if (onLine) onLine(line, p);
      }
    });
    p.stderr.on("data", (d) => {
      err += d.toString();
      if (process.env.NETONLINE_VERBOSE) process.stderr.write("    [c] " + d);
    });
    const timer = setTimeout(() => {
      p.kill();
      reject(new Error(`timeout: ${args.join(" ")}\n${lines.join("\n")}\n${err}`));
    }, 60000);
    p.on("exit", (code) => {
      clearTimeout(timer);
      resolve({ code, lines, err });
    });
  });
}

const show = (r) => [...r.lines, "--- stderr ---", r.err].join("\n");

// A scripted service client (to look at what a host published).
async function wsPeek(url, msg) {
  const ws = new WebSocket(url.replace(/^http/, "ws") + "/api/v1/ws");
  ws.binaryType = "arraybuffer";
  const got = [];
  ws.onmessage = (ev) => typeof ev.data !== "string" && got.push(P.decodeService(new Uint8Array(ev.data)));
  await new Promise((res, rej) => {
    ws.onopen = res;
    ws.onerror = rej;
  });
  ws.send(P.encHello(BUILD, "Observer"));
  ws.send(msg);
  for (let i = 0; i < 200 && got.filter((m) => m.type !== P.S.WELCOME).length === 0; i++) {
    await new Promise((r) => setTimeout(r, 25));
  }
  ws.close();
  return got.find((m) => m.type !== P.S.WELCOME);
}

async function hostThenJoin(url, stun, { poll = false, priv = false } = {}) {
  const extra = [...(poll ? ["--poll"] : []), ...(priv ? ["--private"] : [])];
  let joiner;
  let published;
  const host = await runC([url, stun.addr, "Bond", "host", ...extra], (line) => {
    if (!line.startsWith("CODE ")) return;
    const [, code, vis] = line.split(" ");
    assert.equal(vis, priv ? "private" : "public");
    joiner = (async () => {
      if (!poll) {
        // what the service hands joiners: our STUN-mapped address first
        const info = await wsPeek(url, P.encJoin({ code, nonce: 99, cands: [{ ip: ipNum("127.0.0.1"), port: 50998 }] }));
        published = info?.cands;
      }
      return runC([url, stun.addr, "Alec", "join", code.toLowerCase(), ...(poll ? ["--poll"] : [])]);
    })();
  });
  const j = await joiner;
  assert.equal(host.code, 0, show(host));
  assert.ok(host.lines.includes("JOINED Alec"), show(host));
  assert.ok(host.lines.includes("DONE"), show(host));
  assert.equal(j.code, 0, show(j));
  assert.ok(j.lines.some((l) => l.startsWith("INLOBBY 2 ")), show(j));
  if (published) {
    assert.ok(published.length >= 1, "host published addresses");
    assert.equal(published[0].ip, ipNum("127.0.0.1"), "first address = the STUN-mapped one");
    assert.ok(published[0].port > 0);
  }
  const tag = `${poll ? "HTTPS polling" : "WebSocket"}, ${priv ? "private" : "public"}`;
  console.log(`  ok: host + join by code (${tag}): connected directly${published ? ", STUN address published" : ""}`);
}

async function quickPair(url, stun) {
  let second;
  const first = await runC([url, stun.addr, "Natalya", "quick"], (line) => {
    if (line.startsWith("QUICKHOST ")) second = runC([url, stun.addr, "Boris", "quick"]);
  });
  const b = await second;
  assert.equal(first.code, 0, show(first));
  assert.ok(first.lines.some((l) => l.startsWith("QUICKHOST ")), show(first));
  assert.ok(first.lines.includes("JOINED Boris"), show(first));
  assert.equal(b.code, 0, show(b));
  assert.ok(b.lines.some((l) => l.startsWith("INLOBBY 2 ")), show(b));
  console.log("  ok: quick match: first player hosts, second joins them");
}

// Both press Quick Match at the same moment: both get told to host; the
// newer lobby's player must end up in the older lobby.
async function quickRace(url, stun) {
  const [a, b] = await Promise.all([
    runC([url, stun.addr, "Xenia", "quick"]),
    runC([url, stun.addr, "Ourumov", "quick"]),
  ]);
  const both = [a, b];
  for (const r of both) assert.equal(r.code, 0, show(r));
  const hosted = both.filter((r) => r.lines.includes("DONE"));
  const joined = both.filter((r) => r.lines.some((l) => l.startsWith("INLOBBY 2 ")));
  assert.equal(hosted.length, 1, both.map(show).join("\n====\n"));
  assert.equal(joined.length, 1, both.map(show).join("\n====\n"));
  const raced = both.every((r) => r.lines.some((l) => l.startsWith("QUICKHOST ")));
  console.log(`  ok: simultaneous quick match: one game, two players${raced ? " (both had hosted; merged)" : ""}`);
}

// A quick-match game nobody can reach (its addresses go nowhere -- think of
// a host behind a router that cannot be hole-punched). The first searcher is
// sent there, gives up quietly after a few seconds and hosts instead (the
// service hears which game failed); the next searcher is then sent to that
// reachable game, not to the dead one, although the dead one is older.
async function unreachableGame(url, stun, dir) {
  // where the ghost "is": a socket that swallows everything and never answers
  const hole = dgram.createSocket("udp4");
  hole.on("message", () => {});
  await new Promise((res) => hole.bind(0, "127.0.0.1", res));
  const ghost = new WebSocket(url.replace(/^http/, "ws") + "/api/v1/ws");
  ghost.binaryType = "arraybuffer";
  const ghostIn = [];
  ghost.onmessage = (ev) => typeof ev.data !== "string" && ghostIn.push(P.decodeService(new Uint8Array(ev.data)));
  await new Promise((res, rej) => {
    ghost.onopen = res;
    ghost.onerror = rej;
  });
  ghost.send(P.encHello(BUILD, "Ghost"));
  ghost.send(P.encHost({
    name: "Unreachable", flags: P.F.PUBLIC | P.F.QUICK, maxPlayers: 4, numPlayers: 1,
    cands: [{ ip: ipNum("127.0.0.1"), port: hole.address().port }], players: [{ name: "Ghost", character: 0 }],
  }));
  for (let i = 0; i < 100 && !ghostIn.some((m) => m.type === P.S.HOSTED); i++) await new Promise((r) => setTimeout(r, 20));
  const ghostId = ghostIn.find((m) => m.type === P.S.HOSTED).lobbyId;

  let second;
  const t0 = Date.now();
  const first = await runC([url, stun.addr, "Mishkin", "quick"], (line) => {
    if (line.startsWith("QUICKHOST ")) second = runC([url, stun.addr, "Jaws", "quick"]);
  });
  const b = await second;
  ghost.close();
  hole.close();
  assert.equal(first.code, 0, show(first));
  assert.ok(first.lines.some((l) => l.startsWith("QUICKHOST ")), "fell back to hosting: " + show(first));
  assert.ok(!first.lines.some((l) => l.startsWith("FAILED")), "no error shown while retrying: " + show(first));
  assert.ok(first.lines.includes("JOINED Jaws"), show(first));
  assert.equal(b.code, 0, show(b));
  assert.ok(b.lines.some((l) => l.startsWith("INLOBBY 2 ")), "second searcher joined the reachable game: " + show(b));
  console.log(`  ok: unreachable quick game: skipped quietly, searcher hosted, next searcher sent there (${Math.round((Date.now() - t0) / 1000)} s)`);
  return ghostId;
}

function deferred() {
  let resolve;
  const promise = new Promise((r) => (resolve = r));
  return { promise, resolve };
}

// The QUICKHOST line of a searcher that had to host -- or a failure naming
// what it did instead (e.g. joined a game it should not fit).
function hostLine(proc, d, what) {
  let settled = false;
  return Promise.race([
    d.promise.then((l) => ((settled = true), l)),
    proc.then((r) => {
      if (settled) return null;
      throw new Error(`${what} did not host its own game:\n${show(r)}`);
    }),
  ]);
}

// QUICKHOST <code> <vis> <scenario> <stage> <weapons> <max>
const hostFields = (line) => {
  const f = line.split(" ");
  return { scenario: Number(f[3]), stage: Number(f[4]), weapons: Number(f[5]), max: Number(f[6]) };
};

// Quick-match preferences (D416), through the real runtimes. A Golden Gun
// searcher finds nothing and hosts a Golden Gun game (golden gun weapons); a
// Normal-only searcher does not join it but hosts its own; the next Golden
// Gun searcher is sent to the first game, the next Normal one to the second.
async function prefsMatchmaking(url, stun) {
  const ggUp = deferred();
  const nmUp = deferred();
  const gg = runC([url, stun.addr, "Scaramanga", "quick", "--mode", "3"], (l) => l.startsWith("QUICKHOST ") && ggUp.resolve(l));
  const ggLine = await hostLine(gg, ggUp, "the first Golden Gun searcher");
  const nm = runC([url, stun.addr, "Valentin", "quick", "--mode", "0"], (l) => l.startsWith("QUICKHOST ") && nmUp.resolve(l));
  const nmLine = await hostLine(nm, nmUp, "the Normal-only searcher (a Golden Gun game was open)");
  const ggJoin = await runC([url, stun.addr, "Kananga", "quick", "--mode", "3"]);
  const nmJoin = await runC([url, stun.addr, "Zukovsky", "quick", "--mode", "0"]);
  const [ggHost, nmHost] = await Promise.all([gg, nm]);
  const g = hostFields(ggLine);
  const n = hostFields(nmLine);
  assert.equal(g.scenario, 3, ggLine);
  assert.equal(g.weapons, 13, "a Golden Gun game plays with the golden gun: " + ggLine);
  assert.equal(n.scenario, 0, nmLine);
  assert.ok(ggHost.lines.includes("JOINED Kananga"), "the second Golden Gun searcher joined the Golden Gun game:\n" + show(ggHost));
  assert.ok(nmHost.lines.includes("JOINED Zukovsky"), "the second Normal searcher joined the Normal game:\n" + show(nmHost));
  for (const r of [ggJoin, nmJoin]) assert.ok(r.code === 0 && r.lines.some((l) => l.startsWith("INLOBBY 2 ")), show(r));
  console.log("  ok: preferences: Golden Gun and Normal searchers each get their own game, and the next ones sort into the right one");
}

// Team modes fix the size: a 2-vs-1 search hosts a game for exactly 3 and
// drops a map too small for it (Egyptian holds 2).
async function teamSize(url, stun) {
  const up = deferred();
  const host = runC([url, stun.addr, "Mayday", "quick", "--mode", "7", "--stage", "11"], (l) => l.startsWith("QUICKHOST ") && up.resolve(l));
  const line = await hostLine(host, up, "the 2 vs 1 searcher");
  const joiner = await runC([url, stun.addr, "Oddjob", "quick", "--mode", "7"]);
  const h = await host;
  const f = hostFields(line);
  assert.equal(f.scenario, 7, line);
  assert.equal(f.max, 3, "2 vs 1 is a game for 3: " + line);
  assert.equal(f.stage, 0, "Egyptian (2 players) dropped for 2 vs 1 -- a random fitting map: " + line);
  assert.ok(h.lines.includes("JOINED Oddjob"), show(h));
  assert.ok(joiner.code === 0 && joiner.lines.some((l) => l.startsWith("INLOBBY 2 ")), show(joiner));
  console.log("  ok: team quick match: 2 vs 1 hosts for exactly 3 on a map that holds 3, and fills");
}

async function badCode(url, stun) {
  const r = await runC([url, stun.addr, "Jaws", "join", "ZZZZZZ"]);
  assert.equal(r.code, 1, show(r));
  assert.ok(r.lines.some((l) => l === "FAILED No game with that code"), show(r));
  console.log("  ok: unknown code -> a clear failure");
}

async function main() {
  const { server, dir, port } = await startMockServer(0, { motd: "online e2e" });
  const stun = await startFakeStun();
  const url = `http://127.0.0.1:${port}`;
  try {
    await hostThenJoin(url, stun);
    await hostThenJoin(url, stun, { poll: true });
    await hostThenJoin(url, stun, { priv: true });
    await badCode(url, stun);
    await quickPair(url, stun);
    await quickRace(url, stun);
    await prefsMatchmaking(url, stun);
    await teamSize(url, stun);
    await unreachableGame(url, stun, dir);
    assert.ok(stun.seen.size >= 4, `STUN asked from ${stun.seen.size} sockets`);
    await new Promise((r) => setTimeout(r, 300)); // last connection closes
    const snap = dir.snapshot();
    // Every host here reports a result once someone joins, but no match was
    // ever played: the service only takes results for matches it saw (D416;
    // real results and anonymisation are covered by test/directory.test.js).
    assert.equal(snap.today.matches, 0, `results for matches nobody played were accepted: ${snap.today.matches}`);
    assert.equal(snap.sessions.length, 0, "every lobby gone once its host left");
    console.log("  ok: results for matches nobody played are refused; no lobbies left behind");
    console.log("ONLINE E2E PASSED");
  } finally {
    stun.close();
    server.close();
    server.closeAllConnections?.();
  }
}

main().catch((e) => {
  console.error("ONLINE E2E FAILED:", e);
  process.exit(1);
});
