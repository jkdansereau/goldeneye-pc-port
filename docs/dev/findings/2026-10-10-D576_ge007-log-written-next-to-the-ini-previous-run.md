# D576

<!-- from the old section-F/H table row -->
| D576 | **`ge007.log` written next to the ini (previous run kept as `ge007.prev.log`, 8 MiB cap)** — full `## D576` entry at file tail | FIXED 2026-10-07 (Windows runtime check) |

## D576: `ge007.log` next to the ini — FIXED 2026-10-07

A normal (double-click) launch loses stderr, so every `sysLogPrintf` /
`sysFatalError` line now also goes to `$S/ge007.log` (`port/src/system.c`).
`sysLogOpenFile()` runs in `main()` just before the startup banner (`$S`
resolves from the working/exe dir with no setup), moves a previous log to
`ge007.prev.log`, and registers an `atexit` close. Each line is formatted once
into a 1 KB buffer and written with one call to stderr and one to the file
(whole lines when threads log at once), flushed per line so a crash keeps the
log. The file stops at 8 MiB with one note line; a fatal line is written even
past the cap. If the file cannot be opened, logging stays stderr-only. Lines
over ~1 KB are now truncated on stderr too (none in normal runs).

Implemented by the local model from a scoped brief; the lead fixed one
off-by-one in review (the truncation clamp let the terminator land one byte
past the buffer, because `vsnprintf` returns the untruncated length) and made
the fatal line bypass the cap. **Verified (Windows):** two front-end runs with
`GE_QUITFRAME`: banner is line 1, the first run's log became
`ge007.prev.log`, the file is byte-identical to stderr.
