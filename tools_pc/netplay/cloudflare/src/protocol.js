// GE007 online directory protocol (D410) -- the service side of
// port/net/net_dirproto.c / net_dirproto.h. Change both together.
//
// One message = [u8 type][payload], little-endian. Strings are u8 length +
// bytes (printable ASCII after sanitising); an address is u32 IPv4 (the
// numeric value, e.g. 127.0.0.1 = 0x7F000001) + u16 port.

export const NDP_VERSION = 3;   // 2: QUICK excludeId, WELCOME / LISTED searching (D411)
                                // 3: quick-match preferences; lobby weapons / length (D412)
export const ANY = 0xff;        // a quick-match preference that takes anything
export const MAX_MSG = 2048;

export const C = { HELLO: 1, LIST: 2, HOST: 3, UNHOST: 4, JOIN: 5, QUICK: 6, RESULT: 7, POLL: 8 };
export const S = { WELCOME: 101, LISTED: 102, HOSTED: 103, JOININFO: 105, ERROR: 106, QUICKHOST: 107, JOINREQ: 108 };
export const E = { BAD_REQUEST: 1, VERSION: 2, NOT_FOUND: 3, FULL: 4, IN_MATCH: 5, RATE_LIMIT: 6, BUSY: 7, FORBIDDEN: 8 };
export const F = { PUBLIC: 0x01, QUICK: 0x02 };
export const ST = { WAITING: 0, STARTING: 1, PLAYING: 2 };

export const LIM = {
  NAME: 15, LOBBY_NAME: 23, BUILD: 63, TOKEN: 32, CODE: 6, CHAT: 80, MOTD: 95, COUNTRY: 2,
  CANDS: 4, PLAYERS: 4, LIST: 32,
};

export class ProtoError extends Error {}

// netSanitizeText(): bytes outside 0x20..0x7E become '?', surrounding spaces
// trimmed, empty -> fallback.
export function sanitize(s, fallback = "") {
  let out = "";
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    out += c < 0x20 || c > 0x7e ? "?" : s[i];
  }
  out = out.trim();
  return out.length ? out : fallback;
}

export class Reader {
  constructor(bytes) {
    this.b = bytes;
    this.dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    this.p = 0;
  }
  need(n) {
    if (this.p + n > this.b.length) throw new ProtoError("truncated message");
  }
  left() { return this.b.length - this.p; }
  u8() { this.need(1); return this.b[this.p++]; }
  u16() { this.need(2); const v = this.dv.getUint16(this.p, true); this.p += 2; return v; }
  i16() { this.need(2); const v = this.dv.getInt16(this.p, true); this.p += 2; return v; }
  u32() { this.need(4); const v = this.dv.getUint32(this.p, true); this.p += 4; return v; }
  // Raw string (latin-1), cut to max like nrStr into a max+1 buffer.
  raw(max) {
    const n = this.u8();
    this.need(n);
    let s = "";
    for (let i = 0; i < n; i++) s += String.fromCharCode(this.b[this.p + i]);
    this.p += n;
    return s.length > max ? s.slice(0, max) : s;
  }
  str(max, fallback = "") { return sanitize(this.raw(max), fallback); }
  cands() {
    const n = this.u8();
    if (n > LIM.CANDS) throw new ProtoError("too many addresses");
    const out = [];
    for (let i = 0; i < n; i++) {
      const ip = this.u32();
      const port = this.u16();
      if (candValid(ip, port)) out.push({ ip, port });
    }
    return out;
  }
}

export class Writer {
  constructor(cap = 256) {
    this.b = new Uint8Array(cap);
    this.p = 0;
  }
  grow(n) {
    if (this.p + n <= this.b.length) return;
    let cap = this.b.length * 2;
    while (cap < this.p + n) cap *= 2;
    const nb = new Uint8Array(cap);
    nb.set(this.b.subarray(0, this.p));
    this.b = nb;
  }
  u8(v) { this.grow(1); this.b[this.p++] = v & 0xff; return this; }
  u16(v) { this.grow(2); new DataView(this.b.buffer).setUint16(this.p, v & 0xffff, true); this.p += 2; return this; }
  i16(v) { this.grow(2); new DataView(this.b.buffer).setInt16(this.p, v, true); this.p += 2; return this; }
  u32(v) { this.grow(4); new DataView(this.b.buffer).setUint32(this.p, v >>> 0, true); this.p += 4; return this; }
  str(s, max) {
    s = String(s ?? "");
    const n = Math.min(s.length, max, 255);
    this.u8(n);
    this.grow(n);
    for (let i = 0; i < n; i++) {
      const c = s.charCodeAt(i);
      this.b[this.p++] = c < 0x20 || c > 0x7e ? 0x3f : c;
    }
    return this;
  }
  cands(list) {
    const n = Math.min(list.length, LIM.CANDS);
    this.u8(n);
    for (let i = 0; i < n; i++) this.u32(list[i].ip).u16(list[i].port);
    return this;
  }
  bytes() { return this.b.slice(0, this.p); }
}

export function candValid(ip, port) {
  return ip !== 0 && ip !== 0xffffffff && port !== 0;
}

export function ipToString(ip) {
  return [ip >>> 24, (ip >>> 16) & 255, (ip >>> 8) & 255, ip & 255].join(".");
}

// ---------------------------------------------------------------- decoding

export function decodeClient(bytes) {
  if (!(bytes instanceof Uint8Array)) bytes = new Uint8Array(bytes);
  if (bytes.length < 1 || bytes.length > MAX_MSG) throw new ProtoError("bad message size");
  const r = new Reader(bytes);
  const type = r.u8();
  let m;
  switch (type) {
    case C.HELLO:
      m = { type, version: r.u8(), build: r.raw(LIM.BUILD), name: r.str(LIM.NAME, "Agent") };
      break;
    case C.LIST:
      m = { type };
      break;
    case C.HOST: {
      m = {
        type,
        lobbyId: r.u32(),
        token: r.raw(LIM.TOKEN),
        code: r.raw(LIM.CODE),
        name: r.str(LIM.LOBBY_NAME, "Lobby"),
        flags: r.u8(),
        maxPlayers: r.u8(),
        state: r.u8(),
        numPlayers: r.u8(),
        scenario: r.u8(),
        stage: r.u8(),
        weapons: r.u8(),
        length: r.u8(),
        matchSec: r.u16(),
        cands: r.cands(),
        players: [],
      };
      const n = r.u8();
      if (n > LIM.PLAYERS) throw new ProtoError("too many players");
      for (let i = 0; i < n; i++) {
        m.players.push({ name: r.str(LIM.NAME, "?"), character: r.u8(), team: r.u8(), ready: r.u8() });
      }
      break;
    }
    case C.UNHOST:
      m = { type, lobbyId: r.u32(), token: r.raw(LIM.TOKEN) };
      break;
    case C.JOIN:
      m = { type, lobbyId: r.u32(), code: r.raw(LIM.CODE), nonce: r.u32(), cands: r.cands() };
      break;
    case C.QUICK:
      m = {
        type, nonce: r.u32(), lobbyId: r.u32(), excludeId: r.u32(),
        prefs: { scenario: r.u8(), stage: r.u8(), weapons: r.u8(), length: r.u8(), players: r.u8() },
        cands: r.cands(),
      };
      break;
    case C.RESULT: {
      m = {
        type,
        lobbyId: r.u32(),
        token: r.raw(LIM.TOKEN),
        durationSec: r.u16(),
        scenario: r.u8(),
        stage: r.u8(),
        players: [],
      };
      const n = r.u8();
      if (n > LIM.PLAYERS) throw new ProtoError("too many players");
      for (let i = 0; i < n; i++) {
        m.players.push({ name: r.str(LIM.NAME, "?"), character: r.u8(), team: r.u8(), kills: r.i16(), deaths: r.i16() });
      }
      break;
    }
    case C.POLL:
      m = { type, lobbyId: r.u32(), token: r.raw(LIM.TOKEN) };
      break;
    default:
      throw new ProtoError(`unknown message ${type}`);
  }
  return m;
}

// ---------------------------------------------------------------- encoding

export function encWelcome({ online = 0, lobbies = 0, matches = 0, searching = 0, motd = "" }) {
  return new Writer(32).u8(S.WELCOME).u16(online).u16(lobbies).u16(matches).u16(searching).str(motd, LIM.MOTD).bytes();
}

export function encListed({ online = 0, lobbies = 0, matches = 0, searching = 0, entries = [] }) {
  const w = new Writer(512).u8(S.LISTED).u16(online).u16(lobbies).u16(matches).u16(searching);
  const list = entries.slice(0, LIM.LIST);
  w.u8(list.length);
  for (const e of list) {
    w.u32(e.id).str(e.name, LIM.LOBBY_NAME).str(e.hostName, LIM.NAME).str(e.code || "", LIM.CODE)
      .u8(e.flags).u8(e.state).u8(e.numPlayers).u8(e.maxPlayers).u8(e.scenario).u8(e.stage)
      .u8(e.weapons ?? 0).u8(e.length ?? 0).str(e.country || "", LIM.COUNTRY);
  }
  return w.bytes();
}

export function encHosted({ lobbyId, token, code }) {
  return new Writer(64).u8(S.HOSTED).u32(lobbyId).str(token, LIM.TOKEN).str(code, LIM.CODE).bytes();
}

export function encJoinInfo({ nonce, lobbyId, name, hostName, cands }) {
  return new Writer(96).u8(S.JOININFO).u32(nonce).u32(lobbyId).str(name, LIM.LOBBY_NAME)
    .str(hostName, LIM.NAME).cands(cands).bytes();
}

export function encError(reqType, nonce, code, text) {
  return new Writer(96).u8(S.ERROR).u8(reqType).u32(nonce).u8(code).str(text, LIM.CHAT).bytes();
}

export function encQuickHost(nonce) {
  return new Writer(8).u8(S.QUICKHOST).u32(nonce).bytes();
}

export function encJoinReq({ lobbyId, name, cands }) {
  return new Writer(64).u8(S.JOINREQ).u32(lobbyId).str(name, LIM.NAME).cands(cands).bytes();
}

// ---------------------------------------------------- client-side helpers
// (tests and the mock server's scripted clients)

export function encHello(build, name, version = NDP_VERSION) {
  return new Writer(96).u8(C.HELLO).u8(version).str(build, LIM.BUILD).str(name, LIM.NAME).bytes();
}
export function encList() { return Uint8Array.of(C.LIST); }
export function encHost(h) {
  const w = new Writer(256).u8(C.HOST).u32(h.lobbyId || 0).str(h.token || "", LIM.TOKEN).str(h.code || "", LIM.CODE)
    .str(h.name || "", LIM.LOBBY_NAME).u8(h.flags || 0).u8(h.maxPlayers ?? 4).u8(h.state || 0)
    .u8(h.numPlayers ?? 1).u8(h.scenario || 0).u8(h.stage || 0).u8(h.weapons ?? 11).u8(h.length ?? 2)
    .u16(h.matchSec || 0).cands(h.cands || []);
  const players = (h.players || []).slice(0, LIM.PLAYERS);
  w.u8(players.length);
  for (const p of players) w.str(p.name, LIM.NAME).u8(p.character || 0).u8(p.team || 0).u8(p.ready ? 1 : 0);
  return w.bytes();
}
export function encUnhost(lobbyId, token) {
  return new Writer(48).u8(C.UNHOST).u32(lobbyId).str(token, LIM.TOKEN).bytes();
}
export function encJoin({ lobbyId = 0, code = "", nonce = 0, cands = [] }) {
  return new Writer(48).u8(C.JOIN).u32(lobbyId).str(code, LIM.CODE).u32(nonce).cands(cands).bytes();
}
export function encQuick({ nonce = 0, lobbyId = 0, excludeId = 0, prefs = {}, cands = [] }) {
  const p = { scenario: ANY, stage: ANY, weapons: ANY, length: ANY, players: ANY, ...prefs };
  return new Writer(56).u8(C.QUICK).u32(nonce).u32(lobbyId).u32(excludeId)
    .u8(p.scenario).u8(p.stage).u8(p.weapons).u8(p.length).u8(p.players).cands(cands).bytes();
}
export function encResult(r) {
  const w = new Writer(160).u8(C.RESULT).u32(r.lobbyId).str(r.token, LIM.TOKEN).u16(r.durationSec)
    .u8(r.scenario).u8(r.stage);
  const players = (r.players || []).slice(0, LIM.PLAYERS);
  w.u8(players.length);
  for (const p of players) w.str(p.name, LIM.NAME).u8(p.character || 0).u8(p.team || 0).i16(p.kills || 0).i16(p.deaths || 0);
  return w.bytes();
}
export function encPoll(lobbyId, token) {
  return new Writer(48).u8(C.POLL).u32(lobbyId).str(token, LIM.TOKEN).bytes();
}

export function decodeService(bytes) {
  const r = new Reader(bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes));
  const type = r.u8();
  switch (type) {
    case S.WELCOME:
      return { type, online: r.u16(), lobbies: r.u16(), matches: r.u16(), searching: r.u16(), motd: r.str(LIM.MOTD) };
    case S.LISTED: {
      const m = { type, online: r.u16(), lobbies: r.u16(), matches: r.u16(), searching: r.u16(), entries: [] };
      const n = r.u8();
      for (let i = 0; i < n; i++) {
        m.entries.push({
          id: r.u32(), name: r.str(LIM.LOBBY_NAME), hostName: r.str(LIM.NAME), code: r.str(LIM.CODE),
          flags: r.u8(), state: r.u8(), numPlayers: r.u8(), maxPlayers: r.u8(), scenario: r.u8(), stage: r.u8(),
          weapons: r.u8(), length: r.u8(), country: r.str(LIM.COUNTRY),
        });
      }
      return m;
    }
    case S.HOSTED:
      return { type, lobbyId: r.u32(), token: r.raw(LIM.TOKEN), code: r.raw(LIM.CODE) };
    case S.JOININFO:
      return { type, nonce: r.u32(), lobbyId: r.u32(), name: r.str(LIM.LOBBY_NAME), hostName: r.str(LIM.NAME), cands: r.cands() };
    case S.ERROR:
      return { type, reqType: r.u8(), nonce: r.u32(), code: r.u8(), text: r.str(LIM.CHAT) };
    case S.QUICKHOST:
      return { type, nonce: r.u32() };
    case S.JOINREQ:
      return { type, lobbyId: r.u32(), name: r.str(LIM.NAME), cands: r.cands() };
    default:
      throw new ProtoError(`unknown service message ${type}`);
  }
}

// HTTP transport framing: [u16 len][message] ...
export function frameBatch(messages) {
  let total = 0;
  for (const m of messages) total += 2 + m.length;
  const out = new Uint8Array(total);
  const dv = new DataView(out.buffer);
  let p = 0;
  for (const m of messages) {
    dv.setUint16(p, m.length, true);
    out.set(m, p + 2);
    p += 2 + m.length;
  }
  return out;
}

export function unframeBatch(bytes, maxMessages = 16) {
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const out = [];
  let p = 0;
  while (p < bytes.length) {
    if (p + 2 > bytes.length) throw new ProtoError("truncated batch");
    const n = dv.getUint16(p, true);
    if (n < 1 || n > MAX_MSG || p + 2 + n > bytes.length) throw new ProtoError("bad batch framing");
    out.push(bytes.subarray(p + 2, p + 2 + n));
    p += 2 + n;
    if (out.length > maxMessages) throw new ProtoError("batch too long");
  }
  return out;
}
