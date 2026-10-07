# Netplay tools: matchmaking server + self-test (D413, D414)

> **Looking for the online service** (quick match, game list and codes for
> everyone, a live status page, free on Cloudflare)? That is
> [`cloudflare/`](cloudflare/README.md) (D414). This page covers
> `ge007-netserver`: a server you run yourself, which also relays game
> traffic and so works behind any router.

Two programs built from the game-independent netplay core in `port/net/`
(design: [`docs/dev/NETPLAY-PLAN.md`](../../docs/dev/NETPLAY-PLAN.md);
player guide: [`docs/netplay.md`](../../docs/netplay.md)):

| Program | What it is |
|---|---|
| `ge007-netserver` | the **matchmaking + relay server**: lists public lobbies, joins players by 6-letter code, quick-matches strangers, and relays controller input for server-hosted matches. It never runs the game, needs no ROM and no game assets, and uses almost no CPU. |
| `netplay_selftest` | the **protocol test harness**: a host and up to 4 clients over a simulated network (latency, jitter, loss, duplication, reordering) driving a toy deterministic game, plus a real-UDP loopback run. No ROM needed. |

Both build with any C11 compiler (MSVC, MinGW GCC, Linux GCC/Clang) and need
nothing but the OS socket library — no SDL, no game headers.

## Build

```sh
cmake -S tools_pc/netplay -B build-netplay -DCMAKE_BUILD_TYPE=Release
cmake --build build-netplay
ctest --test-dir build-netplay --output-on-failure    # runs netplay_selftest
```

The game build (`./build-pc.sh`) compiles the same core as the static
library `ge007net` (`port/net/CMakeLists.txt`); these tools do not need it.

## Running a server

```sh
ge007-netserver                   # UDP 27008, defaults below
ge007-netserver --port 27008 --max-sessions 512 --max-lobbies 128 --max-per-ip 8 --stats 60 -v
```

| Option | Default | Meaning |
|---|---|---|
| `--port N` | 27008 | UDP port to listen on |
| `--max-sessions N` | 512 | concurrent connections |
| `--max-lobbies N` | 128 | concurrent lobbies |
| `--max-per-ip N` | 8 | connections from one IP address |
| `--stats SECONDS` | 60 | status line interval (0 = off) |
| `-v` | off | verbose protocol log |

Open **UDP** on that port in the firewall (and forward it if the machine is
behind NAT). Players enter `host:port` (`:27008` may be omitted) under
**F9 → Settings → Server** in the game, or pass `--net-server host`.
Stop the server with Ctrl+C / SIGTERM; it says goodbye to every connected
player before exiting.

**Bandwidth.** Small and constant while a match runs: each player exchanges
a few dozen to ~80 UDP packets per second with the host in each direction
(input records are 3–38 bytes, re-sent until acknowledged; bundles carry up
to four of them). Not yet measured on a real match — expect tens of kbit/s
per player.

## Security notes

- The server only parses its own small binary protocol (bounds-checked
  little-endian reader; every length and index is range-checked; text is
  forced to printable ASCII). It never executes, stores or forwards
  anything but lobby state, chat lines and input records, and it keeps no
  files.
- **No amplification:** a `JOIN` is padded to 256 bytes and a LAN `QUERY` to
  128 bytes, both larger than any reply, so the server can't be used to
  reflect traffic at a spoofed address. Connection attempts are rate-limited
  per address and capped by `--max-per-ip`.
- Matches only pair identical game builds (version hash, ROM region,
  protocol version, pointer width). A modified build that reports the same
  id can at worst desynchronize its own match (the desync detector reports
  it); it cannot affect the server or other lobbies.
- Chat is relayed as-is to the lobby. Run your own server for people you
  know; there is no account system or moderation.

## Self-test

```sh
netplay_selftest                         # full suite, prints ALL TESTS PASSED
netplay_selftest -v                      # with protocol logs
netplay_selftest --live-server HOST:PORT # 2 clients quick-match on a running
                                         # ge007-netserver and play 300 frames
```

What the suite asserts: codec round-trips and truncation safety; byte-
identical bundles and per-frame state hashes on every client (2-player LAN,
4-player internet with 3 % loss, 3-player bad link); desync injection is
detected on every client; a vanished client is cut over to neutral input at
the same frame everywhere; a leader abort ends the match at the same frame
and a rematch works; lobby listing hides private lobbies; create / join by
code / quick match / leader-only start / ready rule / team validation /
quick-lobby autostart; a build mismatch is refused; the local-address helper;
and a 240-frame match over real UDP sockets. D414 added: the STUN codec (RFC
5769 test vector), the online directory protocol (including messages encoded
by the JS service), joining through several candidate addresses (one dead;
and both live, which must still give exactly one player), and the
start-barrier status the waiting screen shows. D416 added:
- **Quick-match preference rules.** C and JS are checked against each other
  on 64 vectors.
- **Hostile input floats** are made finite and bounded, and a decoded record
  does not change when the host re-encodes it.
- **A match that simply ends** returns the lobby to waiting (everyone
  finishes, or one quits).

## Fuzzing (D416)

`netfuzz` attacks the real code the way strangers online could:

- **Decoders:** every game-protocol, directory-protocol and STUN decoder, on
  random and mutated input.
- **An evil host** that completes the handshake and then sends hostile but
  correctly framed messages to a real client, which is driven like the game.
- **An evil joiner** inside a real host's lobby while two real players play
  matches, plus an on-path attacker tampering with and replaying packets,
  plus garbage from anywhere.
- **Invariants** on what the game would consume: finite, bounded input
  floats; counts and slots in range; terminated strings. The fuzzer also
  checks that it reached running matches at all.

Build it with AddressSanitizer:

```sh
cmake -S tools_pc/netplay -B build-net-asan -DNETPLAY_SANITIZE=ON
cmake --build build-net-asan
build-net-asan/netfuzz --iters 150 --seed 1   # NO VIOLATIONS = pass
```

On Windows, run it from a Visual Studio developer prompt so the ASan
runtime DLL is on `PATH`. A seed replays exactly: the stack's own nonces and
ids are seeded too (`netRandomTestSeed`).

Two more harnesses drive the game's own network code against the online
service's local mock (`cloudflare/test/`):

```sh
netdir_test URL host|join CODE|quick|list [--poll]       # the directory client
netonline_test URL STUN NAME host|join CODE|quick [...]  # the whole runtime
```

Run them through `npm run test:integration` and `npm run test:online` in
`cloudflare/`; see its README.
