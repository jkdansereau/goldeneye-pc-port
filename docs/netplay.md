---
title: Online multiplayer
description: Play GoldenEye 007 multiplayer online - every player on their own PC with their own full-window view - through the online service (quick match, game list, codes), LAN, direct connect or your own server.
---

# Online multiplayer

> **Status: new and not yet play-tested in a real match.** The networking
> core, the online service and the matchmaking server are tested, including
> end to end on one PC. The in-game side was built without a test run, and
> home routers have not been tested. Expect rough edges and please report
> problems (see [Troubleshooting](#troubleshooting)).

GoldenEye's 4-player multiplayer, with **every player on their own PC** and
**seeing only their own view, full-window** — not split screen. Everything
from the N64 multiplayer menus is available:

- every scenario: Normal, You Only Live Twice, The Living Daylights (flag
  tag), The Man with the Golden Gun, Licence to Kill, and the 2 vs 2 / 3 vs 1
  / 2 vs 1 team games;
- every multiplayer stage (or Random), every game length, every weapon set;
- all 64 characters, handicaps (health), control styles 1.1–1.4, aim/sight
  adjustment, teams.

Online play is **off until you use it**. The game makes no network
connection until you open the **Online** menu or start the game with a
`--net-*` command-line option. Opening the menu shows how many players are
online.

## How it works (in one paragraph)

Every PC runs the complete, unmodified N64 game for all players at once;
only controller input travels over the network ("lockstep", the same idea
as N64 emulator netplay). The game cannot tell it is online — it just sees
four controllers. Your PC then shows only your own player's view, scaled up
to fill the window. Because every PC must compute exactly the same thing,
**everyone needs the same game build and the same ROM region** (US with US,
PAL with PAL, JP with JP); the game refuses mismatched builds.

## Quick start

Open the **Online** menu in any of these ways:

- press **F9** anywhere;
- pick **Online** under *PC Options* at the bottom of the file-select screen;
- pick **Online Multiplayer** in the PC Options list or the F10 panel.

Then pick one of four ways to play.

### Online (the easy way)

The **ONLINE** section uses the game's online service. It shows how many
players are on and how many games are open. You do not need to forward ports
or know anyone's address.

- **QUICK MATCH** is random matchmaking. Press it and you are put in a
  game with whoever else is searching for the same kind of game. Its line
  shows what you are looking for ("Any game", or e.g. "Golden Gun,
  Facility") and how many players are searching. Once in the game, nobody
  can change its rules.
  - **QUICK MATCH SETTINGS** chooses what you are looking for. Each field
    can be **Any** (no preference); fewer choices find a game sooner. The
    choices are saved.
    - **Mode:** any of the 8 scenarios, including the team games 2 vs 2,
      3 vs 1 and 2 vs 1.
    - **Map:** any of the 11 maps.
    - **Weapons:** any of the 14 weapon sets.
    - **Length:** a time or score limit.
    - **Players:** 2, 3 or 4.

    GoldenEye's own rules are applied as you choose:
    - Team modes fix the number of players (2 vs 2 and 3 vs 1 are 4, 2 vs 1
      is 3) and skip maps too small for them.
    - A small map caps the players: Bunker, Archives and Caverns hold 3,
      Egyptian 2.
    - You Only Live Twice is always "last one standing", and The Man with
      the Golden Gun always uses the golden gun.
    - Choosing "last one standing" as the length picks You Only Live Twice.
  - **Searching:** the screen shows how long you have been looking and how
    many players are online and searching.
  - **Nobody waiting:** your PC opens a game with your settings as its
    rules: "Any" means Normal, a random stage each match, and the standard
    length and weapons. The next searcher whose settings fit is sent to you.
    If two players press it at the same moment, they still end up in the
    same game a few seconds later.
  - **Team games:** the host's game balances the teams; players can't pick
    a side.
  - **Rematch:** after a quick-match game you are readied for the next one
    automatically. Set **READY** to *No* to sit it out, or leave the lobby.
  - **A game can't be reached:** some routers can't be connected through.
    The search then quietly moves on to another game within about 8
    seconds; after three such games, you host one.
  - **Starting:** a quick game starts by itself once everyone in it is
    ready. Arriving from Quick Match readies you. It waits 12 s for more
    players when there are two of you, 8 s with three, and starts after 3 s
    when full.
- **BROWSE GAMES** lists the public games with their mode, map and
  players. **SHOW** filters the list to one mode. Pick a game to join it.
- **HOST A PUBLIC GAME** starts a game on your PC and lists it.
- **HOST A PRIVATE GAME** starts a game that is not listed.
- **JOIN BY CODE**: type a game's code and press Enter.

When you host, the top of your lobby shows a 6-character **code**. Your
friends type it into JOIN BY CODE. The online service only introduces the
players. The match itself runs directly between your PCs.

This works through most home routers. Some cannot be passed through: certain
mobile, "carrier-grade" or strict corporate networks. Joining then fails with
*No response from the host*. Use direct play with a forwarded port instead
(below), or your own server.

**Safety.** The game only talks to an online service over `https://`, and it
sends game traffic only to ordinary internet or LAN addresses. Everything a
peer sends is checked before it reaches the game. A broken or hostile
packet is dropped: it can't crash or freeze anyone's game, and it can't
make players' games disagree.

*Builds without an online service* show **ONLINE (NOT SET UP IN THIS BUILD)**.
The service's address is chosen when the game is built
(`-DGE007_ONLINE_SERVICE_URL`). You can also set it under **SETTINGS →
ONLINE SERVICE**. Anyone can run the service for free on Cloudflare; see
[`tools_pc/netplay/cloudflare/README.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/tools_pc/netplay/cloudflare/README.md).
Opening that address in a browser shows a live page of the games being
played.

### On the same network (LAN)

1. One player picks **HOST ON THIS PC**. Their lobby opens and the status
   line shows the address others can use (e.g. `Others join: 192.168.1.20
   port 27007`).
2. Everyone else picks **LAN GAMES** and selects the game — or types the
   host's address into **JOIN ADDRESS** and presses Enter.

### Over the internet, directly

Same as LAN, but the host must **forward UDP port 27007** on their router to
their PC (or pick a different port under Settings → Host port and forward
that). The others type the host's **public** IP address (and `:port` if not
27007) into **JOIN ADDRESS**. No server needed.

### Through your own server

`ge007-netserver` ships with the source; see
[`tools_pc/netplay/README.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/tools_pc/netplay/README.md).
It is a matchmaking server that also carries the game traffic, so it works
behind any router, but someone has to run it on a reachable machine. Pick
**OWN SERVER** in the Online menu and type its address into **SERVER**.
Then:

- **QUICK MATCH** — join any open quick-match lobby, or start one. As
  online, quick lobbies use the standard rules with a random stage and start
  by themselves once everyone is ready (12 s with two players, 8 s with
  three, 3 s when full; arriving from a quick match readies you).
- **BROWSE LOBBIES** — list the public lobbies and join one.
- **CREATE PUBLIC / PRIVATE LOBBY** — host a lobby on the server. Share the
  6-letter **code** shown at the top of the lobby; private lobbies are only
  reachable by code.
- **JOIN BY CODE** — type a code and press Enter.

## The F9 menu

| Key / button | Does |
|---|---|
| **F9** | open / close (works in the menus and during a match) |
| **Up / Down**, wheel | move |
| **Left / Right** | change the selected setting |
| **Enter**, gamepad **A**, click | select / confirm |
| **Esc**, gamepad **B**, right-click | back (from the top level: close) |
| typing, **Backspace**, **Delete**, **Ctrl+V** | edit the selected text row (address, code, chat, name...) |
| **Shift+F9** | leave the current match immediately (also on the waiting screen) |

While the menu is open it takes over controller 1 (keyboard, mouse and the
first gamepad), like the F10 options panel. **In a match the game keeps
running while the menu is open** — your agent just stands still.

## The lobby

- **PLAYERS** — everyone in the lobby with their character, team, ready
  state and ping. The **leader** (the first player, normally whoever created
  it) controls the match settings.
- **YOU** — your character, health handicap, control style and team (team
  scenarios), and **READY**. Your character is remembered for next time; the
  control style starts at the one the PC controls are tuned for (1.1 Honey,
  or 1.2 Solitaire if *Natural pitch* is on in F10).
- **MATCH** — scenario, stage, length, weapons, aim/sight, input delay, and
  the six game options (invert look, auto-aim, aim control, sight on screen,
  look ahead, ammo on screen). Only the leader can change these; the server
  enforces GoldenEye's own rules (YOLT is always "last one standing", Golden
  Gun always uses the golden gun, team scenarios need the right player
  counts, Egyptian takes at most 2 players and Bunker/Archives/Caverns at
  most 3, and so on).
- **CHAT** — type a line into **SAY** and press Enter.
- **START MATCH** (leader) — needs at least two players, everyone else
  ready, and valid settings; if it can't start, the row says why.
- **LEAVE LOBBY** — leaves (if you are hosting on your PC, this ends the
  game for everyone in it).

**Be on the main menus to play.** A match can only start while your game is
on one of the front-end menus (file select, mode select, the multiplayer
menus...). If you go into a solo mission or the PC Options screen, your
READY is withdrawn after two seconds, so the leader can't start a match you
would miss.

## In the match

- You see your own view, as large as fits the window. In 3–4 player games
  your view has about the same shape as the single-player screen. In
  **2-player** games GoldenEye's views are wide strips, so the picture is
  **letterboxed** (black bars above and below) — that is the original N64
  geometry; a full-height 2-player view is planned.
- A small line at the top left shows your player number, ping and input
  delay. Settings → *Net stats in match* turns it off.
  - **OUT OF SYNC** means the PCs stopped agreeing about the game. Leave,
    start a new match, and please report it.
- **The waiting screen** appears when the game has to wait for more than a
  moment:
  - at the start, until everyone has loaded the stage;
  - during the match, when someone's input is late.

  It lists each player as *ready*, *loading*, *OK* or *lagging*, and names
  who is holding the game up. It shows *waiting for the host* when the delay
  is between you and the host. **Shift+F9** leaves.
- **F9** during a match: *Resume*, *Leave match*, and for the leader *End
  match for everyone*.
- If someone leaves or drops, their agent stands still for the rest of the
  match (and is confirmed automatically on the results screen so everyone
  can return to the menus).
- After the match everyone returns to the lobby; READY is cleared so each
  player chooses whether to play again.

### What is fixed while online

Every PC must compute the same game, so a few PC options that change what
the game itself does are held at their defaults during a match and come back
afterwards: widescreen projection and FOV (the N64 projection is used —
your view is shown at its true shape instead of stretched), draw distance
and level-of-detail distance (the defaults, 250 %), crosshair style / hide,
the hit flash, cheats (off), and the watch-settings changes made from F10
(applied after the match). Purely visual options (resolution, filtering,
MSAA, crosshair colour and size, HUD scale) still apply.

### Input delay

Each player's input is scheduled a few frames ahead so it can reach everyone
in time. **Auto** (the default) picks it from the measured pings when the
match starts: about 2 frames (33 ms) on a LAN, typically 3–6 on the
internet. The leader can fix it at 1–12 frames.

## Command line and settings

| Option | Does |
|---|---|
| `--net-host [port]` | host a game on this PC (default port 27007) and open the lobby |
| `--net-join HOST[:PORT]` | join a game directly |
| `--net-server HOST[:PORT]` | use this matchmaking server (default port 27008) |
| `--net-quick` | connect to the server and quick-match |
| `--net-online-host` | host a public game through the online service |
| `--net-online-join CODE` | join a game by its code through the online service |
| `--net-online-quick` | online quick match |
| `--net-service URL` | use this online service instead of the build's |
| `--net-name NAME` | your player name (up to 15 characters) |

`ge007.ini`, section `[Net]`: `Name`, `Server`, `HostPort`, `ShowStats`,
`Character`.

- `Service`: the online service's address. Empty means this build's default;
  `off` means none.
- `Stun`: the STUN server used to find your public address. Empty means
  `stun.cloudflare.com:3478`; `off` means LAN addresses only.

**Trying it on one PC:** start two copies of the game — the first with
`--net-host`, the second with `--net-join 127.0.0.1` — and play both
windows (one with a gamepad, one with keyboard and mouse).

## Troubleshooting

- **"different game build"** (or a LAN game marked *other version*) —
  everyone needs the same build and ROM region. Install the same release
  everywhere.
- **Can't connect / LAN game not listed** — allow the game through the
  Windows firewall (Windows may ask the first time you go online), check
  the address and port, and for internet hosting check the router's UDP
  port forward. LAN discovery only finds hosts on the default port 27007.
- **ONLINE (CANNOT REACH THE SERVICE)** — no internet, a firewall blocking
  the game's HTTPS connection, or the service is down or over its daily
  free allowance (it resets at 00:00 UTC). LAN and direct play still work.
- **"No response from the host (tried N addresses)"** joining an online
  game — the two routers could not be connected directly (see *Online* above).
  The host can forward their UDP port (Settings → Host port) and host again;
  that makes them reachable from anywhere.
- **Stuck on the waiting screen** — another player is lagging or has
  frozen; after 8 seconds without contact they are dropped. **Shift+F9**
  leaves at once. The window title always shows the online status too.
- **OUT OF SYNC** — please report it with every player's log. The game
  writes its log to standard error; start it from a terminal as
  `ge007.x86_64.exe 2> netlog.txt` (or `./ge007.x86_64 2> netlog.txt` on
  Linux) to capture it. The log records the match id, stage, seed and
  input delay, and the frame where the PCs first disagreed
  (`DESYNC detected at frame N`).

**What is sent, and to whom:**

- **The other players** (directly, or through the host or server you chose)
  get your player name, lobby choices, chat and controller input. They see
  your IP address, as with any online game.
- **The online service** gets your player name and game build when you open
  the Online menu, plus your games' details while you host:
  - name, players, stage, and your public and LAN addresses, which it passes
    only to people joining;
  - finished-match results for the public status page. Private games are
    shown there anonymously.
- **Cloudflare's STUN server** gets one small request when you host or join
  online, so your PC can learn its public address.

Nothing is sent anywhere until you open the Online menu or use a `--net-*`
option, and nothing is sent to the developers.
