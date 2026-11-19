# Finding log: moved

The single `findings.md` log was split on 2026-11-19 into one file per finding
in [`findings/`](findings/). This stub stays so the many code comments that say
`docs/dev/findings.md D45` still lead somewhere. Do not add to it.

- **Find a legacy label** (`D45`, `D176(a)` is `D176a`): `ls docs/dev/findings/*D45_*`.
- **Search by topic:** `python tools_pc/gen_findings_index.py`, then grep
  `docs/dev/findings-index.csv` (untracked, rebuilt locally).
- **A `Dxx` that is not found:** check [`findings/_legacy.md`](findings/_legacy.md)
  (the old scaffolding review and the D24-D121 narrative). Removed feature
  journals (mostly D960-D987) are in git: `git show 6ffaee29:docs/dev/findings.md`.
- **What qualifies as a finding, file naming, how to write one:**
  [`findings/README.md`](findings/README.md).
