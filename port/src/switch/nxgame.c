/*
 * nxgame.c -- gameplay feedback options (project-dam). Game.BloodOnHit is
 * built everywhere (off by default off the Switch); the aim options below it
 * (Aim assist, Aim entry, Stop on aim) work on every platform, Game.AimZoom and
 * Look ahead only show in the Switch menu.
 *
 * Game.BloodOnHit: the puff a bullet makes on a guard's body is, in the
 * original game, the same pale-yellow puff as on a wall: g_HitReactionTable
 * (src/game/chr.c) asks bullet_spark_create for puff type 1 on every body
 * part, and type 1 is { 0xFF, 0xFF, 0xC8 } in g_BulletSparkColors
 * (src/game/glass2.c). The game already has a red puff, type 2 { 0xFF, 0, 0 },
 * with the same image and lifetime (bullet_sparks_init's default branch), that
 * nothing uses. With the option on, the body parts ask for type 2, so a hit on
 * a guard reads as a hit; the gun and the hat keep type 1. Off restores the
 * table as the game ships it. Port-side data only: no game code changes.
 */
#include "platform.h"

#include "game/chr.h"
#include "game/bondview.h"   /* struct player (Game.AimAssist) */
#include "game/stan.h"
#include "config.h"
#include "system.h"

#define PUFF_YELLOW 1   /* g_BulletSparkColors[1], the game's body and wall puff */
#define PUFF_RED    2   /* g_BulletSparkColors[2], unused by the game */
#define HIT_FIRST_BODY_PART 1      /* HIT_LEFT_FOOT */
#define HIT_LAST_BODY_PART  0xF    /* HIT_CHEST; HIT_GUN 0x64 and HIT_HAT 0x6E stay yellow */

/* The game's type 2 is pure red, which reads as paint rather than blood.
 * Nothing in the game uses type 2, so the port tones it down to a dark
 * blood red; the gun and hat puffs and every wall puff are unaffected. */
#define BLOOD_R 0x8C
#define BLOOD_G 0x0A
#define BLOOD_B 0x0A

extern struct rgba_u8 g_BulletSparkColors[8];   /* src/game/glass2.c */

#if defined(PLATFORM_SWITCH)
static int s_bloodOnHit = 1;
#else
static int s_bloodOnHit = 0;   /* the game's own puff unless the player opts in */
#endif

void nxGameApply(void)
{
    const int puff = s_bloodOnHit ? PUFF_RED : PUFF_YELLOW;
    g_BulletSparkColors[PUFF_RED].r = BLOOD_R;
    g_BulletSparkColors[PUFF_RED].g = BLOOD_G;
    g_BulletSparkColors[PUFF_RED].b = BLOOD_B;
    for (int i = 0; g_HitReactionTable[i].hitpart != -1; i++) {
        const s32 part = g_HitReactionTable[i].hitpart;
        if (part >= HIT_FIRST_BODY_PART && part <= HIT_LAST_BODY_PART &&
            g_HitReactionTable[i].impactPuffCount != 0) {
            g_HitReactionTable[i].impactPuffCount = puff;
        }
    }
}

PD_CONSTRUCTOR static void nxBloodRegisterConfig(void)
{
    configRegisterInt("Game.BloodOnHit", &s_bloodOnHit, 0, 1);
}

/*
 * Game.AimZoom: extra zoom while aiming, handheld only. On the 6.2" panel a
 * guard's head is a few pixels wide, so the weapon's own aim zoom (none for
 * most guns) is not enough to place a headshot. With the option on, the
 * rendered vertical FOV is divided by this factor while the player aims
 * (portScaleFovY, port/src/video.c); the game keeps its own FOV, so the shots
 * and the crosshair are unchanged. Centered aim styles and one player only,
 * since the Original crosshair travels over the screen and would no longer
 * line up with the zoomed picture. The factor eases in and out, and the aim
 * stick sensitivity follows it (input.c), so the camera feels as fast on
 * screen as without the zoom.
 */
#include <time.h>

#if defined(PLATFORM_SWITCH)
extern int nxIsHandheld(void);
#else
static int nxIsHandheld(void) { return 0; }   /* Game.AimZoom is a handheld option (nxmem.c) */
#endif
extern s32 getPlayerCount(void);
extern bool currentPlayerGetIsAiming(void);

#define AIMZOOM_EASE_SEC 0.07   /* time constant of the zoom in / out */

static int   s_aimZoom = 2;      /* 0 off, 1 x1.5, 2 x2 */
/* D960 (co-op): every aim feature keeps its state per player (index = the player's number), and the "one player"
 * tests ask coopIsMission(), true in solo and in a co-op campaign. Game thread: the current player; input thread:
 * the slot passed in (nxAimStickP). */
extern s32 get_cur_playernum(void);
extern int coopIsMission(void);
extern struct player *g_playerPointers[4];
static int nxCp(void)
{
    const s32 i = get_cur_playernum();
    return (i >= 0 && i < 4) ? (int)i : 0;
}
static int nxPidx(const struct player *p)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (g_playerPointers[i] == p) {
            return i;
        }
    }
    return 0;
}
static float s_aimZoomCurA[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static double s_aimZoomTA[4];
#define s_aimZoomCur (s_aimZoomCurA[nxCp()])
#define s_aimZoomT   (s_aimZoomTA[nxCp()])

/*
 * Game.AimAssist: the aim stick slows down while the crosshair is on a guard.
 * Once per frame (nxAimAssistFrame, called from lvlRender right before the
 * game's own shot tests) a ray goes through the screen centre and is tested
 * against the on-screen armed guards with the game's real bullet hit test
 * (chrTestHit: the body, head and arms), plus four rays a few screen units to
 * the side that give half strength, so the slowdown starts just before the
 * crosshair gets there. A guard behind a wall, a door or glass does not count
 * (the game's line of sight for its auto-aim). The game's own auto-aim target
 * is not used: it only accepts a guard within about 1.5 body widths of the
 * centre (less on harder levels), its width test reads model extents that come
 * back empty on the PC build, and its aim point is the torso. The strength
 * eases in fast and out slowly so it does not flicker at the edge of a guard.
 * See docs/dev/findings.md D956.
 */
static int   s_aimAssist = 2;    /* 0 off, 1 low, 2 high */
static float s_assistTA[4];      /* eased 0..1, read by the input thread */
static double s_assistClockA[4];
static int   s_losFailA[4];      /* frames in a row the only guard in range was refused by the line of sight */
#define s_assistT     (s_assistTA[nxCp()])
#define s_assistClock (s_assistClockA[nxCp()])
#define s_losFail     (s_losFailA[nxCp()])

extern PropRecord **g_LastOnScreenProp;
extern PropRecord *g_OnScreenPropList[];
extern struct player *g_CurrentPlayer;
extern f32 getPlayer_c_screenwidth(void);
extern f32 getPlayer_c_screenheight(void);
extern f32 getPlayer_c_screenleft(void);
extern f32 getPlayer_c_screentop(void);

#define ASSIST_RING      6.0f    /* screen units (320 across in 4:3) */
#define ASSIST_RING_T    0.5f    /* strength when only a side ray hits */
#define ASSIST_RISE_SEC  0.03    /* time constants of the strength */
#define ASSIST_FALL_SEC  0.15
#define ASSIST_LOS_HOLD  8        /* frames a refused line of sight is ignored */
#define ASSIST_MIN_STEP_SEC 0.004   /* one update per frame at most */

/* The game's line of sight for its auto-aim (chrpropScoreAutoAimTarget):
 * the player's tile to the guard's, blocked by objects, doors and glass. */
static int aimAssistVisible(PropRecord *target)
{
    PropRecord *pp = getCurrentPlayerProp();
    StandTile *line;
    f32 h;
    int ok, ret;

    if (pp == NULL || target == NULL) {
        return 0;
    }
    line = pp->stan;
    h = bondviewGetPlayerDuckingHeightRelated(g_CurrentPlayer);

    bondviewUpdateGuardTankFlagsRelated(pp, FALSE);
    ret = stanTestLineUnobstructed(&line, pp->pos.f[0], pp->pos.f[2], target->pos.f[0], target->pos.f[2],
                                   CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER, h, h, 0.0f, 1.0f);
    bondviewUpdateGuardTankFlagsRelated(pp, TRUE);
    ok = ret != 0 && line == target->stan;
    return ok;
}

/* The game's real hit test (chrTestHit, a bullet's) for a ray
 * through screen point (sx, sy), on this guard only. It needs the guard's hit
 * list (chr->field_20), which only exists between propsTick and the render
 * passes, where the player's own shots are tested: nxAimAssistFrame runs there.
 * The game's shot ray (bullet_path_from_screen_center) draws random numbers, so
 * the direction comes from the pure projection it uses underneath. */
static int aimAssistRayHits(PropRecord *pr, float sx, float sy)
{
    ShotData shot;
    coord2d scr;
    ChrRecord *chr = pr->chr;
    const CHRFLAG flags = chr->chrflags;
    const s8 close = chr->numclosearghs;
    int k, hit = 0;

    scr.x = sx;
    scr.y = sy;
    shot.viewOrigin.x = shot.viewOrigin.y = shot.viewOrigin.z = 0.0f;
    transformAndNormalizeByLength2Dto3D(&scr, &shot.viewDir, 1.0f);
    shot.weapon = getCurrentPlayerWeaponId(GUNRIGHT);
    shot.maxdist = 1.0e9f;
    for (k = 0; k < 10; k++) {
        shot.hits[k].prop = NULL;
        shot.hits[k].hitpart = 0;
        shot.hits[k].node = NULL;
    }
    chrTestHit(pr, &shot);
    chr->chrflags = flags;   /* chrTestHit flags a near miss, which makes the guard flinch: undo */
    chr->numclosearghs = close;
    for (k = 0; k < 10; k++) {
        if (shot.hits[k].prop != NULL) {
            hit = 1;
        }
    }
    return hit;
}

static float aimAssistGuardExact(PropRecord *pr, float cx, float cy)
{
    static const float side[4][2] = { { -ASSIST_RING, 0.0f }, { ASSIST_RING, 0.0f }, { 0.0f, -ASSIST_RING }, { 0.0f, ASSIST_RING } };
    ChrRecord *chr = pr->chr;
    int i;

    if (chr->model == NULL || chr->field_20 == NULL || (pr->flags & PROPFLAG_ONSCREEN) == 0 ||
        chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD) {
        return 0.0f;
    }
    if (aimAssistRayHits(pr, cx, cy)) {
        return 1.0f;
    }
    for (i = 0; i < 4; i++) {
        if (aimAssistRayHits(pr, cx + side[i][0], cy + side[i][1])) {
            return ASSIST_RING_T;
        }
    }
    return 0.0f;
}

/* Strength 0..1 for the screen centre: the best of the visible armed guards. */
static float aimAssistTarget(float cx, float cy)
{
    PropRecord **it;
    float best = 0.0f, blocked = 0.0f;

    for (it = g_OnScreenPropList; it < g_LastOnScreenProp; it++) {
        PropRecord *pr = *it;
        float t;

        if (pr == NULL || pr->type != PROP_TYPE_CHR || pr->chr == NULL) {
            continue;
        }
        if (chrGetEquippedWeaponProp(pr->chr, GUNRIGHT) == 0 &&
            chrGetEquippedWeaponProp(pr->chr, GUNLEFT) == 0) {
            continue;   /* unarmed: not a target for the game either */
        }
        t = aimAssistGuardExact(pr, cx, cy);
        if (t > best) {
            if (aimAssistVisible(pr)) {
                best = t;
            } else if (t > blocked) {
                blocked = t;
            }
        }
    }
    /* The game's line of sight flips between yes and no from one frame to the
     * next on a guard that is plainly in the open, so a refusal only counts
     * once it has held for ASSIST_LOS_HOLD frames in a row (a wall stays a
     * wall; a flicker does not get there). */
    if (best > 0.0f || blocked <= 0.0f) {
        s_losFail = 0;
    } else if (++s_losFail <= ASSIST_LOS_HOLD) {
        best = blocked;
    }
    return best;
}

static void aimAssistUpdate(int active)
{
    float target = 0.0f;
    struct timespec ts;
    double now, dt;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
    if (now - s_assistClock < ASSIST_MIN_STEP_SEC) {
        return;   /* already done for this frame */
    }
    if (active) {
        const float cx = getPlayer_c_screenleft() + 0.5f * getPlayer_c_screenwidth();
        const float cy = getPlayer_c_screentop() + 0.5f * getPlayer_c_screenheight();

        target = aimAssistTarget(cx, cy);
    }
    dt = now - s_assistClock;
    s_assistClock = now;
    if (dt < 0.0 || dt > 0.5) {
        dt = 0.5;
    }
    s_assistT += (float)((target - s_assistT) * (dt / (dt + (target > s_assistT ? ASSIST_RISE_SEC : ASSIST_FALL_SEC))));
    if (s_assistT < 0.01f) { s_assistT = 0.0f; }
}

static int aimAssistActive(void)
{
    double aimMode = 0.0;   /* Input.AimMode: 0 = Original, 1 / 2 = centred */

    configGetValue("Input.AimMode", &aimMode);
    return s_aimAssist > 0 && aimMode != 0.0 && coopIsMission() && currentPlayerGetIsAiming();
}

/* Called from lvlRender (src/game/lv.c, one PORT line) right before
 * chraiCheckUseHeldItems, on the game thread: that is where the game tests the
 * player's own shots, so the guards' hit lists (chr->field_20: built by
 * propsTick, freed by the frame's render passes), on-screen flags, matrices and
 * the on-screen prop list are the ones a bullet would see. Not from the
 * display-list thread (optionsOverlayEmit): the tests read and briefly flag
 * live game state. */
void nxAimAssistFrame(void)
{
    aimAssistUpdate(aimAssistActive());
}

/* Called from the game thread each time the render FOV is computed. */
float nxAimZoomFactor(void)
{
    float target = 1.0f;
    struct timespec ts;
    double now, dt, k;

    double aimMode = 0.0;   /* Input.AimMode: 0 = Original, 1 / 2 = centred */

    configGetValue("Input.AimMode", &aimMode);
    if (s_aimZoom > 0 && nxIsHandheld() && coopIsMission() &&
        aimMode != 0.0 && currentPlayerGetIsAiming()) {
        target = (s_aimZoom == 1) ? 1.5f : 2.0f;
    }
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
    dt = now - s_aimZoomT;
    s_aimZoomT = now;
    if (dt < 0.0 || dt > 0.5) {
        dt = 0.5;   /* first call, or a long stall: settle */
    }
    k = dt / (dt + AIMZOOM_EASE_SEC);
    s_aimZoomCur += (float)((target - s_aimZoomCur) * k);
    if (s_aimZoomCur < 1.0005f) { s_aimZoomCur = 1.0f; }
    return s_aimZoomCur;
}

/* Input thread: one stick axis while aiming = the Aim sensitivity (%), divided
 * by the zoom the picture really has, so the camera crosses the screen as fast
 * as without the zoom. Scoped (sniper, camera: the game's FOV is already under
 * 30) the game's own FOV scaling slows the turn enough, so the Aim sensitivity
 * is not applied there. */
extern f32 viGetFovY(void);

int nxAimStickP(int v, int sens, int slot)
{
    static const float slow[3] = { 1.0f, 0.70f, 0.50f };   /* speed on the guard */
    const f32 fov = viGetFovY();
    const int sl = (slot >= 0 && slot < 4) ? slot : 0;
    const float assist = 1.0f - (1.0f - slow[s_aimAssist]) * s_assistTA[sl];

    if (fov > 0.0f && fov < 30.0f) {
        sens = 100;
    }
    return (int)((float)v * (float)sens * assist / (100.0f * s_aimZoomCurA[sl]));
}

/*
 * Game.AimEntry: what the crosshair does when the aim starts. Off the aim, the
 * game's own auto-aim pulls the crosshair onto a guard. In Centered aim the
 * port stops feeding the game's crosshair integrator, so the game's own
 * damping slides the crosshair back to the centre, taking longer than a few
 * frames, and the guard it was on is left behind: the crosshair seems to leave
 * the target. "Move view" (default) replaces that slide by a short scripted
 * one, AIMENTRY_SEC long: the camera turns by the angle between the crosshair
 * and the centre while the crosshair walks to the centre in step with it, so
 * the crosshair stays on the same point of the world and both are in place
 * after a few frames, whatever the game's damping does. The start is the
 * crosshair as last seen OFF the aim: by the time the aim mode is seen the game
 * has already run its first aim tick and started relaxing it. "Centre
 * crosshair" puts the crosshair at the centre at once. Off keeps the game's
 * behaviour. Called from the pad's Centered branch (input.c), once per poll.
 */
extern double atan(double x);
extern double tan(double x);

#define RAD_TO_DEG 57.29577951308232
#define AIMENTRY_SEC 0.08   /* about five frames */

static int s_aimEntry = 1;   /* 0 off, 1 move view, 2 centre crosshair */

void nxAimEntryStep(struct player *p)
{
    static struct {
        int wasAiming, settling;
        float hipX, hipY, hipPX, hipPY;   /* the crosshair as last seen off the aim */
        float kx, ky;                     /* screen units per unit of crosshair_*_pos */
        double aYaw, aUp;                 /* the angles from the centre to the crosshair, radians */
        double t0, sPrev;
    } st[4];
    typeof(st[0]) *S = &st[nxPidx(p)];
    double aimMode = 0.0;
    struct timespec ts;
    double now, t, s, f, rsx, rsy;
    float offX, offY;
    int aiming;

    configGetValue("Input.AimMode", &aimMode);
    if (p == NULL || s_aimEntry == 0 || aimMode == 0.0) {
        S->wasAiming = 0;
        S->settling = 0;
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
    aiming = p->insightaimmode != 0;
    offX = p->crosshair_angle.f[0] - (p->c_screenleft + p->c_halfwidth);
    offY = p->crosshair_angle.f[1] - (p->c_screentop + p->c_halfheight);

    if (!aiming) {
        S->settling = 0;
        S->wasAiming = 0;
        S->hipX = offX;
        S->hipY = offY;
        S->hipPX = p->crosshair_x_pos;
        S->hipPY = p->crosshair_y_pos;
        return;
    }
    if (!S->wasAiming) {   /* the aim starts */
        S->wasAiming = 1;
        if (s_aimEntry == 2) {
            p->crosshair_x_pos = 0.0f;
            p->crosshair_y_pos = 0.0f;
            return;
        }
        rsx = (double)p->c_recipscalex;
        rsy = (double)p->c_recipscaley;
        if (rsx <= 0.0 || rsy <= 0.0) {
            return;
        }
        S->aYaw = atan((double)S->hipX / rsx);
        S->aUp  = atan((double)(-S->hipY) / rsy);
        S->kx = (S->hipPX > 0.01f || S->hipPX < -0.01f) ? S->hipX / S->hipPX : 0.0f;
        S->ky = (S->hipPY > 0.01f || S->hipPY < -0.01f) ? S->hipY / S->hipPY : 0.0f;
        S->settling = 1;
        S->t0 = now;
        S->sPrev = 0.0;
    }
    if (!S->settling) {
        return;
    }
    rsx = (double)p->c_recipscalex;
    rsy = (double)p->c_recipscaley;
    if (rsx <= 0.0 || rsy <= 0.0) {
        S->settling = 0;
        return;
    }
    t = (now - S->t0) / AIMENTRY_SEC;
    if (t > 1.0) { t = 1.0; }
    s = 1.0 - (1.0 - t) * (1.0 - t);   /* fast at first, soft landing */
    p->vv_theta += (f32)(S->aYaw * (s - S->sPrev) * RAD_TO_DEG);
    p->vv_verta += (f32)(S->aUp * (s - S->sPrev) * RAD_TO_DEG);
    S->sPrev = s;
    if (p->vv_verta >  90.0f) { p->vv_verta =  90.0f; }
    if (p->vv_verta < -90.0f) { p->vv_verta = -90.0f; }
    /* The crosshair walks to the centre in step: what is left of each angle,
     * back to screen units (offset = tan(angle) x c_recipscale), back to the
     * game's crosshair position. */
    f = 1.0 - s;
    if (S->kx != 0.0f) {
        p->crosshair_x_pos = (f32)(tan(S->aYaw * f) * rsx / (double)S->kx);
    }
    if (S->ky != 0.0f) {
        p->crosshair_y_pos = (f32)(-tan(S->aUp * f) * rsy / (double)S->ky);
    }
    if (t >= 1.0) {
        S->settling = 0;
        p->crosshair_x_pos = 0.0f;
        p->crosshair_y_pos = 0.0f;
    }
}

/*
 * Game.AimStop ("Stop on aim"): when the aim is on and the walk stick is at
 * rest, the character stops dead. Without it he keeps gliding for about a
 * second after the stick is released. Not because of the walk speeds
 * (speedgo and speedstrafe fall to 0 in one tick): Bond's translation each
 * tick is the head offset from the walk animation (move_offset += headpos x
 * delta, bondview2.c), and headpos is the output of a single-pole filter
 * (bheadUpdatePos, bondhead.c: headpossum = 0.93 x headpossum + the animation's
 * velocity, headpos = headpossum x 0.07). When the stick is released the input
 * drops to 0 but the filter keeps its charge and goes on pushing Bond for dozens
 * of ticks, and the enemy the auto-aim had picked drifts out of the crosshair.
 * This empties the filter on the horizontal axes (the height, Bond's head bob,
 * is kept) and zeroes the speeds. Any walk input hands control back to the
 * game. The caller (input.c, the pad's Centered branch, once per poll) passes
 * moving = 1 for a cutscene or a vehicle, where the game owns the movement.
 */
static int s_aimStop = 1;   /* 0 off, 1 on */

void nxAimStopStep(struct player *p, int moving)
{
    double aimMode = 0.0;


    if (!s_aimStop || p == NULL || moving || !p->insightaimmode || p->bonddead || !p->outside_watch_menu ||
        p->pause_state != 0) {
        return;
    }
    configGetValue("Input.AimMode", &aimMode);
    if (aimMode == 0.0) {
        return;
    }
    p->speedgo = 0.0f;
    p->speedstrafe = 0.0f;
    p->speedforwards = 0.0f;
    p->speedsideways = 0.0f;
    p->headpossum.f[0] = 0.0f;   /* the filter that keeps pushing Bond */
    p->headpossum.f[2] = 0.0f;
    p->headpos.f[0] = 0.0f;
    p->headpos.f[2] = 0.0f;
}

/*
 * Game.LookAhead (Controller page, "Look ahead (Extended)": off, low, medium,
 * high = 45, 60, 90 degrees per second at most): an auto-level for the
 * Extended controls. The game's own Look ahead (the per-file option on the
 * Game page, bondview2.c 5950-5995) eases the view back toward the horizon
 * while you walk, but it needs moveData.analogWalk > 60, an analogue walk. In
 * the Extended scheme the left stick is sent as digital steps (the C buttons,
 * input.c), so analogWalk stays 0 and it never fires. This does the same job
 * from the port, as its own option: walking forward with the left stick, the
 * look stick at rest for a moment, not aiming, the pitch eases back to level
 * (the game's own base pitch is -4, a view a little low), once it is more than
 * 5 degrees above it or 10 below, the game's own thresholds. Flat horizon only:
 * the game also follows the slope ahead. Called from the pad's Centered branch
 * (input.c), once per poll.
 */
static int s_lookAhead = 3;   /* 0 off, 1 low, 2 medium, 3 high: the top speed of the ease */

#define LOOKAHEAD_TARGET   (0.0f)    /* level with the horizon; the game's own base pitch is -4, a view a little low */
#define LOOKAHEAD_IDLE_SEC 0.5     /* the look stick must have rested this long */
#define LOOKAHEAD_UP       5.0f    /* start when this far above the target ... */
#define LOOKAHEAD_DOWN     10.0f   /* ... or this far below it */
#define LOOKAHEAD_SOFT_DEG 6.0     /* the ease slows over the last degrees to the target */
static const double kLookAheadDps[4] = { 0.0, 45.0, 60.0, 90.0 };   /* degrees per second, by setting */

void nxLookAheadStep(struct player *p, int walkingForward, int lookIdle)
{
    static double idleSinceA[4], lastA[4];
    static int activeA[4];
    const int pi = nxPidx(p);
#define idleSince idleSinceA[pi]
#define last lastA[pi]
#define active activeA[pi]
    double aimMode = 0.0, now, dt, diff, speed, step;
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
    dt = now - last;
    last = now;
    if (dt < 0.0 || dt > 0.1) {
        dt = 0.1;   /* a stall: do not lurch */
    }
    configGetValue("Input.AimMode", &aimMode);
    if (p == NULL || !s_lookAhead || aimMode == 0.0 || p->insightaimmode ||
        p->bonddead || !p->outside_watch_menu || p->pause_state != 0 || !walkingForward || !lookIdle) {
        idleSince = now;
        active = 0;
        return;
    }
    if (now - idleSince < LOOKAHEAD_IDLE_SEC) {
        return;
    }
    diff = (double)LOOKAHEAD_TARGET - (double)p->vv_verta;   /* > 0: the view is below the target */
    if (!active) {
        if (diff < -LOOKAHEAD_UP || diff > LOOKAHEAD_DOWN) {
            active = 1;
        } else {
            return;
        }
    }
    /* The setting's speed all the way, slowing in proportion over the last
     * LOOKAHEAD_SOFT_DEG degrees so it lands softly. (An exponential ease with
     * a fixed time constant made the three settings feel alike: only its first
     * moments depend on the speed.) */
    speed = kLookAheadDps[s_lookAhead];
    if (diff < LOOKAHEAD_SOFT_DEG && diff > -LOOKAHEAD_SOFT_DEG) {
        speed *= (diff < 0.0 ? -diff : diff) / LOOKAHEAD_SOFT_DEG;
    }
    step = speed * dt;
    if (step > (diff < 0.0 ? -diff : diff)) { step = (diff < 0.0 ? -diff : diff); }
    if (diff < 0.0) { step = -step; }
    p->vv_verta += (f32)step;
    if (diff < 1.0 && diff > -1.0) {
        active = 0;
    }
}
#undef idleSince
#undef last
#undef active

PD_CONSTRUCTOR static void nxGameRegisterConfig(void)
{
    configRegisterInt("Game.AimZoom", &s_aimZoom, 0, 2);
    configRegisterInt("Game.AimAssist", &s_aimAssist, 0, 2);
    configRegisterInt("Game.AimEntry", &s_aimEntry, 0, 2);
    configRegisterInt("Game.AimStop", &s_aimStop, 0, 1);
    configRegisterInt("Game.LookAhead", &s_lookAhead, 0, 3);
}
