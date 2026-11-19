# D534

<!-- from the old section-F/H table row -->
| D534 | **Pre-release review of the 2026-10-05 local batch — Ctrl-C in the golden gate could leave the user's save replaced, plus doc accuracy fixes** — release gate; full `## D534` entry at file tail | FIXED 2026-10-05 (`verify.sh`/`capture_p7.sh` INT/TERM trap; release-notes linux-gate wording; dead `security-and-fidelity-status.md` links; future-dated `2026-10-07` labels normalised; D526 probe dangling-else; verify.sh usage range) |

## D534: pre-release review of the 2026-10-05 local batch — Ctrl-C in the golden gate could leave the user's save replaced, plus doc accuracy fixes (release gate)

**Scope:** a read-only review of the 23 local commits of 2026-10-05
(`8fbf8aa0^..0b0f610c`) before the v0.5.0 cut; defects fixed in one batch.

**The real one — an interrupted gate run could clobber `data/ge007.eep`.**
`verify.sh` armed `trap 'restore_ini; restore_eep; kill_ours' EXIT INT TERM`
(`capture_p7.sh` the same shape). Bash runs an INT/TERM trap and then
*continues* the script: the restore put the user's save back and deleted the
snapshot, but `EEP_PIN_BAK` stayed set, so the next level's `pin_eep` skipped
the snapshot and installed the canonical save, and the final EXIT restore
copied from a deleted file — the user's own save was left replaced on disk.
Fix: restore on `EXIT` only, `INT`/`TERM` just `exit 130`/`143` (which fires
EXIT once); `restore_eep`/`restore_ini` now clear their snapshot state, so
they are idempotent. Tooling-only; the recipe is unchanged.

**Doc accuracy:** the release notes said "each platform's gate is green"
while the same paragraph said the linux pixel gate did not run — now "Windows
green; linux crash gate green, pixel verdict owed". `docs/security-and-fidelity-status.md`
was split into `security.md` + `fidelity.md` but still linked from `README.md`,
`.github/SECURITY.md` and the AGENTS.md site-page list (dead on publish) —
repointed. Fifty-odd `2026-10-07` dates (session labels "07a…07k" that leaked
in as calendar dates; every commit is dated 2026-10-05) normalised to
2026-10-05; lettered labels became "2026-10-05 (session 07x)".

**Nits:** the D526 census probe had a dangling `else` (bound to the inner
`if`, so with no `GE_D526BOX` the off-screen filter never ran) — braced;
`verify.sh`'s usage print (`sed -n '2,32p'`) cut the `--json`/`--against`
lines — now `2,34p`; an unbalanced bold in the ROADMAP Golden-baselines row.
