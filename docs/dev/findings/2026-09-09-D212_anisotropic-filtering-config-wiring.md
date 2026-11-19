# D212

<!-- from the old section-F/H table row -->
| D212 | **Anisotropic-filtering config wiring — `Video.Anisotropy` (M-83 QoL wave, port-only).** — full `## D212` entry at file tail | FIXED (2026-09-26 bookkeeping: shipped; user reports it feels good in v0.4.0 play). Earlier: **LANDED, port-only.** Build clean. **Owed:** eyeball at 16× on a grazing surface (Depot roof / Dam walkway) vs 1×. |

## D212 — Anisotropic-filtering config wiring — `Video.Anisotropy` (M-83 QoL wave, port-only).

**Anisotropic-filtering config wiring — `Video.Anisotropy` (M-83 QoL wave, port-only).** The fast3d GL backend already implemented `set_anisotropy_level` / `get_max_anisotropy_level` and applied `GL_TEXTURE_MAX_ANISOTROPY` on mipmapped tiles with a hardcoded `current_anisotropy_level = 4`; there was no way to configure it. Added the `gfx_set_anisotropy_level()` extern-C wrapper in `gfx_pc.cpp` (clamps to `[1, GL max]`, `reset_texture_state()` so live changes take), declared in `gfx_api.h`, and `Video.Anisotropy` (1–16) in `port/src/video.c` wired through `videoApplyImageOptions()` on init + live-config. **Default 4 = the value fast3d already applied ⇒ no visual delta at defaults.** F10 overlay row "Anisotropic".

**Status (from §F table):** **LANDED, port-only.** Build clean. **Owed:** eyeball at 16× on a grazing surface (Depot roof / Dam walkway) vs 1×.
