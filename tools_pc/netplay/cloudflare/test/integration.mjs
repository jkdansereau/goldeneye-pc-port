// End-to-end: the game's C directory client (netdir_test, built from
// tools_pc/netplay) against the service core running in the local mock
// server, over real sockets, in both transports (WebSocket and HTTPS-style
// polling). The other side of each exchange is a scripted client here.
//
//   NETDIR_TEST=path/to/netdir_test node test/integration.mjs
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { startMockServer } from "./mock-server.js";
import * as P from "../src/protocol.js";

const BIN = process.env.NETDIR_TEST || process.argv[2];
if (!BIN) {
  console.error("set NETDIR_TEST to the netdir_test binary");
  process.exit(2);
}
const BUILD = "netdir-test-build|ntsc-final|x86_64-windows|p2|64bit";

function runC(args, onLine) {
  return new Promise((resolve, reject) => {
    const p = spawn(BIN, args, { stdio: ["ignore", "pipe", "pipe"] });
    const lines = [];
    let buf = "";
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
    p.stderr.on("data", (d) => process.stderr.write("    [c] " + d));
    const timer = setTimeout(() => {
      p.kill();
      reject(new Error("timeout; output so far:\n" + lines.join("\n")));
    }, 60000);
    p.on("exit", (code) => {
      clearTimeout(timer);
      resolve({ code, lines });
    });
  });
}

// A scripted service client over the built-in WebSocket.
async function wsClient(url, name) {
  const ws = new WebSocket(url.replace(/^http/, "ws") + "/api/v1/ws");
  ws.binaryType = "arraybuffer";
  const inbox = [];
  const waiters = [];
  ws.onmessage = (ev) => {
    if (typeof ev.data === "string") return;
    const m = P.decodeService(new Uint8Array(ev.data));
    const w = waiters.findIndex((x) => x.pred(m));
    if (w >= 0) waiters.splice(w, 1)[0].resolve(m);
    else inbox.push(m);
  };
  await new Promise((res, rej) => {
    ws.onopen = res;
    ws.onerror = rej;
  });
  const next = (pred, ms = 15000) => {
    const i = inbox.findIndex(pred);
    if (i >= 0) return Promise.resolve(inbox.splice(i, 1)[0]);
    return new Promise((resolve, reject) => {
      const entry = { pred, resolve };
      waiters.push(entry);
      setTimeout(() => {
        const k = waiters.indexOf(entry);
        if (k >= 0) {
          waiters.splice(k, 1);
          reject(new Error("no message in time"));
        }
      }, ms);
    });
  };
  ws.send(P.encHello(BUILD, name));
  await next((m) => m.type === P.S.WELCOME);
  return { ws, next, send: (b) => ws.send(b), close: () => ws.close() };
}

const ip = (s) => s.split(".").reduce((a, b) => ((a << 8) | Number(b)) >>> 0, 0);

async function hostedByC(url, poll) {
  const tag = poll ? "poll" : "ws";
  let joiner = null;
  let lobbyCode = null;
  const res = await runC([url, "host", ...(poll ? ["--poll"] : [])], async (line) => {
    if (line.startsWith("HOSTED ")) {
      const [, , code, transport] = line.split(" ");
      assert.equal(transport, tag);
      lobbyCode = code;
      joiner = await wsClient(url, "JoinerNode");
      joiner.send(P.encJoin({ code, nonce: 4242, cands: [{ ip: ip("198.51.100.7"), port: 40000 }] }));
      const info = await joiner.next((m) => m.type === P.S.JOININFO);
      assert.equal(info.nonce, 4242);
      assert.equal(info.hostName, "HostBond");
      assert.deepEqual(info.cands, [{ ip: ip("203.0.113.5"), port: 51000 }, { ip: ip("192.168.1.20"), port: 27007 }]);
    }
  });
  joiner?.close();
  assert.equal(res.code, 0, res.lines.join("\n"));
  assert.ok(lobbyCode, "lobby code printed");
  assert.ok(res.lines.includes("JOINREQ JoinerNode 1 198.51.100.7:40000"), res.lines.join("\n"));
  assert.ok(res.lines.includes("DONE"));
  console.log(`  ok: C host (${tag}) registered, was told about the joiner, reported, unregistered`);
}

async function joinedByC(url, poll) {
  const host = await wsClient(url, "NodeHost");
  host.send(P.encHost({ name: "Node lobby", flags: P.F.PUBLIC, maxPlayers: 4, numPlayers: 1, stage: 3, cands: [{ ip: ip("203.0.113.77"), port: 52000 }], players: [{ name: "NodeHost", character: 4 }] }));
  const hosted = await host.next((m) => m.type === P.S.HOSTED);
  const res = await runC([url, "join", hosted.code.toLowerCase(), ...(poll ? ["--poll"] : [])]);
  assert.equal(res.code, 0, res.lines.join("\n"));
  const line = res.lines.find((l) => l.startsWith("JOININFO"));
  assert.ok(line, res.lines.join("\n"));
  assert.equal(line, `JOININFO ${hosted.lobbyId} NodeHost 1 203.0.113.77:52000 nonce-ok`);
  const req = await host.next((m) => m.type === P.S.JOINREQ);
  assert.equal(req.name, "JoinerAlec");
  assert.deepEqual(req.cands.map((c) => c.port), [51000, 27007]);

  const listed = await runC([url, "list", ...(poll ? ["--poll"] : [])]);
  assert.ok(listed.lines.some((l) => l.startsWith("ENTRY " + hosted.lobbyId + " " + hosted.code + " NodeHost XX 1/4")), listed.lines.join("\n"));
  host.close();
  console.log(`  ok: C joiner (${poll ? "poll" : "ws"}) got the host's addresses; list shows the lobby`);
}

async function main() {
  const { server, dir, port } = await startMockServer(0, { motd: "integration" });
  const url = `http://127.0.0.1:${port}`;
  try {
    const q = await runC([url, "quick"]);
    assert.equal(q.code, 0, q.lines.join("\n"));
    assert.ok(q.lines.includes("QUICKHOST nonce-ok"), q.lines.join("\n"));
    console.log("  ok: quick match with nobody around -> host one");

    const bad = await runC([url, "join", "ZZZZZZ"]);
    assert.ok(bad.lines.some((l) => l.startsWith("ERROR 3 ")), bad.lines.join("\n"));
    console.log("  ok: unknown code -> NOT_FOUND");

    await hostedByC(url, false);
    await joinedByC(url, false);
    await hostedByC(url, true);
    await joinedByC(url, true);

    const snap = dir.snapshot();
    assert.equal(snap.today.matches, 2, "both C hosts reported their match");
    assert.equal(snap.recent[0].players[0].name, "HostBond");
    const stats = await (await fetch(url + "/api/stats")).json();
    assert.equal(stats.today.matches, 2);
    console.log("  ok: results reached the status page data");
    console.log("INTEGRATION PASSED");
  } finally {
    server.close();
    server.closeAllConnections?.();
  }
}

main().catch((e) => {
  console.error("INTEGRATION FAILED:", e);
  process.exit(1);
});
