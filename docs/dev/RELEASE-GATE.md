# Release gate driver

`tools_pc/release_gate.sh` runs the automatable pre-release checks and prints one
PASS / FAIL / SKIPPED table. It wraps `verify.sh` (pixel gate) and `levelbench.sh`
(scripted run, clean-quit and crash-log detection); every run ends through
`GE_QUITFRAME`, nothing is hard-killed.

```sh
tools_pc/release_gate.sh --dry-run all       # print what would run
tools_pc/release_gate.sh win                 # sweep vs win goldens + fire smoke + exit path
tools_pc/release_gate.sh deck <host>         # 21 levels via deckrunner (shortcut must be up)
GE_MAC_DIR=<remote game dir> tools_pc/release_gate.sh mac <host>
```

Options: `--secs N` (per-level run length, default 30), `--out DIR`. Hosts and
paths come only from arguments or env (`GE_WIN_DIR`, `GE_DECK_HOST`,
`GE_MAC_HOST`, `GE_MAC_DIR`, `GE_MAC_EXE`, `GE_MAC_BUILD_CMD`,
`GE_GATE_SSH_OPTS`). The Deck pixel compare is reported SKIPPED (640x480
goldens vs 1280x800 dumps); Mac has no goldens, so it is crash-free only. The
fire smoke is `GE_INPUTSCRIPT` `MLB1`/`MLB0` (D597, the D595 regression).
Remote job, result and script files are deleted after the summaries are pulled.

## Release and history rules

Moved from `AGENTS.md` §Tree hygiene & release gate (rules 6 and 7).

6. **Release gate.** Before pushing a release: the PII scrub and legal/copyright
   review reports (gitignored, `docs/dev/notes/`) must be run and clean, and the
   README Legal section must name every rights holder the game's trademarks
   implicate. Rewriting pushed history (squash) needs explicit maintainer
   consent, and any such report states the remote + branch it applies to.
7. **Squashes and history rebuilds must not erase contributor PRs.** Release
   lines may squash *our own* work, but every contributor PR whose content
   shipped gets a zero-diff record merge at its timeline position (merge
   commit with the PR head as second parent, tree unchanged) so the PR shows
   as merged and the contributor's commits stay reachable in public history
   with their authorship. If the PR's base is a stale internal branch, merge
   it there via `gh pr merge` so the badge is true; GitHub refuses a base
   change to a branch that already contains the head. Superseded/reworked
   PRs are never merged — comment + let the author close. Before force-
   pushing a rewritten release branch: back up the pushed tip to Forgejo
   (`backup/<branch>-pre-rebuild-<date>`), pass a byte-identical tree-diff
   gate against the old tip, re-run CI, and note the rebuild in any open
   issue that tells people to build the branch.
