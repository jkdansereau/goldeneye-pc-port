# Public site pages (`docs/`) — presentation layer, not a source of truth

The repo's homepage and sub-pages are published by GitHub Pages; the local
preview is `node scratch/build_preview.mjs site` → `scratch/_site/` (served
with `node scratch/serve_preview.mjs 8777`). The site is a **presentation
layer over the reference docs, kept in a separate area, and allowed to go
stale** — never let it poison a work item:

- **Pure site pages** — `docs/index.md`, `documentation.md`,
  `the-software-rsp.md`, `the-asset-pipeline.md`, `framerate-and-pacing.md`,
  `input-and-aim.md`, `saves.md`, `security.md`, `fidelity.md` —
  summarise the ROADMAP / README / finding log / release notes. **Do not
  consult them as work information, cross-check against them, or treat a
  mismatch with `docs/ROADMAP.md` or the finding log as a bug** (that is
  the expected, independent-staleness case). Update them only when the task
  says so, and source every fact from the ROADMAP / README / finding log —
  never the other way round. The footer "last updated" date in
  `docs/_layouts/default.html` is the same class of site-only detail.
- **Shared files** — `docs/internals.md`, `porting-notes.md`,
  `building.md`, `dev-process.md`, `dev/agentic-development.md` are
  authoritative working docs that the site publishes verbatim; edit them
  normally as part of dev work and the site picks up the change for free.

Keep the split intact: no working notes migrate into pure site pages, and
never edit a shared doc merely to make the site read better — if the site is
stale, fix the site page (or the fact upstream, as its own task).
