// Local stand-in for the Cloudflare deployment (D414): the same Directory
// core behind the same routes, on plain HTTP/WebSocket, with no
// dependencies. Used by the C integration test (netdir_test) and to preview
// the status page:
//
//   node test/mock-server.js [port]        then open http://127.0.0.1:port/
//
// Not for production: no TLS, no Durable Object hibernation, state in memory.

import { createServer } from "node:http";
import { createHash, randomUUID } from "node:crypto";
import { readFile } from "node:fs/promises";
import { extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";
import { Directory } from "../src/directory.js";
import * as P from "../src/protocol.js";

const PUBLIC_DIR = fileURLToPath(new URL("../public/", import.meta.url));
const WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
const TYPES = { ".html": "text/html; charset=utf-8", ".js": "text/javascript", ".css": "text/css", ".svg": "image/svg+xml" };

// ---------------------------------------------------------------- RFC 6455

class WsConn {
  constructor(socket, head, { onMessage, onClose }) {
    this.socket = socket;
    this.buf = head && head.length ? Buffer.from(head) : Buffer.alloc(0);
    this.frag = null;
    this.fragType = 0;
    this.closed = false;
    this.onMessage = onMessage;
    this.onClose = onClose;
    socket.setNoDelay(true);
    socket.on("data", (d) => {
      this.buf = Buffer.concat([this.buf, d]);
      this.drain();
    });
    socket.on("close", () => this.finish());
    socket.on("error", () => this.finish());
    if (this.buf.length) setImmediate(() => this.drain());
  }

  finish() {
    if (this.closed) return;
    this.closed = true;
    this.onClose();
  }

  drain() {
    for (;;) {
      const b = this.buf;
      if (b.length < 2) return;
      const fin = (b[0] & 0x80) !== 0;
      const op = b[0] & 0x0f;
      const masked = (b[1] & 0x80) !== 0;
      let len = b[1] & 0x7f;
      let off = 2;
      if (len === 126) {
        if (b.length < 4) return;
        len = b.readUInt16BE(2);
        off = 4;
      } else if (len === 127) {
        if (b.length < 10) return;
        len = Number(b.readBigUInt64BE(2));
        off = 10;
      }
      if (len > 1 << 20) return this.close(1009);
      const need = off + (masked ? 4 : 0) + len;
      if (b.length < need) return;
      let payload = b.subarray(off + (masked ? 4 : 0), need);
      if (masked) {
        const mask = b.subarray(off, off + 4);
        payload = Buffer.from(payload);
        for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];
      }
      this.buf = b.subarray(need);
      if (op === 0x8) return this.close(1000);
      if (op === 0x9) {
        this.frame(0xa, payload);
        continue;
      }
      if (op === 0xa) continue;
      if (op === 0x1 || op === 0x2) {
        this.fragType = op;
        this.frag = [payload];
      } else if (op === 0x0 && this.frag) {
        this.frag.push(payload);
      } else {
        return this.close(1002);
      }
      if (fin) {
        const data = Buffer.concat(this.frag);
        const type = this.fragType;
        this.frag = null;
        this.onMessage(type === 0x1 ? data.toString("utf8") : new Uint8Array(data));
      }
    }
  }

  frame(op, payload) {
    if (this.closed) return;
    const data = Buffer.from(payload);
    let header;
    if (data.length < 126) header = Buffer.from([0x80 | op, data.length]);
    else if (data.length < 65536) {
      header = Buffer.alloc(4);
      header[0] = 0x80 | op;
      header[1] = 126;
      header.writeUInt16BE(data.length, 2);
    } else {
      header = Buffer.alloc(10);
      header[0] = 0x80 | op;
      header[1] = 127;
      header.writeBigUInt64BE(BigInt(data.length), 2);
    }
    this.socket.write(Buffer.concat([header, data]));
  }

  send(data) {
    if (typeof data === "string") this.frame(0x1, Buffer.from(data, "utf8"));
    else this.frame(0x2, data);
  }

  close(code = 1000) {
    if (this.closed) return;
    const p = Buffer.alloc(2);
    p.writeUInt16BE(code, 0);
    this.frame(0x8, p);
    this.socket.end();
    this.finish();
  }
}

function upgrade(req, socket) {
  const key = req.headers["sec-websocket-key"];
  if (!key || (req.headers.upgrade || "").toLowerCase() !== "websocket") {
    socket.end("HTTP/1.1 400 Bad Request\r\n\r\n");
    return false;
  }
  const accept = createHash("sha1").update(key + WS_GUID).digest("base64");
  socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
  return true;
}

// ------------------------------------------------------------- the service

const peerIp = (req) => {
  const ip = req.socket.remoteAddress || "";
  return ip.startsWith("::ffff:") ? ip.slice(7) : ip;
};

export function startMockServer(port = 0, opts = {}) {
  const viewers = new Set();
  let timer = null;
  const dir = new Directory({
    motd: opts.motd ?? "Local test service",
    onChange: () => {
      if (timer) return;
      timer = setTimeout(() => {
        timer = null;
        const snap = JSON.stringify(dir.snapshot());
        for (const v of viewers) v.send(snap);
      }, 200);
    },
  });

  const server = createServer(async (req, res) => {
    const url = new URL(req.url, "http://localhost");
    if (url.pathname === "/api/stats") {
      res.writeHead(200, { "Content-Type": "application/json", "Cache-Control": "no-store" });
      res.end(JSON.stringify(dir.snapshot()));
      return;
    }
    if (url.pathname === "/api/v1/poll" && req.method === "POST") {
      const chunks = [];
      for await (const c of req) chunks.push(c);
      const body = new Uint8Array(Buffer.concat(chunks));
      let msgs;
      try {
        msgs = P.unframeBatch(body);
      } catch {
        res.writeHead(400).end("Bad framing");
        return;
      }
      if (msgs.length > 16) {
        res.writeHead(400).end("Too many messages");
        return;
      }
      const s = { id: "http", ip: peerIp(req), country: "", continent: opts.continent ?? "", transport: "http", push: null, hello: false, build: "", name: "", hostedId: 0 };
      const out = [];
      for (const m of msgs) {
        out.push(...dir.handle(s, m));
        if (s.closeAfter) break;
      }
      res.writeHead(200, { "Content-Type": "application/octet-stream" });
      res.end(Buffer.from(P.frameBatch(out)));
      return;
    }
    // static status page
    let path = url.pathname === "/" ? "/index.html" : url.pathname;
    path = normalize(path).replace(/^([/\\])+/, "");
    try {
      const data = await readFile(join(PUBLIC_DIR, path));
      res.writeHead(200, { "Content-Type": TYPES[extname(path)] || "application/octet-stream" });
      res.end(data);
    } catch {
      res.writeHead(404).end("Not found");
    }
  });

  server.on("upgrade", (req, socket, head) => {
    const url = new URL(req.url, "http://localhost");
    if (url.pathname === "/api/v1/ws") {
      if (!upgrade(req, socket)) return;
      const s = { id: randomUUID(), ip: peerIp(req), country: "XX", continent: opts.continent ?? "", transport: "ws", hello: false, build: "", name: "", hostedId: 0 };
      const conn = new WsConn(socket, head, {
        onMessage: (m) => {
          if (typeof m === "string") {
            if (m === "ping") conn.send("pong");
            return;
          }
          for (const r of dir.handle(s, m)) conn.send(r);
          if (s.closeAfter) conn.close(1008);
        },
        onClose: () => dir.sessionClosed(s),
      });
      s.push = (b) => conn.send(b);
    } else if (url.pathname === "/api/live") {
      if (!upgrade(req, socket)) return;
      const conn = new WsConn(socket, head, { onMessage: () => {}, onClose: () => viewers.delete(conn) });
      viewers.add(conn);
      conn.send(JSON.stringify(dir.snapshot()));
    } else {
      socket.end("HTTP/1.1 404 Not Found\r\n\r\n");
    }
  });

  return new Promise((resolve) => {
    server.listen(port, "127.0.0.1", () => resolve({ server, dir, port: server.address().port }));
  });
}

if (process.argv[1] && fileURLToPath(import.meta.url) === normalize(process.argv[1])) {
  const port = Number(process.argv[2] || process.env.PORT || 8787);
  startMockServer(port).then(({ port: p }) => {
    console.log(`GE007 online mock service on http://127.0.0.1:${p}/  (game: --net-service http://127.0.0.1:${p})`);
  });
}
