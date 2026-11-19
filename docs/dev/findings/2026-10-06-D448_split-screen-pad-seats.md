# D448

<!-- from the old section-F/H table row -->
| D448 | **Split-screen pad seats: `pads[]` indexed by seat and keyed by SDL instance id; seats compact outside a match and stay reserved during one (an unplug no longer shifts other players or flips keyboard/mouse-as-P1 routing; a rescan no longer reopens attached pads).** — full `## D448` entry at file tail | PARTIAL (committed 918ac5f9 on `feat/mp-splitscreen-99`, build + 2P smoke OK; plug/unplug behaviour not exercised at runtime). |

## D448 — Split-screen pad seats keyed by SDL instance id (issue #99, D416 lineage, 2026-09-30)

**Status: PARTIAL.** Committed as `918ac5f9` on `feat/mp-splitscreen-99` (the commit message calls it "DNEW-mp-1"; this is its label). It was recovered from a crashed session's uncommitted worktree (archive `2026-09-30-recovery/mp-uncommitted-input-bg.patch`). Its TEMP-VPADSEAT probe was removed before commit.

**What:** `port/src/input.c` `pads[]` is indexed by *seat*, not by SDL device index, and each seat remembers its SDL joystick instance id. Outside a match, seats compact (the first attached pad is seat 0, and so on). During a match they stay reserved, so unplugging one pad no longer shifts the other players' seats or flips the Auto → keyboard/mouse-as-P1 routing. A rescan keeps pads that are still attached open instead of closing and reopening them. Port-only.

**Verified:** build, plus a 2P smoke (`GE_STARTMP=2 GE_MPVIRT=1`, 1280x720) in Window and Original aspect modes after merging `release/v0.4.1`. **Owed (maintainer, live):** plug/unplug pads mid-match (the seats must not shift), and check the keyboard/mouse-as-P1 routing is unchanged after an unplug. Related: outside PR #122 (hot-plug reopen, dolent) overlaps this area. Review it against this design before merging it.
