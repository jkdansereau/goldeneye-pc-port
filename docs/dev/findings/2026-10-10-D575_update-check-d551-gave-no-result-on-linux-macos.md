# D575

<!-- from the old section-F/H table row -->
| D575 | **Update check (D551) gave no result on Linux/macOS when `/releases/latest` exceeded 64 KB (curl SIGPIPE); parse helpers moved to a header with an offline test** — full `## D575` entry at file tail | FIXED 2026-10-07 (harness 19/19; live Linux/Deck matrix owed) |

## D575: update check gave no result on responses over 64 KB (Linux/macOS) — FIXED 2026-10-07

`fetchLatest()` (`port/src/updatecheck.c`, non-Windows) read curl's output
through `popen()` into a 64 KB buffer and closed the pipe when it was full;
curl, still writing, died on SIGPIPE, and the nonzero exit made the whole
check report "no result" although `tag_name` sits in the first ~1.5 KB. Now
the rest of the pipe is drained before `pclose()`, a full buffer counts as
success (an HTTP error, curl `-f` with an empty body, still fails), and the cap
is 256 KB (static buffer). Windows (WinHTTP) already stopped cleanly at the
cap; unchanged. `parseVer` / `versionNewer` / `extractTag` moved verbatim to
`port/include/updatecheck_parse.h` (static inline, no SDL) so
`tools_pc/updatecheck_test.c` can test them offline: 10 version comparisons
and 9 tag-extraction cases (spaces, newline, missing field, control bytes,
empty, unterminated, over-long tag into a 32-byte buffer, a 300 KB body).

Implemented by the local model from a scoped brief; reviewed by the lead.
**Verified:** harness 19/19 PASS; Windows build rc 0. **Owed (maintainer):**
the live half of the D551 validation matrix: throwaway builds with an old
`GE007_VERSION` on Windows and the Deck against the real endpoint.
