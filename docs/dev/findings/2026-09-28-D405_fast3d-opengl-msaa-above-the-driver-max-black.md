# D405

<!-- from the old section-F/H table row -->
| D405 | **fast3d/OpenGL: MSAA above the driver max black-screens; two `glBindFramebuffer` calls pass an index where a GL name is required** (PR #106, italoarruda, 2026-09-25; label rebased from the PR's D323, which collides with our D323 LOD finding) — full `## D405` entry at file tail | **CLOSED (2026-09-28).** Merged from PR #106 (code file only; security-reviewed clean - port-layer GL state fixes, no external input, no allocations). `resolve_msaa_color_buffer` clamps `msaa_level` to `GL_MAX_SAMPLES`; the resolve/copy paths bind `current_framebuffer ? framebuffers[i].fbo : 0` (the default framebuffer is GL name 0). Both Windows (opengl32) and Linux use this backend. Post-merge sweep 21/21 PASS + MSAA=16 boot check render-verified; link ritual clean. |

## D405 — fast3d/OpenGL MSAA clamp + default-framebuffer bind (PR #106, italoarruda, security-reviewed and merged 2026-09-28; label rebased from the PR's D323)

**PR #106 (merged into v0.4.0).** Two `port/fast3d/gfx_opengl.cpp` fixes, both latent in the shipped renderer (Windows *and* Linux use the OpenGL backend):

1. `resolve_msaa_color_buffer`: `glRenderbufferStorageMultisample` with a `msaa_level` above the driver's `GL_MAX_SAMPLES` silently leaves the renderbuffers unallocated -> black screen when the user sets MSAA beyond what the GPU/driver offers (common on integrated GPUs and old IGP drivers). Fix: clamp `msaa_level` to `GL_MAX_SAMPLES` (queried once) before allocating.
2. Two `glBindFramebuffer(GL_FRAMEBUFFER, ...)` calls passed the *index* `current_framebuffer` (or `framebuffers[current_framebuffer]` while `current_framebuffer == 0`) where a GL *name* is required: when `current_framebuffer == 0` the call must bind `0` (the default framebuffer), not `framebuffers[0].fbo`. Binding index 1-3 as a raw name hits whatever unrelated GL object (or an invalid name -> GL error) that number holds.

**Security review (2026-09-28, per the unvetted-contributor policy):** port-layer only, three hunks, no external input, no allocations, no control-flow changes beyond the clamp; N64 build untouched; no game-code edits. Clean. Integrated by cherry-pick of the PR head's code file (`fc092d99`); the PR's docs hunks were superseded by our canonical findings log, and its D323 label rebased to D405 (our D323 is the G_TL_LOD finding).

**Verification (2026-09-28):** post-merge 21-level sweep PASS (build-pc Windows); MSAA=16 boot check: `-level_09` renders 900+ frames in a 20s window with no black screen and no crash (the clamp is silent by design; on drivers whose `GL_MAX_SAMPLES` < 16 the pre-fix build black-screened here). Link ritual: no undefined port symbols; the only duplicate `nm` hits are pre-existing per-TU internal statics (e.g. `_ZL12framebuffers` in each gfx backend TU) — legal under C++ unique linkage, unchanged by this PR.

**Status:** CLOSED (merged to v0.4.0 as 50e3426a, 2026-09-28; PR #106 closed as landed).
