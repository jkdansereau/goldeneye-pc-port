# D527

<!-- from the old section-F/H table row -->
| D527 | **`GE_PCDUMP` written `first:last` (a colon where the dash belongs) silently dumps EVERY frame** — harness trap (`port/src/video.c`); full `## D527` entry at file tail | OPEN (harness documentation; a log-or-fail-closed fix is owed in the port layer) |

## D527: `GE_PCDUMP` written `first:last` (a colon where the dash belongs) silently dumps EVERY frame (harness trap)

**OPEN (harness documentation; no code change yet).** `port/src/video.c:1357` parses `GE_PCDUMP` with `sscanf("%d-%d:%d")`. If the lo/hi pair is written `first:last` (a colon where the dash belongs), the `%d-%d` part fails to match, the fallback sets `hi=0x7fffffff` and `step=1`, and the dump writes **every** frame from `first` onward at ~921 KB each — a first launch in the 2026-10-05 session ate ~1.5 GB before it was noticed (the run went to ~1500 fps until the window was closed). The failure mode is silent: no error line, the dump just runs.

Fix owed (port-layer only): log a NOTE when the `sscanf` does not consume all three fields (e.g. "GE_PCDUMP 'x:y' is not 'first-last:step' — dumping every frame"), or fail closed (ignore the value) — either way the operator must see which window is active.

**Never do:** write `GE_PCDUMP` as `lo:hi` — use `first-last:step` (a dash between lo and hi, a colon before the step).
