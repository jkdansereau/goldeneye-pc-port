// GoldenEye 007 PC port -- online service (D414-D416). A Cloudflare Worker plus
// one Durable Object: the game's lobby directory and matchmaker, and the
// public status page (public/). Designed for the Workers free plan:
//
//   - game clients hold a hibernatable WebSocket (idle connections cost
//     nothing; "ping" is answered by the runtime without waking the object);
//   - everything a lobby needs survives hibernation in its host's socket
//     attachment -- storage is written only for finished matches (and once
//     for the token-signing secret);
//   - the status page is static assets (free) plus one live WebSocket per
//     viewer that the object pushes snapshots down (outgoing messages are
//     free);
//   - gameplay never comes here: hosts and joiners connect directly (UDP).
//
// Routes: /api/v1/ws (game, WebSocket), /api/v1/poll (game, HTTPS fallback),
// /api/live (status page, WebSocket), /api/stats (JSON). Anything else is a
// static asset.
//
// Hardening (D416), on top of the directory's own (src/directory.js):
//   - the edge turns away an address making too many API requests or
//     connections (Workers rate-limit bindings) before it costs a Durable
//     Object request;
//   - per-address caps on game and viewer sockets (IPv6 per /64);
//   - every game socket has a message budget (token bucket) -- flooding it
//     closes it; so does repeated garbage (the directory counts);
//   - a handler that throws closes that one socket (1011), never the object;
//   - attachments are size-checked (2 KiB limit), batches capped, the stats
//     JSON cached for a second.

import { DurableObject } from "cloudflare:workers";
import { Directory, RECENT_MAX, dayKey } from "./directory.js";
import { ipKey } from "./netaddr.js";
import * as P from "./protocol.js";
import { toHex } from "./sha256.js";

const MAX_CLIENTS = 4000;
const MAX_CLIENTS_PER_ADDR = 8;
const MAX_VIEWERS = 1000;
const MAX_VIEWERS_PER_ADDR = 4;
const MAX_POLL_BODY = 8192;
const MAX_BATCH_MSGS = 16;
const MSG_RATE = 10;          // sustained messages per second per game socket
const MSG_BURST = 40;
const BROADCAST_MIN_MS = 1000;
const STATS_CACHE_MS = 1000;
const KEEP_MATCHES = 500;

const json = (body, status = 200) =>
  new Response(typeof body === "string" ? body : JSON.stringify(body), {
    status,
    headers: {
      "Content-Type": "application/json; charset=utf-8",
      "Cache-Control": "no-store",
      "Access-Control-Allow-Origin": "*",
      "X-Content-Type-Options": "nosniff",
    },
  });

const isUpgrade = (request) => (request.headers.get("Upgrade") || "").toLowerCase() === "websocket";
const clientIp = (request) => {
  const ip = request.headers.get("CF-Connecting-IP") || "";
  return ip.startsWith("::ffff:") ? ip.slice(7) : ip;
};
const tooMany = (what) => new Response(what, { status: 429, headers: { "Retry-After": "60" } });

export class DirectoryDO extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.sessions = new Map();   // server-side WebSocket -> session
    this.lastBroadcast = 0;
    this.broadcastTimer = null;
    this.statsCache = null;
    this.statsAt = 0;
    this.dir = new Directory({
      motd: (env.MOTD || "").slice(0, P.LIM.MOTD),
      onChange: () => {
        this.statsCache = null;
        this.scheduleBroadcast();
      },
      recordMatch: (match, today) => this.persistMatch(match, today),
    });
    // Answered by the runtime itself: keepalives never wake the object.
    ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair("ping", "pong"));
    ctx.blockConcurrencyWhile(async () => {
      this.loadState();
      // Re-adopt the lobbies whose hosts are still connected (the object may
      // have hibernated: memory is gone, the sockets and attachments are not).
      for (const ws of ctx.getWebSockets("client")) {
        const s = this.session(ws);
        const att = ws.deserializeAttachment() || {};
        if (att.lobby) this.dir.adopt(att.lobby, s);
        else if (s.hello) this.dir.browsing.add(s);
      }
    });
  }

  // ---------------------------------------------------------------- storage

  loadState() {
    const sql = this.ctx.storage.sql;
    sql.exec("CREATE TABLE IF NOT EXISTS matches (id INTEGER PRIMARY KEY AUTOINCREMENT, ended INTEGER NOT NULL, data TEXT NOT NULL)");
    sql.exec("CREATE TABLE IF NOT EXISTS daily (day TEXT PRIMARY KEY, matches INTEGER NOT NULL, players INTEGER NOT NULL, peak INTEGER NOT NULL)");
    sql.exec("CREATE TABLE IF NOT EXISTS meta (k TEXT PRIMARY KEY, v TEXT NOT NULL)");
    // The lobby-token signing key: made once, kept forever, so tokens issued
    // before a restart still verify after it.
    let secret = sql.exec("SELECT v FROM meta WHERE k = 'secret'").toArray()[0]?.v;
    if (!secret || !/^[0-9a-f]{64}$/.test(secret)) {
      secret = toHex(crypto.getRandomValues(new Uint8Array(32)));
      sql.exec("INSERT OR REPLACE INTO meta (k, v) VALUES ('secret', ?)", secret);
    }
    this.dir.secret = Uint8Array.from(secret.match(/../g), (h) => parseInt(h, 16));
    this.dir.recent = sql.exec("SELECT data FROM matches ORDER BY id DESC LIMIT ?", RECENT_MAX)
      .toArray().map((r) => JSON.parse(r.data));
    const day = dayKey(Date.now());
    const d = sql.exec("SELECT matches, players, peak FROM daily WHERE day = ?", day).toArray()[0];
    if (d) this.dir.today = { day, matches: d.matches, players: d.players, peak: d.peak };
  }

  persistMatch(match, today) {
    try {
      const sql = this.ctx.storage.sql;
      sql.exec("INSERT INTO matches (ended, data) VALUES (?, ?)", match.ended, JSON.stringify(match));
      sql.exec(
        "INSERT INTO daily (day, matches, players, peak) VALUES (?, ?, ?, ?) " +
          "ON CONFLICT(day) DO UPDATE SET matches = excluded.matches, players = excluded.players, " +
          "peak = MAX(daily.peak, excluded.peak)",
        today.day, today.matches, today.players, today.peak,
      );
      if (today.matches % 50 === 0) {
        sql.exec("DELETE FROM matches WHERE id <= (SELECT MAX(id) FROM matches) - ?", KEEP_MATCHES);
      }
    } catch (e) {
      console.error("persistMatch failed:", e && e.message);   // the page keeps it in memory anyway
    }
  }

  // --------------------------------------------------------------- sessions

  session(ws) {
    let s = this.sessions.get(ws);
    if (!s) {
      const att = ws.deserializeAttachment() || {};
      s = {
        id: att.sid || "",
        ip: att.ip || "",
        country: att.country || "",
        continent: att.continent || "",
        transport: "ws",
        viewer: !!att.viewer,
        hello: !!att.hello,
        build: att.build || "",
        name: att.name || "",
        hostedId: att.hostedId || 0,
        closeAfter: false,
        tokens: MSG_BURST,
        tokensAt: Date.now(),
        push: (bytes) => ws.send(bytes),
      };
      s.key = ipKey(s.ip);
      this.sessions.set(ws, s);
    }
    return s;
  }

  saveSession(ws, s) {
    const l = s.hostedId ? this.dir.lobbies.get(s.hostedId) : null;
    const base = {
      sid: s.id, ip: s.ip, country: s.country, continent: s.continent, hello: s.hello, build: s.build, name: s.name,
      hostedId: l ? s.hostedId : 0,
    };
    try {
      ws.serializeAttachment({ ...base, lobby: l ? Directory.serialize(l) : null });
    } catch {
      // Too big for an attachment (2 KiB): keep the session; the lobby is
      // re-registered by its host's next refresh (signed token: same id/code).
      try {
        ws.serializeAttachment(base);
      } catch {
        /* the socket is closing */
      }
    }
  }

  // Token bucket: MSG_RATE per second, bursts up to MSG_BURST.
  spend(s) {
    const now = Date.now();
    s.tokens = Math.min(MSG_BURST, s.tokens + ((now - s.tokensAt) / 1000) * MSG_RATE);
    s.tokensAt = now;
    if (s.tokens < 1) return false;
    s.tokens -= 1;
    return true;
  }

  // ------------------------------------------------------------------ fetch

  async fetch(request) {
    const url = new URL(request.url);
    try {
      switch (url.pathname) {
        case "/api/v1/ws": return this.acceptClient(request);
        case "/api/v1/poll": return await this.httpPoll(request);
        case "/api/live": return this.acceptViewer(request);
        case "/api/stats": return json(this.stats());
        default: return new Response("Not found", { status: 404 });
      }
    } catch (e) {
      console.error("fetch failed:", url.pathname, e && e.message);
      return new Response("Internal error", { status: 500 });
    }
  }

  stats() {
    const now = Date.now();
    if (!this.statsCache || now - this.statsAt > STATS_CACHE_MS) {
      this.statsCache = JSON.stringify(this.dir.snapshot());
      this.statsAt = now;
    }
    return this.statsCache;
  }

  acceptClient(request) {
    const clients = this.ctx.getWebSockets("client");
    if (clients.length >= MAX_CLIENTS) return new Response("The online service is busy", { status: 503 });
    const ip = clientIp(request);
    const key = ipKey(ip);
    let fromHere = 0;
    for (const ws of clients) if (this.session(ws).key === key) fromHere++;
    if (fromHere >= MAX_CLIENTS_PER_ADDR) return tooMany("Too many connections");

    const [client, server] = Object.values(new WebSocketPair());
    this.ctx.acceptWebSocket(server, ["client"]);
    server.serializeAttachment({
      sid: crypto.randomUUID(), ip, country: request.cf?.country || "", continent: request.cf?.continent || "", hello: false,
    });
    this.session(server);
    return new Response(null, { status: 101, webSocket: client });
  }

  acceptViewer(request) {
    const viewers = this.ctx.getWebSockets("viewer");
    if (viewers.length >= MAX_VIEWERS) return new Response("Busy", { status: 503 });
    const key = ipKey(clientIp(request));
    let fromHere = 0;
    for (const v of viewers) if ((v.deserializeAttachment() || {}).key === key) fromHere++;
    if (fromHere >= MAX_VIEWERS_PER_ADDR) return tooMany("Too many viewers from your address");
    const [client, server] = Object.values(new WebSocketPair());
    this.ctx.acceptWebSocket(server, ["viewer"]);
    server.serializeAttachment({ viewer: true, key });
    server.send(this.stats());
    return new Response(null, { status: 101, webSocket: client });
  }

  async httpPoll(request) {
    if (request.method !== "POST") return new Response("POST only", { status: 405 });
    const declared = Number(request.headers.get("Content-Length") || 0);
    if (declared > MAX_POLL_BODY) return new Response("Bad size", { status: 413 });
    const body = new Uint8Array(await request.arrayBuffer());
    if (body.length === 0 || body.length > MAX_POLL_BODY) return new Response("Bad size", { status: 413 });
    let msgs;
    try {
      msgs = P.unframeBatch(body);
    } catch {
      return new Response("Bad framing", { status: 400 });
    }
    if (msgs.length > MAX_BATCH_MSGS) return new Response("Too many messages", { status: 400 });
    // Stateless: HELLO comes first in every batch; a polling host's join
    // requests wait in its lobby until its next HOST / POLL.
    const ip = clientIp(request);
    const s = {
      id: "http", ip, key: ipKey(ip), country: request.cf?.country || "", continent: request.cf?.continent || "",
      transport: "http", push: null, hello: false, build: "", name: "", hostedId: 0,
    };
    const out = [];
    for (const m of msgs) {
      out.push(...this.dir.handle(s, m));
      if (s.closeAfter) break;
    }
    return new Response(P.frameBatch(out), {
      headers: { "Content-Type": "application/octet-stream", "Cache-Control": "no-store" },
    });
  }

  // -------------------------------------------------------------- websocket

  async webSocketMessage(ws, message) {
    if (typeof message === "string") return;   // keepalives are auto-answered
    const s = this.session(ws);
    if (s.viewer) return;
    if (message.byteLength > P.MAX_MSG) {
      ws.close(1009, "Message too big");
      return;
    }
    if (!this.spend(s)) {
      ws.close(1008, "Too many messages");
      return;
    }
    try {
      const replies = this.dir.handle(s, new Uint8Array(message));
      for (const r of replies) ws.send(r);
      this.saveSession(ws, s);
      if (s.closeAfter) ws.close(1008, "Closing");
    } catch (e) {
      console.error("message handler failed:", e && e.message);
      try {
        ws.close(1011, "Internal error");
      } catch {
        /* already closing */
      }
    }
  }

  async webSocketClose(ws) {
    this.drop(ws);
  }

  async webSocketError(ws) {
    this.drop(ws);
  }

  drop(ws) {
    try {
      const s = this.session(ws);
      if (!s.viewer) this.dir.sessionClosed(s);
    } finally {
      this.sessions.delete(ws);
    }
  }

  // ------------------------------------------------------- status page feed

  scheduleBroadcast() {
    if (this.broadcastTimer) return;
    const wait = Math.max(0, BROADCAST_MIN_MS - (Date.now() - this.lastBroadcast));
    this.broadcastTimer = setTimeout(() => {
      this.broadcastTimer = null;
      this.lastBroadcast = Date.now();
      const viewers = this.ctx.getWebSockets("viewer");
      if (!viewers.length) return;
      let snap;
      try {
        snap = this.stats();
      } catch (e) {
        console.error("snapshot failed:", e && e.message);
        return;
      }
      for (const v of viewers) {
        try {
          v.send(snap);
        } catch {
          /* closing */
        }
      }
    }, wait);
  }
}

// The edge: obvious junk and over-eager addresses are turned away here,
// before they cost a Durable Object request. The rate limiters are optional
// bindings (wrangler.jsonc "ratelimits"): without them, everything passes.
async function edgeAllowed(limiter, key) {
  if (!limiter) return true;
  try {
    const { success } = await limiter.limit({ key });
    return success;
  } catch {
    return true;   // fail open: the directory's own limits still apply
  }
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    // HTTPS only (D416): players' names and addresses never cross the
    // internet in the clear, and nobody on the path can rewrite the page.
    // The game refuses http:// itself; this covers everything else.
    // (`wrangler dev` on this machine stays plain http.)
    if (url.protocol === "http:" && !["localhost", "127.0.0.1", "[::1]"].includes(url.hostname)) {
      if (url.pathname.startsWith("/api/v1/") || (request.method !== "GET" && request.method !== "HEAD")) {
        return new Response("HTTPS required", { status: 403 });
      }
      url.protocol = "https:";
      return Response.redirect(url.toString(), 301);
    }
    if (!url.pathname.startsWith("/api/")) return env.ASSETS.fetch(request);

    const key = ipKey(clientIp(request));
    switch (url.pathname) {
      case "/api/v1/ws":
      case "/api/live":
        if (!isUpgrade(request)) return new Response("Expected a WebSocket", { status: 426 });
        if (!(await edgeAllowed(env.RL_CONNECT, `c:${key}`))) return tooMany("Too many connections -- wait a minute");
        break;
      case "/api/v1/poll":
        if (request.method !== "POST") return new Response("POST only", { status: 405 });
        if (Number(request.headers.get("Content-Length") || 0) > MAX_POLL_BODY) {
          return new Response("Bad size", { status: 413 });
        }
        if (!(await edgeAllowed(env.RL_API, `a:${key}`))) return tooMany("Too many requests -- wait a minute");
        break;
      case "/api/stats":
        if (request.method !== "GET") return new Response("GET only", { status: 405 });
        if (!(await edgeAllowed(env.RL_API, `a:${key}`))) return tooMany("Too many requests -- wait a minute");
        break;
      default:
        return new Response("Not found", { status: 404 });
    }
    const stub = env.DIRECTORY.get(env.DIRECTORY.idFromName("global"));
    return stub.fetch(request);
  },
};
