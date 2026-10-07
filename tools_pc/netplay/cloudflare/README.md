# GoldenEye 007 PC port — online service

The central service every copy of the game uses to find online games
(finding D410). It runs on Cloudflare's **free** tier: a Worker, one Durable
Object and a static status page.

- **Directory and matchmaking:** lists open games and hands out 6-character
  game codes.
- **Random matchmaking (Quick Match):** puts each searcher in the best open
  quick game of their build that fits their preferences. Preferences cover
  mode, map, weapons, length and players; any of them can be "any", and the
  game's own rules apply (team sizes, small maps, YOLT / Golden Gun). "Best"
  means one nobody failed to reach, then one on the same continent, then
  the fullest, then the oldest. If there is none, the searcher is told to
  host one with their preferences as its rules, and the next fitting
  searchers are sent there.
  - A game that two different addresses failed to reach is no longer
    offered.
  - Lone hosts are merged into older games, one way only, so simultaneous
    searchers meet.
- **Rendezvous:** tells a joiner the host's addresses, and tells the host who
  is joining, so they can connect to each other directly.
- **Status page:** anyone who opens the service's address in a browser sees
  the games in progress, players online, and recent results, updated live.

The matches themselves never pass through Cloudflare. A game is hosted by a
player's own PC and connects to the others directly over UDP. The game learns
its public address from Cloudflare's free STUN server (`stun.cloudflare.com`),
and the host "punches" a hole through its router towards each joiner. Relaying
60 Hz lockstep traffic through Workers would use up the free tier within a few
match-hours a day. Workers cannot receive UDP anyway.

```
 game A ──WebSocket──┐                    ┌── browser: status page
                     ├── Worker ── Durable Object ("global")
 game B ──WebSocket──┘   (/api/*)   lobbies in memory, results in SQLite
   │                         ▲
   └──── UDP, direct ────────┘ (only the introduction goes through the service)
```

## Deploy

You need a Cloudflare account (free) and Node.js 18 or newer.

```bash
cd tools_pc/netplay/cloudflare
npm install                 # installs wrangler (Cloudflare's CLI) locally
npx wrangler login          # opens a browser to authorise wrangler
npx wrangler deploy
```

`deploy` prints the service's address, for example
`https://ge007-online.<your-subdomain>.workers.dev`. Open it in a browser to
see the status page.

Then point the game at it, in one of two ways:

- **For everyone using your builds:** configure the build with the URL.
  Players need no settings.

  ```bash
  cmake -B build-pc -DGE007_ONLINE_SERVICE_URL=https://ge007-online.<your-subdomain>.workers.dev
  ```

  Run that once on the build directory. It is a cache variable, so later
  `./build-pc.sh` runs keep it.
- **For one player:** in game, open **Online → Settings → Online service**
  and paste the URL. You can also set `Service=` under `[Net]` in
  `ge007.ini`, or pass `--net-service URL` on the command line. An empty value
  means "this build's default". `off` turns the online service off.

Optional: `MOTD` in `wrangler.jsonc` is a one-line message shown on the
game's Online page (up to 95 characters). Redeploy after changing it.

## What it costs: nothing, within the free tier

These are the free-plan limits as of 2026-10 and how this service spends them:

| Resource (free plan) | Limit | Spent on |
|---|---|---|
| Worker requests | 100,000 / day | one per WebSocket connect, status-page load (`/api/*` only; static files are free) or HTTPS poll |
| Durable Object requests | 100,000 / day | connects, plus incoming WebSocket messages at 20 per request |
| Durable Object duration | 13,000 GB-s / day | the single object, even awake all day, is about 10,800 GB-s |
| SQLite rows written | 100,000 / day | about 3 per finished match |
| STUN (`stun.cloudflare.com`) | free, unmetered | one exchange per game, plus a refresh every 25 s while hosting |

In practice:

- A hosted game costs about 20 Durable Object requests per hour: a refresh
  every 30 s plus keepalives.
- A player browsing the Online page costs about 12 requests per hour.
- The free tier therefore covers thousands of player-hours a day.
- **HTTPS polling** costs much more: about 1,800 requests per hosted hour. It
  is the fallback for networks whose proxies block WebSockets (the game switches
  to it after three failed WebSocket attempts, or with `forcePoll`).

If a limit is reached, Cloudflare refuses requests until the daily reset
(00:00 UTC). In the game, Online shows "cannot reach the service" until then.
LAN, direct-address and self-hosted-server play keep working.

## Limits the service enforces

"Per address" below means per IPv4 address, or per IPv6 /64 (one subscriber
gets a whole /64).

- **Capacity:**
  - 4,000 connected games in total, and 8 per address;
  - 1,000 status-page viewers, and 4 per address;
  - 2,000 open lobbies, and 4 per address (a LAN party may host a few).
- **Edge rate limits** (Workers rate-limit bindings, before the Durable
  Object): 30 connections and 150 API calls per minute per address.
- **Request rates,** per address per minute:
  - 12 new lobbies, 30 joins, 60 list requests, 12 match reports;
  - 60 quick-match requests (a waiting quick host re-asks every 5 s);
  - 10 wrong game codes per 10 minutes (codes are the key to private games);
  - one game is sent at most 20 join requests per minute.
- **Per connection:** 10 messages per second (bursts of 40). HELLO is
  allowed once. After 20 malformed messages the connection is closed.
- **No reflection.** The service hands peers each other's addresses, and
  both sides then send UDP to them. So a public address a player publishes
  must be the one their request came from (an IPv6 player may publish one).
  Private LAN addresses are limited to two. Loopback is allowed only from
  loopback, and ports below 1024 and reserved ranges are dropped.
  (`src/netaddr.js`)
- **Signed lobby tokens.** A host's token is HMAC-SHA256 over its lobby id
  and code, with a secret kept in the Durable Object's storage. After a
  restart, a host re-adopts its id and code only with a valid token, so
  nobody can take over someone else's code.
- **Results only for real matches.** The service must have seen the game go
  WAITING → PLAYING → WAITING. Each match reports once, within 2 minutes of
  its end. Names come from the match's roster and scores are clamped.
- **Lobby lifetime.** A lobby whose host stops refreshing disappears after
  90 s. It disappears at once when the host's connection closes.
- **Private lobbies** are joinable by code only. They never appear in lists,
  and their match results show as "Private game" with anonymous players.
- **Bounded memory:** rate buckets are capped at 50,000, and the status
  snapshot at 200 games.
- **HTTPS only.** The game API refuses plain http, and the page sends HSTS.
  The game itself also refuses any `http://` service except this machine.
- **Input hygiene.** Every message is size-checked and range-checked
  (`src/protocol.js`), and names are sanitised. The status page builds its
  DOM with `textContent` only, and `public/_headers` sets a strict
  Content-Security-Policy.
- **Fuzzed.** Every handler runs under try/catch, and `test/fuzz.test.js`
  fuzzes the decoders and the directory.

## Files

| File | What it is |
|---|---|
| `src/index.js` | Worker entry and `DirectoryDO` (WebSocket hibernation, SQLite history, routes) |
| `src/directory.js` | The directory itself, as plain JS with no Cloudflare APIs, so it is unit-testable |
| `src/protocol.js` | The binary protocol. It mirrors `port/net/net_dirproto.c` byte for byte |
| `src/gamedata.js` | Scenario / stage / weapon / character names, and the quick-match preference rules (mirror of `ndpNormalizePrefs`) |
| `src/netaddr.js` | Address rules: per-address identity (IPv4 / IPv6 /64), the anti-reflection filter for published addresses |
| `src/sha256.js` | SHA-256 / HMAC for the signed lobby tokens (synchronous; no WebCrypto round trips) |
| `public/` | The status page (no build step, no external scripts) |
| `test/*.test.js` | Unit tests (`npm test`), including `fuzz.test.js`: decoders and the directory under random, mutated and adversarial input |
| `test/mock-server.js` | The same directory behind the same routes, on plain Node HTTP/WebSocket |
| `test/integration.mjs` | The game's C directory client (`netdir_test`) against the mock |
| `test/online.mjs` | The game's whole network runtime (`netonline_test`) against the mock and a local STUN server: host, join by code, private games, quick match, simultaneous quick match, preferences (Golden Gun vs Normal searchers, 2 vs 1 sizes), an unreachable game |
| `test/demo.mjs` | The mock with made-up games, for looking at the status page |

## Test locally

No Cloudflare account is needed:

```bash
npm test                    # protocol + directory unit tests
npm run demo                # status page with demo data: http://127.0.0.1:8787/
```

The two end-to-end tests drive the game's own network code, built from
`tools_pc/netplay` (no ROM or SDL needed):

```bash
cmake -S ../ -B ../build-net -DCMAKE_BUILD_TYPE=Release
cmake --build ../build-net --config Release
NETDIR_TEST=../build-net/netdir_test npm run test:integration
NETONLINE_TEST=../build-net/netonline_test npm run test:online
```

`npx wrangler dev` runs the real Worker locally (it needs `npm install`).
Point a game at `http://127.0.0.1:8787` with `--net-service`.

## Protocol

The protocol is binary and little-endian. Every message starts with a type
byte. The authoritative description is `port/net/include/net_dirproto.h`.

- **WebSocket** (`/api/v1/ws`): one message per binary frame.
- **HTTPS fallback** (`POST /api/v1/poll`): a batch per request, each message
  prefixed by a u16 length, in both directions.
- **`/api/stats`:** JSON for the status page.
- **`/api/live`:** a WebSocket that pushes the same JSON at most once a second.

The protocol is at version 3:

- **v2 (D411):** QUICK carries the game the searcher failed to reach;
  WELCOME / LISTED carry how many players are searching.
- **v3 (D412):** QUICK carries the searcher's preferences; HOST / LISTED
  carry each game's weapons and length.

When changing the protocol:

1. Change the C and JS codecs together.
2. Bump `NDP_VERSION`. Old games are told they are incompatible.
3. Regenerate `test/fixtures/c-vectors.txt` with
   `netplay_selftest --dirproto-vectors`.
