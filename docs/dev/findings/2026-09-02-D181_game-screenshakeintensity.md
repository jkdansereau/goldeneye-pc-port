# D181

<!-- from the old section-F/H table row -->
| D181 | **`Game.ScreenShakeIntensity` — first route-(b) `src/` gameplay-cosmetic hook (M-XX QoL run).** — full `## D181` entry at file tail | FIXED (2026-09-26 bookkeeping: shipped; user reports it feels good in v0.4.0 play). Earlier: **Menu row pulled (v0.2.1, user testing).** The hook is correct on its own narrow terms —… |

## D181 — `Game.ScreenShakeIntensity` — first route-(b) `src/` gameplay-cosmetic hook (M-XX QoL run).

**`Game.ScreenShakeIntensity` — first route-(b) `src/` gameplay-cosmetic hook (M-XX QoL run).** `src/fr.c viShake()` gains a single `#ifdef PORT` line — `intensity *= portScreenShakeScale;` (extern `f32`, defined + `configRegisterFloat`'d 0.0–10.0 in `port/src/video.c`) — before the existing clamp. Default `1.0f` ⇒ exact no-op, every headless golden dump byte-unaffected; `0.0` disables explosion/effect screen shake, up to `10.0` exaggerates it. N64 build (no `-DPORT`) keeps the original line verbatim. Precedent per `docs/BACKLOG.md` "Fun features → screen-shake intensity slider" and AGENTS.md #2's documented-exception path. Same policy class as a future FOV hook.

**Status (from §F table):** **Menu row pulled (v0.2.1, user testing).** The hook is correct on its own narrow terms — `viShake()` is called only from `explosion.c`, so the slider does exactly what it says (scales explosion/effect shake) — but that's a much narrower scope than the "Screen shake" label implies to a player: it does not touch the always-on walking head-bob or any getting-shot reaction, so a user swinging it from 0 to max during ordinary play sees nothing and reasonably calls it broken. Removed from `port/src/optionsoverlay.c`'s row table until it covers all screen-shake/view-bob sources, not just explosions; config var (`portScreenShakeScale`) and the `fr.c` hook are untouched. Build green.
