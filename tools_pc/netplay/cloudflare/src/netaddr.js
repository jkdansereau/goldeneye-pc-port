// Address helpers for the directory (D412 hardening).
//
// The directory hands peers each other's addresses, and both sides then send
// UDP to them. Without checks that is a reflection service: a joiner (or a
// host) could publish someone else's address and have game clients send
// traffic there. So a public address someone publishes must be their own --
// the address their request came from -- and the rest is limited to private
// LAN ranges (which only reach the peer's own network) and, for local
// testing, loopback from loopback.

// "1.2.3.4" or the IPv4-mapped "::ffff:1.2.3.4" -> uint32; anything else -> null.
export function ipv4Of(ip) {
  if (typeof ip !== "string") return null;
  const s = ip.startsWith("::ffff:") ? ip.slice(7) : ip;
  const parts = s.split(".");
  if (parts.length !== 4) return null;
  let n = 0;
  for (const p of parts) {
    if (!/^\d{1,3}$/.test(p)) return null;
    const v = Number(p);
    if (v > 255) return null;
    n = (n * 256 + v) >>> 0;
  }
  return n;
}

export function ipv4String(n) {
  return [n >>> 24, (n >>> 16) & 255, (n >>> 8) & 255, n & 255].join(".");
}

// "bad" (never a peer: this-network, link-local, multicast, reserved,
// broadcast), "loopback", "private" (RFC 1918 + carrier-grade NAT) or "public".
export function classifyV4(n) {
  const a = n >>> 24;
  const b = (n >>> 16) & 255;
  if (a === 0 || a >= 224) return "bad";
  if (a === 169 && b === 254) return "bad";
  if (a === 127) return "loopback";
  if (a === 10 || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168) || (a === 100 && b >= 64 && b <= 127)) {
    return "private";
  }
  return "public";
}

// Expand an IPv6 address to 8 groups of 4 hex digits; null if malformed.
function expandV6(ip) {
  if (typeof ip !== "string" || !ip.includes(":") || ip.length > 45) return null;
  const s = ip.split("%")[0].toLowerCase();
  const halves = s.split("::");
  if (halves.length > 2) return null;
  const head = halves[0] ? halves[0].split(":") : [];
  const tail = halves.length === 2 && halves[1] ? halves[1].split(":") : [];
  const fill = 8 - head.length - tail.length;
  if (halves.length === 1 ? head.length !== 8 : fill < 1) return null;
  const groups = [...head, ...Array(halves.length === 2 ? fill : 0).fill("0"), ...tail];
  if (groups.length !== 8 || !groups.every((g) => /^[0-9a-f]{1,4}$/.test(g))) return null;
  return groups.map((g) => g.padStart(4, "0"));
}

// The identity rate limits and per-address caps count: an IPv4 address, or
// an IPv6 /64 (one subscriber gets a whole /64, so per-address limits on
// IPv6 would be no limits at all).
export function ipKey(ip) {
  const v4 = ipv4Of(ip);
  if (v4 !== null) return ipv4String(v4);
  const g = expandV6(ip);
  if (g) return g.slice(0, 4).join(":") + "::/64";
  return typeof ip === "string" && ip.length ? ip.slice(0, 64) : "unknown";
}

// The candidate addresses a peer may publish, given where its request came
// from (`fromIp`). Public addresses must be the sender's own (any port);
// a sender reaching us over IPv6 cannot be checked against its IPv4 STUN
// address, so it gets at most one. Private LAN addresses: at most two.
// Loopback only from loopback (local tests). Ports below 1024 never (the
// game uses 27007 or an ephemeral port). At most `max` in all, deduplicated.
export function cleanCands(cands, fromIp, max = 4) {
  const src = ipv4Of(fromIp);
  const srcKind = src === null ? "v6" : classifyV4(src);
  const out = [];
  let pub = 0;
  let priv = 0;
  for (const c of cands || []) {
    if (!c || !Number.isInteger(c.ip) || !Number.isInteger(c.port)) continue;
    if (c.port < 1024 || c.port > 65535) continue;
    if (out.some((o) => o.ip === c.ip && o.port === c.port)) continue;
    const kind = classifyV4(c.ip >>> 0);
    if (kind === "bad") continue;
    if (kind === "loopback") {
      if (srcKind === "loopback") out.push(c);
      continue;
    }
    if (kind === "private") {
      if (priv < 2) {
        out.push(c);
        priv++;
      }
      continue;
    }
    // public
    if (srcKind === "public") {
      if (c.ip >>> 0 === src) out.push(c);
    } else if (pub < 1) {
      out.push(c);   // IPv6 sender, or a sender on a private / loopback network (LAN service, tests)
      pub++;
    }
  }
  return out.slice(0, max);
}
