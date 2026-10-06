/*
 * Input: SDL2 keyboard/mouse/gamepad -> N64 controller state (Phase 3, D118).
 *
 * The game reads controllers via osContStartReadData / osContGetReadData
 * (src/joy.c). libultra.c's SI section calls into this module once per
 * controller poll: inputUpdate() refreshes SDL + accumulates the mouse-aim
 * delta, then inputComputePad() produces the 16-bit button mask + stick for
 * one controller. This is the single source of controller state on PC --
 * the old contSnapshotFromKeyboard() now delegates here.
 *
 * Deliberately NOT a full port of pd_port/port/src/input.c (1551 lines): GE's
 * menu/config code never calls that module's VK/bind-string API, so this is a
 * focused implementation matching port/include/input.h plus the two helpers
 * libultra.c needs.
 *
 * ------------------------------------------------------------------------
 * BINDING SCHEME (documented in docs/internals.md sec F "D118")
 *
 * GE's default "1.1" control style: analog stick = move/strafe, the four
 * C-buttons = aim/turn/look (DIGITAL on N64), R = aim mode, Z = fire.
 *
 * Keyboard + mouse (controller 0, migrated PC defaults):
 *   W/S/A/D or arrows .. movement / strafe
 *   mouse motion ....... look (mode-aware -- see MOUSE-LOOK below)
 *   left mouse ......... fire; right mouse / LShift .. aim
 *   Q .................. next weapon; E .. dedicated use
 *   R .................. dedicated reload; LCtrl .. independent crouch
 *   Enter / Tab ........ Start
 *
 * Xbox / SDL_GameController (controller 0 merges pad 0 with kbd/mouse;
 * pads 1-3 -> controllers 1-3):
 *   left stick ........ analog stick   (move / strafe)
 *   right stick ....... C-buttons      (digital, 50% threshold -- aim)
 *   right trigger ..... Z trigger      (fire)
 *   left trigger ...... R trigger      (aim mode)
 *   A ................. use/interact (in-game); accept (menus)
 *   X ................. reload (in-game); accept (menus)
 *   B ................. cycle inventory gadgets (in-game); cancel (menus)
 *   Y ................. next weapon (in-game); cancel (menus)
 *   stick clicks ...... crouch (in-game)
 *   LB ................ aim (alternate to LT); RB reserved (no HD assets)
 *   D-pad ............ N64 D-pad
 *   Start ............ Start; Back opens the PC options overlay
 *
 * MOUSE-LOOK (mode-aware, no src/ changes)
 *   GE's aim model (bondview2.c bondviewProcessInput / MoveData) is
 *   mode-dependent, so the mouse->pad mapping is too:
 *
 *   - Hipfire (!insightaimmode): yaw = analog stick-X ("natural turn");
 *     pitch = DIGITAL C-up/C-down (stick-Y here is move fwd/back and does
 *     not pitch). Mouse Y emits a C-button on polls where it moved.
 *   - Aim mode (R / RMB held): yaw AND pitch are analog -- the stick pushed
 *     past +/-60 gives proportional (stick-60)/10 aim speed. Mouse X/Y push
 *     the stick into the 61..80 band. We emit NO C-buttons in this mode
 *     because there they mean crouch / lean / zoom (this was D118c:
 *     aim + look-down -> crouch).
 *
 *   "aim button held" is read from our own RMB/LShift state -- exact for
 *   the default hold-to-aim scheme; a toggle-aim scheme would need a read
 *   of slotPlayer()->insightaimmode (still no logic change).
 *
 *   GE's native pitch is inverted ("flight" style: C-up -> look down). We
 *   hide that: mouse-down looks down by default, MouseInvertY flips it.
 *
 *   Tunable via ge007.ini [Input]: MouseAimSpeed (aim mode), MouseTurnSpeed
 *   (hipfire yaw), MouseInvertY, MouseEnabled. Residual: in hipfire, yaw
 *   (analog) and pitch (digital) still feel different (D118a) -- a fully
 *   analog hipfire pitch needs an #ifdef PORT hook in bondview.c (TODO).
 * ------------------------------------------------------------------------
 */

#include "port_math.h"   /* real system math decls; see header for why */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "input.h"
#include "envflag.h"
#include "optionsoverlay.h"
#include "frontoptions.h"
#include "video.h"
/* D194 absolute aim: read-only access to the live camera (struct player).
 * Game header pulled in through the same shim path every other compiled game
 * file uses; we only READ vv_theta/vv_verta/speedtheta/speedverta/aspect. */
#include "player.h"
#include "language.h"  /* j_text_trigger (control-type mirror) */
#include "gun.h"  /* native weapon flags + dedicated reload entry points */
#include "bondinv.h" /* inventory list for the Xbox-style gadget cycle */

/* D194 spazz diagnosis: game ticks batched into the current poll (lv.h).
 * Read-only; declared locally to avoid pulling lv.h's wider dependency set. */
extern s32 g_ClockTimer;
extern s32 lvlGetControlsLockedFlag(void); /* same gate as bondviewProcessInput */
extern int gameScriptedCameraActive(void);
extern bool bond_interact_object(void); /* chrprop.c: use target, no reload fallback */
extern s32 g_PlayerIsInTank;            /* bondview.c: tank state (D407) */
extern s32 g_BondCanEnterTank;          /* bondview.c: tank board gate (D407) */
extern s32 g_EnterTankAudioState;      /* bondview2.c: tank entry/running state (D407) */
/* gun.c: native weapon-switch entry points (not exposed in gun.h). */
extern ITEM_IDS get_next_weapon_in_cycle_for_hand(GUNHAND hand, s32 direction);
extern void gunRequestHandWeaponChange(enum GUNHAND hand, s32 nextWeapon, s32 cycleDirection);

/* N64 button bits (from PR/os.h -- duplicated here to avoid pulling os.h,
 * whose `u8 errno;` field collides with <errno.h>'s macro). */
#define GE_CONT_A      0x8000
#define GE_CONT_B      0x4000
#define GE_CONT_G      0x2000  /* Z trigger */
#define GE_CONT_START  0x1000
#define GE_CONT_UP     0x0800
#define GE_CONT_DOWN   0x0400
#define GE_CONT_LEFT   0x0200
#define GE_CONT_RIGHT  0x0100
#define GE_CONT_L      0x0020
#define GE_CONT_R      0x0010
#define GE_CONT_E      0x0008  /* C-up    */
#define GE_CONT_D      0x0004  /* C-down  */
#define GE_CONT_C      0x0002  /* C-left  */
#define GE_CONT_F      0x0001  /* C-right */

#define MAX_PADS            4
#define STICK_DEADZONE      7000
#define STICK_MAX          80

/* Front-end pointer mode: GE's menu cursor ("crosshair") is stick-driven
 * (front.c frontUpdateControlStickPosition reads joyGetStickX/Y and
 * integrates a screen position). During a running stage current_menu ==
 * MENU_RUN_STAGE (11, src/bondconstants.h enum MENU); every other value is
 * a front-end / briefing / debrief / cheat screen where the mouse should
 * feel like a pointer, not a look-axis. We read the game global directly
 * (enum MENU is ABI int) -- UI-context only, no logic change.
 * MENU_INVALID (-1) means the front end never ran (bare -level_XX boot):
 * treat that as in-game so direct-launch mouse-look is unaffected. */
extern int current_menu;
#define GE_MENU_RUN_STAGE  11
#define GE_MENU_INVALID    (-1)
/* D169: the front-end cursor lives in the game's virtual-screen field, which
 * is 440x330 in the front end -- not the 320x240 the pointer P-controller
 * assumed. Read the live field so the mouse pointer can reach the whole
 * mission-select grid. These are plain non-static engine accessors
 * (src/game/bondview.c) and globals (src/game/front.c); UI geometry only, no
 * logic change. */
extern float getPlayer_c_screenwidth(void);
extern float getPlayer_c_screenheight(void);
extern float getPlayer_c_screenleft(void);
extern float getPlayer_c_screentop(void);
extern float cursor_h_pos, cursor_v_pos;

/* D194/D238 -- natural-pitch control scheme.
 *
 * GE's hipfire pitch is structurally digital: bondviewProcessInput's default
 * (1.1/HONEY) scheme reads pitch only from C-up/C-down (see D166), so the
 * port emulates continuous pitch by duty-cycling that button -- an
 * approximation that can never fully match yaw's genuinely continuous,
 * unbounded analog mapping, which is the root of the "vertical feels slower"
 * complaint. GE's own 1.2/SOLITARE scheme (cur_player_get/set_control_type,
 * src/game/options.c; dispatch in bondviewProcessInput, bondview2.c ~5192-
 * 5450) gives BOTH pitch and yaw continuous analog stick control in hipfire
 * (canNaturalTurn/canNaturalPitch) -- at the cost of moving movement
 * (forward/back/strafe) from the analog stick onto digital step buttons
 * (digitalStepForward/Back/Left/Right, fed from C-buttons/D-pad). This is an
 * ORIGINAL, player-selectable GE control style (not new game logic) -- we
 * select it from the port the same way the options menu would
 * (cur_player_set_control_type), and remap WASD/gamepad accordingly so
 * movement keeps working under it. No src/ edits; no behavior invented that
 * GE didn't already support. Escape hatch: Input.NaturalPitch=0 reverts to
 * the 1.1/HONEY default with the D166 digital-pitch-pulse hack. */
extern int cur_player_get_control_type(void);
extern void cur_player_set_control_type(int type);

/* D194 absolute aim: live camera/projection accessors (src/fr.c, src/game/,
 * port/src/video.c). All are plain reads of state owned by the game thread,
 * sampled from inputComputePad which already runs in that same context
 * (contSnapshotFromKeyboard <- osContStartReadData <- joy.c) -- no new
 * cross-thread access, and nothing here writes. */
extern f32 viGetFovY(void);
extern s16 viGetViewWidth(void);
extern s16 viGetViewHeight(void);
extern s16 viGetViewLeft(void);
extern s16 viGetViewTop(void);
extern s16 getWidth320or440(void);   /* CFB width the viewport rect lives in (320 NTSC) */
extern s16 getHeight330or240(void);  /* CFB height (240 NTSC) */
extern f32 portScaleFovY(f32 fovy, s32 isTitleScreen);
extern s32 lvlGetCurrentStageToLoad(void);
#define CONTROLLER_CONFIG_HONEY_    0
#define CONTROLLER_CONFIG_SOLITARE_ 1
#define MENU_POINTER_GAIN  1.5
#define TRIG_THRESHOLD     (30 * 256)
#define RSTICK_THRESHOLD   0x4000
/* Mouse-look tuning. GE's aim model is mode-dependent (bondview2.c
 * bondviewProcessInput):
 *   - Hipfire (!insightaimmode): yaw = analog stick-X ("natural turn"),
 *     pitch = DIGITAL C-up/C-down only (stick-Y is move fwd/back here).
 *   - Aim mode (R held):          yaw AND pitch = analog stick pushed past
 *     +/-60 -> proportional (stick-60)/10. C-up/C-down mean crouch/lean/
 *     zoom in this mode, NOT aim -- so we must NOT emit them while aiming
 *     (that was D118c: aim + mouse-down -> crouch).
 * GE's native pitch is inverted ("flight" style): C-up -> look down. We
 * hide that so mouse-down looks down by default; MouseInvertY flips it. */
#define MOUSE_TURN_GAIN     6.0   /* hipfire: raw px this poll -> stick-X counts */
#define MOUSE_PITCH_THRESH  1.5   /* hipfire: px/poll before a C-button fires  */
#define AIM_MOVE_THRESH     0.3   /* aim mode: px/poll before the stick moves   */
#define HIP_PITCH_FULL      6.0   /* hipfire pitch: |px/poll| for a solid C hold (D166) */

/* D194(a) -- aim-mode response curve.
 *
 * bondviewPlayerControlStuff (bondview2.c) turns an aim-mode stick value
 * into turn speed as `(stick_x - 60) / 10.0`, clamped to 1.0 -- i.e. the
 * game's own proportional band is stick in [61,70]; anything from 71 up to
 * whatever AimBand allows (60+AimBand, up to 100) clamps to the exact same
 * max speed as 70. The old code aimed for the port's [61, 60+AimBand] range
 * (up to [61,80]) as if it were all proportional, so a config'd AimBand>10
 * bought nothing, and AIM_GAIN=4.0 reached the (effectively already-maxed)
 * top of that range by ~5 px/poll -- "near bang-bang", matching the D194(a)
 * complaint. Fix: curve-map px/poll onto the game's REAL proportional range
 * [61,70] (independent of AimBand, which still acts as an extra ceiling for
 * anyone who wants to cap below the game's own max), with a configurable
 * gamma so slow motions land near the low end instead of jumping to nearly
 * full speed immediately. */
#define AIM_STICK_MIN        61
#define AIM_STICK_GAME_MAX   70    /* bondview2.c: (stick-60)/10 saturates at stick=70 */
#define AIM_FULL_SPEED_PX    20.0  /* px/poll (at MouseAimSpeed=100) that reaches AIM_STICK_GAME_MAX */

/* D194 GEPD-style aim mapping. While RMB is held (cursor grabbed/hidden),
 * mouse MOVEMENT rotates the view by the same screen angle swept: a
 * sweep across the rendered viewport width sweeps the full horizontal FOV,
 * height <-> fovy. The crosshair stays locked at screen centre and stop
 * moving holds the view where it is (the game's own speed decay coasts it to
 * rest). Because the scale is angle-per-pixel of the CURRENT fovy, scope
 * zooms automatically become finer control. No target, no chase, no
 * snap-back: placement is cumulative and path-dependent, exactly like GEPD.
 */
/* D194 GEPD-mirror aim constants (MouseInjectorPlugin/games/goldeneye.c): */
#define AIM_ABS_DEAD_PX       2.0    /* per-poll px below this is jitter, not aim */
#define GEPD_CROSSHAIR_LIMIT  5.159373283  /* crosshair pos units at the screen edge (GEPD 0x40A51996) */
#define GEPD_EDGE_THRESHOLD   0.72f        /* |pos|/limit beyond which the view scrolls */
#define GEPD_SCROLL_SPEED     475.0        /* GEPD: (ratio-threshold)*475*timestep per tick */
#define GEPD_BASE_FOV         90.0f        /* unzoomed FOV (our native); GEPD uses its 60 override */

/* D194(b) -- mouse sensitivity coupled to frame/poll rate.
 *
 * MOUSE_TURN_GAIN/AIM_GAIN above treat "px accumulated since the last
 * inputComputePad(0) drain" as a fixed per-poll unit, but the real interval
 * between drains drifts with render/scene load (D193 measured a rock-steady
 * 60 sim ticks/s in the idle case, but the drain itself piggybacks on
 * whatever cadence calls contSnapshotFromKeyboard() -- 1-2x per rendered
 * frame per D165's comment -- so a hitch or a variable-length frame changes
 * how much real time one "poll" of accumulated px represents). Net effect:
 * the same physical hand motion emits a different turn rate depending on
 * how ragged the poll cadence is, which reads as "sensitivity changes with
 * movement/frame rate" (the user's own words).
 *
 * Fix: measure real elapsed time since the last drain with a monotonic
 * clock and rescale the accumulated px by (dtRef / dtActual) before it
 * hits MOUSE_TURN_GAIN/AIM_GAIN, where dtRef is the nominal 1/60s tick
 * (matches D193's measured steady-state sim rate) -- so "px this poll"
 * always means "px for a nominal 1/60s tick" regardless of how long that
 * tick actually took in real time. Clamped both directions so one big
 * stall (level load, GC pause) can't fling the view. Menu-cursor paths
 * (D165/D169's P-controller, which has its own poll-count-based settling
 * design) are deliberately NOT touched by this -- gameplay look only. */
#define MOUSE_DT_REF        (1.0 / 60.0)
#define MOUSE_DT_SCALE_MIN  0.25   /* cap correction for an abnormally long poll gap */
#define MOUSE_DT_SCALE_MAX  4.0    /* cap correction for an abnormally short poll gap */

/* Item 1 (D165) — front-end 1:1 pointer. front.c frontUpdateControlStickPosition
 * INTEGRATES the stick as a velocity into a screen-pixel cursor position
 * (cursor_h_pos += (stickx*0.075 +/- 0.5) * delta, deadzone +/-5, clamp +/-70,
 * cursor clamped into the ~320x240 virtual screen rect minus a 20px margin).
 * Feeding it mouse *velocity* therefore gives velocity^2 feel. Instead we run a
 * P-controller: keep our own estimate of where the game cursor is (menuEst*,
 * integrated with the SAME recurrence as front.c), accumulate a target from
 * mouse motion, and emit stick = clamp(GAIN*(target-est)). The estimate re-syncs
 * to the real cursor whenever the target is held at a screen edge. */
#define MENU_CURSOR_LO      20.0
#define MENU_CURSOR_HI_H    300.0
#define MENU_CURSOR_HI_V    220.0
/* D345: after a front-end screen change, re-assert the 1:1 pointer for a few
 * frames if the mouse was used within this many ms (see inputComputePad). */
#define MENU_POINTER_REASSERT_MS 1500.0
#define MENU_CURSOR_MID_H   160.0
#define MENU_CURSOR_MID_V   120.0

static void applyGrab(int want);
static void reconcileGrab(int menuMode);
static void applyCursorVisibility(void);

static int numControllers = 1;
static int connectedMask   = 0x1;   /* controller 0 always present */

static SDL_GameController *pads[MAX_PADS];

/* Local multiplayer controller assignment (issue #99 / D416).
 * Input.MPMode: 0 Auto, 1 PadsOnly, 2 KbmP1.
 *  - Auto: 2+ pads -> pads are P1..PN and keyboard/mouse is ignored in a
 *    match; exactly 1 pad -> keyboard/mouse is P1 and the pad is P2.
 *  - PadsOnly: pads are P1..PN always (1 pad = MP stays locked, as stock).
 *  - KbmP1: keyboard/mouse is P1, pads are P2.. even with 2+ pads.
 * The shift applies only while a 2+ player stage is running; front-end menus
 * keep the pad on controller 0, so nothing changes for solo play or menus.
 * The reported controller mask still counts the shifted slots so the game's
 * own "needs 2 controllers" gates open. */
static int mpInputMode = 0;
static int nRealPads   = 0;
extern s32 getPlayerCount(void);

static int inputMpSplit(void)
{
    return current_menu == GE_MENU_RUN_STAGE && getPlayerCount() >= 2;
}

/* DNEW-mp-1 (issue #99): pad SEATS. pads[] is indexed by seat, not by SDL
 * device index. A seat is taken in connection order (boot: SDL device order)
 * and keyed by the pad's SDL instance id, so a rescan keeps every still-attached
 * pad in its seat. Outside a split-screen match the seats are compacted (P1..PN
 * dense, which the game's joyGetControllerCount needs: it counts contiguous
 * connected bits). During a match a vacated seat stays reserved and its mask bit
 * stays set, so an unplug neither shifts the other players' pads nor tells the
 * game a controller vanished, and a re-plugged pad takes the first free seat,
 * i.e. back into its old one. Compaction runs lazily once the match is over.
 * Seat -> controller slot: slot = seat, or seat + 1 when keyboard/mouse is P1.
 * The routing decision (keyboard/mouse P1 or not) counts the match's sticky
 * seats for the same reason: an Auto-mode unplug from 2 pads to 1 must not flip
 * the keyboard onto P1 and shift every pad by one slot mid-match. */
static SDL_JoystickID padInst[MAX_PADS] = { -1, -1, -1, -1 };
static int s_matchSeatMask = 0;     /* seats occupied at any point this match */
static int s_seatCompactOwed = 0;   /* a seat was vacated in a match */

enum { MPR_KBMP1 = 1, MPR_PADSP1 = 2 };
static int mpRoute(void)
{
    /* In a match, count the seats used at any point this match (the sticky
     * mask only grows), so an unplug never changes the routing. */
    int n = nRealPads;
    if (inputMpSplit()) {
        n = 0;
        for (int s = 0; s < MAX_PADS; ++s) n += (s_matchSeatMask >> s) & 1;
    }
    if ((mpInputMode == 2 && n >= 1) || (mpInputMode == 0 && n == 1))
        return MPR_KBMP1;
    return n >= 2 ? MPR_PADSP1 : 0;
}
static int inputMpKbmP1(void)
{
    return (mpRoute() & MPR_KBMP1) != 0;
}
/* Pads own P1 (2+ pads, not KbmP1): the keyboard/mouse block is skipped. */
static int inputMpPadsOwnP1(void)
{
    return inputMpSplit() && (mpRoute() & MPR_PADSP1) != 0;
}
static SDL_GameController *slotPad(int idx)
{
    if (idx < 0 || idx >= MAX_PADS) return NULL;
    if (inputMpSplit() && inputMpKbmP1()) return idx == 0 ? NULL : pads[idx - 1];
    return pads[idx];
}

/* D416 per-slot input: the controller poll runs on the scheduler thread, where
 * g_CurrentPlayer is whichever player the game thread last selected -- wrong
 * for a split-screen slot. While a slot is being polled (s_slot >= 0) in a
 * 2+ player stage, every player-struct access in this file goes through
 * slotPlayer(), which resolves the player that owns the slot (player N reads
 * controller N, as on the N64). Outside a poll, or in solo, it is exactly
 * g_CurrentPlayer. */
static int s_slot = -1;
static struct player *slotPlayer(void)
{
    if (s_slot >= 0 && s_slot < 4 && inputMpSplit() && g_playerPointers[s_slot])
        return g_playerPointers[s_slot];
    return g_CurrentPlayer;
}

/* In a 2+ player stage this file never calls a game entry point that acts on
 * g_CurrentPlayer (bond_interact_object, reload, gadget cycle, control-type
 * setter): the poll thread is not the game thread, so re-pointing the
 * current-player globals from here races the game tick (first attempt did,
 * and hung the game in bondinvCycleForward after a respawn). Match play
 * routes use/reload through the native B button instead, and reads/writes
 * the slot player's fields directly. */
static int padBPrev[MAX_PADS];          /* B/Y edges; track through menus too */
static int padYPrev[MAX_PADS];
static int padAPrev[MAX_PADS];          /* D416: slots 1+ use/reload edges */
static int padXPrev[MAX_PADS];
static int padSelectPrev = 0;           /* Select (BACK) edge: overlay toggle */

/* D401: per-pad haptics (N64 Rumble Pak -> SDL_GameControllerRumble).
 * padRumbleOn is detected when the pad opens (inputOpenPads); gRumbleScale
 * is the single global Input.RumbleScale config value (0 = silent,
 * 1 = full), applied to every pad. */
static int padRumbleOn[MAX_PADS];
static float gRumbleScale = 0.5f;

static int mouseEnabled   = 1;
static int mouseGrabbed    = 1;     /* released while the window is unfocused */

/* D287: SDL's mouse-mode and cursor calls belong on the window's thread (the
 * host thread that created it and pumps its events). Controller polls run on
 * the scheduler thread (osContStartReadData -> inputComputePad ->
 * reconcileGrab), so they called SDL_SetRelativeMouseMode / SDL_ShowCursor
 * concurrently with the host thread's own focus/click handling. On Windows
 * relative mode starts/stops SDL's raw-input thread and re-clips the cursor;
 * two threads flipping it at once (clicking back into the window after being
 * unfocused) can wedge. Callers now only record the wanted state; the host
 * thread applies it in inputApplyMouseRequests() from videoPumpEvents(). The
 * logical state (mouseGrabbed, deltas) still updates immediately. */
static SDL_SpinLock s_mouseReqLock;
static int s_reqRelative = -1, s_reqCursor = -1;    /* -1: nothing pending */
static int s_appRelative = -1, s_appCursor = -1;    /* host thread only */

static void mouseRequestRelative(int on)
{
    SDL_AtomicLock(&s_mouseReqLock);
    s_reqRelative = on ? 1 : 0;
    SDL_AtomicUnlock(&s_mouseReqLock);
}

static void mouseRequestCursor(int show)
{
    SDL_AtomicLock(&s_mouseReqLock);
    s_reqCursor = show ? 1 : 0;
    SDL_AtomicUnlock(&s_mouseReqLock);
}

/* Host thread only (videoPumpEvents, inputInit). */
void inputApplyMouseRequests(void)
{
    SDL_AtomicLock(&s_mouseReqLock);
    const int rel = s_reqRelative, cur = s_reqCursor;
    s_reqRelative = s_reqCursor = -1;
    SDL_AtomicUnlock(&s_mouseReqLock);

    if (rel >= 0 && rel != s_appRelative) {
        s_appRelative = rel;
        SDL_SetRelativeMouseMode(rel ? SDL_TRUE : SDL_FALSE);
        if (rel) {
            SDL_GetRelativeMouseState(NULL, NULL);   /* drain the accumulated jump */
        }
    }
    if (cur >= 0 && cur != s_appCursor) {
        s_appCursor = cur;
        SDL_ShowCursor(cur ? SDL_ENABLE : SDL_DISABLE);
    }
}

/* WI-1: Quake-style click-to-lock cursor capture. The cursor is free until
 * you click in the game window; ESC (or focus loss, or opening a menu) frees
 * it again. Re-entering a stage while still "armed" re-locks automatically so
 * unpausing / starting a level does not need a click.
 * D239/D192: the legacy always-grab mode (former Input.MouseCaptureMode=0)
 * is removed -- it felt wrong on the file-select menu and its front-end
 * pointer could not reach the outer grid cells. Click-to-lock is the only
 * mode now; existing ini files setting the old key are ignored.
 * Controller input is entirely independent of all of this. */
static int captureArmed     = 0;   /* user has clicked to lock (capture mode) */
static int windowFocused    = 1;
static int mouseAimSpeed  = 16;     /* aim-mode sensitivity, percent (B3: 50 -> 25 M-29 -> 16; still overshot at 25) */
static int gepdSens       = 38;     /* D194 Input.GepdSens: GEPD SENSITIVITY setting, range 1..500
                                        (D304: widened from 1..80 to match Input.MouseTurnSpeed)
                                        (20 -> 25 "a bit slow" -> 30 user-calibrated match point
                                        -> 38: user asked defaults ~20-35% faster than that) */
static int aimBand        = 20;     /* aim mode: usable stick range above the 60 gate */
/* D407: mouse->stick gain multiplier (%, 100 = unchanged) applied ONLY while
 * aiming in tank. In-tank aim must use the legacy velocity-stick path (the
 * game's turret code reads raw stick deflection only), whose px->screen feel
 * differs from the direct-write on-foot models; this knob lets the user dial
 * the turret to taste without touching on-foot aim. */
static int tankAimScale   = 100;
/* D194/D238: default 100 -> 40 (M-123 user calibration). The old gain
 * (MOUSE_TURN_GAIN=6 stick/px) saturated the game's quadratic natural-turn
 * curve at ~13 px/poll, i.e. hipfire ran at full 315 deg/s for any normal
 * movement while GEPD aim mode moves proportionally -- "too fast when I
 * leave that mode". At 40% (2.4 stick/px) the mid-speed view rate matches
 * the GEPD reticle's angular pace at GepdSens=30; flicks still reach full
 * turn speed, only later. */
static int sensLink       = 1;     /* F10 overlay: keep aim/turn sens at the
                                      stock 38:50 ratio (default on) */
static int mouseTurnSpeed = 50;     /* hipfire yaw sensitivity, percent
                                        (40 = M-123 match point at GepdSens=30; 50 = the user's
                                        "~20-35% faster" default request, same +25% as gepdSens) */
static int menuPointerSpeed = 100;  /* front-end cursor speed, percent */
static int mouseInvertY   = 0;      /* 1 = mouse-down looks up */
static int mouseYScale    = 100;    /* extra vertical (pitch) sensitivity, % */
static int mouseSmoothing = 0;      /* 0 = raw; 1..90 = low-pass strength (%) */
static int mouseRawInput  = 0;      /* 1 = bypass OS pointer accel for aim    */
static int mouseDtDecouple = 1;     /* D194(b): normalize look sensitivity by real
                                      * elapsed poll time instead of raw px/poll.
                                      * Escape hatch: 0 = prior (coupled) behaviour. */
static int mouseSensitivity = 100;  /* D238: single master sensitivity, percent --
                                      * multiplies BOTH MouseAimSpeed and MouseTurnSpeed
                                      * so one number tunes overall feel; the two legacy
                                      * knobs stay as an independent per-mode trim on
                                      * top of it (both default 100/16 => unchanged
                                      * unless the user also touches those). */
static int aimCurveGamma  = 160;    /* D194(a): aim-mode response curve exponent x100
                                      * (100 = linear, >100 = more low-speed control /
                                      * less bang-bang, <100 = more twitchy). */
static int aimAbsolute      = 1;    /* D194: 1 = GEPD-style aim while RMB is held --
                                      * crosshair locked at centre, mouse MOVEMENT
                                      * rotates the view 1:1 in screen angle; 0 =
                                      * legacy velocity stick from mouse delta. */
static int mouseDirectLook  = 1;    /* WI-1 (GEPD-INPUT-PLAN.md #89): 1 = hipfire
                                      * writes vv_theta/vv_verta directly (see
                                      * hipDirectCompute), same linear-in-px model
                                      * as aim mode; 0 = legacy stick-curve path
                                      * (MOUSE_TURN_GAIN), kept as an escape hatch. */

/* Input.PdMouseAim -- use the Perfect Dark port's mouse-aim model instead of
 * GEPD's. Mirrors fgsfdsfgs/perfect_dark exactly (bondmove.c/bondgun.c): PD
 * accumulates the mouse delta into a position (`swivelpos`, clamped [-1,1])
 * and then pushes that POSITION into the game's crosshair integrator as its
 * INPUT, with a near-zero crosshair damp (`bgunSwivelWithDamp(x, y, 0.01f)`).
 *
 * The near-zero damp is the whole point. GE's aim path uses the weapon's
 * CrosshairSpeed (~0.8) -- a value chosen for the analog stick, where the
 * integrator does the smoothing. With a mouse that slow integrator is what
 * makes the drawn crosshair step: the port writes once per input poll while
 * the game damps once per sim tick, so the displayed value is write*damp^k
 * with k varying (findings D332). At damp 0.01 the input dominates and k stops
 * mattering -- PD never had this bug because it never gave the mouse a slow
 * damp.
 *
 * In this mode the port only ACCUMULATES; the game's own integrator does the
 * rest, driven by the port-supplied turn through a #ifdef PORT hook in
 * sub_GAME_7F067FBC (gunfire.c). Default off: it changes aim feel, so it is
 * opt-in. */
static int pdMouseAim = 0;

/* D337 -- aim mode, folded (user decision 2026-09-24):
 *   Input.AimMode 0 = N64 (default): the N64 aim model -- crosshair travels,
 *       camera edge-scrolls -- with the mouse fed through the game's own
 *       crosshair integrator at PD's mouse damp (D332 / PR #96). Same range,
 *       edge-scroll and bullet mapping as the old GEPD overwrite, without its
 *       poll-vs-tick step jitter.
 *   Input.AimMode 1 = Centred (PC, opt-in, #104): the mouse turns the camera
 *       and the crosshair stays centred. Not an N64 behaviour; kept for players
 *       who asked for FPS-style aiming.
 * Input.AimLegacyGepd=1 (hidden, one release) restores the old GEPD overwrite
 * for the N64 mode. GE_PDMOUSEAIM=0/1 still forces the integration at launch.
 * Legacy keys: Input.AimStyle 2 (Centred) migrates to AimMode 1; AimStyle 0/1
 * and Input.PdMouseAim map to the default N64 mode. */
enum { AIMMODE_N64 = 0, AIMMODE_CENTRED = 1 };
static int aimMode = AIMMODE_N64;
static int aimLegacyGepd = 0;
static int aimStyleLegacy = 0;   /* Input.AimStyle (D333, superseded) */

static int aimModeGet(void)
{
    if (aimStyleLegacy == 2 && aimMode == AIMMODE_N64) {   /* one-time migration */
        aimMode = AIMMODE_CENTRED;
    }
    aimStyleLegacy = 0;
    pdMouseAim = 0;
    /* D422: every slot gets the configured mode (the aim state is per slot;
     * padDirectCompute is stateless and acts on slotPlayer()). */
    return aimMode;
}

/* D337: which device drove aim last. PD picks its crosshair damp per input
 * device; the mouse integration must not swallow a gamepad's stick aim just
 * because the mouse happens to be grabbed. Set by mouse motion, cleared by a
 * deflected pad stick (inputComputePad). */
static int s_aimDevMouse = 0;

/* D338: Input.AimRange -- how far the N64-mode crosshair can travel.
 *   0 = PC (default, the GEPD/mouse-injector feel): the crosshair reaches the
 *       screen edge, camera edge-scroll from 72% of the way there.
 *   1 = N64: the original stick limits. N64 aim turns the crosshair by
 *       stick*0.65/80 per tick, so full deflection settles at 65% of the
 *       half-width whatever the weapon damp, and the camera starts turning past
 *       stick 60/80 -- 75% of that range (~49% of the half-width). */
static int aimRange = 0;
static double aimRangeScale(void)   { return aimRange ? 0.65 : 1.0; }
static double aimEdgeThreshold(void) { return aimRange ? 0.75 : (double)GEPD_EDGE_THRESHOLD; }

/* D422: FOV of the player that owns the polled slot. In a split-screen
 * stage the poll thread's viGetFovY() is whichever player the game thread
 * last set up (bondview2 viSetFovY(g_CurrentPlayer->zoominfovy)), so read the
 * slot player's own zoominfovy there. Solo: exactly viGetFovY(), as before. */
static f32 slotFovY(void)
{
    if (inputMpSplit() && s_slot >= 0) {
        struct player *p = slotPlayer();
        if (p != NULL && p->zoominfovy > 0.0f)
            return p->zoominfovy;
    }
    return viGetFovY();
}

static int pdMouseAimEnabled(void)
{
    const char *e = GE_ENVSTR("GE_PDMOUSEAIM");
    if (e) return atoi(e) != 0;
    return aimModeGet() == AIMMODE_N64 && !aimLegacyGepd;
}

/* D194 GEPD-aim state. Touched ONLY in inputComputePad (game thread), the
 * same confinement as every other static here. The per-poll grabbed delta
 * drives the view, so there is no free-running angle accumulator for
 * multi-tick catch-up (D193) to double-consume; the frac carry only dithers
 * this poll's stick quantization. */
/* D194 GEPD-mirror aim state: the crosshair position accumulator in game
 * units (±GEPD_CROSSHAIR_LIMIT at the screen edge). Written straight into
 * slotPlayer()->crosshair_x/y_pos + gun_azimuth_angle/turning each tick;
 * bondview2's damped crosshair update keeps running and is simply
 * overwritten each tick (GEPD model, D194). */
/* D422: the aim accumulators are per controller slot (seat), so any slot can
 * own a centred / GEPD aim state (online-ready input layer). Only the
 * keyboard/mouse block touches them today, and it runs for slot 0 only, so
 * solo reads/writes exactly slot 0 as before. AIMS = the slot being polled;
 * off the poll (s_slot == -1, game thread) it is slot 0, as before. */
struct aimSlotState {
    double crossX, crossY;   /* was s_gepdCrossX/Y */
    int gepdHeldPrev;        /* aim held last tick -> adopt on entry */
    int aimHeldPrev;         /* was s_aimHeldPrev */
    int centreClearTicks;    /* was s_centreClearTicks */
};
static struct aimSlotState s_aimSt[4];
#define AIMS (s_aimSt[(s_slot > 0 && s_slot < 4) ? s_slot : 0])
static int aimGepdAccumulate(double dxPx, double dyLook);
static void aimGepdEdgeScroll(void);
static int aimGepdCompute(double dxPx, double dyLook);
static int hipDirectCompute(double dxPx, double dyLook);
static double s_hipThetaDegOut;   /* D428: yaw degrees the last hipDirectCompute applied */
static void swayFeedTheta(double deg);
static int padDirectCompute(int dx, int dy);   /* D404: pad twin of the above */
/* D194: bondview2's "look-ahead" pitch centreing (docentreupdown) arms during
 * hip-fire walking whenever the pitch strays from the horizon target, and --
 * once armed -- keeps pulling vv_verta back to it even in aim mode, EXCEPT
 * while a manual pitch stick (|stick_y|>60) is present. That reads as "the
 * gun always tries to return to centre" the moment the mouse stops. Port-
 * side fix: on entering aim with the spring armed, emit a minimal 2-tick
 * pitch nudge in the direction of current motion so the game's own rule
 * (manual input clears docentreupdown) disarms it; aiming is then free and
 * holds when the mouse stops. No game logic touched -- this is just what
 * stick we choose to present. */
/* (s_aimHeldPrev / s_centreClearTicks live in AIMS since D422.) */
/* D194: always 0 since the free-cursor experiment was reverted (aim uses
 * grabbed relative deltas). Kept because reconcileGrab(),
 * applyCursorVisibility() and the mouse-button mask still branch on it --
 * with it pinned to 0 they take exactly the pre-D194 paths. */
static int    s_absAimSuspend = 0;

static int aimAbsCompute(double dxPx, double dyPx, int *outSx, int *outSy);  /* D194 */
static int naturalPitchMode = 1;    /* D194/D238: 1 = force GE's own 1.2/SOLITARE
                                      * control style for continuous analog pitch;
                                      * 0 = legacy 1.1/HONEY + D166 digital pulse. */
static Uint64 s_lastLookPollCounter = 0;  /* D194(b): monotonic clock, gameplay-look drain only */

/* Gamepad tuning -- defaults reproduce the old hardcoded constants exactly.
 * Wave A (v0.5.0 controller/input wave, docs/dev/CONTROLLER-INPUT-PLAN.md):
 * per-stick deadzone splits the single Input.PadDeadzone into a left (movement)
 * and a right (look) value (padDeadzone stays registered so old inis load; the
 * unset L/R values migrate from it once in inputInit). The look knobs
 * (sens X/Y, smoothing) are feel-only and identity at their defaults. */
#define PAD_DZ_UNSET (-1)   /* C init for padDeadzoneL/R: "not in ini -> migrate" */
static int padDeadzoneL    = PAD_DZ_UNSET;    /* left (movement) stick deadzone, raw 0..32767 */
static int padDeadzoneR    = PAD_DZ_UNSET;    /* right (look) stick deadzone, raw 0..32767 */
static int padDeadzone     = STICK_DEADZONE;  /* legacy shared deadzone (Input.PadDeadzone) */
static int padLookSensX    = 100;             /* right-stick look: horizontal sensitivity, % (100 = native) */
static int padLookSensY    = 100;             /* right-stick look: vertical (pitch) sensitivity, % */
static int padSouthpaw     = 0;               /* 1 = swap fire (G) / grenade (R) trigger actions */
static int padLookSmooth   = 0;               /* right-stick look low-pass strength, 0-10 (0 = off, 10 = max) */
static int padTriggerPct   = 23;              /* trigger press point, % of travel (~30*256) */
static int padLookInvertY  = 0;               /* 1 = invert right-stick (look) Y */
/* Per-pad look-smoothing EMA state (Wave A item 5), one X/Y pair per pad. Reset
 * on hot-unplug (inputRescanPads) so a smoothed value can't stick after the
 * stick snaps back; the low-pass otherwise recentres it toward 0 on its own. */
static double padSmSX[MAX_PADS] = {0};
static double padSmSY[MAX_PADS] = {0};

/* Smoothed mouse delta carried between polls when mouseSmoothing > 0. */
static double mouseSmDX = 0.0, mouseSmDY = 0.0;

/* Raw relative-mouse delta accumulated since the last inputComputePad(0).
 * NOT a persistent aim accumulator: mouse-look is a displacement device and
 * GE's aim is a rate device, so we consume the whole delta each poll and
 * reset -- stop moving and the stick/ C-button releases immediately. */
static double mouseDX = 0.0;
static double mouseDY = 0.0;

/* Mouse-wheel -> weapon cycle (D223): directional, like a normal PC shooter.
 * The port forces CONTROLLER_CONFIG_HONEY (bondview.c:1484), so GE's default
 * scheme applies (bondview2.c:5177-5179): invButtons = A_BUTTON,
 * shootButtons = Z_TRIG, and the cycle signals are (bondview2.c:5337-5346):
 *   forward  = fresh A edge while Z is NOT held
 *   backward = Z fresh edge while A IS held (the N64 "hold A, tap Z" trick)
 * moveData.triggerOn (bondview2.c:5419) requires A to be released, so the
 * synthesized Z edge in the backward sequence can never fire a shot.
 *
 * wheelFwd:  remaining polls of the A pulse (clean press+release edge).
 * wheelBack: 2 = present A-only next poll; 1 = present A+Z next poll (the
 *            fresh-Z-edge-while-A-held poll). A must already read "held"
 *            oldbuttons-wise before Z's edge, hence the two-poll sequence. */
#define WHEEL_FWD_POLLS 2
static int wheelFwd  = 0;
static int wheelBack = 0;

/* Item 1 (D165) — front-end pointer P-controller state. */
static int    menuPointerMode  = 1;    /* 0 = legacy velocity, 1 = 1:1 pointer */
static int    hipfirePitchSpeed = 100; /* D166: hipfire pitch pulse rate, percent */
static int    menuPrevActive = 0;
static double hipPitchPhase = 0.0;              /* D166: hipfire pitch pulse phase 0..1     */
static int    lastMenuMouseX = -1, lastMenuMouseY = -1;  /* WI-2: last abs cursor seen in a menu */
/* D345: carry pointer ownership across front-end screen changes. The game
 * teleports cursor_h/v_pos on some transitions (file select -> mode select
 * calls setCursorPOSforMode(0), front.c:2516); the absolute write below only
 * fires while the OS mouse moves, so with an idle mouse the snap persists
 * until the next movement. lastMenuId detects the change; transitionFrames is
 * a short countdown during which a recent-mouse re-assert is allowed;
 * lastAbsWrite is the perf-counter stamp of the last absolute write (mouse
 * recency). */
static int    menuPointerLastMenuId = -1;
static int    menuPointerTransitionFrames = 0;
static Uint64 menuPointerLastAbsWrite = 0;
/* D345(b): 1 while the 1:1 pointer can own cursor_h/v_pos this poll
 * (menu, mouse enabled + grabbed). Refreshed every pad-0 poll. */
static int    s_menuPointerLive = 0;


/* ------------------------------------------------------------------------ */

static void padDetectRumble(int i)
{
    padRumbleOn[i] = 0;
    /* D401: haptics detection (PD pattern, pd_port input.c:320-324).
     * SDL_GameControllerHasRumble() landed in SDL 2.0.18 even though
     * SDL_GameControllerRumble() is from 2.0.9. */
#if SDL_VERSION_ATLEAST(2, 0, 18)
    padRumbleOn[i] = SDL_GameControllerHasRumble(pads[i]);
#else
    padRumbleOn[i] = SDL_JoystickIsHaptic(SDL_GameControllerGetJoystick(pads[i]));
    if (!padRumbleOn[i]) {
        /* At least on Windows some controllers report no haptics but
         * rumble will still function: assume it's supported for a
         * known controller type. */
        const SDL_GameControllerType ctype = SDL_GameControllerGetType(pads[i]);
        padRumbleOn[i] = ctype && (ctype != SDL_CONTROLLER_TYPE_VIRTUAL);
    }
#endif
}

static int padSeatOf(SDL_JoystickID inst)
{
    for (int s = 0; s < MAX_PADS; ++s) {
        if (pads[s] && padInst[s] == inst) return s;
    }
    return -1;
}

/* Seat table maintenance (DNEW-mp-1, see padInst above). Called at boot and on
 * every rescan: drops detached pads, compacts outside a match, opens any new
 * controller into the first free seat, then rebuilds the controller mask. */
static void inputOpenPads(void)
{
    const int inMatch = inputMpSplit();

    /* 1. drop pads that are gone (the handle of a detached pad is dead) */
    for (int s = 0; s < MAX_PADS; ++s) {
        if (pads[s] && !SDL_GameControllerGetAttached(pads[s])) {
            sysLogPrintf(LOG_NOTE, "input: seat %d pad detached%s", s,
                         inMatch ? " (seat kept for the match)" : "");
            SDL_GameControllerClose(pads[s]);
            pads[s] = NULL;
            padInst[s] = -1;
            padRumbleOn[s] = 0;
            if (inMatch) s_seatCompactOwed = 1;
        }
    }

    /* 2. outside a match, seats are dense in their existing order */
    if (!inMatch) {
        int d = 0;
        for (int s = 0; s < MAX_PADS; ++s) {
            if (!pads[s]) continue;
            if (s != d) {
                pads[d] = pads[s];           pads[s] = NULL;
                padInst[d] = padInst[s];     padInst[s] = -1;
                padRumbleOn[d] = padRumbleOn[s]; padRumbleOn[s] = 0;
                sysLogPrintf(LOG_NOTE, "input: pad '%s' moved seat %d -> %d",
                             SDL_GameControllerName(pads[d]), s, d);
            }
            d++;
        }
        s_seatCompactOwed = 0;
    }

    /* 3. open new controllers into the first free seat */
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n; ++i) {
        if (!SDL_IsGameController(i)) {
            continue;
        }
        if (padSeatOf(SDL_JoystickGetDeviceInstanceID(i)) >= 0) {
            continue;   /* already seated */
        }
        int seat = -1;
        for (int s = 0; s < MAX_PADS; ++s) {
            if (!pads[s]) { seat = s; break; }
        }
        if (seat < 0) {
            break;      /* all seats taken */
        }
        pads[seat] = SDL_GameControllerOpen(i);
        if (pads[seat]) {
            padInst[seat] = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[seat]));
            padDetectRumble(seat);
            sysLogPrintf(LOG_NOTE, "input: opened gamepad %d '%s' as seat %d (rumble %s)",
                         i, SDL_GameControllerName(pads[seat]), seat, padRumbleOn[seat] ? "yes" : "no");
        }
    }

    /* 4. controller mask: seat bits, shifted up one when keyboard/mouse is P1 */
    int seatMask = 0;
    nRealPads = 0;
    for (int s = 0; s < MAX_PADS; ++s) {
        if (pads[s]) { seatMask |= 1 << s; nRealPads++; }
    }
    if (inMatch) {
        s_matchSeatMask |= seatMask;   /* reserved seats stay "connected, idle" */
        seatMask = s_matchSeatMask;
    } else {
        s_matchSeatMask = seatMask;
    }
    connectedMask = 0x1 | seatMask;
    if (inputMpKbmP1()) {
        /* keyboard/mouse is controller 0; seat s is controller s + 1 */
        connectedMask = (0x1 | (seatMask << 1)) & ((1 << MAX_PADS) - 1);
    }
    {   /* D416 test harness: GE_MPVIRT=<n> reports n extra idle controllers
         * (1..3) so the MP menu unlocks and a 2P+ stage loads with no pads. */
        const char *v = getenv("GE_MPVIRT");
        int n = v ? atoi(v) : 0;
        if (n > 0) {
            if (n > MAX_PADS - 1) n = MAX_PADS - 1;
            connectedMask |= (1 << (n + 1)) - 1;
        }
    }
    numControllers = 1;
    for (int i = 1; i < MAX_PADS; ++i) {
        if (connectedMask & (1 << i)) {
            numControllers = i + 1;
        }
    }
}

/* ------------------------------------------------------------------------
 * Scripted input (test harness, port-only). GE_INPUTSCRIPT lets a headless
 * run walk the front-end / pause menus with no human at the keyboard.
 *
 *   GE_INPUTSCRIPT="120:START;180:A;240:A;600:SDOWN;900:SNONE,A"
 *
 * Each entry is `<frame>:<tok>[,<tok>...]`. Buttons (A B Z START L R UP DOWN
 * LEFT RIGHT CUP CDOWN CLEFT CRIGHT) pulse for INPUTSCRIPT_PULSE controller
 * reads from <frame>. Analog-stick tokens (SUP SDOWN SLEFT SRIGHT) are
 * SUSTAINED: the stick stays deflected until a later entry changes it; SNONE
 * re-centres it. CHOLD/CREL hold/release GEPD crouch (D377);
 * UHOLD/UREL and RELOADHOLD/RELOADREL exercise dedicated use/reload (D378).
 * "Frame" = count of controller-0 reads since launch (roughly
 * 2 per rendered frame -- watch GE_INPUTLOG to calibrate). Unset env => no
 * effect; when set it is the ONLY controller-0 input source.
 *
 * GE_INPUTSCRIPT_P1 / _P2 / _P3 do the same for slots 1-3 (split-screen
 * seats), so a headless run can drive players 2-4 behind GE_MPVIRT/
 * GE_STARTMP with no physical pads:
 *   GE_INPUTSCRIPT_P1="600:ZHOLD;900:SUP;1200:SNONE,A"
 * Same entry syntax; "frame" = count of THAT controller's reads since
 * launch. Slots 1-3 have no mouse path, so mouse tokens (MDX/MDY) and
 * CHOLD/CREL are ignored there (one warning per script if they appear).
 * Unset => the seat stays idle exactly as before. */
#define INPUTSCRIPT_MAX     64
#define INPUTSCRIPT_PULSE   6

struct scriptEntry { long frame; unsigned mask; int sx, sy; int hasStick;
                     int hasMouse, mdx, mdy; int hasHold, hold;    /* D337 mouse/aim tokens */
                     int hasZHold, zhold;                          /* D207: sustained fire */
                     int hasCrouch, crouch;                       /* D377: free-crouch QA */
                     int hasUse, use, hasReload, reload; };        /* D378: split-action QA */
/* Per-slot state. Slot 0 is the original GE_INPUTSCRIPT harness (byte-for-
 * byte unchanged); slots 1-3 are GE_INPUTSCRIPT_P1..P3 for split-screen test
 * seats (virtual or physical pads). */
struct scriptSlot {
    struct scriptEntry entries[INPUTSCRIPT_MAX];
    int count;        /* -1 = not parsed yet, 0 = parsed empty */
    long frame;       /* controller reads for this slot since launch */
    int curSX, curSY; /* stick set by the last scriptApply() for this slot */
    int mouseOn, mdx, mdy;   /* sustained scripted mouse (slot 0 only) */
    int hold, zhold, crouch, use, reload;  /* latest sustained holds */
    int ignoredWarned;       /* P1..P3: warned once about mouse/crouch tokens */
};
static struct scriptSlot scriptSlots[4] = {
    { .count = -1 }, { .count = -1 }, { .count = -1 }, { .count = -1 },
};

static const char *scriptTag(int slot)
{
    if (slot == 0) return "GE_INPUTSCRIPT";
    return slot == 1 ? "GE_INPUTSCRIPT_P1" : slot == 2 ? "GE_INPUTSCRIPT_P2"
                                                       : "GE_INPUTSCRIPT_P3";
}

static const char *scriptEnv(int slot)
{
    if (slot == 0) return getenv("GE_INPUTSCRIPT");
    return slot == 1 ? getenv("GE_INPUTSCRIPT_P1")
         : slot == 2 ? getenv("GE_INPUTSCRIPT_P2") : getenv("GE_INPUTSCRIPT_P3");
}

/* Apply one token to `e`. Buttons: A B Z START L R UP DOWN LEFT RIGHT CUP
 * CDOWN CLEFT CRIGHT (D-pad/C-buttons). Analog stick: SUP SDOWN SLEFT SRIGHT
 * (full +/-80 deflection -- moves menu cursors). */
static void scriptApplyToken(struct scriptEntry *e, const char *s, int n,
                             int slot)
{
    struct { const char *k; unsigned v; } btn[] = {
        {"A",GE_CONT_A}, {"B",GE_CONT_B}, {"Z",GE_CONT_G}, {"START",GE_CONT_START},
        {"L",GE_CONT_L}, {"R",GE_CONT_R}, {"UP",GE_CONT_UP}, {"DOWN",GE_CONT_DOWN},
        {"LEFT",GE_CONT_LEFT}, {"RIGHT",GE_CONT_RIGHT},
        {"CUP",GE_CONT_E}, {"CDOWN",GE_CONT_D}, {"CLEFT",GE_CONT_C}, {"CRIGHT",GE_CONT_F},
    };
    for (size_t i = 0; i < sizeof(btn)/sizeof(btn[0]); ++i) {
        if ((int)strlen(btn[i].k) == n && SDL_strncasecmp(btn[i].k, s, n) == 0) {
            e->mask |= btn[i].v;
            return;
        }
    }
    /* D337: sustained scripted mouse (px per poll) and aim hold, for headless
     * aim-model measurements: MDX<n> MDY<n> (latest entry wins, 0 stops),
     * RHOLD / RREL (hold / release the game's R aim button). */
    if (n > 3 && (SDL_strncasecmp("MDX", s, 3) == 0 || SDL_strncasecmp("MDY", s, 3) == 0)) {
        if (slot > 0) {   /* no mouse path on slots 1-3: ignore, warn once */
            struct scriptSlot *st = &scriptSlots[slot];
            if (!st->ignoredWarned) {
                st->ignoredWarned = 1;
                sysLogPrintf(LOG_WARNING, "%s: mouse tokens are ignored for slots 1-3 ('%.*s')",
                             scriptTag(slot), n, s);
            }
            return;
        }
        char num[16]; int k = n - 3; if (k > 15) k = 15;
        memcpy(num, s + 3, k); num[k] = 0;
        e->hasMouse = 1;
        if (s[2] == 'X' || s[2] == 'x') e->mdx = atoi(num); else e->mdy = atoi(num);
        return;
    }
    if (n == 5 && SDL_strncasecmp("RHOLD", s, 5) == 0) { e->hasHold = 1; e->hold = 1; return; }
    if (n == 5 && SDL_strncasecmp("LHOLD", s, 5) == 0) { e->hasHold = 1; e->hold = 2; return; }
    if (n == 4 && SDL_strncasecmp("RREL", s, 4) == 0)  { e->hasHold = 1; e->hold = 0; return; }
    /* D207: ZHOLD / ZREL hold / release fire (Z) for automatic weapons;
     * independent of RHOLD so aim + fire can be held together. */
    if (n == 5 && SDL_strncasecmp("ZHOLD", s, 5) == 0) { e->hasZHold = 1; e->zhold = 1; return; }
    if (n == 4 && SDL_strncasecmp("ZREL", s, 4) == 0)  { e->hasZHold = 1; e->zhold = 0; return; }
    if ((n == 5 && SDL_strncasecmp("CHOLD", s, 5) == 0) ||
        (n == 4 && SDL_strncasecmp("CREL", s, 4) == 0)) {
        if (slot > 0) {   /* no crouch path on slots 1-3: ignore, warn once */
            struct scriptSlot *st = &scriptSlots[slot];
            if (!st->ignoredWarned) {
                st->ignoredWarned = 1;
                sysLogPrintf(LOG_WARNING, "%s: CHOLD/CREL are ignored for slots 1-3 ('%.*s')",
                             scriptTag(slot), n, s);
            }
            return;
        }
        e->hasCrouch = 1; e->crouch = (SDL_strncasecmp("CHOLD", s, 5) == 0);
        return;
    }
    if (n == 5 && SDL_strncasecmp("UHOLD", s, 5) == 0) { e->hasUse = 1; e->use = 1; return; }
    if (n == 4 && SDL_strncasecmp("UREL", s, 4) == 0)  { e->hasUse = 1; e->use = 0; return; }
    if (n == 10 && SDL_strncasecmp("RELOADHOLD", s, 10) == 0) { e->hasReload = 1; e->reload = 1; return; }
    if (n == 9 && SDL_strncasecmp("RELOADREL", s, 9) == 0) { e->hasReload = 1; e->reload = 0; return; }
    e->hasStick = 1;
    if (n == 3 && SDL_strncasecmp("SUP", s, 3) == 0)      { e->sy =  STICK_MAX; return; }
    if (n == 5 && SDL_strncasecmp("SDOWN", s, 5) == 0)    { e->sy = -STICK_MAX; return; }
    if (n == 5 && SDL_strncasecmp("SLEFT", s, 5) == 0)    { e->sx = -STICK_MAX; return; }
    if (n == 6 && SDL_strncasecmp("SRIGHT", s, 6) == 0)   { e->sx =  STICK_MAX; return; }
    if (n == 5 && SDL_strncasecmp("SNONE", s, 5) == 0)    { return; }  /* recentre */
    e->hasStick = 0;
    sysLogPrintf(LOG_WARNING, "%s: unknown token '%.*s'", scriptTag(slot), n, s);
}

static void scriptParse(int slot)
{
    struct scriptSlot *st = &scriptSlots[slot];
    const char *tag = scriptTag(slot);
    st->count = 0;
    const char *env = scriptEnv(slot);
    if (!env || !*env) {
        return;
    }
    const char *p = env;
    while (*p && st->count < INPUTSCRIPT_MAX) {
        char *end = NULL;
        long fr = strtol(p, &end, 10);
        if (end == p || *end != ':') {
            sysLogPrintf(LOG_WARNING, "%s: bad entry near '%s'", tag, p);
            break;
        }
        p = end + 1;
        struct scriptEntry *e = &st->entries[st->count];
        e->frame = fr;
        e->mask = 0;
        e->sx = e->sy = 0;
        e->hasStick = 0;
        e->hasMouse = e->mdx = e->mdy = 0;
        e->hasHold = e->hold = 0;
        e->hasZHold = e->zhold = 0;
        e->hasCrouch = e->crouch = 0;
        e->hasUse = e->use = e->hasReload = e->reload = 0;
        while (*p && *p != ';') {
            const char *tok = p;
            while (*p && *p != ',' && *p != ';') ++p;
            scriptApplyToken(e, tok, (int)(p - tok), slot);
            if (*p == ',') ++p;
        }
        if (*p == ';') ++p;
        st->count++;
    }
    sysLogPrintf(LOG_INFO, "%s: %d entr%s parsed",
                 tag, st->count, st->count == 1 ? "y" : "ies");
}

static int scriptIsActive(int slot)
{
    struct scriptSlot *st = &scriptSlots[slot];
    if (st->count < 0) {
        scriptParse(slot);
    }
    return st->count > 0;
}

/* When a script is loaded it is the SOLE source of controller-0 input: real
 * keyboard/mouse/pad is ignored so headless menu walks are deterministic
 * (a relative-mouse SDL window with no focus otherwise spews phantom deltas).
 * Returns the scripted button mask for the current frame; advances the frame
 * counter (call exactly once per controller-0 read). */
/* Refresh the latest sustained holds for one slot from its entries (latest
 * entry wins). Slot 0's mouse fields are read by the keyboard/mouse path; on
 * slots 1-3 only hold/zhold/use/reload can be set (mouse and crouch tokens
 * are ignored at parse time). */
static void scriptRefreshHolds(int slot)
{
    struct scriptSlot *st = &scriptSlots[slot];
    long bestM = -1, bestH = -1, bestZ = -1, bestC = -1, bestU = -1, bestR = -1;
    for (int i = 0; i < st->count; ++i) {
        long d = st->frame - st->entries[i].frame;
        if (d < 0) continue;
        if (st->entries[i].hasMouse && st->entries[i].frame > bestM) {
            bestM = st->entries[i].frame;
            st->mouseOn = 1;
            st->mdx = st->entries[i].mdx;
            st->mdy = st->entries[i].mdy;
        }
        if (st->entries[i].hasHold && st->entries[i].frame > bestH) {
            bestH = st->entries[i].frame;
            st->hold = st->entries[i].hold;
        }
        if (st->entries[i].hasZHold && st->entries[i].frame > bestZ) {
            bestZ = st->entries[i].frame;
            st->zhold = st->entries[i].zhold;
        }
        if (st->entries[i].hasCrouch && st->entries[i].frame > bestC) {
            bestC = st->entries[i].frame;
            st->crouch = st->entries[i].crouch;
        }
        if (st->entries[i].hasUse && st->entries[i].frame > bestU) {
            bestU = st->entries[i].frame;
            st->use = st->entries[i].use;
        }
        if (st->entries[i].hasReload && st->entries[i].frame > bestR) {
            bestR = st->entries[i].frame;
            st->reload = st->entries[i].reload;
        }
    }
}

static unsigned scriptApply(int slot, unsigned button)
{
    struct scriptSlot *st = &scriptSlots[slot];
    if (!scriptIsActive(slot)) {
        return button;
    }
    scriptRefreshHolds(slot);   /* D207: refresh sustained holds even off the mouse path (idempotent) */
    unsigned m = 0;
    long bestStickFrame = -1;
    for (int i = 0; i < st->count; ++i) {
        long d = st->frame - st->entries[i].frame;
        if (d >= 0 && d < INPUTSCRIPT_PULSE) {
            m |= st->entries[i].mask;
        }
        /* sustained stick: the latest-starting entry that carried a stick token */
        if (d >= 0 && st->entries[i].hasStick && st->entries[i].frame > bestStickFrame) {
            bestStickFrame = st->entries[i].frame;
            st->curSX = st->entries[i].sx;
            st->curSY = st->entries[i].sy;
        }
    }
    if (st->hold == 1) m |= GE_CONT_R;   /* D337 RHOLD */
    if (st->hold == 2) m |= GE_CONT_L;   /* D337 LHOLD (Q / "LeanLeft") */
    if (st->zhold) m |= GE_CONT_G;       /* D207 ZHOLD (fire) */
    st->frame++;
    return m;
}

static void inputRebuildBinds(void);   /* D214; defined below with keyDown() */
static void inputMigrateBinds(void);   /* D380; after configLoad */
static void inputBindingProbe(void);   /* D383/D384; opt-in, restores ini state */
static void pcOptionsKeyboardPad(const Uint8 *ks, Uint32 mb, int blocked,
                                 unsigned *button, int *sx, int *sy);

static int bindsVersion = 0; /* D380/D386: versioned migration of effective ini binds */
static int crouchMode = 0;   /* 0 = hold, 1 = toggle (latched crouch input) */

int inputInit(void)
{
    if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER)) {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
            sysLogPrintf(LOG_WARNING, "input: SDL_INIT_GAMECONTROLLER failed: %s",
                         SDL_GetError());
        }
    }

    /* D401: make BT PS4/PS5 pads report haptics (PD input.c:696-697). Must
     * be set before the pads below are opened. */
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");

    for (int i = 0; i < MAX_PADS; ++i) {
        pads[i] = NULL;
        padInst[i] = -1;
    }
    inputOpenPads();

    inputMigrateBinds();  /* D380: materialise effective GEPD defaults once */
    inputRebuildBinds();   /* D214: parse [Bind] now that configLoad() has run */

    /* Wave A: one-time per-stick deadzone migration. Old inis carry a single
     * Input.PadDeadzone; if the player has not set the new per-stick keys, seed
     * both from it (the C-init sentinel PAD_DZ_UNSET means "not in the ini").
     * Runs before any configSave, so the saved L/R values are always valid. */
    if (padDeadzoneL == PAD_DZ_UNSET) padDeadzoneL = padDeadzone;
    if (padDeadzoneR == PAD_DZ_UNSET) padDeadzoneR = padDeadzone;
    if (getenv("GE_BINDPROBE")) inputBindingProbe();

    sysLogPrintf(LOG_INFO, "input: PC bindings, crouch mode %s",
                 crouchMode ? "toggle" : "hold");

    /* Relative mouse mode for mouse-look. Click-to-lock: we start released
     * and wait for a click in the window (video.c -> inputNotifyClick). */
    mouseGrabbed = 0;
    if (mouseEnabled) {
        if (mouseRawInput) {
            /* Feed the raw device delta straight through: no OS pointer
             * acceleration, no warp-based emulation. Must be set before
             * relative mode is enabled. */
            SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0");
            SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "0");
            sysLogPrintf(LOG_INFO, "input: raw mouse input (no OS pointer accel)");
        }
        mouseRequestRelative(mouseGrabbed);
    }

    applyCursorVisibility();   /* hide the OS cursor if we start focused */
    inputApplyMouseRequests(); /* inputInit runs on the host thread */

    sysLogPrintf(LOG_INFO, "input: ready (mask=0x%x, %d controller(s), aimSpeed=%d)",
                 connectedMask, numControllers, mouseAimSpeed);
    return connectedMask;
}

void inputDestroy(void)
{
    for (int i = 0; i < MAX_PADS; ++i) {
        if (pads[i]) {
            SDL_GameControllerClose(pads[i]);
            pads[i] = NULL;
        }
        padInst[i] = -1;
        padRumbleOn[i] = 0;   /* D401: closed */
    }
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER)) {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
}

/* Poll once per controller-read: refresh SDL device state and integrate the
 * mouse-aim delta into the accumulator. */
/* D450: controller hot-plug. SDL_CONTROLLERDEVICEADDED / REMOVED land on
 * whichever of the two event pumps dequeues them first: video.c's host loop
 * or fast3d's gfx_sdl_handle_events (render thread, every frame), and the
 * latter used to drop them, so a replugged pad was never reopened. Both pumps
 * now only post a request; the rescan runs here, at the top of inputUpdate(),
 * on the thread that is about to read pads[], so a pad is never closed under
 * a reader. */
static SDL_atomic_t s_rescanReq;

void inputRequestRescan(void)
{
    SDL_AtomicSet(&s_rescanReq, 1);
}

void inputUpdate(void)
{
    if (SDL_AtomicSet(&s_rescanReq, 0)) inputRescanPads();   /* D450 */
    SDL_GameControllerUpdate();
    if (s_seatCompactOwed && !inputMpSplit()) {
        inputOpenPads();   /* DNEW-mp-1: match over -> compact the seats */
    }

    /* D287: skip until the host thread has actually enabled relative mode
     * (a benign racy read of a host-thread int), so the few ms between the
     * logical grab and its application do not read absolute cursor motion. */
    if (!mouseEnabled || !mouseGrabbed || s_appRelative != 1) {
        return;
    }

    int dx = 0, dy = 0;
    SDL_GetRelativeMouseState(&dx, &dy);

    /* Raw px; per-mode sensitivity is applied in inputComputePad(). Accumulate
     * in case inputUpdate() is polled more than once between pad reads. */
    mouseDX += dx;
    mouseDY += dy;
}

static int scaleAxis(int v, int dz)
{
    if (dz < 0) dz = 0;
    if (dz > 30000) dz = 30000;
    if (v > -dz && v < dz) {
        return 0;
    }
    if (v < 0) v += dz; else v -= dz;
    int out = (int)((long)v * STICK_MAX / (32767 - dz));
    if (out >  STICK_MAX) out =  STICK_MAX;
    if (out < -STICK_MAX) out = -STICK_MAX;
    return out;
}

static int keyDown(const Uint8 *ks, SDL_Scancode sc)
{
    return ks && sc != SDL_SCANCODE_UNKNOWN && ks[sc];
}

/* ------------------------------------------------------------------------
 * D214 — keyboard rebinding (PD parity: config-string driven, no UI).
 *
 * Each gameplay action maps to a comma-separated list of SDL scancode names
 * (as printed by SDL_GetScancodeName: "W", "Up", "Left Ctrl", "Space", ...).
 * The defaults reproduce the previously-hardcoded FPS layout exactly, so a
 * fresh or [Bind]-less ini changes nothing. Parsed once in inputInit(), after
 * configLoad(). D385: buttons 1..5 share these slots with keyboard keys.
 * ---------------------------------------------------------------------- */
enum {
    IA_FORWARD, IA_BACK, IA_STRAFE_L, IA_STRAFE_R, IA_TURN_L, IA_TURN_R,
    IA_FIRE, IA_AIM, IA_ACTION, IA_CANCEL, IA_LEAN_L, IA_START,
    /* Reload and crouch use the port's dedicated action/stance paths in
     * playable stages; the native B/C-down buttons have other meanings. */
    IA_RELOAD, IA_CROUCH, IA_COUNT
};

static const struct { const char *key; const char *def; } kBindDefs[IA_COUNT] = {
    [IA_FORWARD]  = { "Input.Bind.Forward",     "W,Up"          },
    [IA_BACK]     = { "Input.Bind.Back",        "S,Down"        },
    [IA_STRAFE_L] = { "Input.Bind.StrafeLeft",  "A"             },
    [IA_STRAFE_R] = { "Input.Bind.StrafeRight", "D"             },
    [IA_TURN_L]   = { "Input.Bind.TurnLeft",    "Left"          },
    [IA_TURN_R]   = { "Input.Bind.TurnRight",   "Right"         },
    [IA_FIRE]     = { "Input.Bind.Fire",        "Left Ctrl"     },
    [IA_AIM]      = { "Input.Bind.Aim",         "Left Shift"    },
    [IA_ACTION]   = { "Input.Bind.Action",      "Space,Z,E"     },
    [IA_CANCEL]   = { "Input.Bind.Cancel",      "X,R,F,Escape"  },
    [IA_LEAN_L]   = { "Input.Bind.LeanLeft",    "Q"             },
    [IA_START]    = { "Input.Bind.Start",       "Return,Tab"    },
    [IA_RELOAD]   = { "Input.Bind.Reload",      ""              },
    [IA_CROUCH]   = { "Input.Bind.Crouch",      ""              },
};

/* Legacy pre-D380 N64 defaults (kBindDefs above) are only a migration
 * signature. The single PC layout is the GEPD/mouse default: Q = A
 * button (accept/next weapon), E = B button (use/cancel/reload),
 * R = dedicated reload, Ctrl = crouch). The preset is the effective default
 * for a one-time ini conversion only. On disk, every Bind key now stores
 * its actual effective value; `NONE` explicitly unbinds even a preset key.
 * Fire loses its Left Ctrl key (Mouse 1 fires; Ctrl crouches), LeanLeft unbound.
 * NULL entries retain their old defaults (movement etc.). */
static const char *const kGepdPreset[IA_COUNT] = {
    [IA_FIRE]   = "Mouse 1",
    [IA_AIM]    = "Mouse 3,Left Shift",
    [IA_ACTION] = "Q",
    [IA_CANCEL] = "E",
    [IA_LEAN_L] = "",
    [IA_RELOAD] = "R",
    [IA_CROUCH] = "Left Ctrl",
};
/* D416: crouch state is per controller slot (slot 0 only in solo). */
static int s_crouchLatchA[4], s_crouchHeldPrevA[4], s_crouchAppliedA[4];
static struct player *s_crouchPlayerA[4];
#define CSLOT() (s_slot > 0 && s_slot < 4 ? s_slot : 0)
#define s_crouchLatch    (s_crouchLatchA[CSLOT()])
#define s_crouchHeldPrev (s_crouchHeldPrevA[CSLOT()])
#define s_crouchApplied  (s_crouchAppliedA[CSLOT()])   /* port-owned stance; not the native C-down crouch */
#define s_crouchPlayer   (s_crouchPlayerA[CSLOT()])
static int s_useHeldPrev = 0, s_reloadHeldPrev = 0;

/* GEPD-style crouch operates on the game's existing stance field, without
 * holding the aim button. The game interpolates ducking_height_offset from
 * crouchpos (bondview2.c:7038-7074). Only undo a stance we applied; preserve
 * auto-crouch in tight spaces (autocrouchpos is a separate minimum). */
static void inputDropCrouch(void)
{
    /* The engine can take over the stance (tank, respawn, etc.) between
     * polls; do not undo its new value or write into a different player. */
    if (s_crouchApplied && slotPlayer() == s_crouchPlayer &&
        slotPlayer()->crouchpos == CROUCH_SQUAT)
        slotPlayer()->crouchpos = CROUCH_STAND;
    s_crouchApplied = 0;
    s_crouchPlayer = NULL;
}

#define BIND_MAX_KEYS INPUT_BIND_SLOTS
static char         g_bindStr[IA_COUNT][64];
static int          s_legacyBindWarned[IA_COUNT];
static int g_bind[IA_COUNT][BIND_MAX_KEYS];

/* Formerly LMB/RMB were unconditional, even for a NONE keyboard bind. Move
 * that effective behaviour into the two visible slots on first v2 load.
 * If both slots are already occupied, preserve both user keys and log the
 * mouse default that could not fit; never keep an invisible active third. */
static void migrateMouseDefault(int a, int button)
{
    char old[sizeof(g_bindStr[0])];
    snprintf(old, sizeof(old), "%s", g_bindStr[a]);
    char name[16];
    snprintf(name, sizeof(name), "Mouse %d", button);
    if (strstr(old, name)) return;
    const char *first = old;
    const char *comma = strchr(old, ',');
    if (comma && strchr(comma + 1, ',')) {
        sysLogPrintf(LOG_WARNING, "input: %s has extra legacy keys; cannot add %s in two slots",
                     kBindDefs[a].key, name);
        return;
    }
    if (comma && strncmp(old, "NONE,", 5) != 0 &&
        SDL_strcasecmp(comma + 1, "NONE") != 0) {
        sysLogPrintf(LOG_WARNING, "input: %s already has two keys; rebind a slot to restore %s",
                     kBindDefs[a].key, name);
        return;
    }
    if (SDL_strcasecmp(first, "NONE") == 0 || !*old) first = "";
    else if (comma && strncmp(old, "NONE,", 5) == 0) first = comma + 1;
    else if (comma) {
        /* A trailing NONE slot is empty; preserve the first key. */
        char *end = strchr(old, ',');
        *end = 0;
        first = old;
    }
    if (strlen(name) + (*first ? 1 + strlen(first) : 0) >= sizeof(g_bindStr[a])) {
        sysLogPrintf(LOG_WARNING, "input: %s too long to add %s; unchanged",
                     kBindDefs[a].key, name);
        return;
    }
    strcpy(g_bindStr[a], name);
    if (*first) {
        strcat(g_bindStr[a], ",");
        strcat(g_bindStr[a], first);
    }
}

static void inputMigrateBinds(void)
{
    if (bindsVersion >= 3) return;
    if (bindsVersion == 2) {
        /* D386: D385 called SDL button 2 "right" in its default, but SDL
         * button 2 is middle; button 3 is right. Only fix the exact old
         * default, never overwrite a user's custom Aim assignment. */
        if (strcmp(g_bindStr[IA_AIM], "Mouse 2,Left Shift") == 0)
            snprintf(g_bindStr[IA_AIM], sizeof(g_bindStr[IA_AIM]),
                     "Mouse 3,Left Shift");
    }
    if (bindsVersion == 0) {
        for (int a = 0; a < IA_COUNT; a++) {
            const char *preset = kGepdPreset[a];
            if (preset && strcmp(g_bindStr[a], kBindDefs[a].def) == 0)
                snprintf(g_bindStr[a], sizeof(g_bindStr[a]), "%s", preset);
        }
    }
    if (bindsVersion < 2) {
        migrateMouseDefault(IA_FIRE, SDL_BUTTON_LEFT);
        migrateMouseDefault(IA_AIM, SDL_BUTTON_RIGHT);
    }
    bindsVersion = 3;
    configSave();
    sysLogPrintf(LOG_INFO, "input: migrated binds to visible PC keyboard/mouse slots");
}

static void inputRebuildBinds(void)
{
    for (int a = 0; a < IA_COUNT; a++) {
        for (int k = 0; k < BIND_MAX_KEYS; k++) {
            g_bind[a][k] = SDL_SCANCODE_UNKNOWN;
        }
        /* D380: ini values ARE the effective bindings, not hidden defaults.
         * NONE is an explicit unbind (PD-port convention). */
        const char *def = SDL_strcasecmp(g_bindStr[a], "NONE") == 0 ? "" : g_bindStr[a];
        /* D384: do not silently activate invisible 3rd/4th keys in the
         * two-slot UI. Preserve the old ini text until the user edits this
         * particular action, at which point only its visible pair is saved. */
        const char *second = strchr(def, ',');
        if (second && strchr(second + 1, ',') && !s_legacyBindWarned[a]) {
            s_legacyBindWarned[a] = 1;
            sysLogPrintf(LOG_WARNING, "input: %s has legacy extra keys (ignored; edit this action to remove)",
                         kBindDefs[a].key);
        }
        char buf[64];
        strncpy(buf, def, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        /* An empty bind or NONE is intentionally unbound, not a typo. */
        int hadInput = (def == g_bindStr[a] && g_bindStr[a][0] != 0);

        int n = 0, valid = 0;
        for (char *tok = strtok(buf, ","); tok && n < BIND_MAX_KEYS; tok = strtok(NULL, ",")) {
            while (*tok == ' ' || *tok == '\t') tok++;
            char *end = tok + strlen(tok);
            while (end > tok && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
            if (!*tok) continue;
            /* Explicit NONE preserves an empty slot before later keys. */
            if (SDL_strcasecmp(tok, "NONE") == 0) { n++; continue; }
            int sc;
            if (!SDL_strncasecmp(tok, "Mouse ", 6) &&
                tok[6] >= '1' && tok[6] <= '5' && tok[7] == 0) {
                sc = INPUT_BIND_MOUSE(tok[6] - '0');
            } else if (!strncmp(tok, "SC:", 3)) {
                char *endnum;
                long id = strtol(tok + 3, &endnum, 10);
                sc = *endnum == 0 && id > 0 && id < SDL_NUM_SCANCODES
                    ? (SDL_Scancode)id : SDL_SCANCODE_UNKNOWN;
            } else {
                sc = SDL_GetScancodeFromName(tok);
            }
            if (sc == SDL_SCANCODE_UNKNOWN) {
                sysLogPrintf(LOG_WARNING, "input: %s: unknown binding '%s'",
                             kBindDefs[a].key, tok);
                continue;
            }
            g_bind[a][n++] = sc;
            valid++;
        }
        /* v0.4.0 M3: an intentionally-empty string (a preset or default
         * that leaves an action unbound, e.g. GEPD Fire) is silent --
         * only a non-empty string that produced no valid keys warns. */
        if (valid == 0 && hadInput) {
            sysLogPrintf(LOG_WARNING, "input: %s has no valid keys; action unbound",
                         kBindDefs[a].key);
        }
    }
}

/* Re-derive binds after an F10 input option or future key capture
 * (optionsoverlay rowSetCommit hook, scheduler thread).
 * g_bind is read per-frame by the game thread's actHeld(): a mid-poll
 * rebuild can at worst drop or add one scancode for one frame (the
 * D214 startup-rebuild is the same thread-pairing, just at boot).
 * Also drops the crouch latch on a layout/mode change. */
void inputBindingsApply(void)
{
    s_crouchLatch = 0;
    s_crouchHeldPrev = 0;
    inputDropCrouch();
    inputRebuildBinds();
    sysLogPrintf(LOG_INFO, "input: PC bindings, crouch %s applied",
                 crouchMode ? "toggle" : "hold");
}

/* D384/D385: the UI edits two honest keyboard/mouse slots. Legacy extra ini tokens
 * remain unmodified on disk until this action is explicitly edited; they
 * cannot fire invisibly while only two slots are displayed. */
static int bindingAction(const char *key)
{
    if (!key) return -1;
    for (int a = 0; a < IA_COUNT; a++)
        if (!strcmp(kBindDefs[a].key, key)) return a;
    return -1;
}

const char *inputBindingSlot(const char *key, int slot)
{
    int a = bindingAction(key);
    if (a < 0 || slot < 0 || slot >= BIND_MAX_KEYS ||
        g_bind[a][slot] == SDL_SCANCODE_UNKNOWN) return "NONE";
    if (g_bind[a][slot] > SDL_NUM_SCANCODES &&
        g_bind[a][slot] <= INPUT_BIND_MOUSE(5)) {
        static const char *names[] = { "Mouse 1", "Mouse 2", "Mouse 3", "Mouse 4", "Mouse 5" };
        return names[g_bind[a][slot] - INPUT_BIND_MOUSE(1)];
    }
    const char *name = SDL_GetScancodeName((SDL_Scancode)g_bind[a][slot]);
    if (name && *name && !strchr(name, ',')) return name;
    /* Rare SDL names contain commas (e.g. keypad comma), which the ini
     * treats as separators. The stable numeric fallback remains bindable. */
    static _Thread_local char fallback[16];
    snprintf(fallback, sizeof(fallback), "SC:%d", (int)g_bind[a][slot]);
    return fallback;
}

int inputBindingSetSlot(const char *key, int slot, int sc)
{
    int a = bindingAction(key);
    if (a < 0 || slot < 0 || slot >= BIND_MAX_KEYS ||
        sc < SDL_SCANCODE_UNKNOWN || sc == SDL_NUM_SCANCODES ||
        sc > INPUT_BIND_MOUSE(5)) return 0;
    int slots[BIND_MAX_KEYS];
    memcpy(slots, g_bind[a], sizeof(slots));
    if (sc != SDL_SCANCODE_UNKNOWN) {
        for (int i = 0; i < BIND_MAX_KEYS; i++)
            if (i != slot && slots[i] == sc) slots[i] = SDL_SCANCODE_UNKNOWN;
    }
    slots[slot] = sc;
    char buf[sizeof(g_bindStr[0])] = {0};
    int used = 0;
    for (int i = 0; i < BIND_MAX_KEYS; i++) {
        if (slots[i] == SDL_SCANCODE_UNKNOWN) {
            /* Only serialize holes before a later bound slot. */
            int later = 0;
            for (int j = i + 1; j < BIND_MAX_KEYS; j++)
                if (slots[j] != SDL_SCANCODE_UNKNOWN) later = 1;
            if (!later) continue;
            if (used + 5 + (used != 0) >= (int)sizeof(buf)) return 0;
            if (used) buf[used++] = ',';
            memcpy(buf + used, "NONE", 4);
            used += 4;
            buf[used] = 0;
            continue;
        }
        const char *name;
        char fallback[32];
        if (slots[i] > SDL_NUM_SCANCODES) {
            snprintf(fallback, sizeof(fallback), "Mouse %d", slots[i] - SDL_NUM_SCANCODES);
            name = fallback;
        } else name = SDL_GetScancodeName((SDL_Scancode)slots[i]);
        if (!name || !*name || strchr(name, ',')) {
            snprintf(fallback, sizeof(fallback), "SC:%d", (int)slots[i]);
            name = fallback;
        }
        int len = (int)strlen(name);
        if (used + len + (used != 0) >= (int)sizeof(buf)) return 0;
        if (used) buf[used++] = ',';
        memcpy(buf + used, name, len);
        used += len;
        buf[used] = 0;
    }
    snprintf(g_bindStr[a], sizeof(g_bindStr[a]), "%s", used ? buf : "NONE");
    inputBindingsApply();
    return 1;
}

int inputBindingResetKey(const char *key)
{
    int a = bindingAction(key);
    if (a < 0) return 0;
    const char *def = kGepdPreset[a] ? kGepdPreset[a] : kBindDefs[a].def;
    snprintf(g_bindStr[a], sizeof(g_bindStr[a]), "%s", def);
    return 1;
}

/* Isolated boot probe: validate multi-slot persistence without writing ini.
 * The entire original binding is restored before gameplay begins. */
static void inputBindingProbe(void)
{
    const char *key = "Input.Bind.Crouch";
    int a = bindingAction(key), ok = a >= 0;
    char saved[sizeof(g_bindStr[0])];
    if (!ok) return;
    memcpy(saved, g_bindStr[a], sizeof(saved));
    ok &= inputBindingSetSlot(key, 1, SDL_SCANCODE_KP_COMMA);
    ok &= g_bind[a][1] == SDL_SCANCODE_KP_COMMA;
    ok &= inputBindingSetSlot(key, 0, SDL_SCANCODE_UNKNOWN);
    ok &= g_bind[a][0] == SDL_SCANCODE_UNKNOWN && g_bind[a][1] == SDL_SCANCODE_KP_COMMA;
    ok &= strstr(g_bindStr[a], "NONE,") == g_bindStr[a];
    ok &= inputBindingSetSlot(key, 1, SDL_SCANCODE_UNKNOWN);
    ok &= strcmp(g_bindStr[a], "NONE") == 0;
    memcpy(g_bindStr[a], saved, sizeof(saved));
    inputBindingsApply();
    /* Mouse buttons are real, serializable bindings; duplicate assignment
     * clears the other visible slot rather than leaving a hidden action. */
    key = "Input.Bind.Fire";
    a = bindingAction(key);
    memcpy(saved, g_bindStr[a], sizeof(saved));
    ok &= inputBindingSetSlot(key, 0, INPUT_BIND_MOUSE(5));
    ok &= strcmp(inputBindingSlot(key, 0), "Mouse 5") == 0;
    ok &= inputBindingSetSlot(key, 1, INPUT_BIND_MOUSE(5));
    ok &= g_bind[a][0] == 0 && g_bind[a][1] == INPUT_BIND_MOUSE(5);
    ok &= strcmp(g_bindStr[a], "NONE,Mouse 5") == 0;
    memcpy(g_bindStr[a], saved, sizeof(saved));
    inputBindingsApply();
    Uint8 keys[SDL_NUM_SCANCODES] = {0};
    unsigned menuButton = 0;
    int menuX = 0, menuY = 0;
    keys[SDL_SCANCODE_A] = keys[SDL_SCANCODE_D] = keys[SDL_SCANCODE_C] = 1;
    pcOptionsKeyboardPad(keys, 0, 0, &menuButton, &menuX, &menuY);
    ok &= !menuButton && !menuX && !menuY; /* gameplay keys cannot steer editor */
    keys[SDL_SCANCODE_LEFT] = keys[SDL_SCANCODE_RETURN] = 1;
    pcOptionsKeyboardPad(keys, 0, 0, &menuButton, &menuX, &menuY);
    ok &= menuX == -STICK_MAX && !menuY && menuButton == GE_CONT_A;
    pcOptionsKeyboardPad(keys, 0, 1, &menuButton, &menuX, &menuY);
    ok &= !menuButton && !menuX && !menuY;
    sysLogPrintf(ok ? LOG_INFO : LOG_ERROR,
                 "GE_BINDPROBE: %s (2 slots, mouse 1-5, sparse/comma, A/D/C ignored, arrows/Enter, modal isolation; ini restored)",
                 ok ? "PASS" : "FAIL");
}

static int actHeld(const Uint8 *ks, int act)
{
    /* Front-end mouse clicks stay fixed UI controls. Click-to-lock while
     * playing cannot fire a newly rebound button until the grab succeeds. */
    Uint32 mb = mouseEnabled && mouseGrabbed &&
                current_menu == GE_MENU_RUN_STAGE && !optionsOverlayIsOpen()
              ? SDL_GetMouseState(NULL, NULL) : 0;
    for (int k = 0; k < BIND_MAX_KEYS; k++) {
        int code = g_bind[act][k];
        if (code > SDL_NUM_SCANCODES && code <= INPUT_BIND_MOUSE(5)) {
            if (mb & SDL_BUTTON(code - SDL_NUM_SCANCODES)) return 1;
        } else if (code > SDL_SCANCODE_UNKNOWN && code < SDL_NUM_SCANCODES &&
                   keyDown(ks, (SDL_Scancode)code)) return 1;
    }
    return 0;
}

/* PC settings use immutable keyboard navigation, independent of gameplay
 * bindings (which are being edited here). Do not consume A/D/C as controls. */
static void pcOptionsKeyboardPad(const Uint8 *ks, Uint32 mb, int blocked,
                                 unsigned *button, int *sx, int *sy)
{
    *button = 0;
    *sx = *sy = 0;
    if (blocked) return;
    if (ks[SDL_SCANCODE_UP] || ks[SDL_SCANCODE_KP_8] || ks[SDL_SCANCODE_W]) *sy = STICK_MAX;
    if (ks[SDL_SCANCODE_DOWN] || ks[SDL_SCANCODE_KP_2] || ks[SDL_SCANCODE_S]) *sy = -STICK_MAX;
    if (ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_KP_4]) *sx = -STICK_MAX;
    if (ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_KP_6]) *sx = STICK_MAX;
    if (ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER]) *button |= GE_CONT_A;
    if (ks[SDL_SCANCODE_ESCAPE]) *button |= GE_CONT_B;
    if (mb & SDL_BUTTON(SDL_BUTTON_LEFT)) *button |= GE_CONT_A;
    if (mb & SDL_BUTTON(SDL_BUTTON_RIGHT)) *button |= GE_CONT_B;
}

/* Direct use/reload and modern face buttons share the same game-state gate.
 * In watch, pause, tank, front end or scripted cameras use native menu bits. */
static int inputCanUseGameplayActions(int menuMode)
{
    return !menuMode && slotPlayer() && !slotPlayer()->bonddead &&
           slotPlayer()->outside_watch_menu && !slotPlayer()->pause_state &&
           !slotPlayer()->mpmenuon && !g_PlayerIsInTank &&
           !lvlGetControlsLockedFlag() && !gameScriptedCameraActive();
}

/* D394: the Xbox B gadget cycle has no N64 button equivalent (native A
 * deliberately skips IDs >= ITEM_BOMBCASE). Select the next owned,
 * equippable inventory gadget using the game's normal weapon-switch path;
 * never conjure a missing item or touch the N64 game logic. The first press
 * from a gun selects the lowest gadget; the next press wraps the list. */
static void inputCycleGadget(void)
{
    if (inputMpSplit()) return;   /* D419: acts on g_CurrentPlayer; solo only */
    struct player *p = slotPlayer();
    if (!p || !p->ptr_inventory_first_in_cycle) return;

    int current = get_next_weapon_in_cycle_for_hand(GUNRIGHT, 1);
    int first = -1, next = -1;
    InvItem *head = p->ptr_inventory_first_in_cycle;
    InvItem *item = head;
    do {
        if (item->type == INV_ITEM_WEAPON) {
            int id = item->type_inv_item.type_weap.weapon;
            if (id >= ITEM_BOMBCASE && id <= ITEM_WATCHMAGNETATTRACT) {
                if (first < 0 || id < first) first = id;
                if (id > current && (next < 0 || id < next)) next = id;
            }
        }
        item = item->next;
    } while (item && item != head);

    if (next < 0) next = first;
    if (next >= 0 && next != current) {
        gunRequestHandWeaponChange(GUNRIGHT, next, 1);
        gunRequestHandWeaponChange(GUNLEFT, ITEM_UNARMED, 1);
    }
}

/* Fill button mask + stick for controller idx. Returns the 16-bit mask. */
static unsigned inputComputePadSlot(int idx, signed char *stick_x, signed char *stick_y);
unsigned inputComputePad(int idx, signed char *stick_x, signed char *stick_y)
{
    unsigned r;
    if (idx > 0 && inputMpSplit() && idx >= getPlayerCount()) {   /* D416: no player owns this slot */
        if (stick_x) *stick_x = 0;
        if (stick_y) *stick_y = 0;
        return 0;
    }
    s_slot = idx;
    r = inputComputePadSlot(idx, stick_x, stick_y);
    s_slot = -1;
    return r;
}

static unsigned inputComputePadSlot(int idx, signed char *stick_x, signed char *stick_y)
{
    unsigned button = 0;
    int sx = 0, sy = 0;
    int crouchNow = 0;

    /* D194/D238: self-correcting every poll -- cheap (plain field writes,
     * see options.c cur_player_set_control_type), and re-asserts itself if
     * anything else ever calls the setter (menu, save load) in between. */
    if ((idx == 0 || inputMpSplit()) && slotPlayer() != NULL &&
        (idx == 0 || idx < getPlayerCount())) {   /* D416: each slot rewrites only its own player's style */
        int wantSolitare = naturalPitchMode ? CONTROLLER_CONFIG_SOLITARE_ : CONTROLLER_CONFIG_HONEY_;
        struct player *sp = slotPlayer();
        if (!inputMpSplit()) {
            if (cur_player_get_control_type() != wantSolitare)
                cur_player_set_control_type(wantSolitare);
        } else if (sp->cur_player_control_type_0 != wantSolitare) {
            /* mirror of options.c cur_player_set_control_type on the slot's own player */
            sp->cur_player_control_type_0 = wantSolitare;
            sp->cur_player_control_type_1 = wantSolitare;
            sp->cur_player_control_type_2 = (float)wantSolitare;
            sp->neg_vspacing_for_control_type_entry = -((j_text_trigger ? 14 : 10) * wantSolitare);
            sp->has_set_control_type_data = TRUE;
        }
    }

    if (idx < 0 || idx >= MAX_PADS) {
        if (stick_x) *stick_x = 0;
        if (stick_y) *stick_y = 0;
        return 0;
    }

    /* F10 options overlay: while it is open, controller 0 is fully swallowed
     * (neutral pad, no stick) and the nav keys / wheel / gamepad drive the
     * overlay instead. Mirrors the WI-1 "cursor free in a stage -> withhold
     * input" pattern. Controllers 1-3 are untouched. */
    if (idx == 0 && optionsOverlayIsOpen()) {
        const Uint8 *overlayKs = SDL_GetKeyboardState(NULL);
        s_useHeldPrev = actHeld(overlayKs, IA_CANCEL) ||
            (pads[0] && SDL_GameControllerGetButton(pads[0], SDL_CONTROLLER_BUTTON_A));
        s_reloadHeldPrev = actHeld(overlayKs, IA_RELOAD) ||
            (pads[0] && SDL_GameControllerGetButton(pads[0], SDL_CONTROLLER_BUTTON_X));
        s_crouchLatch = 0;
        s_crouchHeldPrev = 0;
        inputDropCrouch();
        padBPrev[0] = pads[0] ? SDL_GameControllerGetButton(pads[0], SDL_CONTROLLER_BUTTON_B) : 0;
        padYPrev[0] = pads[0] ? SDL_GameControllerGetButton(pads[0], SDL_CONTROLLER_BUTTON_Y) : 0;
        /* Select closes the overlay. padSelectPrev is tracked on this path
         * and the open path below alike, so a button held across the
         * transition cannot immediately re-toggle it. */
        int selNow = pads[0] ? SDL_GameControllerGetButton(pads[0],
                                                           SDL_CONTROLLER_BUTTON_BACK) : 0;
        if (selNow && !padSelectPrev) optionsOverlayToggle();
        padSelectPrev = selNow;
        optionsOverlayHandleInput();
        if (stick_x) *stick_x = 0;
        if (stick_y) *stick_y = 0;
        s_menuPointerLive = 0;   /* D345(b): the overlay owns the mouse */
        return 0;
    }

    /* ---- keyboard + mouse: controller 0 only (not when pads own P1, D416) ---- */
    if (idx == 0 && !inputMpPadsOwnP1()) {
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        Uint32 mb = mouseEnabled ? SDL_GetMouseState(NULL, NULL) : 0;
        int menuMode = (current_menu != GE_MENU_RUN_STAGE &&
                        current_menu != GE_MENU_INVALID);

        /* D345: detect front-end screen changes (any current_menu flip,
         * including into MENU_PC_OPTIONS / the F10 overlay's parent state)
         * and open a short re-assert window. Runs every poll so the
         * countdown drains even when the pointer block below is skipped
         * (mouse disabled, unfocused window, legacy velocity mode). */
        if ((int)current_menu != menuPointerLastMenuId) {
            menuPointerLastMenuId = (int)current_menu;
            menuPointerTransitionFrames = 3;   /* this poll + two more */
        }
        if (menuPointerTransitionFrames > 0) menuPointerTransitionFrames--;

        /* D194: aim mode drives the view from GRABBED relative deltas (see
         * aimAbsCompute) -- the cursor stays hidden and clipped to the window
         * for the whole hold. The earlier free-cursor experiment (suspend the
         * grab, read absolute position) is reverted: the visible cursor could
         * leave the window and OS micro-jitter read as view jitter.
         * s_absAimSuspend therefore stays 0; the readers that honour it
         * (reconcileGrab/applyCursorVisibility/button mask) behave exactly as
         * pre-D194. */
        s_absAimSuspend = 0;
        reconcileGrab(menuMode);
        /* D337 harness: scripted sustained mouse replaces the real deltas and
         * behaves as a grabbed mouse (headless windows are never focused). */
        if (scriptIsActive(0)) {
            scriptRefreshHolds(0);
            if (scriptSlots[0].mouseOn) {
                mouseDX = (double)scriptSlots[0].mdx;
                mouseDY = (double)scriptSlots[0].mdy;
                mouseGrabbed = 1;
            }
        }
        /* D196: the F10 options overlay forces the OS cursor visible via
         * inputSuspendForOverlay() but nothing re-hides it on close unless
         * reconcileGrab() happens to re-grab (only true if you had already
         * clicked-to-lock in a stage). Re-assert the correct visibility
         * every poll here -- this line is unreachable while the overlay is
         * open (early return above), so it only fires once it has closed. */
        applyCursorVisibility();

        /* Click-to-lock, in a stage, cursor free: the mouse buttons must not
         * reach the game (no phantom fire) -- the first click only re-locks
         * (handled in video.c -> inputNotifyClick). Menus keep their buttons. */
        /* ...but not while an aim hold has the cursor out for absolute aim:
         * then the buttons are intentional (RMB is the hold itself, LMB fires). */
        if (!mouseGrabbed && !menuMode && !s_absAimSuspend) {
            mb = 0;
        }

        /* GE default control (1.1): stick Y = move fwd/back, stick X = turn,
         * C-left/right = sidestep, C-up/down = look. FPS layout: W/S move,
         * A/D strafe (C-buttons), mouse X turns (stick X), mouse Y looks. */
        /* D194/D238 natural-pitch mode (GE's own 1.2/SOLITARE style) reads
         * forward/back from digital C-up/C-down instead of the analog stick
         * (bondview2.c: digitalStepForward/Back <- U/D_JPAD|U/D_CBUTTONS),
         * because the stick's Y axis is what carries continuous analog
         * pitch there instead. Strafe and turn are unchanged -- both
         * schemes read them the same way. */
        /* D345(d): natural pitch is an in-stage aim scheme -- the N64 front
         * end navigates on the analog stick only, so in menus W/S (and the
         * Up/Down arrows) must emit stick even with NaturalPitch=1, or no
         * front-end screen (file select, cheat, MENU_PC_OPTIONS) can be
         * driven vertically from the keyboard. Watch/pause keeps the C-button
         * mapping: it is current_menu==RUN_STAGE (menuMode 0), N64-style. */
        if (naturalPitchMode && !menuMode) {
            if (actHeld(ks, IA_FORWARD)) button |= GE_CONT_E;   /* C-up = forward   */
            if (actHeld(ks, IA_BACK))    button |= GE_CONT_D;   /* C-down = back    */
        } else {
            if (actHeld(ks, IA_FORWARD))  sy =  STICK_MAX;
            if (actHeld(ks, IA_BACK))     sy = -STICK_MAX;
        }
        if (actHeld(ks, IA_STRAFE_L)) button |= GE_CONT_C;   /* strafe left  */
        if (actHeld(ks, IA_STRAFE_R)) button |= GE_CONT_F;   /* strafe right */
        if (actHeld(ks, IA_TURN_L))   sx = -STICK_MAX;       /* keyboard turn */
        if (actHeld(ks, IA_TURN_R))   sx =  STICK_MAX;
        if (sx) s_aimDevMouse = 0;   /* D337: keyboard stick turn is the active aim device */

        if (actHeld(ks, IA_FIRE))
            button |= GE_CONT_G;
        /* aimButton = the Aim bind (emits the game's R);
         * aimHeld = "the game is in aim mode" for routing the mouse. D337: the
         * game's aim buttons in 1.1/1.2 are L|R (bondview2.c aimButtons), so Q
         * ("LeanLeft", sends L) is a second aim button -- it used to leave the
         * mouse on the hipfire camera path, the accidental "centred aim" #104
         * found. Routing also follows the game's own insightaimmode, which
         * covers the Aim Control "Toggle" option and gamepad aim. The R emit
         * stays tied to the physical button so Toggle still sees edges. */
        int aimButton = actHeld(ks, IA_AIM);
        int aimHeld = aimButton || actHeld(ks, IA_LEAN_L) ||
                      (slotPlayer() != NULL && slotPlayer()->insightaimmode);
        int aimRisingEdgeAim = aimHeld && !AIMS.aimHeldPrev;
        if (aimRisingEdgeAim && slotPlayer() && slotPlayer()->docentreupdown)
            AIMS.centreClearTicks = 2;   /* see D194 centre-spring note above */
        if (!aimHeld) {
            AIMS.centreClearTicks = 0;
            /* GEPD adopts the game's current crosshair pos every non-aim
             * frame so re-entry starts where the game left it. */
            AIMS.gepdHeldPrev = 0;
            if (slotPlayer()) {
                if (pdMouseAimEnabled()) {
                    /* D338: the accumulator is in GEPD units (±LIMIT = edge);
                     * adopt through the game's screen mapping (offset =
                     * pos*(1-damp), gunfire.c) and the aim-range scale, so aim
                     * entry starts exactly where the crosshair is drawn. */
                    double k = (1.0 - (double) slotPlayer()->guncrossdamp) / 0.99
                             * GEPD_CROSSHAIR_LIMIT / aimRangeScale();
                    AIMS.crossX = (double) slotPlayer()->crosshair_x_pos * k;
                    AIMS.crossY = (double) slotPlayer()->crosshair_y_pos * k;
                } else {
                    AIMS.crossX = (double) slotPlayer()->crosshair_x_pos / aimRangeScale();
                    AIMS.crossY = (double) slotPlayer()->crosshair_y_pos / aimRangeScale();
                }
            }
        }
        AIMS.aimHeldPrev = aimHeld;
        if (aimRisingEdgeAim && slotPlayer() && slotPlayer()->docentreupdown
            && configGetInputLog()) {
            sysLogPrintf(LOG_NOTE,
                "GE_INPUTLOG absaim centre-spring armed at aim entry; nudging to clear");
        }
        if (aimButton)
            button |= GE_CONT_R;
        if (actHeld(ks, IA_ACTION))
            button |= GE_CONT_A;
        if (wheelFwd > 0) {             /* wheel up: fresh A edge = cycle forward */
            button |= GE_CONT_A;
            wheelFwd--;
        } else if (wheelBack > 0) {     /* wheel down: A+Z together, not staggered.
             * D223 follow-up: staggering (A alone for a poll, THEN adding Z) races
             * the real game-tick rate -- if the two states land in separate ticks,
             * the "A alone" tick is itself a fresh A edge with no Z held, which
             * the game's own weaponForwardOffset formula reads as a genuine
             * cycle-FORWARD request (bondview2.c weaponForwardOffset/
             * weaponBackOffset, both control-scheme sites) *before* the
             * correcting backward tick runs -- so depending on real-time
             * poll/tick alignment (D117-class nondeterminism) a single wheel-down
             * notch could silently do a stray forward step, or forward-then-back
             * (net a skipped slot on wrap). Presenting A and Z together from the
             * very first poll means whichever single tick samples the 0->(A|Z)
             * transition sees them rising simultaneously; weaponForwardOffset
             * requires Z NOT held, so it's unambiguous -- only backward fires,
             * every time, regardless of tick timing. moveData.triggerOn (actual
             * fire) is separately gated off while A/invButtons is held, so this
             * doesn't risk an accidental shot either. */
            button |= GE_CONT_A | GE_CONT_G;
            wheelBack--;
        }
        /* D378/D393: GE's B tap calls bond_interact_object(), then reloads
         * only when no target was found (lv.c:796). E and pad A use the same
         * interaction with NO fallback; R and pad X only reload. Their
         * combined edges prevent a held input from repeatedly using a door.
         * In menus/watch/tank the pad keeps native accept/cancel buttons. */
        SDL_GameController *slot0Pad = slotPad(0);
        int padUse = slot0Pad && SDL_GameControllerGetButton(slot0Pad, SDL_CONTROLLER_BUTTON_A);
        int padReload = slot0Pad && SDL_GameControllerGetButton(slot0Pad, SDL_CONTROLLER_BUTTON_X);
        int useNow = scriptIsActive(0) ? scriptSlots[0].use : (actHeld(ks, IA_CANCEL) || padUse);
        int reloadNow = scriptIsActive(0) ? scriptSlots[0].reload : (actHeld(ks, IA_RELOAD) || padReload);
        int playable = inputCanUseGameplayActions(menuMode);
        /* D407: on the N64 the B bit also drives bondview2.c's tank
         * handlers (board when g_BondCanEnterTank, exit while
         * g_PlayerIsInTank). The D378/D393 split above routes E/pad A to a
         * dedicated interact call without presenting B, which left tank
         * board/exit dead on PC (the tank blocks movement but can never be
         * entered -- Runway, Streets). Present B only in the tank states so
         * E keeps its no-reload-fallback semantics everywhere else.
         * In the tank states the dedicated interact call is ALSO skipped:
         * on the N64 the use/reload fallback (lv.c:796) runs on the B bit
         * AFTER the board handler in the same tick, so pre-activating the
         * tank prop on the input side (propobjInteract ->
         * RUNTIMEBITFLAG_ACTIVATED, propobj.c) reorders the N64 sequence
         * and flaps g_BondCanEnterTank between input sampling and the
         * board check (user: still can't board, 2026-09-28). */
        int tankState = (g_PlayerIsInTank == 1 || g_BondCanEnterTank != 0);
        /* D407 (cont.): board-animation lockout. The game's B-tap handler
         * (bondview2.c) is a toggle: while g_PlayerIsInTank is set, a B tap
         * is the EXIT path. Boarding commits a ~45-frame (0.75 s) sit blend
         * during which g_EnterTankAudioState stays TANK_RUN_STATE_NOT_RUNNING;
         * only once it finishes does the engine SFX start. A key bounce or a
         * player re-pressing E in that silent window ("nothing is happening")
         * hits the exit a few frames later and silently cancels the whole
         * animation -- 2026-09-28 playtest: every attempt was a
         * board->exit microcycle, so no seat, no engine, and the tank camera
         * transition reverted ("tries to do something, cancels out"). An N64
         * player simply presses once and waits; give the keyboard the same
         * guarantee in the port layer: suppress B from E while the entry
         * animation is running. Once seated, E = exit again. */
        static int s_tankBoardLock = 0;
        s_tankBoardLock = (g_PlayerIsInTank == 1 &&
                           g_EnterTankAudioState == TANK_RUN_STATE_NOT_RUNNING);
        if (playable) {
            if (useNow && !s_useHeldPrev && !tankState && inputMpSplit()) {
                button |= GE_CONT_B;   /* D419: native use (+ reload fallback); no cross-thread game call in a match */
            } else if (useNow && !s_useHeldPrev && !tankState) {
                bool empty = bond_interact_object();
                if (configGetInputLog()) sysLogPrintf(LOG_NOTE, "GE_INPUTLOG dedicated use (no target=%d)", empty);
            }
        } else if ((scriptIsActive(0) ? scriptSlots[0].use : actHeld(ks, IA_CANCEL)) && !tankState) {
            /* A on the pad is still native accept outside playable stages.
             * D407: NOT in tank states -- there B must come only from the
             * locked edge path below. This mapping is level-triggered on E;
             * in-tank (playable=false) it would re-present B on any E press,
             * bypassing the board-animation lockout and re-edging the game's
             * own exit toggle (2026-09-28: unprobed board->exit microcycles). */
            button |= GE_CONT_B;
        }
        if (useNow && !s_useHeldPrev && tankState && !s_tankBoardLock)
            button |= GE_CONT_B;
        if (reloadNow && !s_reloadHeldPrev && playable && inputMpSplit()) {
            button |= GE_CONT_B;
        } else if (reloadNow && !s_reloadHeldPrev && playable) {
            attempt_reload_item_in_hand(GUNRIGHT);
            attempt_reload_item_in_hand(GUNLEFT);
            if (configGetInputLog()) sysLogPrintf(LOG_NOTE, "GE_INPUTLOG dedicated reload (no use)");
        }
        s_useHeldPrev = useNow;
        s_reloadHeldPrev = reloadNow;
        crouchNow = actHeld(ks, IA_CROUCH);
        if (actHeld(ks, IA_LEAN_L))
            button |= GE_CONT_L;
        if (actHeld(ks, IA_START))
            button |= GE_CONT_START;

        /* Mouse-look. Mode-dependent (see the tuning-constants comment):
         *   aim mode  -> push the analog stick past +/-60 for proportional
         *                yaw + pitch; emit NO C-buttons (they mean crouch here).
         *   hipfire   -> yaw on analog stick-X; pitch on digital C-up/C-down.
         * "look down" convention: mouse-down looks down by default; GE's
         * native pitch is inverted so hipfire down = C-up (GE_CONT_E) and
         * aim-mode down = +stick_y. MouseInvertY flips both. */
        if (mouseEnabled) {
            double invert = mouseInvertY ? -1.0 : 1.0;

            /* Optional exponential low-pass (mouseSmoothing = blend % of the
             * previous poll). Off (0) => edx/edy are the raw deltas. */
            double edx = mouseDX, edy = mouseDY;
            if (mouseSmoothing > 0) {
                double a = mouseSmoothing / 100.0;
                if (a > 0.90) a = 0.90;
                mouseSmDX = mouseSmDX * a + edx * (1.0 - a);
                mouseSmDY = mouseSmDY * a + edy * (1.0 - a);
                edx = mouseSmDX;
                edy = mouseSmDY;
            }
            edy *= mouseYScale / 100.0;

            double dyLook = edy * invert;   /* >0 => look down */
            if (!menuMode && (fabs(edx) > 0.01 || fabs(dyLook) > 0.01))
                s_aimDevMouse = 1;          /* D337: mouse is the active aim device */

            /* D194(b): gameplay-look-only dt normalization -- see the
             * MOUSE_DT_REF comment above. Computed unconditionally (so the
             * clock stays warm across mode switches) but only ever applied
             * below, inside the aimHeld/hipfire branches. */
            double lookDtScale = 1.0;
            {
                Uint64 now = SDL_GetPerformanceCounter();
                if (s_lastLookPollCounter != 0) {
                    double freq = (double)SDL_GetPerformanceFrequency();
                    double dtActual = (double)(now - s_lastLookPollCounter) / freq;
                    if (dtActual > 0.0005) {   /* ignore sub-ms jitter / double polls */
                        lookDtScale = MOUSE_DT_REF / dtActual;
                        if (lookDtScale < MOUSE_DT_SCALE_MIN) lookDtScale = MOUSE_DT_SCALE_MIN;
                        if (lookDtScale > MOUSE_DT_SCALE_MAX) lookDtScale = MOUSE_DT_SCALE_MAX;
                    }
                }
                s_lastLookPollCounter = now;
            }
            if (!mouseDtDecouple) lookDtScale = 1.0;

            if (configGetInputLog() && !menuMode && (aimHeld || fabs(edx) > 0.01 || fabs(dyLook) > 0.01)) {
                sysLogPrintf(LOG_NOTE, "GE_INPUTLOG lookdt scale=%.3f raw=(%.2f,%.2f)",
                             lookDtScale, edx, dyLook);
            }

            if (menuMode && !menuPointerMode) {
                /* Legacy velocity mode (Input.MenuPointerMode = 0): mouse
                 * velocity -> stick. Kept as a fallback; integrates as
                 * velocity^2 through front.c (D165). */
                double g = MENU_POINTER_GAIN * (menuPointerSpeed / 100.0);
                sx += (int)(edx * g);
                sy -= (int)(dyLook * g);   /* front-end cursor: +sy = up */
            } else if (menuMode) {
                /* Front-end menu pointer.
                 *
                 * front.c frontUpdateControlStickPosition() is the game's cursor
                 * integrator: it reads joyGetStickX/Y, applies a +/-5 deadband,
                 * clamps the stick to +/-70, then does
                 *   cursor_h_pos += (stick*0.075 +/- 0.5) * timerDelta
                 * and clamps cursor_h/v_pos into [screenleft+20 ..
                 * screenleft+screenwidth-20] x [screentop+20 ..
                 * screentop+screenheight-20]. That integrator caps the cursor at
                 * ~5.75 virtual px per poll, so feeding it a stick derived from
                 * an absolute mouse position (D165/D169 P-controller) is
                 * unavoidably laggy/floaty and desyncs from the real cursor.
                 *
                 * With click-to-lock the front end has a
                 * real free OS cursor, so we know exactly where the pointer is.
                 * Write cursor_h/v_pos DIRECTLY from the absolute mouse position
                 * whenever the mouse moved, and emit a zero stick so the game's
                 * integrator adds nothing (stick 0 -> deadband). When the mouse
                 * is idle we leave the cursor alone and let the keyboard stick
                 * (WASD/arrows, added above) drive it through the game as usual.
                 * cursor_h/v_pos are the plain front.c UI globals every menu
                 * hit-test already reads -- no logic change, just where the
                 * pointer is placed. */
                double loH = MENU_CURSOR_LO, hiH = MENU_CURSOR_HI_H;
                double loV = MENU_CURSOR_LO, hiV = MENU_CURSOR_HI_V;
                {
                    double sw = getPlayer_c_screenwidth(),  sh = getPlayer_c_screenheight();
                    double sl = getPlayer_c_screenleft(),   st = getPlayer_c_screentop();
                    if (sw > 200.0 && sw < 2000.0 && sh > 150.0 && sh < 2000.0) {
                        loH = sl + 20.0;  hiH = sl + sw - 20.0;
                        loV = st + 20.0;  hiV = st + sh - 20.0;
                    }
                }

                int haveAbs = 0;
                if (!mouseGrabbed) {
                    int mx = 0, my = 0;
                    SDL_GetMouseState(&mx, &my);
                    SDL_Window *w = SDL_GetMouseFocus();
                    int ww = 0, wh = 0;
                    if (w) SDL_GetWindowSize(w, &ww, &wh);
                    if (ww > 0 && wh > 0) {
                        haveAbs = 1;
                        if (!menuPrevActive) {
                            /* just entered a menu: adopt the current pointer as
                             * the baseline, don't yank the cursor this frame */
                            lastMenuMouseX = mx;
                            lastMenuMouseY = my;
                        }
                        /* D345: re-assert right after a screen change when the
                         * mouse was the recent navigation device (click-to-
                         * enter). A stale idle mouse (WASD/arrow navigation)
                         * keeps the cede-to-keyboard behaviour: no yank. */
                        Uint64 pcNow = SDL_GetPerformanceCounter();
                        int mouseRecent = (menuPointerLastAbsWrite != 0) &&
                            ((double)(pcNow - menuPointerLastAbsWrite) /
                             (double)SDL_GetPerformanceFrequency() * 1000.0) <
                            MENU_POINTER_REASSERT_MS;
                        if ((mx != lastMenuMouseX || my != lastMenuMouseY) ||
                            (menuPointerTransitionFrames > 0 && mouseRecent)) {
                            double fx = (double)mx / (double)ww;
                            double fy = (double)my / (double)wh;
                            /* D447: Video.AspectMode = Original draws into a
                             * letter/pillarboxed rect; map across that rect. */
                            {
                                double rx0, ry0, rx1, ry1;
                                videoGetOutputRectFrac(&rx0, &ry0, &rx1, &ry1);
                                if (rx1 > rx0 && ry1 > ry0) {
                                    fx = (fx - rx0) / (rx1 - rx0);
                                    fy = (fy - ry0) / (ry1 - ry0);
                                }
                            }
                            /* D335: under native widescreen the front end is
                             * pillarboxed to a centred 4:3 region
                             * (front.c menu_jump_constructor_handler), so map
                             * the pointer across that region, not the window. */
                            {
                                extern f32 portNativeAspect(void);
                                f32 na = portNativeAspect();
                                if (na > (4.0f / 3.0f)) {
                                    double vis = (4.0 / 3.0) / (double)na;   /* visible width fraction */
                                    fx = (fx - (1.0 - vis) * 0.5) / vis;
                                }
                            }
                            if (fx < 0.0) fx = 0.0; else if (fx > 1.0) fx = 1.0;
                            if (fy < 0.0) fy = 0.0; else if (fy > 1.0) fy = 1.0;
                            cursor_h_pos = (float)(loH + fx * (hiH - loH));
                            cursor_v_pos = (float)(loV + fy * (hiV - loV));
                            sx = 0;   /* pointer owns the cursor this poll */
                            sy = 0;
                            menuPointerLastAbsWrite = pcNow;
                        }
                        lastMenuMouseX = mx;
                        lastMenuMouseY = my;
                    }
                }

                if (configGetInputLog()) {
                    sysLogPrintf(LOG_NOTE,
                        "GE_INPUTLOG menuptr abs=%d cursor=(%.1f,%.1f) stick=(%d,%d)",
                        haveAbs, (double)cursor_h_pos, (double)cursor_v_pos, sx, sy);
                }
            } else if (slotPlayer() != NULL &&
                       (!slotPlayer()->outside_watch_menu ||
                        slotPlayer()->pause_state != 0)) {
                /* D330 (#103): in-stage watch / pause -- the mouse drives
                 * nothing. menuMode is 0 here (current_menu is still
                 * RUN_STAGE) and hipDirectCompute declines on this same gate,
                 * so the legacy stick fallbacks below used to turn mouse
                 * motion into stick deflection (and an RMB hold ran the GEPD
                 * aim writes, which GEPD itself gates on !watch): a ~15 px flick
                 * crossed the watch's +/-0x2E left/right threshold, which on
                 * a selected option means "pick OFF/ON" (options.c
                 * game_option_toggle_input) and otherwise flips watch pages,
                 * while mouse Y scrolled the rows. Auto-aim / look-ahead
                 * "wouldn't stay on". Keyboard and pad still drive the watch
                 * exactly as before. */
            } else if (aimHeld) {
                /* D194 GEPD-mirror aim: direct crosshair/camera writes, no
                 * look stick (see aimGepdCompute). Keyboard turn (sx/sy set
                 * above) still works. Otherwise fall through to the legacy
                 * velocity stick below.
                 * D407 follow-up: in-tank the game aims the turret from raw
                 * stick deflection ONLY (bondview2.c HONEY/SOLITARE branch:
                 * |stick_x|/|stick_y| > 60 -> aimTurn/speedVerta), so the
                 * direct-write models must be bypassed there and the legacy
                 * velocity stick emitted instead. */
                int tankAimStick = (g_PlayerIsInTank == 1);
                if (!tankAimStick && aimModeGet() == AIMMODE_CENTRED) {
                    /* D333: FPS-style centred aim -- the camera takes the
                     * mouse (same path as hipfire, which also declines on
                     * the watch/pause/cutscene gates) and no stick is
                     * emitted, so the crosshair settles at centre. */
                    hipDirectCompute(edx * lookDtScale, dyLook * lookDtScale);
                } else if (!tankAimStick && pdMouseAimEnabled()) {
                    /* PD model: accumulate only. The game's own integrator is
                     * driven by the port-supplied turn through the
                     * sub_GAME_7F067FBC hook (see Input.PdMouseAim above). */
                    aimGepdAccumulate(edx, dyLook);
                    /* Edge-scroll belongs to the aim position, not to the
                     * crosshair model -- without this the camera stops
                     * following the crosshair at the screen edge. */
                    aimGepdEdgeScroll();
                } else if (tankAimStick || !aimGepdCompute(edx * lookDtScale, dyLook * lookDtScale)) {
                double aimEdx = edx * lookDtScale, aimDyLook = dyLook * lookDtScale;
                double aimSens = (mouseAimSpeed / 100.0) * (mouseSensitivity / 100.0);
                if (tankAimStick) aimSens *= tankAimScale / 100.0;   /* D407 */
                double gamma = aimCurveGamma / 100.0;
                double normX = fabs(aimEdx) * aimSens / AIM_FULL_SPEED_PX;
                double normY = fabs(aimDyLook) * aimSens / AIM_FULL_SPEED_PX;
                if (normX > 1.0) normX = 1.0;
                if (normY > 1.0) normY = 1.0;
                int ceilStick = 60 + aimBand;
                if (ceilStick > AIM_STICK_GAME_MAX) ceilStick = AIM_STICK_GAME_MAX;
                if (fabs(aimEdx) >= AIM_MOVE_THRESH) {
                    int m = AIM_STICK_MIN + (int)(pow(normX, gamma) * (AIM_STICK_GAME_MAX - AIM_STICK_MIN));
                    if (m > ceilStick) m = ceilStick;
                    sx += (aimEdx > 0) ? m : -m;
                }
                if (fabs(aimDyLook) >= AIM_MOVE_THRESH) {
                    int m = AIM_STICK_MIN + (int)(pow(normY, gamma) * (AIM_STICK_GAME_MAX - AIM_STICK_MIN));
                    if (m > ceilStick) m = ceilStick;
                    sy += (aimDyLook > 0) ? m : -m;   /* +stick_y = look down */
                }
                } /* D194: end legacy velocity-aim fallback */

                /* D194 centre-spring clear: a minimal pitch stick for a couple
                 * of ticks -- enough for bondview2's manual-input rule to clear
                 * docentreupdown, small enough (~0.1-0.3 deg) not to read as a
                 * jerk. Only when we are not already pitching this poll. */
                if (AIMS.centreClearTicks > 0 && slotPlayer() &&
                    sy >= -60 && sy <= 60) {
                    sy = (slotPlayer()->speedverta >= 0.0f) ? -61 : 61;
                    AIMS.centreClearTicks--;
                }
            } else {
                double hipEdx = edx * lookDtScale, hipDyLook = dyLook * lookDtScale;
                double hipSens = (mouseTurnSpeed / 100.0) * (mouseSensitivity / 100.0);
                /* WI-1: direct camera write, same linear px->degree model as
                 * aim mode (GEPD's hipfire branch) -- bypasses the N64 stick's
                 * quadratic natural-turn curve entirely (D238/#89: that curve
                 * saturated at ~13 px/poll, making slow motion "almost
                 * unrecognised" and fast motion bang-bang). Falls through to
                 * the legacy stick path below when it declines (disabled, no
                 * player, or a safety gate is closed). */
                /* D426 (#116): in a tank the hull turns from stick X (analogTurn) and stick Y
                 * is drive, so the direct camera write is ignored/overwritten -- emit
                 * mouse X as stick turn (scaled by Input.TankAimScale) and drop Y. */
                int inTankHip = (g_PlayerIsInTank == 1);
                if (!inTankHip && mouseDirectLook && hipDirectCompute(hipEdx, hipDyLook)) {
                    swayFeedTheta(s_hipThetaDegOut);   /* D428: hipfire gun turn-lag */
                    /* Pitch handled inside hipDirectCompute too; nothing left
                     * to do for yaw/pitch this poll. Digital pitch-pulse
                     * (naturalPitchMode==0) still applies below only in the
                     * legacy path, so skip both branches here. */
                } else {
                sx += (int)(hipEdx * hipSens * MOUSE_TURN_GAIN * (inTankHip ? tankAimScale / 100.0 : 1.0));
                if (inTankHip) {
                    /* no pitch/drive from the mouse in a tank */
                } else if (naturalPitchMode) {
                    /* D194/D238: SOLITARE gives hipfire pitch the same
                     * continuous analog stick treatment as yaw -- same
                     * formula as the sx line above, so X and Y are, by
                     * construction, symmetric. Forward/back moved to
                     * digital C-up/C-down above, freeing the stick's Y axis
                     * for this. */
                    sy += (int)(hipDyLook * hipSens * MOUSE_TURN_GAIN);
                } else {
                    /* D166 (legacy): hipfire pitch as C-button pulses whose
                     * frequency scales with mouse-Y speed -- fast mouse =
                     * solid hold, slow = sparse taps. Kept as the
                     * Input.NaturalPitch=0 escape hatch. */
                    double sp = fabs(hipDyLook);
                    if (sp >= MOUSE_PITCH_THRESH) {
                        double duty = sp * (hipfirePitchSpeed / 100.0) * (mouseSensitivity / 100.0) / HIP_PITCH_FULL;
                        if (duty > 1.0) duty = 1.0;
                        hipPitchPhase += duty;
                        if (hipPitchPhase >= 1.0) {
                            hipPitchPhase -= 1.0;
                            button |= (hipDyLook > 0) ? GE_CONT_E : GE_CONT_D; /* E=C-up=look down */
                        }
                    } else {
                        hipPitchPhase = 0.0;
                    }
                }
                } /* end mouseDirectLook fallback (WI-1) */
            }
        }

        if (menuMode && mouseEnabled) {
            /* Clicks are select / back in the front end, not fire / aim. */
            button &= ~(GE_CONT_G | GE_CONT_R);
            if (mb & SDL_BUTTON(SDL_BUTTON_LEFT))  button |= GE_CONT_A;
            if (mb & SDL_BUTTON(SDL_BUTTON_RIGHT)) button |= GE_CONT_B;
        }
        /* D384: the PC settings editor MUST NOT navigate using live game
         * binds. Otherwise capturing A/D/C instantly turns that held key
         * into C-left/right or stick input, cycling the selected bind slot
         * and chasing the menu cursor (user repro, bind log). Physical arrow
         * keys and Enter/Escape are fixed menu controls; mouse/pad still
         * work. Scripted QA input remains the sole source when active. */
        if (current_menu == MENU_PC_OPTIONS && !scriptIsActive(0))
        {
            pcOptionsKeyboardPad(ks, mb, optionsBindingInputBlocked(), &button, &sx, &sy);
            /* D407(b): on this screen the mouse wheel scrolls the row list
             * -- wheel up = step UP, wheel down = step DOWN (same sign
             * convention as the W/S keys above: +STICK_MAX = up). The
             * original mapping had them swapped (2026-09-28 user report).
             * Front-end menus otherwise leave the wheel queue unconsumed in
             * menu mode -- clear it here so a scroll can't leak into the
             * next stage's weapon cycle (D223). */
            if (!optionsBindingInputBlocked())
            {
                if (wheelBack > 0)      { sy = STICK_MAX;  wheelBack = 0; }   /* wheel up   */
                else if (wheelFwd > 0)  { sy = -STICK_MAX; wheelFwd = 0; }    /* wheel down */
            }
        }
        menuPrevActive = menuMode;
        s_menuPointerLive = (menuMode && mouseEnabled && !mouseGrabbed) ? 1 : 0;

        mouseDX = 0.0;
        mouseDY = 0.0;
    }

    /* ---- gamepad ---- */
    SDL_GameController *pad = slotPad(idx);
    if (pad) {
        int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        int ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        int rx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX);
        int ry = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY);

        /* D282: front-end menus (main menu, file select, mission-select map)
         * share this same stick_x/stick_y channel with in-game look/movement.
         * naturalPitchMode (SOLITARE, default-on) routes that channel to the
         * RIGHT stick for continuous look -- fine in a level, unintuitive in
         * a menu, where the left stick is the expected primary-navigation
         * input on a modern pad/Deck (matches the F10 overlay's own
         * left-stick nav, optionsoverlay.c). Outside a running stage, always
         * source stick_x/stick_y from the left stick, regardless of
         * naturalPitchMode -- a menu-context input remap only, no game-logic
         * change and no effect on in-level control feel. */
        int padMenuMode = (current_menu != GE_MENU_RUN_STAGE &&
                           current_menu != GE_MENU_INVALID);

        if (padMenuMode) {
            int px = scaleAxis(lx, padDeadzoneL);
            int py = -scaleAxis(ly, padDeadzoneL);       /* SDL up = negative -> N64 up = positive */
            if (px) sx = px;
            if (py) sy = py;
        } else if (naturalPitchMode) {
            /* D194/D238: SOLITARE swaps stick roles -- left stick becomes
             * digital-step movement (same analog-for-movement tradeoff as
             * the keyboard remap above), right stick becomes continuous
             * natural look (replacing its old digital-C-button emulation),
             * matching how the mouse now drives look continuously too. */
            if (ly < -RSTICK_THRESHOLD) button |= GE_CONT_E;   /* stick up = forward */
            if (ly >  RSTICK_THRESHOLD) button |= GE_CONT_D;   /* stick down = back  */
            if (lx < -RSTICK_THRESHOLD) button |= GE_CONT_C;   /* strafe left        */
            if (lx >  RSTICK_THRESHOLD) button |= GE_CONT_F;   /* strafe right       */

            /* Wave A (v0.5.0): right-stick look -- per-stick deadzone (right),
             * per-axis sensitivity (% of native, 100 = unchanged, re-clamped to
             * STICK_MAX), and an optional low-pass. All feel-only, identity at
             * their defaults (sens 100%, smoothing off). */
            int rxs = scaleAxis(rx, padDeadzoneR);
            int rys = -scaleAxis(ry, padDeadzoneR);   /* SDL up = negative -> N64 up = positive */
            if (padLookSensX != 100 && rxs) rxs = rxs * padLookSensX / 100;
            if (padLookSensY != 100 && rys) rys = rys * padLookSensY / 100;
            if (rxs >  STICK_MAX) rxs =  STICK_MAX; else if (rxs < -STICK_MAX) rxs = -STICK_MAX;
            if (rys >  STICK_MAX) rys =  STICK_MAX; else if (rys < -STICK_MAX) rys = -STICK_MAX;
            if (idx == 0 && (rxs || rys)) s_aimDevMouse = 0;   /* D337: pad aim active */
            if (padLookSmooth > 0) {
                double a = padLookSmooth * 0.09;   /* 10 -> 0.9 (heaviest); 0 = off */
                padSmSX[idx] = padSmSX[idx] * a + (double)rxs * (1.0 - a);
                padSmSY[idx] = padSmSY[idx] * a + (double)rys * (1.0 - a);
                rxs = (int)lround(padSmSX[idx]);
                rys = (int)lround(padSmSY[idx]);
            }
            if (padLookInvertY) rys = -rys;
            if (aimModeGet() == AIMMODE_CENTRED && padDirectCompute(rxs, rys)) {
                /* D404 (resolved by implementation, 2026-09-28): pad CENTRED
                 * aim -- the stick drives the camera directly (crosshair
                 * pinned at centre), the pad-side twin of the mouse CENTRED
                 * branch above, so the Aim-style toggle is no longer inert
                 * on a pad. No look stick is emitted this poll; when
                 * padDirectCompute declines (dead/watch/pause/cutscene) the
                 * legacy stick emission below runs, as the mouse branch
                 * falls back to its legacy path. AIMMODE_N64 keeps today's
                 * behaviour: the deflection feeds the game's crosshair
                 * integrator (travel + edge scroll + spring-back -- the
                 * original controller feel). */
            } else {
                if (rxs) sx = rxs;
                if (rys) sy = rys;
            }
        } else {
            int px = scaleAxis(lx, padDeadzoneL);
            int py = -scaleAxis(ly, padDeadzoneL);       /* SDL up = negative -> N64 up = positive */
            if (px) sx = px;
            if (py) sy = py;
            if (idx == 0 && (px || py)) s_aimDevMouse = 0;   /* D337: pad aim active */

            if (padLookInvertY) ry = -ry;
            if (rx >  RSTICK_THRESHOLD) button |= GE_CONT_F;
            if (rx < -RSTICK_THRESHOLD) button |= GE_CONT_C;
            if (ry >  RSTICK_THRESHOLD) button |= GE_CONT_D;
            if (ry < -RSTICK_THRESHOLD) button |= GE_CONT_E;
        }

        int trigPt = padTriggerPct * 327;   /* % of the 0..32767 trigger travel */
        if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > trigPt)
            button |= GE_CONT_G;
        if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > trigPt)
            button |= GE_CONT_R;
        /* Wave A: southpaw swaps the fire (G) and grenade (R) trigger actions
         * (right<->left trigger), after the raw edges above are captured. */
        if (padSouthpaw) {
            int t = button;
            button = (t & ~(GE_CONT_G | GE_CONT_R))
                   | ((t & GE_CONT_G) ? GE_CONT_R : 0)
                   | ((t & GE_CONT_R) ? GE_CONT_G : 0);
        }

        /* Xbox 1.1 Jinx-style gameplay: A=use, X=reload (handled by the
         * keyboard action gate), B=gadgets, Y=weapons, stick clicks=crouch,
         * LB=aim. RB's remaster HD toggle is unavailable in the N64 port.
         * Menus/watch/tank retain native accept/cancel mappings. */
        int padA = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A);
        int padX = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X);
        int padB = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B);
        int padY = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y);
        int facePlayable = (idx == 0 || inputMpSplit()) && inputCanUseGameplayActions(padMenuMode);
        if (facePlayable) {
            if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSTICK) ||
                SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSTICK))
                crouchNow = 1;
            if (idx > 0 || inputMpPadsOwnP1()) {
                /* Slots 1+ (and a pad-owned P1) have no keyboard block: pad A / X = the native B
                 * button on the press edge (use, reload when nothing to use). */
                if ((padA && !padAPrev[idx]) || (padX && !padXPrev[idx]))
                    button |= GE_CONT_B;
            }
            if (padB && !padBPrev[idx]) inputCycleGadget();
            if (padY && !padYPrev[idx]) button |= GE_CONT_A;
        } else {
            if (padA || padX) button |= GE_CONT_A;
            if (padB || padY) button |= GE_CONT_B;
        }
        padBPrev[idx] = padB;
        padYPrev[idx] = padY;
        padAPrev[idx] = padA;
        padXPrev[idx] = padX;
        if (facePlayable && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))
            button |= GE_CONT_R;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START))
            button |= GE_CONT_START;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP))
            button |= GE_CONT_UP;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
            button |= GE_CONT_DOWN;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))
            button |= GE_CONT_LEFT;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
            button |= GE_CONT_RIGHT;

        /* Select (BACK) opens the F10 options overlay -- the gamepad
         * equivalent of the F10 key for controller-only machines (Steam
         * Deck). The game never reads BACK, so nothing is withheld. */
        {
            int selNow = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK);
            /* D343: not on the PC options screen (one options UI at a time). */
            if (selNow && !padSelectPrev && !frontOptionsBlocksOverlay()) optionsOverlayToggle();
            padSelectPrev = selNow;
        }
    }

    if (idx == 0 && scriptIsActive(0)) crouchNow = scriptSlots[0].crouch;

    /* The GEPD preset's crouch key is independent of aim. Do not turn it
     * into C-down: outside aim C-down moves backwards in 1.2, and inside
     * aim it also zooms on weapons that disable crouch. This is a PC input
     * adapter writing the same stance field the native game adjusts, not
     * a change to the N64 game logic. */
    if (idx == 0 || inputMpSplit()) {
        int stage = current_menu == GE_MENU_RUN_STAGE || current_menu == GE_MENU_INVALID;
        if (!stage) s_crouchLatch = 0;
        if (crouchMode == 1) {
            if (stage && crouchNow && !s_crouchHeldPrev) s_crouchLatch ^= 1;
            s_crouchHeldPrev = crouchNow;
            crouchNow = s_crouchLatch;
        }
        struct player *p = slotPlayer();
        int canCrouch = stage && p && !p->bonddead && p->outside_watch_menu &&
                        !p->pause_state && !p->mpmenuon && !g_PlayerIsInTank &&
                        !lvlGetControlsLockedFlag() &&
                        !gameScriptedCameraActive();
        if (canCrouch && (crouchNow || s_crouchApplied)) {   /* weapon lookup only when it can matter */
            canCrouch = !bondwalkItemCheckBitflags(p->hands[GUNRIGHT].weaponnum,
                                                   WEAPONSTATBITFLAG_DISABLE_CROUCH);
        }
        if (canCrouch && crouchNow) {
            /* In aim mode the native crouchUp branch runs every tick without
             * C-down, undoing the port stance by one step. Feed the native
             * axis ONLY while aiming; hipfire still crouches by stance alone. */
            if (p->insightaimmode || (button & (GE_CONT_R | GE_CONT_L)))
                button |= GE_CONT_D;
            p->crouchpos = CROUCH_SQUAT;
            if (!s_crouchApplied && configGetInputLog())
                sysLogPrintf(LOG_NOTE, "GE_INPUTLOG free crouch applied (aim=%d)", p->insightaimmode);
            s_crouchApplied = 1;
            s_crouchPlayer = p;
        } else if (!(canCrouch && (button & GE_CONT_D) && p->insightaimmode)) {
            /* If native C-down is still held in aim, let the game retain its
             * own crouch on this tick rather than force a stand. */
            if (s_crouchApplied && configGetInputLog())
                sysLogPrintf(LOG_NOTE, "GE_INPUTLOG free crouch released");
            inputDropCrouch();
        } else {
            s_crouchApplied = 0;
            s_crouchPlayer = NULL;
        }
    }

    if (idx == 0 && scriptIsActive(0)) {
        /* D263: apply BEFORE the stick is written out -- scripted stick
         * tokens (SUP/SDOWN/SLEFT/SRIGHT) used to be discarded. */
        button = scriptApply(0, button);
        sx = scriptSlots[0].curSX;
        sy = scriptSlots[0].curSY;
    } else if (idx > 0 && idx <= 3 && scriptIsActive(idx)) {
        /* Per-slot harness (GE_INPUTSCRIPT_P1..P3): same sole-source
         * semantics for a split-screen test seat. Virtual seats behind
         * GE_MPVIRT reach here with button/sx/sy still at their idle zeros
         * (the gamepad block above is guarded by `if (pad)`), so an
         * unscripted seat stays idle exactly as before. */
        button = scriptApply(idx, button);
        sx = scriptSlots[idx].curSX;
        sy = scriptSlots[idx].curSY;
    }

    if (sx > STICK_MAX)  sx = STICK_MAX;
    if (sx < -STICK_MAX) sx = -STICK_MAX;
    if (sy > STICK_MAX)  sy = STICK_MAX;
    if (sy < -STICK_MAX) sy = -STICK_MAX;

    if (stick_x) *stick_x = (signed char)sx;
    if (stick_y) *stick_y = (signed char)sy;

    /* D337: GE_AIMLOG=1 -- per-poll aim-model trace (the game's own crosshair
     * state), for measuring jitter / range / edge-scroll headlessly. */
    if (idx == 0 && slotPlayer() != NULL && GE_ENVFLAG("GE_AIMLOG") &&
        (current_menu == GE_MENU_RUN_STAGE || current_menu == GE_MENU_INVALID)) {
        sysLogPrintf(LOG_NOTE, "AIMLOG f=%ld aim=%d dev=%d drawx=%.3f cx=%.5f cy=%.5f theta=%.4f verta=%.4f acc=%.4f",
                     scriptSlots[0].frame, (int)slotPlayer()->insightaimmode, s_aimDevMouse,
                     (double)slotPlayer()->crosshair_angle.f[0],
                     (double)slotPlayer()->crosshair_x_pos, (double)slotPlayer()->crosshair_y_pos,
                     (double)slotPlayer()->vv_theta, (double)slotPlayer()->vv_verta, AIMS.crossX);
    }
    if (configGetInputLog() && (button || sx || sy)) {
        sysLogPrintf(LOG_NOTE, "GE_INPUTLOG cont%d: btn=%04x stick=(%d,%d)",
                     idx, button, sx, sy);
    }
    return button;
}

int inputMenuPointerLive(void)
{
    /* D345(b): true while the 1:1 menu pointer owns cursor_h/v_pos -- in a
     * menu, the abs pointer is available, and the mouse was used recently.
     * Port screens that teleport the crosshair (frontoptions.c cursorToItem)
     * skip their snap when this is set: the cursor is already under the OS
     * pointer. Keyboard/D-pad users (stale mouse) still get the snap. */
    if (!s_menuPointerLive || menuPointerLastAbsWrite == 0) return 0;
    Uint64 now = SDL_GetPerformanceCounter();
    return ((double)(now - menuPointerLastAbsWrite) /
            (double)SDL_GetPerformanceFrequency() * 1000.0) <
           MENU_POINTER_REASSERT_MS;
}

static void applyGrab(int want)
{
    want = want && mouseEnabled;
    if (want == mouseGrabbed) {
        return;
    }
    mouseGrabbed = want;
    mouseRequestRelative(want);   /* D287: applied on the host thread */
    mouseDX = mouseDY = 0.0;
}

/* Reconcile the SDL grab state with what the current mode wants. Called once
 * per controller-0 poll (menuMode known there) and from the notify hooks. */
static void reconcileGrab(int menuMode)
{
    int want = captureArmed && windowFocused && !menuMode; /* click-to-lock */
    if (s_absAimSuspend) {
        want = 0;   /* D194: an aim hold wants the free cursor for absolute aim */
    }
    applyGrab(want);
}

/* Hide the OS cursor while the game window is focused (normal PC-game
 * behaviour): in a stage the mouse drives the look axis, and in menus GE draws
 * its own crosshair that now tracks the pointer 1:1 -- a visible OS arrow on
 * top is just clutter. Show it again when focus is lost so the desktop behaves
 * normally. Relative-mouse mode hides the cursor too, but only while grabbed;
 * this covers the free-but-focused states (menus, pre-click stage). */
static void applyCursorVisibility(void)
{
    /* D194: while an aim hold has the cursor out for absolute aim, show it --
     * the user is pointing with it and needs to see where. */
    int hide = windowFocused && mouseEnabled && !s_absAimSuspend;
    mouseRequestCursor(!hide);    /* D287: applied on the host thread */
}

/* video.c focus events. Records focus and lets reconcileGrab() decide. */
void inputSetMouseGrab(int on)
{
    windowFocused = on ? 1 : 0;
    reconcileGrab(0);   /* menuMode re-checked on the next poll anyway */
    applyCursorVisibility();
}

/* A mouse click landed in the game window (video.c): arm + grab. */
void inputNotifyClick(void)
{
    if (mouseEnabled && windowFocused) {
        captureArmed = 1;
        reconcileGrab(0);
    }
}

/* ESC pressed (video.c): release the cursor and report 1 so the caller can
 * swallow the key; reports 0 when there is nothing to release. */
int inputReleaseCapture(void)
{
    if (mouseGrabbed) {
        captureArmed = 0;
        applyGrab(0);
        return 1;
    }
    return 0;
}

/* F10 options overlay: while it owns controller 0 the inputComputePad poll
 * early-returns before reconcileGrab()/applyCursorVisibility(), so whatever
 * grab state was live when F10 was pressed (relative mode + hidden cursor in a
 * stage) would persist and the mouse UI would be unusable. Force the cursor
 * free + visible every poll; the normal reconcile resumes once the overlay
 * closes and the early-return no longer fires. */
int inputPadButton(int idx, SDL_GameControllerButton b)
{
    if (idx < 0 || idx >= MAX_PADS || !pads[idx]) return 0;
    return SDL_GameControllerGetButton(pads[idx], b);
}

short inputPadAxis(int idx, SDL_GameControllerAxis a)
{
    if (idx < 0 || idx >= MAX_PADS || !pads[idx]) return 0;
    return SDL_GameControllerGetAxis(pads[idx], a);
}

void inputSuspendForOverlay(void)
{
    if (mouseGrabbed) {
        mouseGrabbed = 0;
        mouseRequestRelative(0);
        mouseDX = mouseDY = 0.0;
    }
    mouseRequestCursor(1);
}

void inputPostWheel(int notches)
{
    /* D223: keep the direction. v0.2.1: swapped per user request -- up now
     * cycles to the PREVIOUS weapon, down to the NEXT one (PC convention
     * "scroll down = advance list"). A burst of same-direction notches just
     * re-arms the same pulse; a direction change mid-sequence restarts it
     * (last wins). */
    if (notches > 0)      { wheelBack = 2;              wheelFwd = 0; }
    else if (notches < 0) { wheelFwd = WHEEL_FWD_POLLS; wheelBack = 0; }
}

/* D450: rescan requested by inputRequestRescan() from either SDL event pump
 * on SDL_CONTROLLERDEVICEADDED/REMOVED and performed at the top of
 * inputUpdate(), on the scheduler thread, before pads[] is read. Re-syncs
 * the pad seats (inputOpenPads); `connectedMask` bit 0 (keyboard/mouse) is
 * always kept. The game re-reads the mask through joyCheckStatus (every 120
 * polls and on any errno edge, src/joy.c), so a newly seated pad becomes its
 * own controller channel within ~2 s. */
void inputRescanPads(void)
{
    /* DNEW-mp-1: still-attached pads keep their open handle and seat;
     * inputOpenPads closes only detached ones and seats new ones. */
    for (int i = 0; i < MAX_PADS; ++i) {
        padSmSX[i] = 0.0;     /* Wave A: clear the look-smoothing EMA so a stick
        padSmSY[i] = 0.0;     * snap-back after re-plug can't lag from a stale value */
    }
    inputOpenPads();
    sysLogPrintf(LOG_NOTE, "input: rescanned pads (mask=0x%x, %d controller(s))",
                 connectedMask, numControllers);
}

/* ------------------------------------------------------------------------
 * D401: Rumble Pak -> real gamepad haptics.
 *
 * The game's Rumble-Pak state machine (src/joy.c joyRumblePakInit/Tick;
 * events from gunfire.c / bondview2.c / lv.c / sched.c) drives the
 * osMotor* shims in port/src/libultra.c, which route here. `strength`
 * is 0..1 and `time` is seconds; a 0-strength / 0-duration call stops
 * an active rumble. The global Input.RumbleScale config value (0 = fully
 * silent) scales the strength for every pad. idx is the N64 controller
 * channel 0..3 (MAXCONTROLLERS may be 6 on JPN; out-of-range is a no-op).
 * ------------------------------------------------------------------------ */
int inputRumbleSupported(int idx)
{
    if (idx < 0 || idx >= MAX_PADS) return 0;
    if (inputMpSplit() && inputMpKbmP1()) return idx >= 1 ? padRumbleOn[idx - 1] : 0;
    return padRumbleOn[idx];
}

void inputRumble(int idx, f32 strength, f32 time)
{
    if (idx < 0 || idx >= MAX_PADS) return;
    SDL_GameController *rpad = slotPad(idx);
    const int rIdx = (inputMpSplit() && inputMpKbmP1()) ? idx - 1 : idx;
    if (!rpad) return;
    if (gRumbleScale <= 0.f) return;
    if (padRumbleOn[rIdx]) {
        strength *= gRumbleScale;
        if (strength <= 0.f) {
            strength = 0.f;
            time = 0.f;
        } else {
            strength *= 65535.f;
            time *= 1000.f;
        }
        SDL_GameControllerRumble(rpad, (Uint16)strength, (Uint16)strength, (Uint32)time);
    }
}

f32 inputRumbleGetScale(void)
{
    return gRumbleScale;
}

void inputRumbleSetScale(f32 v)
{
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    gRumbleScale = v;
}

int inputConnectedMask(void)
{
    return connectedMask;
}

int inputGetNumControllers(void)
{
    return numControllers;
}

/* D194 absolute (cursor-anchored) aim mode -- GEPD "mouse injector" style.
 *
 * Instead of synthesizing a velocity stick from this poll's mouse delta
 * (which can only ever be one of ten discrete (stick-60)/10 speeds, with a
 * hard 10%-speed floor on the smallest nonzero nudge), drive the game's own
 * aim-speed plant as a closed position loop: the cursor's screen position IS
 * the desired aim direction. On RMB press / every mouse move we record the
 * view angles that would put the point under the cursor at screen center;
 * each poll we emit a stick proportional to the remaining angular error, so
 * the crosshair slews to the cursor and HOLDS there -- stationary cursor =
 * stationary aim (even while walking), no floor jump, no release snap.
 *
 * Returns 1 if it handled this poll (callers must use outSx/outSy); 0 means
 * fall back to the legacy velocity stick (grabbed mouse, menus, gamepad,
 * degenerate geometry).
 */
/* D194 GEPD-mirror aim -- direct crosshair/camera writes.
 *
 * Mirrors MouseInjectorPlugin (games/goldeneye.c) in model:
 *   - mouse motion moves a crosshair POSITION accumulator (not the view),
 *     clamped to ±GEPD_CROSSHAIR_LIMIT (the screen edge);
 *   - the accumulator is written straight into crosshair_x/y_pos and the
 *     gun/arm pose (gun_azimuth_angle/turning) via GEPD's formulas;
 *   - the view only scrolls when the crosshair passes 72% toward the edge:
 *     cam += (ratio-0.72)*475*timestep*(fov/basefov);
 *   - no look stick is emitted while active. bondview2's damped crosshair
 *     auto-centre (caclulate_gun_crosshair_position_rotation) keeps RUNNING
 *     -- exactly like GEPD, which patches nothing there: the per-frame
 *     overwrite simply wins over the damping, and the game derives
 *     crosshair_angle (HUD crosshair + aim ray) and field_FFC (arm/gun
 *     screen offset in gunUpdateAndFire) from our written values. On aim
 *     release we stop writing and the game's damping eases everything back
 *     to centre itself.
 *
 * Sensitivity: Input.GepdSens (GEPD SENSITIVITY setting; default 20 ~= 1:1
 * window-px per crosshair-screen-px at full zoom).
 *
 * Returns 1 if it handled this poll (callers must NOT emit a look stick);
 * 0 means fall back to the legacy velocity stick.
 */
/* Accumulate this poll's mouse delta into the aim position. Shared by both
 * aim models: GEPD pushes the result into crosshair_x/y_pos itself, PD feeds
 * it to the game's integrator as the turn (see Input.PdMouseAim). Returns 0
 * if aim is not usable this poll. */
static int aimGepdAccumulate(double dxPx, double dyLook)
{
    struct player *p = slotPlayer();

    /* Needs the grabbed-cursor relative deltas (capture mode, locked in a
     * stage) and a live player. dxPx/dyLook are this poll's px. */
    if (!aimAbsolute || !mouseGrabbed || p == NULL)
        return 0;

    /* On entry adopt the game's current position (GEPD adopts every
     * non-aim frame; input.c does that in the !aimHeld branch). */
    if (!AIMS.gepdHeldPrev) {
        AIMS.crossX = (double) p->crosshair_x_pos;
        AIMS.crossY = (double) p->crosshair_y_pos;
    }
    AIMS.gepdHeldPrev = 1;

    /* Crosshair position: GEPD crosshairpos += delta/10 * (SENS/292). */
    /* D194/D238: master sensitivity scales aim mode too, so MouseSensitivity
     * moves BOTH hipfire and aim together ("in line"); GepdSens sets the
     * aim-mode offset from that shared baseline. At master=100 this is
     * exactly the M-123-calibrated feel. */
    double sens = (double) gepdSens / 2920.0 * (mouseSensitivity / 100.0);
    AIMS.crossX += dxPx * sens;
    AIMS.crossY += dyLook * sens;      /* +dyLook = look down = crosshair down */
    if (AIMS.crossX >  GEPD_CROSSHAIR_LIMIT) AIMS.crossX =  GEPD_CROSSHAIR_LIMIT;
    if (AIMS.crossX < -GEPD_CROSSHAIR_LIMIT) AIMS.crossX = -GEPD_CROSSHAIR_LIMIT;
    if (AIMS.crossY >  GEPD_CROSSHAIR_LIMIT) AIMS.crossY =  GEPD_CROSSHAIR_LIMIT;
    if (AIMS.crossY < -GEPD_CROSSHAIR_LIMIT) AIMS.crossY = -GEPD_CROSSHAIR_LIMIT;
    return 1;
}

/* Camera edge-scroll: past GEPD_EDGE_THRESHOLD (72%) of the way to the
 * crosshair limit, scroll the view proportionally to the overshoot, scaled by
 * zoom like GEPD's (fov/basefov). This belongs to the aim POSITION, not to
 * whichever model owns the crosshair, so both call it -- PD's model needs it
 * too, or dragging the crosshair to the edge no longer moves the camera. */
static void aimGepdEdgeScroll(void)
{
    struct player *p = slotPlayer();
    f32 fov = slotFovY();

    double rX = AIMS.crossX / GEPD_CROSSHAIR_LIMIT;
    double rY = AIMS.crossY / GEPD_CROSSHAIR_LIMIT;
    double aimx = 0.0, aimy = 0.0;
    const double th = aimEdgeThreshold();   /* D338 */
    if (rX >  th) aimx = (rX - th) * GEPD_SCROLL_SPEED / 60.0;
    else if (rX < -th) aimx = (rX + th) * GEPD_SCROLL_SPEED / 60.0;
    if (rY >  th) aimy = (rY - th) * GEPD_SCROLL_SPEED / 60.0;
    else if (rY < -th) aimy = (rY + th) * GEPD_SCROLL_SPEED / 60.0;

    f32 scale = (fov > 0.0f) ? fov / GEPD_BASE_FOV : 1.0f;
    if (aimx != 0.0) {
        /* GEPD does a bare `camx += ...` -- no [0,360) wrap. The game never
         * wraps vv_theta in on-foot play either (bondviewApplyVertaTheta only
         * sin/cos's it; the tank code is the sole wraper), so keep the
         * accumulator unbounded. Wrapping turned a small negative edge-scroll
         * step into a one-tick +360 spike in the raw value (D194 playtest log,
         * line 534: cam 0.5 -> 359.7). */
        p->vv_theta += (f32) aimx * scale;
    }
    if (aimy != 0.0) {
        p->vv_verta -= (f32) aimy * scale;   /* crosshair low -> look down */
        if (p->vv_verta >  90.0f) p->vv_verta =  90.0f;
        if (p->vv_verta < -90.0f) p->vv_verta = -90.0f;
    }
}

static int aimGepdCompute(double dxPx, double dyLook)
{
    struct player *p = slotPlayer();

    if (!aimGepdAccumulate(dxPx, dyLook))
        return 0;

    f32 fov = slotFovY();
    f32 fovratio = (fov > 0.0f) ? fov / GEPD_BASE_FOV : 1.0f;

    /* Pre-overwrite residue: what last tick's damped update left behind
     * (crosshair_pos = pos*damp + turn, gunfire.c caclulate_gun_crosshair_...
     * runs AFTER our write each tick). The gap between this and our write
     * below is the game-side turn term (autoaimx/y or speedtheta*0.3) --
     * logged for the D194 spazz diagnosis. */
    double resX = (double) p->crosshair_x_pos;
    double resY = (double) p->crosshair_y_pos;

    /* Crosshair + gun/arm pose (GEPD formulas, RATIOFACTOR=1 for our 4:3
     * viewport; failsafe weapon offsets 0.15/0 as in goldeneye.c). */
    p->crosshair_x_pos = (f32) (AIMS.crossX * aimRangeScale());   /* D338 */
    p->crosshair_y_pos = (f32) (AIMS.crossY * aimRangeScale());
    p->gun_azimuth_angle   = (f32) (AIMS.crossX * (1.11f + 0.15f * 1.5f) + fovratio - 1.0f);
    p->gun_azimuth_turning = (f32) (AIMS.crossY * 1.11f + fovratio - 1.0f);

    aimGepdEdgeScroll();

    if (configGetInputLog()) {
        /* ct = g_ClockTimer: game ticks batched into this poll. If the
         * displayed crosshair shrinks by damp^ct with ct varying tick to
         * tick (frame-pacing catch-up), game= will show fluctuating shrink
         * at d=(0,0) -- the multi-tick spazz hypothesis. */
        sysLogPrintf(LOG_NOTE,
            "GE_INPUTLOG gepdaim d=(%.1f,%.1f) cross=(%.2f,%.2f) game=(%.2f,%.2f) aa=(%.2f,%.2f) st=%.3f sv=%.3f ct=%d cam=(%.1f,%.1f)",
            dxPx, dyLook, AIMS.crossX, AIMS.crossY, resX, resY,
            (double)p->autoaimx, (double)p->autoaimy,
            (double)p->speedtheta, (double)p->speedverta,
            g_ClockTimer, (double)p->vv_theta, (double)p->vv_verta);
    }
    return 1;
}

/* WI-1 (GEPD-INPUT-PLAN.md #89): hipfire direct camera write.
 *
 * Mirrors GEPD's hipfire branch (games/goldeneye.c): `camx += XPOS/10 *
 * (SENS/40) * (fov/basefov)` degrees this poll -- linear in px, FOV-scaled,
 * no stick curve in between. We fold GEPD's SENS/40 into our own
 * MouseTurnSpeed*MouseSensitivity product (both already 100% at defaults,
 * same "in line" convention D238 established for aimGepdCompute's GepdSens):
 * at defaults this reduces to exactly GEPD's px/10 baseline.
 *
 * dxPx/dyLook are this poll's dt-scaled px (same convention as
 * aimGepdCompute -- callers pre-multiply by lookDtScale).
 *
 * GEPD's safety gates (`camera==4||0 && menupage==11 && !dead && !watch &&
 * !pause`) matter here in a way they didn't for aim mode: aim mode requires
 * RMB held, so it naturally can't fire during a death/cutscene/pause state a
 * player would be holding RMB through; hipfire looks are always live, so a
 * frozen or scripted-camera state (POSEND/INTRO cutscenes, death cam, pause)
 * must be checked explicitly or this would fight the game's own camera
 * control during those states. `current_menu` already gates menupage==11
 * (checked by the caller's menuMode branch, same as aim mode); the rest are
 * read here directly -- read-only, no logic change.
 *
 * Returns 1 if it handled this poll (caller must not also run the legacy
 * stick path); 0 to fall back (disabled, no player, or a safety gate is
 * closed -- e.g. mid-death or mid-cutscene, matching GEPD's !dead/!watch).
 */
/* D428 (#117): the gun's turn-lag ("sway") is driven by player->speedtheta
 * (gunSetBondWeaponSway, bondview2.c), which the game derives from STICK
 * deflection. Direct mouse look writes vv_theta instead and leaves speedtheta
 * at ~0, so the gun stayed glued to the view. Accumulate the yaw degrees the
 * direct path applied (poll thread) and hand the game thread an equivalent
 * turn speed once per tick: the exact inverse of the game's own yaw
 * integration (see portGunSwayTheta). Hipfire only; display-side. */
/* D428 follow-up (#99): one accumulator per player. Fed on the poll thread
 * for the polled slot (player N owns slot N), drained on the game thread for
 * the current player. Solo = slot/player 0 only, as before. The game-side hook
 * (bondview2.c, D428 Rule-2 block) still drains player 0 only; see findings
 * D428 "split-screen note" for the one-line widening it would need. */
static SDL_atomic_t s_swayThetaMdeg[4];

static void swayFeedTheta(double deg)
{
    SDL_AtomicAdd(&s_swayThetaMdeg[(s_slot > 0 && s_slot < 4) ? s_slot : 0],
                  (int)lround(deg * 1000.0));
}

float portGunSwayTheta(float dt)
{
    extern s32 get_cur_playernum(void);
    s32 pn = get_cur_playernum();
    int m = SDL_AtomicSet(&s_swayThetaMdeg[(pn > 0 && pn < 4) ? pn : 0], 0);
    if (dt <= 0.0f)
        return 0.0f;
    /* Exact inverse of the game's own yaw integration (bondview2.c:
     * vv_theta += speedtheta * dt * 3.5) and of its natural-turn speed, which is
     * set straight from the stick with no ramp: speedtheta = (stick/70)^2 * fov/60,
     * i.e. |speedtheta| <= fov/60 (~1.0). So a mouse turn of X degrees/tick gives
     * the same gun lag the stick turning at that speed did on the N64. (An earlier
     * version used the vertical axis' delta/(2*dt) clamped to 0.7: too weak at full
     * speed, too strong at slow speed.) */
    f32 fov = viGetFovY();
    double lim = (fov > 0.0f) ? (double)fov / 60.0 : 1.0;
    double v = (m / 1000.0) / (3.5 * (double)dt);
    if (v >  lim) v =  lim;
    if (v < -lim) v = -lim;
    if (m != 0 && configGetInputLog())
        sysLogPrintf(LOG_NOTE, "GE_INPUTLOG gunsway theta=%.1f deg speed=%.3f", m / 1000.0, v);
    return (float)v;
}

static int hipDirectCompute(double dxPx, double dyLook)
{
    struct player *p = slotPlayer();

    if (!mouseDirectLook || !mouseGrabbed || p == NULL)
        return 0;

    /* GEPD: !dead && !watch && !pause. bonddead/outside_watch_menu/
     * pause_state are the decomp-canonical equivalents (bondview.h). */
    if (p->bonddead || !p->outside_watch_menu || p->pause_state != 0)
        return 0;

    /* M-190: the "frozen or scripted-camera state (POSEND/INTRO cutscenes...)
     * must be checked explicitly" gate this function's own comment above has
     * always documented, but never actually implemented -- found live: the
     * mouse could still freely move the camera during the Dam intro flyover
     * and the abseil-jump cutscene, both CAMERAMODE_INTRO/POSEND, which are
     * supposed to be camera-locked. bondviewFrozenMoveBond (bondview2.c:7947)
     * already calls bondviewProcessInput(0,0,0,0) to correctly ignore stick
     * input during these modes, but that gate lives entirely inside the
     * stick-based dispatch (MoveBond vs bondviewFrozenMoveBond,
     * bondview2.c:8268) -- this function writes p->vv_theta/vv_verta
     * directly, bypassing that dispatch entirely. Reuses the same
     * scripted-camera test D243's clamps use (bondview2.c, gameScriptedCameraActive,
     * formerly d243mCutsceneActive -- renamed since this isn't D243-specific). */
    if (gameScriptedCameraActive())
        return 0;

    f32 fov = slotFovY();
    f32 scale = (fov > 0.0f) ? fov / GEPD_BASE_FOV : 1.0f;
    double sens = (mouseTurnSpeed / 100.0) * (mouseSensitivity / 100.0);

    /* dyLook already carries MouseInvertY + MouseYScale (applied by the
     * caller before dt-scaling, same as every other consumer of dyLook) --
     * do not re-apply either here. */
    s_hipThetaDegOut = dxPx * 0.1 * sens * scale;
    p->vv_theta += (f32) s_hipThetaDegOut;
    p->vv_verta -= (f32) (dyLook * 0.1 * sens * scale);
    if (p->vv_verta >  90.0f) p->vv_verta =  90.0f;
    if (p->vv_verta < -90.0f) p->vv_verta = -90.0f;

    if (configGetInputLog()) {
        sysLogPrintf(LOG_NOTE,
            "GE_INPUTLOG hipdirect d=(%.1f,%.1f) cam=(%.1f,%.1f)",
            dxPx, dyLook, (double)p->vv_theta, (double)p->vv_verta);
    }
    return 1;
}

/* D404 (resolved by implementation, 2026-09-28): the pad-side twin of
 * hipDirectCompute's CENTRED path -- the right stick drives the camera
 * directly and the crosshair stays pinned at centre, so the Aim-style
 * toggle is no longer inert on a pad (it used to be: the pad always
 * emitted deflection into the game's N64 crosshair integrator, i.e. it
 * was *always in N64 mode*). Same safety gates as hipDirectCompute
 * (dead / watch / pause / scripted camera); the mouseDirectLook/
 * mouseGrabbed gate is dropped -- it is meaningless for a pad. Input
 * units are stick deflection (STICK_MAX = full, the same unit the N64
 * path feeds the game's integrator).
 *
 * Gain = the N64 natural-turn curve VERBATIM (bondview2.c, canNaturalTurn
 * / canNaturalPitch branches): v = clamp(deflection/70, \u00b11); v =
 * sign(v)\u00b7v\u00b2; camera += v \u00b7 (fov/60) \u00b7 3.5 deg/poll. First pass used a
 * linear 2.0 deg/poll \u00b7 (fov/GEPD_BASE_FOV) gain, which the user measured
 * as a dramatic sensitivity loss in Centred mode (linear vs quadratic
 * curve, and the 60-vs-90 FOV base compounded it to ~2.6\u00d7 slower at full
 * stick). With the verbatim curve, full-stick Centred turns at exactly
 * the N64 path's top rate -- switching modes changes the crosshair
 * behaviour, not the feel. */
static int padDirectCompute(int dx, int dy)
{
    struct player *p = slotPlayer();

    if (p == NULL)
        return 0;
    if (p->bonddead || !p->outside_watch_menu || p->pause_state != 0)
        return 0;
    if (gameScriptedCameraActive())
        return 0;

    if (dx == 0 && dy == 0)
        return 1;   /* nothing to turn; caller simply emits no stick */

    f32 fov = slotFovY();
    double k = (fov > 0.0f) ? (double)fov / 60.0 : 1.0;   /* bondview2's base */
    double v;

    v = (double)dx / 70.0;   /* analogTurn = raw stick \u00b1 5, /70 -- bondview2.c:6222 */
    if (v > 1.0) v = 1.0; else if (v < -1.0) v = -1.0;
    if (v >= 0.0) v *= v; else v = -v * v;
    p->vv_theta += (f32) (v * k * 3.5);

    v = (double)dy / 70.0;   /* analogPitch, /70 -- bondview2.c:6161 */
    if (v > 1.0) v = 1.0; else if (v < -1.0) v = -1.0;
    if (v >= 0.0) v *= v; else v = -v * v;
    p->vv_verta -= (f32) (v * k * 3.5);   /* game: speedverta = -v\u00b7(fov/60) */
    if (p->vv_verta >  90.0f) p->vv_verta =  90.0f;
    if (p->vv_verta < -90.0f) p->vv_verta = -90.0f;

    if (configGetInputLog()) {
        sysLogPrintf(LOG_NOTE,
            "GE_INPUTLOG paddirect d=(%d,%d) cam=(%.1f,%.1f)",
            dx, dy, (double)p->vv_theta, (double)p->vv_verta);
    }
    return 1;
}

/* --- Input.PdMouseAim: the port side of PD's mouse-aim model ----------------
 * See the flag's comment above for the design. PD's own implementation lives
 * in its GAME files (bondmove.c/bondgun.c) behind `#ifndef PLATFORM_N64`, with
 * the port supplying the input; we mirror that with `#ifdef PORT` hooks in
 * GE's aim path (sub_GAME_7F067FBC, gunfire.c).
 *
 * The port accumulates the mouse delta (aimGepdAccumulate, shared with GEPD's
 * model) and the game's own crosshair integrator does the rest. PD's range is
 * swivelpos in [-1,1] == the screen edge, which is what the game's display
 * formula maps to half a screen width; GEPD's crosshair limit is that same
 * edge in its own units, so dividing by it gives PD's units. */
int portMouseAimPdActive(void)
{
    /* D337: only while the mouse is the device driving aim -- otherwise the
     * pad's stick turn (the N64 path, weapon damp) must reach the game. */
    return pdMouseAimEnabled() && mouseGrabbed && s_aimDevMouse;
}

int portMouseAimPdGetTurn(f32 *tx, f32 *ty)
{
    if (!portMouseAimPdActive() || g_CurrentPlayer == NULL)
        return 0;
    if (inputMpSplit() && player_num != 0)   /* D416: the mouse owns player 0 only */
        return 0;
    /* Game thread: never AIMS (s_slot belongs to the poll thread). The mouse
     * lives on slot 0 (D422). */
    if (tx) *tx = (f32) (s_aimSt[0].crossX / GEPD_CROSSHAIR_LIMIT * aimRangeScale());   /* D338 */
    if (ty) *ty = (f32) (s_aimSt[0].crossY / GEPD_CROSSHAIR_LIMIT * aimRangeScale());
    return 1;
}

PD_CONSTRUCTOR static void inputConfigInit(void)
{
    configRegisterInt("Input.MPMode", &mpInputMode, 0, 2);   /* D416: 0 Auto, 1 PadsOnly, 2 KbmP1 */
    configRegisterInt("Input.MouseEnabled", &mouseEnabled, 0, 1);
    configRegisterInt("Input.MouseAimSpeed", &mouseAimSpeed, 1, 500);
    configRegisterInt("Input.PdMouseAim",     &pdMouseAim,     0, 1);   /* legacy -> AimStyle 1 */
    configRegisterInt("Input.AimStyle",       &aimStyleLegacy, 0, 2);   /* D333 legacy -> AimMode */
    configRegisterInt("Input.AimMode",        &aimMode,        0, 1);   /* D337 */
    configRegisterInt("Input.AimLegacyGepd",  &aimLegacyGepd,  0, 1);   /* D337 hidden fallback */
    configRegisterInt("Input.AimRange",       &aimRange,       0, 1);   /* D338 PC / N64 */
    configRegisterInt("Input.AimAbsolute", &aimAbsolute, 0, 1);  /* D194 */
    configRegisterInt("Input.MouseDirectLook", &mouseDirectLook, 0, 1);  /* WI-1 */
    /* D194: renamed Input.GepdSens -> Input.AimModeSens (community name for
     * the RMB aim mode; "GEPD" is internal provenance jargon). The old key
     * stays registered against the same variable as a deprecated alias --
     * if both appear in an ini, the later line wins. */
    /* D304: widened from the old (1,80) clamp to match Input.MouseTurnSpeed's
     * (1,500) range exactly -- both are the same underlying "reference SENS
     * passthrough" unit (see aimGepdCompute/hipDirectCompute), consumed by
     * two different, intentional per-mode divisor constants (2920 vs the
     * turn-speed path's /100*0.1), so a shared range lets the two sliders be
     * directly comparable ("1:1" in the menu) instead of arbitrarily capped
     * at different, mismatched ceilings. Old 80 cap was well below even this
     * value's own reference-model native ceiling (100 raw = 500% in the
     * source GEPD injector's own display convention). */
    configRegisterInt("Input.AimModeSens", &gepdSens, 1, 500);
    configRegisterInt("Input.GepdSens",    &gepdSens, 1, 500);   /* deprecated alias */
    configRegisterInt("Input.AimBand", &aimBand, 5, 40);
    configRegisterInt("Input.TankAimScale", &tankAimScale, 10, 300);   /* D407 */
    configRegisterInt("Input.MouseTurnSpeed", &mouseTurnSpeed, 1, 500);
    configRegisterInt("Input.SensLink", &sensLink, 0, 1);
    configRegisterInt("Input.MenuPointerSpeed", &menuPointerSpeed, 10, 500);
    configRegisterInt("Input.MenuPointerMode", &menuPointerMode, 0, 1);
    configRegisterInt("Input.HipfirePitchSpeed", &hipfirePitchSpeed, 10, 500);
    configRegisterInt("Input.MouseInvertY", &mouseInvertY, 0, 1);
    configRegisterInt("Input.MouseYScale", &mouseYScale, 1, 500);
    configRegisterInt("Input.MouseSmoothing", &mouseSmoothing, 0, 90);
    configRegisterInt("Input.MouseRawInput", &mouseRawInput, 0, 1);
    configRegisterInt("Input.MouseDtDecouple", &mouseDtDecouple, 0, 1);  /* D194(b) */
    configRegisterInt("Input.MouseSensitivity", &mouseSensitivity, 1, 500);  /* D238 */
    configRegisterInt("Input.MouseAimCurve", &aimCurveGamma, 50, 400);  /* D194(a), x100 */
    configRegisterInt("Input.NaturalPitch", &naturalPitchMode, 0, 1);  /* D194/D238 */
    configRegisterInt("Input.PadDeadzone", &padDeadzone, 0, 30000);  /* legacy; Wave A migrates to L/R */
    configRegisterInt("Input.PadDeadzoneL", &padDeadzoneL, 0, 30000);  /* Wave A: left (movement) stick */
    configRegisterInt("Input.PadDeadzoneR", &padDeadzoneR, 0, 30000);  /* Wave A: right (look) stick */
    configRegisterInt("Input.PadLookSensX", &padLookSensX, 25, 200);   /* Wave A: look horizontal sensitivity (%) */
    configRegisterInt("Input.PadLookSensY", &padLookSensY, 25, 200);   /* Wave A: look vertical (pitch) sensitivity (%) */
    configRegisterInt("Input.PadSouthpaw", &padSouthpaw, 0, 1);        /* Wave A: swap fire/grenade triggers */
    configRegisterInt("Input.PadLookSmooth", &padLookSmooth, 0, 10);   /* Wave A: look low-pass strength, 0-10 (0 = off) */
    configRegisterInt("Input.PadTriggerPct", &padTriggerPct, 1, 99);
    configRegisterInt("Input.PadLookInvertY", &padLookInvertY, 0, 1);
    /* D401: global rumble scale (0 = silent .. 1 = full), applied to every
     * pad in inputRumble(). One setting for all pads (user call, 2026-09-28):
     * the original per-pad Input.PlayerN.RumbleScale keys were dropped --
     * stale ones in an existing ini log a one-time "unknown key" note and
     * self-clean on the next configSave. */
    configRegisterFloat("Input.RumbleScale", &gRumbleScale, 0.f, 1.f);
    /* D380: single PC layout. Old Input.Layout is ignored and removed on
     * configSave; BindingsVersion marks the one-time stale-default migration. */
    configRegisterInt("Input.BindingsVersion", &bindsVersion, 0, 3);
    configRegisterInt("Input.CrouchMode", &crouchMode, 0, 1);

    /* Seed fresh ini files with the REAL PC binding strings. Older ini
     * values are migrated at inputInit after configLoad, exactly once. */
    for (int a = 0; a < IA_COUNT; a++) {
        const char *def = kGepdPreset[a] ? kGepdPreset[a] : kBindDefs[a].def;
        strncpy(g_bindStr[a], def, sizeof(g_bindStr[a]) - 1);
        configRegisterString(kBindDefs[a].key, g_bindStr[a], sizeof(g_bindStr[a]));
    }
}
