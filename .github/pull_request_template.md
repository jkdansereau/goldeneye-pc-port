<!--
Read CONTRIBUTING.md first. This is a faithfulness-focused port; the ground
rules there are non-negotiable.
-->

## What this changes

<!-- One or two sentences. Link the issue it closes: "Closes #123". -->

## Why

<!-- The reasoning, not just the diff. What was wrong / missing? -->

## Scope check

- [ ] No changes under `src/` or `include/` — **or** the only changes are the
      narrow `#ifdef PORT` ABI exception (CONTRIBUTING.md rule 2), and each is
      documented in `docs/porting-notes.md` / `docs/dev/findings.md`.
- [ ] `Makefile`, `tools/`, `rsp/`, `ld/` untouched (N64 build).
- [ ] If `CMakeLists.txt` `REGION_DEFS` changed, it still matches the N64
      `Makefile` per-region macro set exactly.

## Verification

<!-- What you actually ran. Delete lines that don't apply. -->

- [ ] `./build-pc.sh ntsc-final` — clean configure + link
- [ ] Crash-free run of at least one level (`-level_09`)
- [ ] Single-frame `GE_PCDUMP` diff against the committed golden — no
      unexpected change
- [ ] pal-final / jpn-final also configured

Platform tested: <!-- e.g. Windows 10 / MSYS2 MINGW64 -->

## Notes for the reviewer

<!-- Anything uncertain, follow-ups, or areas that need a closer look. -->
