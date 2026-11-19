# D551

<!-- from the old section-F/H table row -->
| D551 | **Opt-in update check (`Game.CheckUpdates`, default off): one HTTPS GET per launch to GitHub `/releases/latest`; `GE007_VERSION` added** — full `## D551` entry at file tail | ADDED 2026-10-06 (port layer only; live-verified Windows/Linux/Deck + F10 UI capture; security review fixes applied) |

## D551: opt-in update check (`Game.CheckUpdates`, default off) -- ADDED 2026-10-06 (v0.5.0 finalize)

**What.** `port/src/updatecheck.c` (+ `updatecheck.h`). `updateCheckStart()` is
the only network entry point; its first line is `if (!cfgCheckUpdates) return;`,
so with the ini value 0 there is no thread, no DNS and no library load. It is
called once from `main.c` right after `configLoad()`. When on: one detached
thread makes a single HTTPS GET to `api.github.com/repos/<repo>/releases/latest`
(5 s timeouts, fixed User-Agent, nothing else sent), parses `tag_name`, and
compares it with the new `GE007_VERSION` (CMake `set(GE007_VERSION "0.5.0")`,
bump on every release; `versioninfo.h.in`). Failure is silent (one log line).
`/releases/latest` excludes drafts and pre-releases on purpose (maintainer
decision).

**Windows.** No link-time winhttp dependency: `winhttp.dll` is `LoadLibraryW`'d
inside the opt-in path, nine functions resolved with `GetProcAddress`,
`FreeLibrary` at the end, so the exe's import table never lists it. Linux runs
`curl` via `popen` (URL single-quoted); no curl = no result.

**UI.** Game page toggle "Check for updates" (+ reset-table entry, tip text) and
an action row "Update available: open page" shown only once a newer tag is
known (`SDL_OpenURL` on the releases page). CI: the tag-only `release` job now
fails if the tag differs from `v` + `GE007_VERSION`.

**Status.** Port layer only, no game logic touched. Build-verified; exe
imports no winhttp (objdump). **Live-verified 2026-10-06 (Windows, isolated
work dir, orderly GE_QUITFRAME quit):** Off -> no update-check activity;
On -> real HTTPS GET, `update check: up to date (latest v0.4.0, running
v0.5.0)`; On with a temporary GE007_VERSION 0.3.0 build -> `v0.4.0 is
available (running v0.3.0)`. **Linux + Steam Deck (2026-10-06, system
curl):** off -> no activity, on -> "up to date". **UI:** scripted F10 run
(GE_OVNAV) on the 0.3.0 build: the "Check for updates" toggle and the
"Update available: open page" row render at the bottom of the Game page
within the column widths, and the row appears live while the page is open
(visibility is rebuilt every emit). **Security review (2026-10-06):**
SHIP-WITH-FIXES, fixes applied: winhttp.dll loaded with
`LOAD_LIBRARY_SEARCH_SYSTEM32` (no DLL planting from the exe folder);
curl run as `curl -q --proto =https` (ignores ~/.curlrc, HTTPS only);
tag restricted to `[0-9A-Za-z._-]` before it reaches the log;
`snprintf` instead of a 32-byte `memcpy`; help text says the change
applies from the next launch; security docs name the curl process, the
System32 load, and that GitHub sees the request and the IP. Confirmed
fine: gate ordering (configLoad before updateCheckStart), constant URLs
(no injection), TLS validation left at system defaults, bounded parsing,
SDL_OpenURL with a constant URL, CI tag check uses the env var quoted.
Owed: nothing beyond pressing the row once in a live session (opens the
browser).
