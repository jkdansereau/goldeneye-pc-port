// Preview the status page with made-up sessions:  node test/demo.mjs [port]
// Starts the mock service, seeds a few lobbies / a match / finished
// results through ordinary protocol clients, and keeps running.
import { startMockServer } from "./mock-server.js";
import * as P from "../src/protocol.js";

const BUILD = "demo-build|ntsc-final|x86_64-windows|p2|64bit";
const port = Number(process.argv[2] || 8787);
const { port: p } = await startMockServer(port, { motd: "Demo data" });
const url = `ws://127.0.0.1:${p}/api/v1/ws`;
const ip = (s) => s.split(".").reduce((a, b) => ((a << 8) | Number(b)) >>> 0, 0);

async function client(name) {
  const ws = new WebSocket(url);
  ws.binaryType = "arraybuffer";
  const replies = [];
  ws.onmessage = (ev) => typeof ev.data !== "string" && replies.push(P.decodeService(new Uint8Array(ev.data)));
  await new Promise((r) => (ws.onopen = r));
  ws.send(P.encHello(BUILD, name));
  return { ws, replies, send: (b) => ws.send(b) };
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const lobbies = [
  { host: "Boris", name: "Facility rumble", flags: P.F.PUBLIC, stage: 7, scenario: 0, players: [["Boris", 5], ["Xenia", 3]] },
  { host: "Natalya", name: "Quick Match", flags: P.F.PUBLIC | P.F.QUICK, stage: 0, scenario: 0, players: [["Natalya", 1]] },
  { host: "Trev", name: "Temple LTK", flags: P.F.PUBLIC, stage: 1, scenario: 4, state: P.ST.PLAYING, matchSec: 312, players: [["Trev", 2], ["Jaws", 9], ["Oddjob", 10], ["May Day", 8]] },
  { host: "Q", name: "Friends only", flags: 0, stage: 5, scenario: 5, players: [["Q", 0], ["R", 0], ["M", 0]] },
];
const hosts = [];
for (const l of lobbies) {
  const c = await client(l.host);
  c.send(P.encHost({
    name: l.name, flags: l.flags, maxPlayers: 4, numPlayers: l.players.length, state: l.state || 0, scenario: l.scenario,
    stage: l.stage, matchSec: l.matchSec || 0, cands: [{ ip: ip("203.0.113.9"), port: 27007 }],
    players: l.players.map(([name, character]) => ({ name, character })),
  }));
  hosts.push(c);
}
await sleep(300);
const results = [
  { stage: 2, scenario: 0, durationSec: 600, players: [["Bond", 0, 14, 6], ["Alec", 2, 9, 11], ["Ourumov", 4, 5, 12]] },
  { stage: 11, scenario: 3, durationSec: 241, players: [["Baron", 11, 7, 2], ["Mishkin", 7, 2, 7]] },
];
for (const [i, r] of results.entries()) {
  const h = hosts[i];
  const hosted = h.replies.find((m) => m.type === P.S.HOSTED);
  h.send(P.encResult({ lobbyId: hosted.lobbyId, token: hosted.token, durationSec: r.durationSec, scenario: r.scenario, stage: r.stage,
    players: r.players.map(([name, character, kills, deaths]) => ({ name, character, kills, deaths })) }));
}
console.log(`demo status page: http://127.0.0.1:${p}/`);
