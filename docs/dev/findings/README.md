# Findings

A finding is a **trap**: a root cause that someone hitting the same symptom
would otherwise spend a session rediscovering. Platform and ABI quirks, an
assumption that looked right and was not, a hypothesis that was ruled out and
should not be re-walked.

It is not a progress log. The PR, the issue and the commit message track a
feature; `docs/ROADMAP.md` tracks the big items that are still too vague for an
issue.

A "not a port bug" verdict is a finding too, and a valuable one: the N64 does
this too (confirmed on the cartridge or in ares), so nobody should chase it as
a regression. Examples: the Dam intro truck wheels (D306), the Jungle ferns
drawn over the leaves in front of them (D980), Bunker 1's missing solo sky
(D976). Keep them even when the entry is closed as a non-issue.

## What does not get a finding

- A feature's build, measurement or "console test owed" status. PR description.
- A parked or abandoned experiment. The PR or the issue it belongs to. (If the
  experiment taught a trap, write that trap, not the experiment's diary.)
- Merge order or "this depends on that". PR description ("depends on #N") or a
  stacked PR.
- A bug introduced and fixed inside the same unmerged branch. Commit message.
- A tuning value, a new option, a design decision. The code comment or the
  option's own doc.

Test: would a stranger who hits this symptom in six months save time by reading
it? If not, it is not a finding.

## File naming

One file per finding, `docs/dev/findings/YYYY-MM-DD-slug.md`.

- The date is the day the file is written. The date plus the slug is the
  identity; there is no counter, so two branches never collide.
- The slug is kebab-case, at most about 48 characters, and says what the trap
  is (`switch-sdl-lists-one-joystick-per-npad`).
- Findings from before 2026-11-19 keep their old label in the name:
  `YYYY-MM-DD-Dxx_slug.md`. Code comments and docs that cite `D961` resolve
  with `ls docs/dev/findings/*D961_*`. The date is when the entry reached
  `main`, not necessarily when the bug was found.
- `_legacy.md` holds the pre-split head of the old log (scaffolding review, the
  D24-D121 session narrative, range rows). It is frozen.
- Feature journals (build and measurement logs, mostly D960-D987) were removed on
  2026-11-19. Their text is in git: `git show 6ffaee29:docs/dev/findings.md`.

## Writing one

Suggested sections: Symptom, Root cause, Fix or workaround (with the files),
Ruled out. Keep it to what the next reader needs.

A finding has no status. If it turns out to be wrong or incomplete, edit it in
place with a dated `Update` section, or delete it. Do not edit another
finding's file from a feature PR except to correct it.

Subagents do not write findings. They put the write-up in their report or in
the PR; the lead decides whether it meets the test above and files it.

## Looking one up

```sh
python tools_pc/gen_findings_index.py     # writes the untracked docs/dev/findings-index.csv
grep -i "joystick" docs/dev/findings-index.csv
ls docs/dev/findings/*D961_*              # a legacy label
python tools_pc/gen_findings_index.py --check   # CI: file names and unique labels
```

Read the one file, not the directory.
