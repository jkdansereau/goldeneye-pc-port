/*
 * F10 in-game options overlay -- approach (C) from docs/dev/OPTIONS-MENU-PLAN.md.
 *
 * Port-layer only. No src/ menu code is touched: the overlay draws its own
 * fast3d 2D display list (appended after the game DL in gfx_run) and edits the
 * port-owned config.c variables directly. Live knobs apply immediately; the
 * two that need an FBO/window rebuild (MSAA, Fullscreen) are tagged "(restart)".
 *
 * Menu PD alignment (T2/T3): PD's dialog anatomy -- hub of big-font rows ->
 * flat sub-dialogs, gradient title strip, tapering focus rule, separator +
 * Back as the last row, Select-player hop before Controller / Key bindings --
 * in GE's watch palette. Rows still resolve to the same ini keys.
 *
 * The panel adapts to whatever 2D space it is drawn in (320x240 in-game vs
 * 440x330 on front-end screens -- viSetXY differs) and scrolls when the row
 * list outgrows the viewport (wheel / arrows at the edges). F10 opens at a
 * category list; each category has its own short page and Back row. Cyclic
 * rows (MSAA, texture filter, resolution, toggles) wrap in both directions.
 *
 * Text + fill helpers are the game's own (textRender / microcode_constructor /
 * gDPFillRectangle) reached by extern -- same pattern input.c uses to read
 * current_menu / cursor_h_pos. This is a rendering/UI view, not a logic change.
 *
 * Diagnostic: set GE_OPTIONSOVERLAY=1 to auto-open at boot (headless layout
 * check). Env-gated, harmless when unset.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include <PR/ultratypes.h>
/* gbi.h's gDP* DL macros use _SHIFTL/_SHIFTR but do not define them -- game TUs
 * get them from <ultra64.h>/<PR/mbi.h>, which also drags in N64 OS headers that
 * shadow libc here. Define the two pure macros locally (verbatim from mbi.h) so
 * this stays a plain port TU. Without them GCC/ld fails "undefined reference to
 * _SHIFTL" (MinGW's chain happens to provide it). */
#ifndef _SHIFTL
#define _SHIFTL(v, s, w) ((u32)(((u32)(v) & ((0x01 << (w)) - 1)) << (s)))
#define _SHIFTR(v, s, w) ((u32)(((u32)(v) >> (s)) & ((0x01 << (w)) - 1)))
#endif
#include <PR/gbi.h>
#include <bondconstants.h>

extern MENU current_menu;

#include "platform.h"
#include "system.h"
#include "config.h"
#include "video.h"
#include "input.h"
#include "front.h"   /* selected_folder_num (D356 stage probe) */
#include "bondtypes.h"   /* sImageTableEntry (D555 crosshair pointer: typed field reads) */
#include "file.h"   /* save_data (D356 reset probe: second-file isolation) */
#include "optionsoverlay.h"
#include "hudaspect.h"   /* D335b/D472: PORT_HUD_ASPECT */
#include "watchsettings.h"
#include "audio.h"
#include "updatecheck.h"
#include "../fast3d/gfx_api.h"

/* file2.c; same extern as watchsettings.c (not in a header). */
extern save_data *fileGetSaveForFoldernum(u32 folder);

/* D324 class: -Iinclude resolves <math.h> to GE's N64 stub, which does not
 * declare lround; without this the call is an implicit `int lround()`
 * (GCC only rescued it via its builtin signature). */
long lround(double x);

/* ---- game symbols (rendering/UI only; see input.c for the same pattern) ---- */
struct font;
struct fontchar;
extern struct font     *ptrFontBankGothic;
extern struct fontchar *ptrFontBankGothicChars;
extern struct font     *ptrFontZurichBold;
extern struct fontchar *ptrFontZurichBoldChars;
extern Gfx  *microcode_constructor(Gfx *gdl);
extern Gfx  *textRender(Gfx *gdl, s32 *x, s32 *y, char *text, struct fontchar *chars,
                        struct font *font, u32 colour, s32 width, s32 height,
                        u32 yOffset, s32 lineheight);
extern void  textMeasure(s32 *textheight, s32 *textwidth, char *text,
                         struct fontchar *chars, struct font *font, s32 lineheight);
extern s16   viGetX(void);
extern s16   viGetY(void);
extern void  viSetXY(s16 x, s16 y);
/* D555 rework: the game's own crosshair sprite + its draw helpers (front.c
 * frontDrawCursor / gunfire.c gunDrawSight use exactly these). */
extern struct sImageTableEntry *crosshairimage;
extern void texSelect(Gfx **gdlptr, struct sImageTableEntry *tconfig, u32 arg2, s32 arg3, u32 ulst);
extern void display_image_at_position(Gfx **DL, f32 *xypos, f32 *halfedxy, s32 width, s32 height,
                                      s32 rotateleft90, s32 fliph, s32 flipv, s32 red, s32 green,
                                      s32 blue, s32 alpha, s32 format, s32 param_14);

/* ------------------------------------------------------------------------ */

enum { ROW_TOGGLE, ROW_SLIDER, ROW_ENUM, ROW_MSAA, ROW_RES, ROW_ACTION, ROW_FPSCAP,
       ROW_HEADER, ROW_BOND_FILE, ROW_BIND /* key / mouse-button capture */,
       ROW_PADBIND /* D469: gamepad button capture (per seat) */,
       ROW_PADSEAT /* D469: which controller seat the pad rows edit */,
       ROW_AUDIODEV /* D470: audio output device (string, cycles Default + enumerated) */ };

/* D346: wording pass -- Nightdive/Turok + PD-port conventions: title-case
 * On/Off, no all-caps value strings. Display-only; config stores 0/1 either way. */
static const char *const kOnOff[]     = { "Off", "On", NULL };
static const char *const kHold[]      = { "Hold", "Toggle", NULL };
static const char *const kAspectMode[] = { "Fill window", "Original", "16:9", "21:9", NULL };   /* D447: Original = 4:3 (16:9 with the game Ratio) bars; D508: 2..3 force a ratio */
static const char *const kTexFilter[] = { "Nearest", "Bilinear", "3-point", "Trilinear", NULL };   /* Trilinear option (playtest 2026-10-03) */
static const char *const kPadPreset[] = { "1.1 Jinx", "1.2 Christmas", "1.3 Frost", "Custom", "1.4 Elektra", NULL };   /* D498: index = stored value; inputPadPresetStep gives the display order */
/* D516: the game's own control styles, shown in Original (index = style value). */
static const char *const kOrigStyle[] = { "1.1 Honey", "1.2 Solitaire", "1.3 Kissy", "1.4 Goodnight",
                                          "2.1 Plenty", "2.2 Galore", "2.3 Domino", "2.4 Goodhead", NULL };
static int s_padSeat = 0;   /* D469: seat edited by the Controller page rows */
static const char *const kControlScheme[] = { "Extended", "Original", NULL };   /* D513/D554: display text only (index = stored value) */
static const char *const kAimMode[]   = { "Original", "Centered", NULL };   /* D337 */
static int displayModeNow(void);   /* defined with the preset helpers below */
static const char *const kDisplayModeName[3];
static const char *const kAimRange[]  = { "Extended", "Original", NULL };             /* D338 */
static const int         kMsaaSeq[]   = { 1, 2, 4, 8, 16 };   /* D443: 16x added */
#define MSAA_N ((int)(sizeof(kMsaaSeq) / sizeof(kMsaaSeq[0])))
/* v0.4.0 modern options wave: value names for the new rows (they land
 * in the functional sections -- Turok standard, D356 -- not a bucket). */
static const char *const kOnOffRev[]  = { "On", "Off", NULL }; /* 0 = On */
/* D379: the authored N64 sprite is red. Original is the identity path;
 * White at index 7 uses the same alpha-mask combiner as other true hues. */
static const char *const kCrosshairColor[] = {
    "Original (red)", "Green", "Red", "Blue", "Yellow", "Cyan", "Magenta",
    "White", "Custom", NULL,
};
static const char *const kFullscreenMode[] = { "Borderless", "Exclusive", NULL };   /* D511 */
static const char *const kCrosshairStyle[] = { "Original", "Thin cross", NULL };
static const char *const kGameplayView[] = { "Original", "Extended", NULL };   /* D468: 0 = N64 area, 1 = everything drawn (PD port) */
/* D186: the sim's own tick pacemaker is hardcoded to the console's native VI
 * rate (60Hz NTSC / 50Hz PAL, port/src/libultra.c) -- Video.FpsCap can only
 * throttle down from there, never past it, and throttling it below 30
 * throttles game logic itself (video.c already force-uncaps anything under
 * 30). A free 0-360 slider therefore had a huge dead zone (every value above
 * the console rate is a no-op, every value 1-29 silently snaps to 0) with
 * only two states that actually do anything. Exposed as a plain 30/60 toggle
 * instead (user ask, 2026-09-18). A third "uncapped" (0, skip the port's own
 * frame-pacing wait) state exists at the config level and old inis may still
 * have it, but it's dropped from the menu: with VSync on (the default) it's
 * indistinguishable from 60, and with VSync off it just burns GPU time
 * re-presenting the same simulated frame -- confusing for no real benefit. */
static const int         kFpsCapSeq[] = { 30, 60 };

/* Windowed-mode resolution presets. Filtered at init to those that fit the
 * desktop; the Resolution row cycles the surviving list. */
static const int kResList[][2] = {
    {  640,  480 }, {  800,  600 }, {  960,  720 }, { 1024,  768 },
    { 1152,  864 }, { 1280,  720 }, { 1280,  800 }, { 1280,  960 },
    { 1366,  768 }, { 1440,  900 }, { 1600,  900 }, { 1600, 1200 },
    { 1680, 1050 }, { 1920, 1080 }, { 1920, 1200 }, { 2560, 1440 },
    { 3200, 1800 }, { 3840, 2160 },
};
#define NUM_RES ((int)(sizeof(kResList) / sizeof(kResList[0])))
static int s_resFit[NUM_RES];   /* indices into kResList that fit the desktop */
static int s_resFitN = 0;
static int s_resSel  = 0;       /* index into s_resFit */
static int s_dragRow = -1;      /* scheduler-thread mouse drag */
static SDL_atomic_t s_dragWatchField; /* field+1, host may close F10 mid-drag */
/* D489: field+1 of a watch slider stepped by D-pad/stick/arrows. Each commit
 * is three joyDisablePoll/joyEnablePoll handshakes plus an EEPROM file
 * rewrite on the game thread, so committing every held-repeat detent stalled
 * frames. Steps stage like a mouse drag; optionsAdjustCommitPending() saves
 * once when the direction is released. */
static SDL_atomic_t s_adjWatchField;

struct Row {
    const char        *key;
    const char        *label;
    int                kind;
    double             step;
    const char *const *names;    /* ROW_TOGGLE / ROW_ENUM value names */
    int                restart;  /* value change needs a restart      */
    double             uiMin, uiMax; /* 0,0 -> use the registered clamp */

    /* resolved from config.c at init */
    int                found;
    int                type;     /* CONFIG_OPT_*    */
    void              *ptr;
    double             cfgMin, cfgMax;

    /* Optional conditional row visibility (e.g. Aim range while centred).
     * Resolved to hidePtr at init. */
    const char        *hiddenIfOn;
    int               *hidePtr;
    /* Show RGB sliders only while the colour selector is set to Custom. */
    const char        *shownWhen;
    int                shownValue;
    int               *showPtr;
    /* D516: 1 = only with Input.ControlScheme Ext, 2 = only with Original.
     * Resolved to schemePtr at init (shownWhen can't express a second condition). */
    int                schemeOnly;
    int               *schemePtr;

    /* D346: value-text decoration (display only). unit is appended to integer
     * slider values ("%"/"x"); dispDiv>0 divides the raw value before display
     * (e.g. deadzone raw 0..30000 -> % of full stick). NULL/0 = plain int. */
    const char        *unit;
    int                dispDiv;

    /* D356: 1 = this row's value lives in the selected save file (per-file
     * watch rows). Carries the dim "(per profile)" tag on the only section that
     * mixes scopes (GAMEPLAY/HUD); on headers the flag is recomputed at init to
     * mean "this section contains per-file rows" (drives the "(File N)"
     * title annotation in both UIs). */
    int                saveScoped;
    int                bindSlot; /* D383/D385: selected key/mouse slot, 0..1 */

    /* Menu PD alignment: 1 = hub-only row (F10 hub bottom, after a separator;
     * hidden on its section page). */
    int                hub;
};
static int rowSchemeOk(const struct Row *r);   /* D516 */

/* Every dialog ends with a separator + Back row (PD pattern). Both are
 * non-config ROW_ACTION rows, so every "skip ROW_ACTION" loop (resets, probes)
 * already ignores them; they are told apart by key prefix. SEP(n) is a plain
 * mid-page group separator (PD: menuitem separators between option groups). */
#define SEP(n) { .key="__Sep" #n, .label="", .kind=ROW_ACTION }
#define SEP_BACK(n) SEP(n), { .key="__Back" #n, .label="Back", .kind=ROW_ACTION }

/* Menu PD parity (R7): PD's six groups -- Video, Audio, Mouse, Controller,
 * Game, Key Bindings -- shared by the F10 overlay and the front-end PC Options.
 * Physical order here keeps each dialog's rows contiguous after its header
 * (resets and the probes walk header -> next header); the hub / tab order is
 * kRootOrder below. Controller opens a Select player page first (PD pattern).
 * All rows use designated initializers (D351 class). */
static struct Row rows[] = {
    { .key="__HdrVideo", .label="VIDEO", .kind=ROW_HEADER },
    /* T4-lite Display mode: derived Modern / Original N64 / Custom from
     * videoPresetIsActive (D440 key table in video.c); a dropdown (F10) or
     * left/right (both UIs) applies the picked preset at once. No new ini key:
     * Custom is simply "neither preset's keys all match" and flips back when
     * they do, so it is shown but not selectable. */
    { .key="__DisplayMode", .label="Display mode", .kind=ROW_ACTION },
    SEP(V1),
    { .key="Video.Fullscreen", .label="Fullscreen", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    { .key="Video.FullscreenMode", .label="Fullscreen mode", .kind=ROW_ENUM, .step=1, .names=kFullscreenMode, .shownWhen="Video.Fullscreen", .shownValue=1 },   /* D511 */
    { .key="__CenterWindow", .label="Center window", .kind=ROW_ACTION, .shownWhen="Video.Fullscreen", .shownValue=0 },   /* D511: windowed only */
    { .key="__Resolution", .label="Resolution", .kind=ROW_RES },
    /* D447: Original letter/pillarboxes to the console aspect (4:3, or 16:9 while
     * the watch-menu Ratio is 16:9); Window fills the window. */
    { .key="Video.AspectMode", .label="Aspect ratio", .kind=ROW_ENUM, .step=1, .names=kAspectMode },
    /* D334: native widescreen (world projected at the window aspect, Hor+).
     * While on, "Widescreen auto FOV" has no effect (it was the stretch-era
     * vertical-FOV compensation). */
    { .key="Video.NativeWidescreen", .label="Native widescreen", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    { .key="Video.WidescreenAuto", .label="Widescreen auto FOV", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    { .key="Video.MSAA", .label="Anti-aliasing", .kind=ROW_MSAA, .restart=1 },
    SEP(V2),
    { .key="Video.VSync", .label="VSync", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    { .key="Video.FpsCap", .label="Frame rate cap", .kind=ROW_FPSCAP },
    { .key="Video.DisplayFPS", .label="Show FPS", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    SEP(V3),
    { .key="Video.TextureFilter", .label="Texture filter", .kind=ROW_ENUM, .step=1, .names=kTexFilter },
    { .key="Video.Anisotropy", .label="Anisotropic filtering", .kind=ROW_SLIDER, .step=1, .unit="x" },
    SEP(V4),
    { .key="Video.SafeAreaCrop", .label="Crop overscan", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    /* 1.0x..8.0x the authored distance (ini 100..800 %; 2.5x default).
     * Legacy AutoFov ini keys remain supported but no longer hide sliders. */
    { .key="Video.DrawDistance", .label="Draw distance", .kind=ROW_SLIDER, .step=25, .uiMin=100, .uiMax=800 },
    /* D540: fog start/end as a multiple of the level's own (N64) fog;
     * capped at the draw distance. 0.5x..8.0x, 1.0x default. */
    { .key="Video.FogDistance", .label="Fog distance", .kind=ROW_SLIDER, .step=25, .uiMin=100, .uiMax=800 }   /* D565: no < 100 */,
    { .key="Video.LodDistance", .label="LOD distance", .kind=ROW_SLIDER, .step=25, .uiMin=100, .uiMax=800 },
    SEP(V5),
    /* D181 re-exposed (PD "Explosion shake"): named for what it scales (explosions only). */
    { .key="Game.ScreenShakeIntensity", .label="Explosion shake", .kind=ROW_SLIDER, .step=0.1, .uiMin=0, .uiMax=3 },
    { .key="__ResetVideo", .label="Reset to defaults", .kind=ROW_ACTION },
    SEP_BACK(Video),

    { .key="__HdrAudio", .label="AUDIO", .kind=ROW_HEADER },
    /* D470: port-level master volume (final gain on the mixed output, whole %)
     * and the output device (live switch; Default = system default). */
    { .key="Audio.MasterVolume", .label="Master volume", .kind=ROW_SLIDER, .step=5, .unit="%" },
    { .key="Audio.Device", .label="Output device", .kind=ROW_AUDIODEV },
    /* D356: the per-file volume sliders (homogeneous section: title annotation only). */
    { .key="Bond.Music", .label="Music volume", .kind=ROW_SLIDER, .step=328,
      .uiMax=32767, .cfgMax=32767, .found=1, .unit="%", .dispDiv=328, .saveScoped=1 },
    { .key="Bond.FX", .label="FX volume", .kind=ROW_SLIDER, .step=328,
      .uiMax=32767, .cfgMax=32767, .found=1, .unit="%", .dispDiv=328, .saveScoped=1 },
    { .key="__ResetAudio", .label="Reset to defaults", .kind=ROW_ACTION },
    SEP_BACK(Audio),

    { .key="__HdrMouse", .label="MOUSE", .kind=ROW_HEADER },
    /* PD parity (maintainer list 2026-10-04): the long-standing ini key finally gets a row; default on. */
    { .key="Input.MouseEnabled", .label="Mouse enabled", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    { .key="Input.MouseSensitivity", .label="Horizontal sensitivity", .kind=ROW_SLIDER, .step=10,
      .uiMin=10, .uiMax=400 }, /* D443/D357: shown as a multiplier, raw 100 = 1.0x; storage unchanged */
    /* Wave A (v0.5.0, CONTROLLER-INPUT-PLAN item 6): Input.MouseYScale (extra
     * vertical/pitch sensitivity, %); input.c already applies it. */
    { .key="Input.MouseYScale", .label="Vertical sensitivity", .kind=ROW_SLIDER, .step=10,
      .uiMin=10, .uiMax=400 }, /* D506: shown as a multiplier like the horizontal row, raw 100 = 1.0x */
    { .key="Input.MouseInvertY", .label="Invert look", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    { .key="Input.MouseSmoothing", .label="Smoothing", .kind=ROW_SLIDER, .step=5, .unit="%" },
    { .key="Input.MouseRawInput", .label="Raw input", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    /* D337: N64 = the N64 aim model (crosshair travels, camera edge-scrolls)
     * with the mouse fed through the game's integrator at PD's mouse damp --
     * the default. CENTRED (PC) = opt-in FPS-style aim (#104), not N64.
     * Device-neutral (D404): applies to every aim input. */
    { .key="Input.AimMode", .label="Aim style", .kind=ROW_ENUM, .step=1, .names=kAimMode },
    /* D338: how far the N64-style crosshair travels. Hidden while the aim
     * style is CENTRED (PC), where the crosshair doesn't travel. */
    { .key="Input.AimRange", .label="Aim range", .kind=ROW_ENUM, .step=1, .names=kAimRange, .hiddenIfOn="Input.AimMode" },
    /* Existing ini-only keys exposed by the PD-parity regroup (rows only; input.c
     * already reads them). */
    { .key="Input.CrosshairCursor", .label="Crosshair pointer", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    { .key="__ResetMouse", .label="Reset to defaults", .kind=ROW_ACTION },
    SEP_BACK(Mouse),

    /* Controller: top-level hub entry; its page is the one-step Select player
     * list (PD pattern, option A ratified) that sets the pad seat (s_padSeat)
     * and opens that seat's Controller page. Hub label CONTROLLER, page title
     * SELECT PLAYER (optionsRowTitle). */
    { .key="__HdrSelPlayerC", .label="CONTROLLER", .kind=ROW_HEADER },
    { .key="__SelPlayerC1", .label="Player 1", .kind=ROW_ACTION },
    { .key="__SelPlayerC2", .label="Player 2", .kind=ROW_ACTION },
    { .key="__SelPlayerC3", .label="Player 3", .kind=ROW_ACTION },
    { .key="__SelPlayerC4", .label="Player 4", .kind=ROW_ACTION },
    SEP_BACK(SelPlayerC),

    /* D469: preset selector; Custom unlocks per-seat, per-action pad rebinding
     * (two slots per action, pad-driven capture). */
    { .key="__HdrController", .label="CONTROLLER", .kind=ROW_HEADER },
    { .key="Input.ControlScheme", .label="Control style", .kind=ROW_ENUM, .step=1, .names=kControlScheme },   /* D513 */
    { .key="Input.PadPreset", .label="Layout presets", .kind=ROW_ENUM, .step=1, .names=kPadPreset, .schemeOnly=1 },
    /* D516: Original shows the game's own styles (the save's, per seat), not the port presets. */
    { .key="Bond.Control", .label="Layout presets", .kind=ROW_ENUM, .step=1, .names=kOrigStyle,
      .found=1, .uiMax=7, .cfgMax=7, .schemeOnly=2 },
    { .key="Input.Pad.Fire", .label="Fire", .kind=ROW_PADBIND, .shownWhen="Input.PadPreset", .shownValue=3, .schemeOnly=1 },
    { .key="Input.Pad.Aim", .label="Aim", .kind=ROW_PADBIND, .shownWhen="Input.PadPreset", .shownValue=3, .schemeOnly=1 },
    { .key="Input.Pad.Use", .label="Use/back", .kind=ROW_PADBIND, .shownWhen="Input.PadPreset", .shownValue=3, .schemeOnly=1 },
    { .key="Input.Pad.Reload", .label="Reload", .kind=ROW_PADBIND, .shownWhen="Input.PadPreset", .shownValue=3, .schemeOnly=1 },
    { .key="Input.Pad.Crouch", .label="Crouch", .kind=ROW_PADBIND, .shownWhen="Input.PadPreset", .shownValue=3, .schemeOnly=1 },
    { .key="Input.Pad.NextWeapon", .label="Next weapon", .kind=ROW_PADBIND, .shownWhen="Input.PadPreset", .shownValue=3, .schemeOnly=1 },
    { .key="Input.Pad.PrevWeapon", .label="Previous weapon", .kind=ROW_PADBIND, .shownWhen="Input.PadPreset", .shownValue=3, .schemeOnly=1 },
    { .key="Input.Pad.Gadget", .label="Cycle gadget", .kind=ROW_PADBIND, .shownWhen="Input.PadPreset", .shownValue=3, .schemeOnly=1 },
    { .key="Input.Pad.Start", .label="Pause/start", .kind=ROW_PADBIND, .shownWhen="Input.PadPreset", .shownValue=3, .schemeOnly=1 },
    { .key="Input.PadLookInvertY", .label="Invert look", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    /* Wave A: per-stick deadzone + right-stick (natural-pitch) feel options. */
    { .key="Input.PadDeadzoneL", .label="Left stick deadzone", .kind=ROW_SLIDER, .step=300, .unit="%", .dispDiv=300 },
    { .key="Input.PadDeadzoneR", .label="Right stick deadzone", .kind=ROW_SLIDER, .step=300, .unit="%", .dispDiv=300 },
    { .key="Input.PadLookSensX", .label="Horizontal look sensitivity", .kind=ROW_SLIDER, .step=5 },
    { .key="Input.PadLookSensY", .label="Vertical look sensitivity", .kind=ROW_SLIDER, .step=5 },
    { .key="Input.PadLookSmooth", .label="Look smoothing", .kind=ROW_SLIDER, .step=1 },   /* D506: raw 0-10, shown as whole % */
    { .key="Input.PadSouthpaw", .label="Southpaw", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    { .key="Input.PadTriggerPct", .label="Trigger threshold", .kind=ROW_SLIDER, .step=1, .unit="%" },   /* D506: raw is already a percent */
    /* D401 item 3: one global Rumble Pak strength (0 = silent, 1 = full, dflt 0.5). */
    { .key="Input.RumbleScale", .label="Vibration", .kind=ROW_SLIDER, .step=0.05 },
    { .key="__ResetController", .label="Reset to defaults", .kind=ROW_ACTION },
    SEP_BACK(Controller),

    /* PD Key Bindings: one flat list (movement binds, separator, action binds).
     * Mouse buttons 1-5 share the visible slots with keys; pads stay fixed. */
    { .key="__HdrBindings", .label="KEY BINDINGS", .kind=ROW_HEADER },
    { .key="Input.Bind.Forward", .label="Move forward", .kind=ROW_BIND },
    { .key="Input.Bind.Back", .label="Move backward", .kind=ROW_BIND },
    { .key="Input.Bind.StrafeLeft", .label="Strafe left", .kind=ROW_BIND },
    { .key="Input.Bind.StrafeRight", .label="Strafe right", .kind=ROW_BIND },
    { .key="Input.Bind.TurnLeft", .label="Turn left", .kind=ROW_BIND },
    { .key="Input.Bind.TurnRight", .label="Turn right", .kind=ROW_BIND },
    SEP(K1),
    { .key="Input.Bind.Fire", .label="Fire", .kind=ROW_BIND },
    { .key="Input.Bind.Aim", .label="Aim", .kind=ROW_BIND },
    { .key="Input.Bind.Action", .label="Action/next weapon", .kind=ROW_BIND },
    { .key="Input.Bind.Cancel", .label="Use/back", .kind=ROW_BIND },
    { .key="Input.Bind.LeanLeft", .label="Lean left", .kind=ROW_BIND },
    { .key="Input.Bind.Start", .label="Pause/start", .kind=ROW_BIND },
    { .key="Input.Bind.Reload", .label="Reload", .kind=ROW_BIND },
    { .key="Input.Bind.Crouch", .label="Crouch", .kind=ROW_BIND },
    SEP(K2),
    { .key="__ResetBindings", .label="Reset to defaults", .kind=ROW_ACTION },
    SEP_BACK(Bindings),

    { .key="__HdrGame", .label="GAME", .kind=ROW_HEADER },
    /* Aim control (hold vs toggle the aim button) is a per-file watch row
     * like Auto-aim; the port honours the game's toggle. The game's own
     * "Look up/down" (Bond.Look) row was removed (D564): the port's mouse and
     * centred-pad look write the view directly and never consult it, so the
     * Mouse / Controller "Invert look" rows are the only inversion controls;
     * watchsettings.c pins the game's value to its factory default. */
    { .key="Bond.AimControl", .label="Aim control", .kind=ROW_TOGGLE,
      .names=kHold, .found=1, .uiMax=1, .cfgMax=1, .saveScoped=1 },
    { .key="Bond.AutoAim", .label="Auto-aim", .kind=ROW_TOGGLE,
      .names=kOnOff, .found=1, .uiMax=1, .cfgMax=1, .saveScoped=1 },
    { .key="Bond.LookAhead", .label="Look ahead", .kind=ROW_TOGGLE,
      .names=kOnOff, .found=1, .uiMax=1, .cfgMax=1, .saveScoped=1 },
    { .key="Bond.Sight", .label="Sight on screen", .kind=ROW_TOGGLE,
      .names=kOnOff, .found=1, .uiMax=1, .cfgMax=1, .saveScoped=1 },
    { .key="Bond.Ammo", .label="Ammo on screen", .kind=ROW_TOGGLE,
      .names=kOnOff, .found=1, .uiMax=1, .cfgMax=1, .saveScoped=1 },
    SEP(G1),
    /* Crouch mode lives here (PD keeps it in "Game"; ratified T1 gate 1).
     * D371/D374: GEPD layout default; crouch defaults to hold. */
    { .key="Input.CrouchMode", .label="Crouch mode", .kind=ROW_ENUM, .step=1, .names=kHold },
    /* D546: shown as VERTICAL degrees like PD's "Vert FOV" (60 = N64, the
     * middle of 30..90), 5-degree steps; storage stays Video.FovScale % (a
     * float, so each step is exact: 5 deg = 100/12 %). Was horizontal degrees
     * (D443/D357), which moved with the window aspect and never landed round.
     * Moved here from Graphics (PD puts Vert FOV in Game). */
    { .key="Video.FovScale", .label="Field of view", .kind=ROW_SLIDER, .step=100.0 / 12.0,
      .uiMin=50, .uiMax=150 },
    /* D468: off = AI awareness keeps the cartridge-widest view (16:9 at the
     * game's FOV) under ultrawide or a raised FOV; on = AI sees the full view. */
    /* D468: which part of the screen gameplay on-screen tests use; key kept for ini compat. */
    { .key="Game.AIWideView", .label="Gameplay view area", .kind=ROW_TOGGLE, .step=1, .names=kGameplayView },
    /* D232: the community "no damage flash" toggle (red/green hit-flash overlay in bondview2). */
    { .key="Game.NoHitFlash", .label="No hit flash", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    SEP(G2),
    { .key="Video.CrosshairHide", .label="Show crosshair", .kind=ROW_TOGGLE, .step=1, .names=kOnOffRev },
    { .key="Video.CrosshairPersistent", .label="Crosshair always on", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },   /* D436 */
    { .key="Video.CrosshairColor", .label="Crosshair color", .kind=ROW_ENUM, .step=1, .names=kCrosshairColor },
    { .key="Video.CrosshairRed", .label="Red", .kind=ROW_SLIDER, .step=5, .shownWhen="Video.CrosshairColor", .shownValue=8 },
    { .key="Video.CrosshairGreen", .label="Green", .kind=ROW_SLIDER, .step=5, .shownWhen="Video.CrosshairColor", .shownValue=8 },
    { .key="Video.CrosshairBlue", .label="Blue", .kind=ROW_SLIDER, .step=5, .shownWhen="Video.CrosshairColor", .shownValue=8 },
    { .key="Video.CrosshairSize", .label="Crosshair size", .kind=ROW_SLIDER, .step=5, .unit="%" },
    { .key="Video.CrosshairStyle", .label="Crosshair style", .kind=ROW_ENUM, .step=1, .names=kCrosshairStyle },
    { .key="Video.CrosshairAlpha", .label="Crosshair opacity", .kind=ROW_SLIDER, .step=5, .unit="%" },   /* D511 */
    { .key="Video.CrosshairHealthColor", .label="Crosshair color by health", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },   /* D511 */
    /* D226: scales the ammo counter, pickup/status text and dialogue. */
    { .key="Game.HudScale", .label="HUD scale", .kind=ROW_SLIDER, .step=5, .unit="%" },
    SEP(G3),
    /* D216/Game.SkipIntro: D408 -- with it on, a failed/aborted
     * mission skips the post-mission failure dossier (MENU_MISSION_FAILED). */
    { .key="Game.SkipIntro", .label="Skip intro", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    /* D257: everything-unlocked goodie. D442: a pure query-time/RAM override
     * (file2.c fileGetIsCheatUnlocked + fileIsStageUnlockedAtDifficulty, plus
     * the debug flags set by portAllUnlockedApply in main.c); no save bytes are
     * patched, read or written. Applies live (rowSetCommit). */
    { .key="Game.AllUnlocked", .label="All unlocked",
      .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    SEP(G4),
    /* D551: opt-in update check; the action row appears only once a newer release is known. */
    { .key="Game.CheckUpdates", .label="Check for updates", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    { .key="__UpdateOpen", .label="Update available: open page", .kind=ROW_ACTION },
    { .key="__ResetGame", .label="Reset to defaults", .kind=ROW_ACTION },
    /* Hub-only rows (.hub): shown after a separator at the bottom of the F10 hub,
     * hidden on the Game page. The front options screen still lists them here. */
    { .key="__SepHub", .label="", .kind=ROW_ACTION, .hub=1 },
    /* D293: in-game quit; not config-backed. */
    { .key="__QuitToDesktop", .label="Quit to Desktop", .kind=ROW_ACTION, .hub=1 },
    /* D443: orderly quit (D344) + relaunch, so "(restart)" rows like
     * Anti-aliasing can be applied without leaving the game by hand. */
    { .key="__RestartGame", .label="Restart Game", .kind=ROW_ACTION, .hub=1 },
    SEP_BACK(Game),
    /* D519: the Bond-file (profile) chooser. Not a section row: overlayUpdateVisible injects it as the
     * first row of every page that has profile-backed rows, on file select only (the old front-end
     * screen's top "Profile" row, D356). */
    { .key="__BondFile", .label="Profile", .kind=ROW_BOND_FILE },
};
/* Hub / tab order (PD's Extended hub: Video, Audio, Mouse, Controller, Game,
 * Key Bindings). Differs from the physical order so the hub rows stay with Game. */
static const char *const kRootOrder[] = {
    "__HdrVideo", "__HdrAudio", "__HdrMouse", "__HdrSelPlayerC", "__HdrGame", "__HdrBindings",
};
#define NUM_ROOTS ((int)(sizeof(kRootOrder) / sizeof(kRootOrder[0])))
#define NUM_ROWS ((int)(sizeof(rows) / sizeof(rows[0])))

static int rowIndexByKey(const char *key)
{
    for (int i = 0; i < NUM_ROWS; i++)
        if (strcmp(rows[i].key, key) == 0) return i;
    return -1;
}

/* D388: one shared page tree, used by both the front and F10 surfaces.
 * ROW_HEADER still delimits each section's reset range; link actions only
 * navigate and must never be treated as settings or the Quit action. */
int optionsRowHeaderParent(int i)
{
    if (i < 0 || i >= NUM_ROWS || rows[i].kind != ROW_HEADER) return -1;
    /* Select player is the Controller hub entry; its Controller page nests under it. */
    if (!strcmp(rows[i].key, "__HdrController")) return rowIndexByKey("__HdrSelPlayerC");
    return -1;
}

/* k-th top-level page in hub / tab order (PD: Video, Audio, Mouse, Controller,
 * Game, Key Bindings) as a rows[] header index, or -1 past the end. */
int optionsRootHeader(int k)
{
    if (k < 0 || k >= NUM_ROOTS) return -1;
    return rowIndexByKey(kRootOrder[k]);
}

static int isSepRow(const struct Row *r)
{
    return r->kind == ROW_ACTION && !strncmp(r->key, "__Sep", 5);
}

/* On/Off rows draw as PD checkboxes; named two-way rows (Reverse/Upright,
 * Hold/Toggle, the restart row) keep their value text. */
static int overlayRowIsCheckbox(const struct Row *r)
{
    return r->kind == ROW_TOGGLE && (r->names == kOnOff || r->names == kOnOffRev);
}

static int isBackRow(const struct Row *r)
{
    return r->kind == ROW_ACTION && !strncmp(r->key, "__Back", 6);
}

/* "Select player" rows ("__SelPlayerC2"): the digit is the seat, 1-based. */
static int selPlayerSeat(const struct Row *r)
{
    if (r->kind != ROW_ACTION || strncmp(r->key, "__SelPlayer", 11)) return -1;
    return r->key[12] - '1';
}

int optionsRowChildHeader(int i)
{
    if (i < 0 || i >= NUM_ROWS) return -1;
    const char *key = rows[i].key;
    if (!strncmp(key, "__SelPlayerC", 12)) return rowIndexByKey("__HdrController");
    return -1;
}

static int  s_inited = 0;
static volatile int s_open = 0;
/* D519: the overlay reopens where it was closed. Identity = rows[] indices (the table is
 * static; only visibility changes between front end / level / scheme), checked on reopen. */
static int s_memValid = 0, s_memSec = -1, s_memRow = -1, s_memScroll = 0, s_memSeat = 0;
static int s_seedPrev = 0;   /* D519: first handled frame after an open adopts the held buttons as "previous" */
static int  s_sel = 0;        /* selection, index into s_visIdx (visible list) */
static int  s_section = -1;   /* -1 = category list; otherwise rows[] header index */
static SDL_atomic_t s_backPending; /* ESC is received on the host thread */

/* Visible-row list: category headers on the root page, or the active
 * section's Back header and content (including conditional rows). Built
 * on the scheduler thread; s_scroll is the first displayed entry. */
static int  s_visIdx[NUM_ROWS];
static int  s_visN = 0;
static int  s_scroll = 0;
static SDL_atomic_t s_wheelPending;   /* D314: host-thread wheel notches */
static int  s_mouseActive = 0;   /* last input was the mouse: hover cues a row + the hint line hides */
static int  s_hover = -1;        /* visible-list index under the pointer while s_mouseActive (cue only, never selection) */
static int  s_rmbBlock = 0;      /* D544: right button held across a binding capture; re-armed on release */

/* D383: SDL_KEYDOWN belongs to videoPumpEvents (host), while both menu
 * controllers consume it on their own threads. Only the one-shot scancode
 * crosses threads; binding strings/row state remain menu-thread owned. */
static SDL_atomic_t s_bindCaptureActive;
static SDL_atomic_t s_bindPadCancelHold; /* swallow B until released after cancel */
static SDL_SpinLock s_bindCaptureLock;
static int s_bindPending = -1, s_bindCaptureRow = -1, s_bindError = 0;
static SDL_atomic_t s_bindHold; /* captured key/mouse button, shared with pad poll */

/* D469: gamepad capture (Controller page). Same modal flag as the key/mouse
 * capture (so F10/Back/Start cannot close the page mid-capture), but driven
 * by the edited seat's pad. Arming: nothing is accepted until every source
 * is released (the A press that opened the modal must not bind itself).
 * Cancel: Esc, tap B, or tap Back (either pad 0 or the edited pad), so a
 * controller-only user is never trapped (D395). Clear: Delete, or HOLD Back.
 * B itself is bindable by HOLDING it (a tap cancels). */
static int s_padCapMode = 0, s_padCapRow = -1, s_padCapSeat = 0, s_padCapArmed = 0, s_padCapSwallow = 0;
static uint64_t s_padCapBackT = 0, s_padCapBT = 0;
#define PADCAP_HOLD_US 600000

static void bindingBegin(struct Row *r)
{
    if (r && r->found && r->kind == ROW_PADBIND) {
        SDL_AtomicLock(&s_bindCaptureLock);
        s_bindPending = -1;
        SDL_AtomicUnlock(&s_bindCaptureLock);
        s_bindError = 0;
        s_padCapMode = 1;
        s_padCapRow = (int)(r - rows);
        s_padCapSeat = s_padSeat;
        s_padCapArmed = 0;
        s_padCapSwallow = 0;
        s_padCapBackT = s_padCapBT = 0;
        s_bindCaptureRow = s_padCapRow;
        SDL_AtomicSet(&s_bindPadCancelHold, 0);
        SDL_AtomicSet(&s_bindHold, SDL_SCANCODE_UNKNOWN);
        SDL_AtomicSet(&s_bindCaptureActive, 1);
        return;
    }
    s_padCapMode = 0;
    if (!r || !r->found || r->kind != ROW_BIND) return;
    SDL_AtomicLock(&s_bindCaptureLock);
    s_bindPending = -1;
    SDL_AtomicUnlock(&s_bindCaptureLock);
    s_bindCaptureRow = (int)(r - rows);
    s_bindError = 0;
    SDL_AtomicSet(&s_bindPadCancelHold, 0);
    SDL_AtomicSet(&s_bindHold, SDL_SCANCODE_UNKNOWN);
    SDL_AtomicSet(&s_bindCaptureActive, 1);
}

int optionsBindingCaptureActive(void)
{
    return SDL_AtomicGet(&s_bindCaptureActive) != 0;
}

int optionsBindingInputBlocked(void)
{
    if (optionsBindingCaptureActive()) return 1;
    int code = SDL_AtomicGet(&s_bindHold);
    if (code > SDL_NUM_SCANCODES && code <= INPUT_BIND_MOUSE(5))
        return (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(code - SDL_NUM_SCANCODES)) != 0;
    return code > SDL_SCANCODE_UNKNOWN && code < SDL_NUM_SCANCODES &&
           SDL_GetKeyboardState(NULL)[code];
}

/* Called before F10/ESC/screenshot handling on the SDL event host thread.
 * Do not bind reserved system shortcuts or gamepad events. */
int optionsBindingKeyDown(const SDL_KeyboardEvent *ev)
{
    if (!optionsBindingCaptureActive()) return 0;
    if ((ev->keysym.sym == SDLK_F4 && (ev->keysym.mod & KMOD_ALT))) return 0;
    if (ev->repeat) return 1;
    SDL_Scancode sc = ev->keysym.scancode;
    if (s_padCapMode) {   /* D469: pad capture takes only Esc (cancel) / Delete (clear) from the keyboard */
        if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_DELETE) {
            SDL_AtomicLock(&s_bindCaptureLock);
            if (s_bindPending < 0) s_bindPending = (int)sc;
            SDL_AtomicUnlock(&s_bindCaptureLock);
        }
        return 1;
    }
    if (sc == SDL_SCANCODE_F10 || sc == SDL_SCANCODE_F12 ||
        sc == SDL_SCANCODE_UNKNOWN) return 1;
    SDL_AtomicLock(&s_bindCaptureLock);
    if (s_bindPending < 0) s_bindPending = (int)sc;
    SDL_AtomicUnlock(&s_bindCaptureLock);
    return 1;
}

/* Keep menu clicking and click-to-lock out of a mouse-binding capture. */
int optionsBindingMouseDown(const SDL_MouseButtonEvent *ev)
{
    if (!optionsBindingCaptureActive()) return 0;
    if (s_padCapMode) return 1;   /* D469: swallow clicks during a pad capture */
    if (ev->button >= 1 && ev->button <= 5) {
        SDL_AtomicLock(&s_bindCaptureLock);
        if (s_bindPending < 0) s_bindPending = INPUT_BIND_MOUSE(ev->button);
        SDL_AtomicUnlock(&s_bindCaptureLock);
    }
    return 1;
}

/* The capturing screen calls this once per input tick. Return 1 while the
 * modal is active and through the captured key's release, so it cannot
 * simultaneously trigger menu navigation or a second action. */
static void padCapFinish(void)
{
    SDL_AtomicSet(&s_bindCaptureActive, 0);
    s_padCapSwallow = 1;   /* swallow until every button is released again */
}

static int padCaptureTick(void)
{
    int seat = s_padCapSeat;
    unsigned raw = inputPadHeldSources(seat);
    int back = inputPadButton(seat, SDL_CONTROLLER_BUTTON_BACK) || inputPadButton(inputOverlayOwnerPad(), SDL_CONTROLLER_BUTTON_BACK);
    int bHeld = inputPadButton(seat, SDL_CONTROLLER_BUTTON_B) || inputPadButton(inputOverlayOwnerPad(), SDL_CONTROLLER_BUTTON_B);
    int act = inputPadActionForKey(rows[s_padCapRow].key);
    uint64_t now = sysGetMicroseconds();

    SDL_AtomicLock(&s_bindCaptureLock);
    int key = s_bindPending;
    s_bindPending = -1;
    SDL_AtomicUnlock(&s_bindCaptureLock);
    if (key == SDL_SCANCODE_ESCAPE) { padCapFinish(); return 1; }
    if (key == SDL_SCANCODE_DELETE) {
        inputPadBindingSet(seat, act, rows[s_padCapRow].bindSlot, -1);
        configSave();
        padCapFinish();
        return 1;
    }
    if (!s_padCapArmed) {   /* release-before-capture arming */
        if (!raw && !back && !bHeld) s_padCapArmed = 1;
        return 1;
    }
    /* Back: tap = cancel, hold = clear */
    if (back) {
        if (!s_padCapBackT) s_padCapBackT = now;
        if (now - s_padCapBackT >= PADCAP_HOLD_US) {
            inputPadBindingSet(seat, act, rows[s_padCapRow].bindSlot, -1);
            configSave();
            sysLogPrintf(LOG_INFO, "pad bind: %s seat %d slot %d cleared", rows[s_padCapRow].key, seat + 1, rows[s_padCapRow].bindSlot + 1);
            padCapFinish();
        }
        return 1;
    } else if (s_padCapBackT) {
        padCapFinish();   /* released early: cancel */
        return 1;
    }
    /* B: tap = cancel, hold = bind B (to the edited pad only) */
    if (bHeld) {
        if (!s_padCapBT) s_padCapBT = now;
        if (now - s_padCapBT >= PADCAP_HOLD_US && (raw & 2u)) {
            inputPadBindingSet(seat, act, rows[s_padCapRow].bindSlot, 1 /* b */);
            configSave();
            sysLogPrintf(LOG_INFO, "pad bind: %s seat %d slot %d = b", rows[s_padCapRow].key, seat + 1, rows[s_padCapRow].bindSlot + 1);
            padCapFinish();
        }
        return 1;
    } else if (s_padCapBT) {
        padCapFinish();   /* tap: cancel */
        return 1;
    }
    for (int src = 0; src < inputPadSourceCount(); src++) {
        if (raw & (1u << src)) {
            inputPadBindingSet(seat, act, rows[s_padCapRow].bindSlot, src);
            configSave();
            sysLogPrintf(LOG_INFO, "pad bind: %s seat %d slot %d = %s", rows[s_padCapRow].key, seat + 1,
                         rows[s_padCapRow].bindSlot + 1, inputPadSourceName(src));
            padCapFinish();
            return 1;
        }
    }
    return 1;
}

int optionsBindingCaptureTick(void)
{
    if (s_padCapMode) {   /* D469 */
        if (optionsBindingCaptureActive()) return padCaptureTick();
        if (s_padCapSwallow) {
            unsigned raw = inputPadHeldSources(s_padCapSeat);
            if (raw || inputPadButton(inputOverlayOwnerPad(), SDL_CONTROLLER_BUTTON_B) || inputPadButton(inputOverlayOwnerPad(), SDL_CONTROLLER_BUTTON_A) ||
                inputPadButton(inputOverlayOwnerPad(), SDL_CONTROLLER_BUTTON_X) || inputPadButton(inputOverlayOwnerPad(), SDL_CONTROLLER_BUTTON_BACK) ||
                inputPadButton(s_padCapSeat, SDL_CONTROLLER_BUTTON_BACK))
                return 1;
            s_padCapSwallow = 0;
        }
        s_padCapMode = 0;
        return 0;
    }
    /* D395: capture only accepts keyboard/mouse. A controller B must be
     * able to cancel a modal entered with a keyboard/mouse; swallow B until
     * release so that the same press cannot also back out of the page. */
    if (SDL_AtomicGet(&s_bindPadCancelHold)) {
        if (!inputPadButton(inputOverlayOwnerPad(), SDL_CONTROLLER_BUTTON_B))
            SDL_AtomicSet(&s_bindPadCancelHold, 0);
        return 1;
    }
    if (optionsBindingCaptureActive()) {
        if (inputPadButton(inputOverlayOwnerPad(), SDL_CONTROLLER_BUTTON_B)) {
            SDL_AtomicLock(&s_bindCaptureLock);
            s_bindPending = -1;
            SDL_AtomicUnlock(&s_bindCaptureLock);
            SDL_AtomicSet(&s_bindCaptureActive, 0);
            SDL_AtomicSet(&s_bindPadCancelHold, 1);
            return 1;
        }
        SDL_AtomicLock(&s_bindCaptureLock);
        int sc = s_bindPending;
        s_bindPending = -1;
        SDL_AtomicUnlock(&s_bindCaptureLock);
        if (sc >= 0) {
            struct Row *r = &rows[s_bindCaptureRow];
            if (sc != SDL_SCANCODE_ESCAPE) {
                int chosen = sc == SDL_SCANCODE_DELETE
                    ? SDL_SCANCODE_UNKNOWN : sc;
                if (!inputBindingSetSlot(r->key, r->bindSlot, chosen)) {
                    s_bindError = 1; /* oversized name: leave ini unchanged */
                } else {
                    configSave();
                    sysLogPrintf(LOG_INFO, "bind capture: %s slot %d = %s",
                                 r->key, r->bindSlot + 1,
                                 inputBindingSlot(r->key, r->bindSlot));
                }
            }
            SDL_AtomicSet(&s_bindHold, sc);
            SDL_AtomicSet(&s_bindCaptureActive, 0);
        }
        return 1;
    }
    int hold = SDL_AtomicGet(&s_bindHold);
    if (hold != SDL_SCANCODE_UNKNOWN) {
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        if (hold > SDL_NUM_SCANCODES && hold <= INPUT_BIND_MOUSE(5)) {
            if (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(hold - SDL_NUM_SCANCODES)) return 1;
        } else if (hold > SDL_SCANCODE_UNKNOWN && hold < SDL_NUM_SCANCODES && ks[hold]) return 1;
        SDL_AtomicSet(&s_bindHold, SDL_SCANCODE_UNKNOWN);
    }
    return 0;
}

/* D213: optional on-screen FPS readout (PD parity: Video.DisplayFPS).
 * Drawn top-right whenever enabled, independent of the F10 panel. */
static int      s_showFps = 0;
static char     s_fpsText[16] = "";

PD_CONSTRUCTOR static void overlayConfigInit(void)
{
    configRegisterInt("Video.DisplayFPS", &s_showFps, 0, 1);
}

/* Sampled once per emitted frame; recomputes the string every ~0.5 s. */
static void fpsTick(void)
{
    static uint64_t winStartUs = 0;
    static int      frames = 0;

    uint64_t nowUs = sysGetMicroseconds();
    if (winStartUs == 0) {
        winStartUs = nowUs;
        return;
    }
    frames++;
    uint64_t dtUs = nowUs - winStartUs;
    if (dtUs >= 500000) {
        int fps = (int)((double)frames * 1e6 / (double)dtUs + 0.5);
        snprintf(s_fpsText, sizeof(s_fpsText), "%d FPS", fps);
        winStartUs = nowUs;
        frames = 0;
    }
}

/* Layout (game 2D pixel space = viGetX() x viGetY(), ~320x240). Shared by the
 * emit path and the mouse hit-testing in optionsOverlayHandleInput().
 * PD-style dialog (menu PD parity R4): sized to its content and centred, one
 * LINEHEIGHT-class title bar, flat translucent body, no footer (the hint line
 * sits under the box). Rows are OV_LINE units apart; the label face is Zurich
 * Bold (GE's only mixed-case face), see bodyFont(). */
#define OV_LINE   14
#define OV_TEXT_PCT 78   /* label face scale, % (see drawBody) */
#define OV_TITLE_H 13   /* PD title bar: one text line + 2 */
#define OV_PAD_TOP 3
#define OV_PAD_BOT 4
#define OV_HINT_H 53    /* D507: row description + two key-hint lines under the box (reserved when sizing) */
#define OV_TEXT_DY 1    /* label baseline offset inside a row */
struct OvLayout {
    s32 left, right, top, titleB, contentY, bottom, hintY;
    s32 labelX, valueR, barX0, barX1, maxRows;
};

static double s_scrollF = 0.0;   /* eased (drawn) scroll position, in rows; s_scroll is the target */
static int    s_scrollSnap = 1;  /* snap s_scrollF to s_scroll on the next emit (page change) */

/* D510: the overlay lays out on a canonical 320-wide canvas, whatever the
 * game's current VI canvas is (320x240 in a level, 440x330 on the front
 * end); fast3d scales the overlay DL's rects by ovScale() (gfx_set_overlay_scale)
 * so its on-screen size depends only on the window. */
extern s32 portHudScalePercent(void);   /* video.c: Game.HudScale %, 75..150 (D226) */
/* D512: Game.HudScale also scales the whole dialog (box, text, FPS counter):
 * the layout canvas shrinks by 100/hud and fast3d scales the DL back up by
 * hud/100, so the box stays centred, fills the same window area share, and a
 * taller scale just means fewer visible rows (scrolls as before). 100 % is
 * the S2 mapping exactly. */
static s32 ovHud(void)
{
    s32 h = portHudScalePercent();
    return h < 75 ? 75 : (h > 150 ? 150 : h);
}
static s32 ovW(void) { return (s32)lround(320.0 * 100.0 / (double)ovHud()); }
static s32 ovH(void)
{
    s32 x = viGetX(), y = viGetY();
    double h = x > 0 ? (double)y * 320.0 / (double)x : 240.0;
    return (s32)lround(h * 100.0 / (double)ovHud());
}
static float ovScale(void)
{
    s32 x = viGetX();
    return (x > 0 ? (float)x / 320.0f : 1.0f) * (float)ovHud() / 100.0f;
}
extern void gfx_set_overlay_scale(float s);

static struct OvLayout overlayLayout(void)
{
    struct OvLayout o;
    s32 w = ovW(), h = ovH();
    s32 cardW = s_section < 0 ? 196 : w - 16;   /* PD hub dialogs are narrow; settings pages wide */
    if (cardW > 304) cardW = 304;
    o.maxRows = (h - 2 * 4 - OV_HINT_H - OV_TITLE_H - 1 - OV_PAD_TOP - OV_PAD_BOT) / OV_LINE;
    if (o.maxRows < 4) o.maxRows = 4;
    int contentN = s_visN - (s_section >= 0 ? 1 : 0);   /* a section's title is not a row */
    int drawn = contentN < o.maxRows ? contentN : o.maxRows;
    s32 boxH = OV_TITLE_H + 1 + OV_PAD_TOP + drawn * OV_LINE + OV_PAD_BOT;
    o.left = (w - cardW) / 2;
    o.right = o.left + cardW;
    o.top = (h - boxH) / 2;
    if (o.top + boxH + 4 + OV_HINT_H > h) o.top = h - OV_HINT_H - 4 - boxH;   /* tall pages: keep the hint line on screen */
    if (o.top < 4) o.top = 4;
    o.titleB = o.top + OV_TITLE_H;
    o.contentY = o.titleB + 1 + OV_PAD_TOP;
    o.bottom = o.top + boxH;
    o.hintY = o.bottom + 3;
    o.labelX = o.left + 10;
    o.valueR = o.right - 10;
    o.barX1 = o.valueR - 54;
    o.barX0 = o.barX1 - 66;
    return o;
}

static int maxVisibleRows(void) { return overlayLayout().maxRows; }

/* Shared with slider dragging and its render path. */
static void sliderBarSpan(s32 *x0, s32 *x1)
{
    struct OvLayout o = overlayLayout();
    *x0 = o.barX0;
    *x1 = o.barX1;
}

/* Visible position or -1. Follows the drawn (eased) scroll so a click lands on
 * the row it visually covers while the list is still gliding. Rows own their
 * whole OV_LINE band; hits outside the body are rejected. */
static int overlayRowAtY(double ox, double oy)
{
    struct OvLayout o = overlayLayout();
    if (ox < o.left + 1 || ox >= o.right - 1) return -1;
    if (oy < o.contentY || oy >= o.contentY + o.maxRows * OV_LINE) return -1;
    int p0 = (int)s_scrollF;
    for (int p = p0; p <= p0 + o.maxRows && p < s_visN; p++) {
        double y = o.contentY + (p - s_scrollF) * OV_LINE;
        if (y < o.contentY - 0.5 && y + OV_LINE <= o.contentY) continue;
        if (oy >= y && oy < y + OV_LINE) return p;
    }
    return -1;
}

/* D556: mouse scrollbar. A thin track in the card's right margin (the value text
 * ends at valueR = right - 10, so nothing is overlapped at any HUD scale), drawn
 * and hit-tested only while the page overflows (the same condition as the "^ v"
 * title marker). The thumb follows the drawn (eased) scroll. Returns 0 when the
 * page does not overflow. */
static int overlayStepSel(int pos, int dir);
struct OvScrollbar { s32 x0, x1, hx0, hx1, ty0, ty1, th0, th1; int first, span; };
static int overlayScrollbar(struct OvScrollbar *sb)
{
    struct OvLayout o = overlayLayout();
    int first = s_section >= 0 ? 1 : 0;
    int contentN = s_visN - first;
    if (contentN <= o.maxRows) return 0;
    sb->first = first;
    sb->span = contentN - o.maxRows;   /* scroll range, in rows */
    sb->x0 = o.right - 7;  sb->x1 = o.right - 4;      /* drawn */
    sb->hx0 = o.right - 9; sb->hx1 = o.right - 1;     /* hit area (a little wider) */
    sb->ty0 = o.contentY;  sb->ty1 = o.contentY + o.maxRows * OV_LINE;
    s32 trackH = sb->ty1 - sb->ty0;
    s32 thumbH = (s32)lround((double)trackH * o.maxRows / contentN);
    if (thumbH < 10) thumbH = 10;
    double f = (s_scrollF - first) / (double)sb->span;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    sb->th0 = sb->ty0 + (s32)lround(f * (double)(trackH - thumbH));
    sb->th1 = sb->th0 + thumbH;
    return 1;
}

/* Scroll the window to `target` (first displayed entry) from the scrollbar, then pull the
 * selection into view the way the wheel does (overlayUpdateScroll would otherwise snap the
 * window straight back to the old selection). */
static void overlayScrollTo(int target)
{
    struct OvLayout o = overlayLayout();
    int first = s_section >= 0 ? 1 : 0;
    int maxScroll = s_visN - o.maxRows;
    if (target > maxScroll) target = maxScroll;
    if (target < first) target = first;
    s_scroll = target;
    int lo = s_scroll, hi = s_scroll + o.maxRows - 1;
    if (s_sel < lo) {
        s_sel = lo;
        if (isSepRow(&rows[s_visIdx[s_sel]])) s_sel = overlayStepSel(s_sel, +1);
    } else if (s_sel > hi) {
        s_sel = hi;
        if (isSepRow(&rows[s_visIdx[s_sel]])) s_sel = overlayStepSel(s_sel, -1);
    }
    s_mouseActive = 1;
}

/* Opt-in geometry check on both the first and last scroll windows: every
 * drawn row must be hit by its own band (top and bottom edge), and the gutter
 * outside the card must never hit. */
static int overlayHitCheck(void)
{
    struct OvLayout o = overlayLayout();
    int first = s_section >= 0 ? 1 : 0;
    int lastScroll = s_visN - first > o.maxRows ? s_visN - o.maxRows : first;
    int saved = s_scroll; double savedF = s_scrollF; int bad = 0;
    for (int scroll = first; scroll <= lastScroll; scroll += lastScroll - first ? lastScroll - first : 1) {
        s_scroll = scroll; s_scrollF = scroll;
        int last = scroll + o.maxRows < s_visN ? scroll + o.maxRows : s_visN;
        for (int p = scroll; p < last; p++) {
            double y = o.contentY + (p - scroll) * OV_LINE;
            if (overlayRowAtY(o.labelX, y + 1) != p ||
                overlayRowAtY(o.labelX, y + OV_LINE - 1) != p ||
                overlayRowAtY(o.left - 1, y + 5) != -1) bad++;
        }
    }
    s_scroll = saved; s_scrollF = savedF;
    return bad;
}

/* Rebuild the visible-row list and keep the selection in range. */
/* Move through the current category list or section (including its Back row). */
static int overlayStepSel(int pos, int dir)
{
    int first = s_section >= 0 ? 1 : 0;   /* idx 0 of a section is its title */
    int q = pos;
    for (;;) {
        q += dir;
        if (q < first || q >= s_visN) return pos;
        if (!isSepRow(&rows[s_visIdx[q]])) return q;   /* separators are not selectable */
    }
}

static void overlayUpdateVisible(void)
{
    s_visN = 0;
    for (int i = 0; i < NUM_ROWS; i++) {
        /* Root: the six pages, built below. Controller's seat page opens its
         * Controller page through the Player N link rows. */
        if (s_section < 0) {
            continue;   /* root list is built below in hub order */
        } else if (i == s_section) {
            s_visIdx[s_visN++] = i; /* title (not selectable) */
        } else if (i > s_section) {
            if (rows[i].kind == ROW_HEADER) break;
            if (rows[i].hub) continue;   /* hub-only rows live on the root list */
            if (rows[i].kind != ROW_BOND_FILE &&
                !(rows[i].hidePtr && *rows[i].hidePtr) &&
                (!rows[i].showPtr || *rows[i].showPtr == rows[i].shownValue) &&
                rowSchemeOk(&rows[i]))
                s_visIdx[s_visN++] = i;
        }
    }
    if (s_section >= 0 && current_menu == MENU_FILE_SELECT) {   /* D519: profile chooser, first row of profile-backed pages */
        /* D556: the chooser sits directly above the first profile-backed row, never above the
         * row that decides whether that row exists (Controller page: Control style stays first,
         * the Original-only Profile + Original layout rows follow it). */
        int firstBond = -1, chooser = -1;
        for (int q = 1; q < s_visN && firstBond < 0; q++)
            if (watchSettingsFieldForKey(rows[s_visIdx[q]].key) >= 0) firstBond = q;
        for (int i = 0; i < NUM_ROWS; i++) if (rows[i].kind == ROW_BOND_FILE) chooser = i;
        if (firstBond >= 1 && chooser >= 0 && s_visN < (int)(sizeof(s_visIdx) / sizeof(s_visIdx[0]))) {
            memmove(&s_visIdx[firstBond + 1], &s_visIdx[firstBond], (size_t)(s_visN - firstBond) * sizeof(s_visIdx[0]));
            s_visIdx[firstBond] = chooser;
            s_visN++;
        }
    }
    if (s_section < 0) {   /* the six pages in PD hub order, then hub-only rows (separator, Restart, Quit) */
        for (int k = 0; k < NUM_ROOTS; k++)
            s_visIdx[s_visN++] = optionsRootHeader(k);
        for (int i = 0; i < NUM_ROWS; i++)
            if (rows[i].hub) s_visIdx[s_visN++] = i;
    }
    if (s_visN == 0) {   /* cannot happen (toggles have no hide source) */
        s_visIdx[s_visN++] = 0;
    }
    if (s_sel < (s_section >= 0 ? 1 : 0)) {
        s_sel = s_section >= 0 ? 1 : 0;
    }
    if (s_sel >= s_visN) {
        s_sel = s_visN - 1;
    }
}

/* Keep the selected row on screen: shift the window ONLY when the selection
 * is actually outside it, by the minimum amount needed.
 *
 * D304 fix: this used to unconditionally set `s_scroll = s_sel - (maxV-1)`
 * -- i.e. bottom-anchor the selected row -- on every single call, including
 * every mouse click. A click on a row already visible (anywhere but the very
 * last slot) still forced the whole list to re-scroll so that row landed at
 * the bottom, shifting every row's on-screen position for the rest of that
 * same frame -- so whatever row the user then saw/clicked at that same
 * screen position was a DIFFERENT (usually the next, i.e. "below") setting.
 * Reported as "the F10 menu keeps jumping to the setting below when I left
 * click" (user, 2026-09-18) -- clicking any row not already at the bottom
 * slot reproduced it every time. Fix: only move the window when the
 * selection is above the top or below the bottom of the current view. */
static void overlayUpdateScroll(void)
{
    int maxV = maxVisibleRows();
    int first = s_section >= 0 ? 1 : 0; /* section header stays pinned */
    int count = s_visN - first;
    if (count <= maxV) {
        s_scroll = first;
        return;
    }
    if (s_scroll < first) s_scroll = first;
    if (s_sel >= first && s_sel < s_scroll) {
        s_scroll = s_sel;
    } else if (s_sel > s_scroll + maxV - 1) {
        s_scroll = s_sel - (maxV - 1);
    }
    if (s_scroll > s_visN - maxV) s_scroll = s_visN - maxV;
}

static void ddClose(void);
static void ddOpen(int rowIdx);
static void ddConfirm(int k);
static void rowAdjust(struct Row *r, int dir);
static int  s_ddRow;
/* D563: nav-input latch. Armed on open / page enter / back; the next poll snapshots
 * every nav input held then, and each stays ignored until released once. */
static int s_latchArm = 0;
static unsigned s_latchMask = 0;
static void overlayOpenHeader(int hdr)
{
    s_latchArm = 1;
    ddClose();
    s_section = hdr;
    s_sel = 1;
    s_scroll = 0;
    s_scrollSnap = 1;
    optionsResetClear();
    overlayUpdateVisible();
    /* Select player pages open on the seat last edited. */
    if (!strncmp(rows[hdr].key, "__HdrSelPlayer", 14) && s_padSeat >= 0 && s_padSeat < 4 &&
        1 + s_padSeat < s_visN)
        s_sel = 1 + s_padSeat;
    overlayUpdateScroll();
    sysLogPrintf(LOG_INFO, "optionsoverlay: opened %s", rows[hdr].label);
}

/* D518: pad presets are per seat. The "Layout preset" row and the Custom-only
 * bind rows (shownWhen Input.PadPreset) must follow the selected seat, so
 * re-point them whenever the seat changes; input.c reads each seat's own
 * value, so editing another player's preset can't touch this one. */
static void padSetSeat(int seat)
{
    if (seat < 0 || seat > 3) return;
    s_padSeat = seat;
    watchSettingsSetControlSeat(seat);
    int *pp = inputPadPresetPtr(seat);
    for (int i = 0; i < NUM_ROWS; i++) {
        if (!strcmp(rows[i].key, "Input.PadPreset")) rows[i].ptr = pp;
        else if (rows[i].kind == ROW_PADBIND) rows[i].showPtr = pp;
    }
}

/* A "Player N" row on a Select player page picks the seat before it opens the
 * Controller / Key bindings page; every other link just opens its header. */
static void overlayOpenLink(int row)
{
    int seat = selPlayerSeat(&rows[row]);
    if (seat >= 0) padSetSeat(seat);
    overlayOpenHeader(optionsRowChildHeader(row));
}

static void overlayBackOne(void)
{
    int child = s_section;
    if (child < 0) return;
    s_latchArm = 1;
    ddClose();
    s_section = optionsRowHeaderParent(child);
    s_sel = 0;
    s_scroll = 0;
    s_scrollSnap = 1;
    optionsResetClear();
    overlayUpdateVisible();
    /* Re-select the link that opened this child (or the root category).
     * This works for both keyboard and pointer navigation. */
    for (int p = 0; p < s_visN; p++) {
        int i = s_visIdx[p];
        if ((s_section < 0 && i == child) ||
            (s_section >= 0 && optionsRowChildHeader(i) == child &&
             (selPlayerSeat(&rows[i]) < 0 || selPlayerSeat(&rows[i]) == s_padSeat))) {
            s_sel = p;
            break;
        }
    }
    overlayUpdateScroll();
    sysLogPrintf(LOG_INFO, "optionsoverlay: back from %s to %s",
                 rows[child].label, s_section < 0 ? "categories" : rows[s_section].label);
}

/* ------------------------------------------------------------------------ */

static void resolveCb(const char *key, int type, void *ptr, double min, double max,
                      double step, const char *label, const char *const *names,
                      void *ctx)
{
    (void)step; (void)label; (void)names; (void)ctx;
    for (int i = 0; i < NUM_ROWS; i++) {
        if (strcmp(rows[i].key, key) == 0) {
            rows[i].found  = 1;
            rows[i].type   = type;
            rows[i].ptr    = ptr;
            rows[i].cfgMin = min;
            rows[i].cfgMax = max;
            return;
        }
    }
}

static void presetProbe(void);   /* D440 GE_PRESETPROBE, below */

/* Test hook (D558): applies the GE_OPTIONSOVERLAY / _SECTION / _SELECTBACK / _DROPDOWN /
 * _SELECT=<row key> request. Called from init and, with GE_OPTIONSOVERLAY_ATFRAME=<n>, again
 * at emit frame n so it survives the level load (page state resets on load). */
static void overlayApplyTestRequest(const char *e)
{
    s_open = 1;
        /* Diagnostic screenshots: 1 = root, 2..7 = the six pages in hub order;
         * GE_OPTIONSOVERLAY_SECTION=__HdrController etc. targets nested pages. */
        int page = atoi(e) - 2;
        if (page >= 0) {
            if (optionsRootHeader(page) >= 0) overlayOpenHeader(optionsRootHeader(page));
        }
        /* Isolated screenshot/probe of a nested page, never a menu mode. */
        const char *section = getenv("GE_OPTIONSOVERLAY_SECTION");
        int nested = section ? rowIndexByKey(section) : -1;
        if (nested >= 0 && rows[nested].kind == ROW_HEADER)
            overlayOpenHeader(nested);
        /* Screenshot-only selection state for checking the pinned Back
         * row against a normal selected setting on the same page. */
        if (s_section >= 0 && getenv("GE_OPTIONSOVERLAY_SELECTBACK"))
            s_sel = s_visN - 1;   /* Back is the last row of every page */
        /* Screenshot-only: open a dropdown popup (GE_OPTIONSOVERLAY_DROPDOWN=<row key>) and focus its row. */
        {
            const char *ddk = getenv("GE_OPTIONSOVERLAY_DROPDOWN");
            int ddi = ddk ? rowIndexByKey(ddk) : -1;
            if (ddi >= 0) {
                for (int q = 0; q < s_visN; q++) if (s_visIdx[q] == ddi) s_sel = q;
                ddOpen(ddi);
            }
        }
    {   /* D558: GE_OPTIONSOVERLAY_SELECT=<row key> selects that row on the open page. */
        const char *sk = getenv("GE_OPTIONSOVERLAY_SELECT");
        int si = sk ? rowIndexByKey(sk) : -1;
        if (si >= 0) {
            overlayUpdateVisible();
            for (int q = 0; q < s_visN; q++) if (s_visIdx[q] == si) s_sel = q;
        }
    }
}

static void overlayInit(void)
{
    if (s_inited) {
        return;
    }
    s_inited = 1;

    /* Publish display metadata so config.c / future consumers can see it,
     * without config.c knowing any specific key. */
    for (int i = 0; i < NUM_ROWS; i++) {
        configSetOptionMeta(rows[i].key, rows[i].label, rows[i].step, rows[i].names);
    }
    configForEachOption(resolveCb, NULL);

    for (int i = 0; i < NUM_ROWS; i++) {
        if (rows[i].kind == ROW_RES || rows[i].kind == ROW_ACTION ||
            rows[i].kind == ROW_HEADER || rows[i].kind == ROW_BOND_FILE ||
            rows[i].kind == ROW_PADSEAT || rows[i].kind == ROW_PADBIND || rows[i].kind == ROW_AUDIODEV ||
            watchSettingsFieldForKey(rows[i].key) >= 0) {
            rows[i].found = 1;   /* not config-backed */
            continue;
        }
        if (!rows[i].found) {
            sysLogPrintf(LOG_WARNING, "optionsoverlay: option '%s' not registered",
                         rows[i].key);
        }
    }

    /* Resolve the hidden-while-on sources (manual % rows vs their auto
     * toggles), then build the initial visible list. */
    for (int i = 0; i < NUM_ROWS; i++) {
        for (int j = 0; j < NUM_ROWS; j++) {
            if (!rows[j].found) continue;
            if (rows[i].hiddenIfOn &&
                strcmp(rows[j].key, rows[i].hiddenIfOn) == 0)
                rows[i].hidePtr = rows[j].ptr;
            if (rows[i].shownWhen &&
                strcmp(rows[j].key, rows[i].shownWhen) == 0)
                rows[i].showPtr = rows[j].ptr;
            if (rows[i].schemeOnly && strcmp(rows[j].key, "Input.ControlScheme") == 0)
                rows[i].schemePtr = rows[j].ptr;
        }
    }
    /* D356: on a header row, saveScoped means "this section contains
     * per-file rows" (drives the "(File N)" title annotation in both UIs);
     * on a content row it is the literal per-file flag from the table. */
    for (int i = 0; i < NUM_ROWS; i++) {
        if (rows[i].kind != ROW_HEADER) {
            continue;
        }
        int has = 0;
        for (int j = i + 1; j < NUM_ROWS && rows[j].kind != ROW_HEADER; j++)
            has |= rows[j].saveScoped;
        rows[i].saveScoped = has;
    }
    overlayUpdateVisible();

    /* D388: opt-in in-memory walk of the real F10 page tree. No settings,
     * binds or profile bytes are written; restore the UI state afterward. */
    if (getenv("GE_OPTIONTREEPROBE")) {
        int oldSection = s_section, oldSel = s_sel, oldScroll = s_scroll;
        int bad = 0;
        static const char *const roots[] = {
            "__HdrVideo", "__HdrAudio", "__HdrMouse", "__HdrSelPlayerC", "__HdrGame", "__HdrBindings"
        };
        s_section = -1; s_sel = 0; s_scroll = 0;
        overlayUpdateVisible();
        bad += overlayHitCheck();
        if (s_visN != 9) bad++;   /* 6 pages + separator + Restart + Quit */
        for (int p = 0; p < 6 && p < s_visN; p++)
            if (strcmp(rows[s_visIdx[p]].key, roots[p])) bad++;
        /* Pages may scroll (PD dialogs do); overlayHitCheck walks the first and
         * last scroll windows instead of a no-scroll row cap. */
        static const struct { const char *page, *childA, *childB; } pages[] = {
            { "__HdrVideo", "__DisplayMode", "Game.ScreenShakeIntensity" },
            { "__HdrAudio", "Audio.MasterVolume", "Bond.FX" },
            { "__HdrMouse", "Input.MouseSensitivity", "Input.MouseRawInput" },
            { "__HdrSelPlayerC", "__SelPlayerC1", "__SelPlayerC4" },
            { "__HdrController", "Input.ControlScheme", "__ResetController" },
            { "__HdrBindings", "Input.Bind.Forward", "__ResetBindings" },
            { "__HdrGame", "Bond.AimControl", "__ResetGame" },
        };
        for (size_t k = 0; k < sizeof(pages) / sizeof(pages[0]); k++) {
            int hdr = rowIndexByKey(pages[k].page), gotA = 0, gotB = 0;
            overlayOpenHeader(hdr);
            bad += overlayHitCheck();
            for (int p = 1; p < s_visN; p++) {
                gotA |= !strcmp(rows[s_visIdx[p]].key, pages[k].childA);
                if (pages[k].childB)
                    gotB |= !strcmp(rows[s_visIdx[p]].key, pages[k].childB);
                if (!strcmp(rows[s_visIdx[p]].key, "Video.LowEndMode")) bad++;
                if (rows[s_visIdx[p]].hub) bad++;   /* hub-only rows never leak onto a page */
            }
            if (!gotA || (pages[k].childB && !gotB)) bad++;
            if (strncmp(rows[s_visIdx[s_visN - 1]].key, "__Back", 6)) bad++;   /* Back closes every page */
            overlayBackOne();
            int parent = optionsRowHeaderParent(hdr);
            if (s_section != parent) bad++;
        }
        /* Moved rows: HUD rows and FOV live on Game; no stale pages exist. */
        {
            static const char *const gameKeys[] = {
                "Bond.Sight", "Bond.Ammo", "Game.HudScale", "Video.FovScale", "Input.CrouchMode",
                "Video.CrosshairColor", "Game.SkipIntro"
            };
            overlayOpenHeader(rowIndexByKey("__HdrGame"));
            for (size_t k = 0; k < sizeof(gameKeys) / sizeof(gameKeys[0]); k++) {
                int seen = 0;
                for (int p = 1; p < s_visN; p++)
                    seen |= !strcmp(rows[s_visIdx[p]].key, gameKeys[k]);
                if (!seen) bad++;
            }
        }
        if (rowIndexByKey("__HdrInput") >= 0 || rowIndexByKey("__HdrHUD") >= 0 ||
            rowIndexByKey("__HdrGraphics") >= 0) bad++;
        if (optionsRowChildHeader(rowIndexByKey("__SelPlayerC1")) != rowIndexByKey("__HdrController") ||
            optionsRowHeaderParent(rowIndexByKey("__HdrController")) != rowIndexByKey("__HdrSelPlayerC")) bad++;
        s_section = oldSection; s_sel = oldSel; s_scroll = oldScroll;
        overlayUpdateVisible();
        sysLogPrintf(bad ? LOG_ERROR : LOG_INFO,
                     "GE_OPTIONTREEPROBE: %s (6 roots + hub rows, Select player -> Controller, flat Key Bindings, Game holds HUD/FOV, scroll windows hit-test, Back last)",
                     bad ? "FAIL" : "PASS");
    }

    if (getenv("GE_PRESETPROBE")) presetProbe();

    /* Build the windowed-resolution preset list: presets that fit the desktop,
     * plus the current window size snapped to the nearest surviving entry. */
    {
        int dw = 1920, dh = 1080;
        videoGetDesktopSize(&dw, &dh);
        s_resFitN = 0;
        for (int i = 0; i < NUM_RES; i++) {
            if (kResList[i][0] <= dw && kResList[i][1] <= dh) {
                s_resFit[s_resFitN++] = i;
            }
        }
        if (s_resFitN == 0) {
            s_resFit[s_resFitN++] = 0;
        }
        int cw = 0, ch = 0;
        videoGetWindowSize(&cw, &ch);
        long best = -1;
        for (int k = 0; k < s_resFitN; k++) {
            int i = s_resFit[k];
            long d = labs((long)kResList[i][0] - cw) +
                     labs((long)kResList[i][1] - ch);
            if (best < 0 || d < best) { best = d; s_resSel = k; }
        }
    }

    const char *e = getenv("GE_OPTIONSOVERLAY");
    if (e && atoi(e) != 0) {
        overlayApplyTestRequest(e);
        sysLogPrintf(LOG_INFO, "optionsoverlay: auto-opened (GE_OPTIONSOVERLAY=%s)", e);
        /* D383 isolated host->menu smoke probe. Only with explicit opt-in;
         * test data dir must be private because capture persists the key. */
        const char *probe = getenv("GE_BINDCAPTUREPROBE");
        if (probe && *probe) {
            SDL_Scancode sc = !SDL_strcasecmp(probe, "ESC") ? SDL_SCANCODE_ESCAPE :
                              !SDL_strcasecmp(probe, "DEL") ? SDL_SCANCODE_DELETE :
                              SDL_SCANCODE_F2;
            const char *bindKey = getenv("GE_BINDCAPTUREACTION");
            if (!bindKey || !*bindKey) bindKey = "Input.Bind.Fire";
            for (int i = 0; i < NUM_ROWS; i++) {
                if (!strcmp(rows[i].key, bindKey) && rows[i].kind == ROW_BIND) {
                    bindingBegin(&rows[i]);
                    sysLogPrintf(LOG_INFO, "GE_BINDCAPTUREPROBE: %s found=%d active=%d",
                                 bindKey, rows[i].found, optionsBindingCaptureActive());
                    SDL_Event ev = {0};
                    if (!SDL_strcasecmp(probe, "MOUSE5")) {
                        ev.button.type = SDL_MOUSEBUTTONDOWN;
                        ev.button.state = SDL_PRESSED;
                        ev.button.button = SDL_BUTTON_X2;
                    } else {
                        ev.key.type = SDL_KEYDOWN;
                        ev.key.state = SDL_PRESSED;
                        ev.key.repeat = 0;
                        ev.key.keysym.scancode = sc;
                        ev.key.keysym.sym = SDL_GetKeyFromScancode(sc);
                        ev.key.keysym.mod = KMOD_NONE;
                    }
                    int sent = SDL_PushEvent(&ev);
                    sysLogPrintf(LOG_INFO, "GE_BINDCAPTUREPROBE: queued=%d (%s)", sent, SDL_GetError());
                    break;
                }
            }
        }
    }
}

/* D516: scheme-conditional rows (see Row.schemeOnly). */
static int rowSchemeOk(const struct Row *r)
{
    if (!strcmp(r->key, "__UpdateOpen")) return updateCheckAvailable() != NULL;   /* D551 */
    if (!r->schemeOnly || !r->schemePtr) return 1;
    return (*r->schemePtr != 0) == (r->schemeOnly == 2);
}

static double rowGet(const struct Row *r)
{
    int field = watchSettingsFieldForKey(r->key);
    if (field >= 0) return (double)watchSettingsRead(field);
    if (!r->found || !r->ptr) {
        return 0.0;
    }
    switch (r->type) {
    case CONFIG_OPT_INT:   return (double)*(int *)r->ptr;
    case CONFIG_OPT_UINT:  return (double)*(unsigned int *)r->ptr;
    case CONFIG_OPT_FLOAT: return (double)*(float *)r->ptr;
    default:               return 0.0;
    }
}

static double rowLo(const struct Row *r)
{
    return (r->uiMin != r->uiMax) ? r->uiMin : r->cfgMin;
}
static double rowHi(const struct Row *r)
{
    return (r->uiMin != r->uiMax) ? r->uiMax : r->cfgMax;
}

/* D506: the old 50/100 "calibrated default" mapping is retired. Every slider is
 * a linear bar showing a real unit (%, x, deg), so the same quantity reads the
 * same way in every row and in both UIs. Kept as a hook (returns "none") so the
 * piecewise mapping below stays a no-op rather than being rewritten. */
static double calibratedDefault(const struct Row *r)
{
    (void)r;
    return -1.0;
}
static double rowFractionAt(const struct Row *r, double v)
{
    double lo = rowLo(r), hi = rowHi(r), def = calibratedDefault(r);
    if (hi <= lo) return 0.0;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    if (def > lo && def < hi)
        return v <= def ? 0.5 * (v - lo) / (def - lo)
                        : 0.5 + 0.5 * (v - def) / (hi - def);
    return (v - lo) / (hi - lo);
}
static double rowValueAtFraction(const struct Row *r, double f)
{
    double lo = rowLo(r), hi = rowHi(r), def = calibratedDefault(r);
    if (f < 0.0) f = 0.0;
    if (f > 1.0) f = 1.0;
    if (def > lo && def < hi)
        return f <= 0.5 ? lo + (def - lo) * f * 2.0
                        : def + (hi - def) * (f - 0.5) * 2.0;
    return lo + (hi - lo) * f;
}

static struct Row *rowByKey(const char *key)
{
    int i = rowIndexByKey(key);
    return i < 0 ? NULL : &rows[i];
}

static int s_linkDepth = 0;   /* re-entrancy guard for the sens link below */

static void rowSet(struct Row *r, double v);

static void rowSetCommit(struct Row *r, double v, int commit)
{
    double lo = rowLo(r), hi = rowHi(r);
    if (lo != hi) {
        if (v < lo) v = lo;
        if (v > hi) v = hi;
    }
    int field = watchSettingsFieldForKey(r->key);
    if (field >= 0) {
        int want = (int)lround(v);
        /* Held drags poll far more often than the mouse moves to a new
         * slider detent. Don't enqueue another audio update for the same
         * value; release still commits via watchSettingsCommit(). */
        if (watchSettingsRead(field) != want)
            watchSettingsSet(field, want, commit);
        return;
    }
    switch (r->type) {
    case CONFIG_OPT_INT:
        if (*(int *)r->ptr == (int)lround(v)) return;
        *(int *)r->ptr = (int)lround(v); break;
    case CONFIG_OPT_UINT: {
        unsigned int want = (unsigned int)(v < 0 ? 0 : lround(v));
        if (*(unsigned int *)r->ptr == want) return;
        *(unsigned int *)r->ptr = want; break;
    }
    case CONFIG_OPT_FLOAT:
        if (*(float *)r->ptr == (float)v) return;
        *(float *)r->ptr = (float)v; break;
    default: return;
    }

    /* D382: only the specific live GL/SDL knob that changed needs a render-
     * thread apply. Reticle, draw/LOD, widescreen and HUD settings are read
     * directly; rebuilding textures for every RGB/FOV detent caused hitches. */
    if (strcmp(r->key, "Video.Fullscreen") == 0) {
        videoRequestFullscreen((int)lround(v));
    } else if (strcmp(r->key, "Video.FullscreenMode") == 0) {   /* D511 */
        videoRequestFullscreenMode((int)lround(v));
    } else if (strncmp(r->key, "Video.", 6) == 0 && !r->restart) {
        videoRequestLiveConfigForKey(r->key);
    }

    /* Crouch mode drops the latch; future bind-capture rows can call the
     * same rebuild hook. */
    if (strcmp(r->key, "Input.CrouchMode") == 0) inputBindingsApply();

    /* D257/D442: All unlocked applies live (port/src/main.c); open screens
     * such as mission select refresh on re-entry. */
    if (strcmp(r->key, "Game.AllUnlocked") == 0) {
        extern void portAllUnlockedApply(void);
        portAllUnlockedApply();
    }

    /* Linked aim/turn sensitivity (Input.SensLink, default on): moving either
     * knob scales the other to hold the stock default ratio -- AimModeSens 38
     * : MouseTurnSpeed 50 (the D194/D238 calibrated defaults). */
    if (!s_linkDepth && strcmp(r->key, "Input.SensLink") != 0) {
        struct Row *lk = rowByKey("Input.SensLink");
        if (lk && lk->found && *(int *)lk->ptr != 0) {
            struct Row *o = NULL;
            double nv = 0.0;
            if (strcmp(r->key, "Input.MouseTurnSpeed") == 0) {
                o = rowByKey("Input.AimModeSens");
                nv = v * 38.0 / 50.0;
            } else if (strcmp(r->key, "Input.AimModeSens") == 0) {
                o = rowByKey("Input.MouseTurnSpeed");
                nv = v * 50.0 / 38.0;
            }
            if (o && o->found) {
                s_linkDepth = 1;
                rowSet(o, nv);
                s_linkDepth = 0;
            }
        }
    }
}

static void rowSet(struct Row *r, double v) { rowSetCommit(r, v, 1); }

/* D516 (extends D489 from sliders to every Bond-file value row): a stepped
 * toggle/enum used to commit on EVERY step -- each commit is three
 * joyDisablePoll/joyEnablePoll handshakes plus an EEPROM rewrite on the game
 * thread, so holding left/right on e.g. the 8-style "Controller style" row
 * stalled frames for as long as it was held (maintainer live pass
 * 2026-10-04: adjusting the old control style in a mission dropped the fps).
 * Stage like the sliders; optionsAdjustCommitPending() saves once on release
 * (it runs every frame while left/right is not held, and on F10 close). Rows
 * without a watch field fall through to a plain commit. */
static void rowSetStepWatch(struct Row *r, double v)
{
    int field = watchSettingsFieldForKey(r->key);
    if (field < 0) { rowSet(r, v); return; }
    int prev = SDL_AtomicSet(&s_adjWatchField, field + 1);
    if (prev > 0 && prev != field + 1) watchSettingsCommit(prev - 1);
    rowSetCommit(r, v, 0);
}

/* D356 reset rows (defined below, in the reset block). */
static struct Row *rowAt(int i);
static int isResetRow(const struct Row *r);
static int isArmRow(const struct Row *r);
static void rowActivateReset(struct Row *r);

static void rowAdjust(struct Row *r, int dir)
{
    if (!r->found || r->kind == ROW_HEADER) {   /* D237: headers have no value */
        return;
    }
    if (r->kind == ROW_BOND_FILE) {
        if (current_menu == MENU_FILE_SELECT) watchSettingsChooseFile(dir);   /* front end: file select */
        return;
    }
    if (watchSettingsFieldForKey(r->key) == WATCH_SETTING_CONTROL) {   /* D516: no live player -> n/a */
        if (watchSettingsRead(WATCH_SETTING_CONTROL) < 0) return;
    } else
    if (watchSettingsFieldForKey(r->key) >= 0 && !watchSettingsAvailable()) return;
    double v = rowGet(r);
    switch (r->kind) {
    case ROW_TOGGLE:
        rowSetStepWatch(r, (v != 0.0) ? 0.0 : 1.0);
        break;
    case ROW_MSAA: {
        int idx = 0;
        for (int i = 0; i < MSAA_N; i++) {
            if (kMsaaSeq[i] == (int)lround(v)) idx = i;
        }
        /* Wrap like a normal settings-menu cycle: OFF->2x->4x->8x->16x->OFF,
         * in both directions (left/right click and arrows all roll). */
        idx = (idx + dir + MSAA_N) % MSAA_N;
        rowSetStepWatch(r, (double)kMsaaSeq[idx]);
        break;
    }
    case ROW_ENUM: {
        double lo = r->cfgMin, hi = r->cfgMax;
        if (!strcmp(r->key, "Input.PadPreset")) {   /* D498: step through the preset display order (0,1,2,4,3) */
            rowSetStepWatch(r, (double)inputPadPresetStep((int)lround(v), dir));
            break;
        }
        if (!strcmp(r->key, "Video.CrosshairColor")) {
            /* D569 (PD model): Original sprite or a Custom RGB colour; the old
             * named presets (1..7) are migrated to Custom at startup (video.c). */
            rowSetStepWatch(r, (int)lround(v) == 8 ? 0.0 : 8.0);
            break;
        }
        if (!strcmp(r->key, "Video.TextureFilter")) {
            /* D499: menu order Nearest, Bilinear, Trilinear, 3-Point (stored
             * 0, 1, 3, 2: 2 stays 3-Point for existing inis and the Original
             * N64 preset), so Bilinear and Trilinear sit side by side for A/B. */
            static const int order[4] = { 0, 1, 3, 2 };
            int cur = (int)lround(v), i = 0;
            while (i < 4 && order[i] != cur) i++;
            if (i == 4) i = 0;
            rowSetStepWatch(r, (double)order[(i + (dir > 0 ? 1 : 3)) % 4]);
            break;
        }
        v += dir;
        if (v < lo) v = hi;
        if (v > hi) v = lo;
        rowSetStepWatch(r, v);
        break;
    }
    case ROW_FPSCAP: {
        int idx = 0;
        for (int i = 0; i < 2; i++) {
            if (kFpsCapSeq[i] == (int)lround(v)) idx = i;
        }
        idx = (idx + dir + 2) % 2;
        rowSetStepWatch(r, (double)kFpsCapSeq[idx]);
        break;
    }
    case ROW_RES: {
        if (s_resFitN <= 0 || videoIsFullscreen()) {
            break;   /* resolution is windowed-only */
        }
        s_resSel += (dir >= 0) ? 1 : -1;
        if (s_resSel < 0) s_resSel = s_resFitN - 1;
        if (s_resSel >= s_resFitN) s_resSel = 0;
        int i = s_resFit[s_resSel];
        videoRequestWindowSize(kResList[i][0], kResList[i][1]);
        break;
    }
    case ROW_BIND:
    case ROW_PADBIND:
        r->bindSlot = (r->bindSlot + dir + INPUT_BIND_SLOTS) % INPUT_BIND_SLOTS;
        break;
    case ROW_PADSEAT:
        padSetSeat((s_padSeat + (dir >= 0 ? 1 : 3)) % 4);   /* D518 */
        break;
    case ROW_AUDIODEV: {
        /* D470: cycle Default + enumerated outputs; applied live on the audio thread. */
        int n = audioDeviceRefresh();
        int idx = 0;   /* 0 = Default */
        for (int k = 0; k < n; k++)
            if (!strcmp(audioDeviceName(k), audioDeviceCurrentName())) idx = k + 1;
        idx = (idx + (dir >= 0 ? 1 : n) ) % (n + 1);
        audioDeviceRequest(idx == 0 ? "" : audioDeviceName(idx - 1));
        sysLogPrintf(LOG_INFO, "optionsoverlay: audio device -> \"%s\"", idx == 0 ? "Default" : audioDeviceName(idx - 1));
        break;
    }
    case ROW_ACTION:
        if (!strcmp(r->key, "__DisplayMode")) {   /* left/right steps Modern <-> Original N64; Custom -> Modern (right) / N64 (left) */
            int cur = displayModeNow();
            videoApplyPreset((cur == 0 || (cur == 2 && dir < 0)) ? VIDEO_PRESET_N64 : VIDEO_PRESET_PORT);
            break;
        }
        if (optionsRowChildHeader((int)(r - rows)) >= 0) break; /* link, not a Quit or value */
        if (isSepRow(r)) break;
        if (isBackRow(r)) { if (dir > 0) overlayBackOne(); break; }
        /* D356: reset rows arm/confirm through the shared activation
         * contract (edge-triggered by their callers: fresh key press / fresh
         * click / A-press -- the held-repeat paths below never re-fire them).
         * The quit row is the only non-reset ROW_ACTION left. */
        if (isArmRow(r)) { rowActivateReset(r); break; }
        if (!strcmp(r->key, "__UpdateOpen")) {   /* D551 */
            if (dir > 0) updateCheckOpenReleases();
            break;
        }
        if (!strcmp(r->key, "__CenterWindow")) {   /* D511: host thread does the SDL call */
            if (dir > 0) videoRequestCenterWindow();
            break;
        }
        /* D293: same exit path as SDL_QUIT / Alt+F4 (video.c), just reachable
         * without OS window chrome or a keyboard. */
        if (!strcmp(r->key, "__RestartGame")) {
            /* D443: same orderly D344 quit; main.c's atexit handler saves
             * config again and then relaunches (sysRelaunchSelf). */
            sysLogPrintf(LOG_INFO, "optionsoverlay: restart game requested");
            configSave();
            videoRequestRestart("Restart game");
            break;
        }
        sysLogPrintf(LOG_INFO, "optionsoverlay: quit to desktop requested");
        configSave();
        videoRequestQuit("Quit to desktop");   /* D344: never exit() off the host thread */
        break;
    default: /* ROW_SLIDER */
        if (!strcmp(r->key, "Video.FovScale")) {
            /* D546: step to the next 5-degree vertical mark (60 deg = 100 %);
             * an off-grid value from an older ini snaps to the next mark. */
            double d = v * 0.6;   /* 30..90: positive, so (long) truncation = floor */
            long lo = (long)(d / 5.0 + 1e-6), hi = (long)(d / 5.0 + 1.0 - 1e-6);
            double g = (dir >= 0) ? (lo + 1) * 5.0 : (hi - 1) * 5.0;
            rowSet(r, g / 0.6);
            break;
        }
        {
            int field = watchSettingsFieldForKey(r->key);
            if (field >= 0) {
                /* D489: stage only; a different pending field saves first. */
                int prev = SDL_AtomicSet(&s_adjWatchField, field + 1);
                if (prev > 0 && prev != field + 1) watchSettingsCommit(prev - 1);
                rowSetCommit(r, v + dir * r->step, 0);
                break;
            }
        }
        rowSet(r, v + dir * r->step);
        break;
    }
}

static s32 bodyWidth(const char *str);
/* ---- PD dropdown (menuitemDropdownRender / menugfxDrawDropdownBackground) ----
 * Rows that choose one value from a list (enum, anti-aliasing, resolution,
 * frame cap, output device) open a popup under the row: up/down/wheel/hover
 * pick, Enter/A/click confirm, Esc/B/right-click/outside-click/closing the menu
 * cancel. Left/right on the closed row still steps the value (quick cycling).
 * Two-way named toggles (Reverse/Upright, Hold/Toggle) stay plain value rows. */
#define DD_MAX 24
#define DD_VIS 9
static int  s_ddRow = -1;           /* rows[] index of the open dropdown, -1 = closed */
static int  s_ddSel = 0, s_ddTop = 0, s_ddN = 0, s_ddCur = 0;
static int  s_ddVals[DD_MAX];
static char s_ddLab[DD_MAX][40];

static int rowIsDropdown(const struct Row *r)
{
    if (!r->found) return 0;
    if (r->kind == ROW_ENUM && !strcmp(r->key, "Video.CrosshairColor")) return 0;   /* D569: Original/Custom stepper (PD) */
    if (r->kind == ROW_ENUM || r->kind == ROW_MSAA || r->kind == ROW_FPSCAP) return 1;
    if (r->kind == ROW_RES) return !videoIsFullscreen() && s_resFitN > 0;
    if (r->kind == ROW_AUDIODEV) return 1;
    if (r->kind == ROW_ACTION && !strcmp(r->key, "__DisplayMode")) return 1;
    return 0;
}

/* Fill the item list in display order; s_ddCur = the item holding the current value. */
static void ddBuild(const struct Row *r)
{
    double v = rowGet(r);
    int cur = (int)lround(v);
    s_ddN = 0; s_ddCur = 0;
    if (r->kind == ROW_ENUM && r->names) {
        int order[DD_MAX], n = 0, cnt = 0;
        while (r->names[cnt] && cnt < DD_MAX) cnt++;
        if (!strcmp(r->key, "Video.TextureFilter")) {   /* D499 menu order */
            static const int o4[4] = { 0, 1, 3, 2 };
            for (int i = 0; i < 4 && i < cnt; i++) order[n++] = o4[i];
        } else if (!strcmp(r->key, "Input.PadPreset")) {   /* D498 display order */
            int c = 0;
            do { order[n++] = c; c = inputPadPresetStep(c, +1); } while (c != 0 && n < cnt);
        } else {
            for (int i = 0; i < cnt; i++) order[n++] = i;
        }
        for (int i = 0; i < n; i++) {
            s_ddVals[i] = order[i];
            snprintf(s_ddLab[i], sizeof(s_ddLab[i]), "%s", r->names[order[i]]);
            if (order[i] == cur) s_ddCur = i;
        }
        s_ddN = n;
    } else if (r->kind == ROW_ACTION) {   /* Display mode: Custom is shown (current) but picking it is a no-op */
        for (int i = 0; i < 3; i++) { s_ddVals[i] = i; snprintf(s_ddLab[i], sizeof(s_ddLab[i]), "%s", kDisplayModeName[i]); }
        s_ddN = 3;
        s_ddCur = displayModeNow();
    } else if (r->kind == ROW_MSAA) {
        for (int i = 0; i < MSAA_N; i++) {
            s_ddVals[i] = kMsaaSeq[i];
            if (kMsaaSeq[i] <= 1) snprintf(s_ddLab[i], sizeof(s_ddLab[i]), "None");
            else snprintf(s_ddLab[i], sizeof(s_ddLab[i]), "%dx", kMsaaSeq[i]);
            if (kMsaaSeq[i] == cur) s_ddCur = i;
        }
        s_ddN = MSAA_N;
    } else if (r->kind == ROW_FPSCAP) {
        for (int i = 0; i < 2; i++) {
            s_ddVals[i] = kFpsCapSeq[i];
            snprintf(s_ddLab[i], sizeof(s_ddLab[i]), "%d FPS", kFpsCapSeq[i]);
            if (kFpsCapSeq[i] == cur) s_ddCur = i;
        }
        s_ddN = 2;
    } else if (r->kind == ROW_RES) {
        for (int k = 0; k < s_resFitN && k < DD_MAX; k++) {
            int i = s_resFit[k];
            s_ddVals[k] = k;
            snprintf(s_ddLab[k], sizeof(s_ddLab[k]), "%d x %d", kResList[i][0], kResList[i][1]);
        }
        s_ddN = s_resFitN < DD_MAX ? s_resFitN : DD_MAX;
        s_ddCur = s_resSel < s_ddN ? s_resSel : 0;
    } else if (r->kind == ROW_AUDIODEV) {
        int n = audioDeviceRefresh();
        if (n > DD_MAX - 1) n = DD_MAX - 1;
        s_ddVals[0] = 0;
        snprintf(s_ddLab[0], sizeof(s_ddLab[0]), "Default");
        for (int k = 0; k < n; k++) {
            s_ddVals[k + 1] = k + 1;
            snprintf(s_ddLab[k + 1], sizeof(s_ddLab[k + 1]), "%.34s", audioDeviceName(k));
            if (!strcmp(audioDeviceName(k), audioDeviceCurrentName())) s_ddCur = k + 1;
        }
        s_ddN = n + 1;
    }
}

static void ddOpen(int rowIdx)
{
    struct Row *r = &rows[rowIdx];
    if (!rowIsDropdown(r)) return;
    /* D516: no live player -> the style row is n/a (value -1); never open a popup on it. */
    if (watchSettingsFieldForKey(r->key) == WATCH_SETTING_CONTROL && watchSettingsRead(WATCH_SETTING_CONTROL) < 0) return;
    ddBuild(r);
    if (s_ddN <= 0) return;
    s_ddRow = rowIdx;
    s_ddSel = s_ddCur;
    s_ddTop = s_ddSel >= DD_VIS ? s_ddSel - DD_VIS + 1 : 0;
    optionsResetClear();
    sysLogPrintf(LOG_INFO, "optionsoverlay: dropdown '%s' opened (%d items)", r->label, s_ddN);
}

static void ddClose(void) { s_ddRow = -1; }

static void ddMove(int dir)
{
    int q = s_ddSel + dir;
    if (q < 0 || q >= s_ddN) return;   /* clamped, like the main list */
    s_ddSel = q;
    if (s_ddSel < s_ddTop) s_ddTop = s_ddSel;
    if (s_ddSel >= s_ddTop + DD_VIS) s_ddTop = s_ddSel - DD_VIS + 1;
}

static void ddConfirm(int k)
{
    if (s_ddRow < 0 || k < 0 || k >= s_ddN) { ddClose(); return; }
    struct Row *r = &rows[s_ddRow];
    int val = s_ddVals[k];
    ddClose();
    switch (r->kind) {
    case ROW_RES: {
        s_resSel = val;
        int i = s_resFit[s_resSel];
        videoRequestWindowSize(kResList[i][0], kResList[i][1]);
        break;
    }
    case ROW_AUDIODEV:
        audioDeviceRequest(val == 0 ? "" : audioDeviceName(val - 1));
        break;
    case ROW_ACTION:   /* Display mode */
        if (val == 0) videoApplyPreset(VIDEO_PRESET_PORT);
        else if (val == 1) videoApplyPreset(VIDEO_PRESET_N64);
        break;
    default:
        rowSet(r, (double)val);
        break;
    }
}

/* Popup rectangle in overlay units; false if the owning row is not on screen. */
static int ddGeom(s32 *x0, s32 *y0, s32 *x1, s32 *y1, int *vis)
{
    if (s_ddRow < 0) return 0;
    struct OvLayout o = overlayLayout();
    int p = -1;
    for (int k = 0; k < s_visN; k++) if (s_visIdx[k] == s_ddRow) p = k;
    if (p < 0) return 0;
    s32 rowY = o.contentY + (s32)lround((p - s_scrollF) * OV_LINE);
    s32 w = 40;
    for (int i = 0; i < s_ddN; i++) { s32 lw = bodyWidth(s_ddLab[i]) + 20; if (lw > w) w = lw; }
    if (w > o.right - o.left - 16) w = o.right - o.left - 16;
    int v = s_ddN < DD_VIS ? s_ddN : DD_VIS;
    s32 h = v * OV_LINE + 2;
    *x1 = o.valueR + 3;
    *x0 = *x1 - w;
    s32 H = ovH();
    *y0 = rowY + OV_LINE;
    if (*y0 + h > H - 4) *y0 = rowY - h;   /* no room below: open upward */
    /* D568: no room above either (a long list on a mid-page row): keep the whole
     * popup on screen, flush with the bottom margin. A negative top used to
     * draw the labels above the screen while the fill wrapped down past the
     * bottom edge (an empty, misplaced box). */
    if (*y0 < 4) *y0 = (H - 4 - h > 4) ? H - 4 - h : 4;
    *y1 = *y0 + h;
    *vis = v;
    return 1;
}

/* Popup item under overlay-space (ox, oy), or -1. */
static int ddHit(double ox, double oy)
{
    s32 x0, y0, x1, y1; int vis;
    if (!ddGeom(&x0, &y0, &x1, &y1, &vis)) return -1;
    if (ox < x0 || ox >= x1 || oy < y0 + 1 || oy >= y1 - 1) return -1;
    int k = (int)((oy - y0 - 1) / OV_LINE);
    return (k >= 0 && k < vis) ? s_ddTop + k : -1;
}

void optionsAdjustCommitPending(void)
{
    int pending = SDL_AtomicSet(&s_adjWatchField, 0);
    if (pending > 0) watchSettingsCommit(pending - 1);
}

/* ------------------------------------------------------------------------ */
/* D356: per-section "Reset to defaults" (plan Gate E, made concrete).       */
/* The activation contract (plan §5.4): edge-triggered, two-step            */
/* arm -> confirm within 3 s, one commit per confirmed activation,          */
/* disarm on timeout or navigating away. The arm state is shared by the     */
/* F10 overlay and the front options screen (they are never open at the     */
/* same time; both clear it on open/close).                                 */
/* ------------------------------------------------------------------------ */

#define RESET_ARM_US 3000000   /* arm expires after 3 s */

static int  s_resetArmedRow = -1;   /* rows[] index of the armed reset row */
static uint64_t s_resetArmedUs = 0;

static int isResetRow(const struct Row *r)
{
    return r->kind == ROW_ACTION && strncmp(r->key, "__Reset", 7) == 0;
}

/* D440: the preset rows share the reset rows' two-step arm ->
 * confirm activation (and every edge-trigger guard that comes with it), but
 * are NOT section resets (the GE_WSPROBE_RESET probe keeps isResetRow). */
static int isPresetRow(const struct Row *r)
{
    return r->kind == ROW_ACTION && strncmp(r->key, "__Preset", 8) == 0;
}

/* T4-lite Display mode (no ini key): derived from the D440 preset key tables.
 * 0 = Modern (port defaults), 1 = Original N64, 2 = Custom (a bundled key was
 * edited; flips back by itself when the keys match a preset again). Arming
 * targets "the other one": Modern -> Original N64, anything else -> Modern. */
static int displayModeNow(void)
{
    if (videoPresetIsActive(VIDEO_PRESET_PORT)) return 0;
    if (videoPresetIsActive(VIDEO_PRESET_N64))  return 1;
    return 2;
}
static const char *const kDisplayModeName[3] = { "Modern", "Original N64", "Custom" };


static int isArmRow(const struct Row *r)
{
    return isResetRow(r) || isPresetRow(r);
}

int optionsRowIsReset(int i)
{
    struct Row *r = rowAt(i);
    return r && isArmRow(r);
}

/* ini rows reset to the port's C initializers (verified at implementation
 * time, D356 -- each entry cites its source variable): the reset table
 * mirrors them; config.c itself has no central default table (first-write
 * "defaults" are just the current values). __Resolution is NOT here: it is
 * action-backed (s_resSel + videoRequestWindowSize), not a registered config
 * row, so a numeric default cannot express it -- a documented exclusion (the
 * findings D356 entry states it); the player's resolution choice survives a
 * VIDEO reset. */
static const struct { const char *key; double def; } kResetDefaults[] = {
    /* INPUT (port/src/input.c initializers) */
    { "Input.MouseEnabled",       1 },   /* = 1 (on) */
    { "Input.CrosshairCursor",    1 },   /* = 1 (on, D555) */
    { "Input.MouseSensitivity", 100 },  /* static int mouseSensitivity = 100 */
    { "Input.MouseYScale",      100 },  /* = 100 (native), Wave A item 6 */
    { "Input.MouseInvertY",      0 },   /* = 0 */
    { "Input.AimMode",           0 },   /* = AIMMODE_N64 (0) */
    { "Input.AimRange",          0 },   /* = 0 (PC) */
    { "Input.PadLookInvertY",    0 },   /* = 0 */
    { "Input.PadDeadzoneL",     7500 },/* = STICK_DEADZONE (7500 = 25%, D546), Wave A */
    { "Input.PadDeadzoneR",     7500 },/* = STICK_DEADZONE (7500 = 25%, D546), Wave A */
    { "Input.PadLookSensX",     100 },  /* = 100 (native), Wave A */
    { "Input.PadLookSensY",     100 },  /* = 100 (native), Wave A */
    { "Input.PadLookSmooth",      0 },  /* = 0 (off), Wave A */
    { "Input.PadSouthpaw",        0 },  /* = 0 (off), Wave A */
    { "Input.PadTriggerPct",     25 },  /* = 25 (was 23; rounded, D508) */
    { "Input.RumbleScale", 0.5 },       /* = gRumbleScale (0.5f), D401 */
    { "Input.CrouchMode",        1 },   /* = 1 (toggle; D556) */
    { "Input.MenuPointerMode",   1 },   /* = 1 (direct pointer); no F10 row since D561 (front-end-only legacy option, ini key kept) */
    { "Input.MouseSmoothing",    0 },   /* = 0 (raw) */
    { "Input.MouseRawInput",     0 },   /* = 0 (off) */
    { "Input.ControlScheme",     0 },   /* = 0 (Modern), D513 */
    { "Input.PadPreset",         0 },   /* = 0 (Jinx 1.1), D469 */
    /* GRAPHICS (port/src/video.c initializers) */
    { "Video.MSAA",                 2 },   /* = 2 */
    { "Video.TextureFilter",        1 },   /* = 1 (bilinear) */
    { "Video.Anisotropy",           4 },   /* = 4 */
    { "Video.FovScale",            100 },  /* = 100 */
    { "Video.NativeWidescreen",      1 },  /* = 1 */
    { "Video.WidescreenAuto",        1 },  /* = 1 */
    { "Video.SafeAreaCrop",          1 },  /* = 1 */
    { "Video.AspectMode",            0 },  /* = 0 (Window), D447 */
    { "Video.DrawDistance",        200 },  /* 2.0x (D546) */
    { "Video.FogDistance",         100 },  /* 1.0x = N64 fog (D540) */
    { "Video.LodDistance",         200 },  /* 2.0x (D546) */
    { "Video.CrosshairHide",      0 },   /* = 0 (on, N64) */
    { "Video.CrosshairPersistent", 0 },   /* D436: off = N64 (aim mode only) */
    { "Video.CrosshairColor",   0 },   /* = 0 (authored red sprite) */
    { "Video.CrosshairRed",   255 },
    { "Video.CrosshairGreen", 255 },
    { "Video.CrosshairBlue",  255 },
    { "Video.CrosshairSize",  100 },   /* = 100% */
    { "Video.CrosshairStyle",   0 },   /* = 0 (original) */
    { "Video.CrosshairAlpha", 100 },   /* = 100% (D511) */
    { "Video.CrosshairHealthColor", 0 },   /* = 0 (off, D511) */
    /* GAMEPLAY ini rows (port/src/video.c initializers) */
    { "Game.SkipIntro",   0 },   /* = 0 */
    { "Game.CheckUpdates", 0 },  /* = 0 (off, D551) */
    { "Game.NoHitFlash",  0 },   /* = 0 */
    { "Game.AllUnlocked", 0 },   /* = 0 -- D257's "default ON" note was stale */
    { "Game.HudScale",   100 },  /* = 100 (D226) */
    { "Game.ScreenShakeIntensity", 1 },   /* = portScreenShakeScale (1.0f), D181 */
    /* VIDEO (port/src/video.c initializers; DisplayFPS in overlayConfigInit) */
    { "Video.Fullscreen", 0 },   /* = 0 (windowed) */
    { "Video.FullscreenMode", 0 },   /* = 0 (borderless, D511) */
    { "Video.VSync",        1 }, /* = 1 (on) */
    { "Video.FpsCap",      60 }, /* = 60 */
    { "Video.DisplayFPS",   0 }, /* registered 0 */
    /* AUDIO (port/src/audio.c, D470) */
    { "Audio.MasterVolume", 100 }, /* = 100 (bit-identical passthrough) */
};

static double kResetDefault(const char *key)
{
    for (size_t i = 0; i < sizeof(kResetDefaults) / sizeof(kResetDefaults[0]); i++)
        if (strcmp(kResetDefaults[i].key, key) == 0) return kResetDefaults[i].def;
    return -1.0;
}

/* The section's DECLARED row range (its header through the next header) --
 * NOT the visible list, so conditionally hidden rows (e.g. Aim range while
 * centred) are reset too; their live
 * values are written through the same ptr/rowSet path, visibility
 * irrelevant. Per row, its OWN scope is reset: watch rows -> the selected
 * file's BLANKSAVEDATA values through the normal commit path (front: direct
 * write; stage: D352 queue, applied+persisted by the game thread); ini rows
 * -> the table above (applied live now, persisted like any ini edit when the
 * screen/overlay closes). No cross-scope surprise: a section reset touches
 * only that section's rows, and a file reset never touches other files. */
static void rowResetSection(int iReset)
{
    int hdr = iReset - 1;
    while (hdr >= 0 && rows[hdr].kind != ROW_HEADER) hdr--;
    if (hdr < 0) {
        sysLogPrintf(LOG_WARNING, "optionsoverlay: reset row %d has no section header", iReset);
        return;
    }
    int end = hdr + 1;
    while (end < NUM_ROWS && rows[end].kind != ROW_HEADER) end++;
    int nWatch = 0, nIni = 0, nBinds = 0;
    for (int j = hdr + 1; j < end; j++) {
        struct Row *r = &rows[j];
        if (r->kind == ROW_ACTION) continue;             /* reset rows, Quit */
        if (strcmp(r->key, "__Resolution") == 0) continue;  /* documented exclusion */
        if (strcmp(r->key, "Bond.Control") == 0) continue;  /* D516: the save's style is not a port setting; reset leaves it */
        if (r->kind == ROW_BIND) {
            nBinds += inputBindingResetKey(r->key);
            r->bindSlot = 0;
            continue;
        }
        if (r->kind == ROW_AUDIODEV) { audioDeviceRequest(""); continue; }   /* D470 */
        if (r->kind == ROW_PADBIND) {   /* D469: reset the action on every seat */
            inputPadBindingReset(inputPadActionForKey(r->key));
            r->bindSlot = 0;
            continue;
        }
        if (r->kind == ROW_PADSEAT) continue;
        int field = watchSettingsFieldForKey(r->key);
        if (field >= 0) {
            watchSettingsSet((enum WatchSettingField)field,
                             watchSettingsBlankValue((enum WatchSettingField)field), 1);
            nWatch++;
        } else if (r->found && r->ptr) {
            double def = kResetDefault(r->key);
            if (def < 0.0) continue;
            rowSetCommit(r, def, 1);
            nIni++;
        }
    }
    if (nBinds) inputBindingsApply();
    sysLogPrintf(LOG_INFO, "optionsoverlay: section reset '%s': %d file row(s), %d ini row(s), %d key row(s)",
                 rows[hdr].label, nWatch, nIni, nBinds);
}

/* Arm (first edge) or confirm (second edge within RESET_ARM_US). Callers
 * guarantee edges: the front screen routes only fresh presses (its held-key
 * repeat is suppressed for reset rows) and F10's fresh-press / fresh-click
 * paths; the held-repeat branches skip reset rows entirely. */
static void rowActivateReset(struct Row *r)
{
    if (!isArmRow(r)) return;
    uint64_t now = sysGetMicroseconds();
    int i = (int)(r - rows);
    if (s_resetArmedRow == i && now - s_resetArmedUs <= RESET_ARM_US) {
        s_resetArmedRow = -1;
        if (isPresetRow(r)) {
            /* D440: ini keys only (live-applied; persisted on
             * close like any ini edit). Never window/resolution, bindings,
             * volumes or save-file rows. */
            videoApplyPreset(VIDEO_PRESET_PORT);
            return;
        }
        rowResetSection(i);   /* exactly one commit per confirmed activation */
        return;
    }
    s_resetArmedRow = i;
    s_resetArmedUs = now;
    int h = i - 1;
    while (h >= 0 && rows[h].kind != ROW_HEADER) h--;
    if (isPresetRow(r))
        sysLogPrintf(LOG_INFO, "optionsoverlay: '%s' armed (confirm within 3 s)", r->label);
    else
        sysLogPrintf(LOG_INFO, "optionsoverlay: section reset '%s' armed (confirm within 3 s)",
                     h >= 0 ? rows[h].label : "?");
}

void optionsRowActivateReset(int i)
{
    struct Row *r = rowAt(i);
    if (r) rowActivateReset(r);
}

/* Timeout / navigate-away disarm. selRow = the rows[] index the UI has
 * selected (any other value, e.g. -1, disarms). */
void optionsResetMaintain(int selRow)
{
    if (s_resetArmedRow < 0) return;
    uint64_t now = sysGetMicroseconds();
    if (s_resetArmedRow != selRow || now - s_resetArmedUs > RESET_ARM_US)
        s_resetArmedRow = -1;
}

void optionsResetClear(void)
{
    s_resetArmedRow = -1;
}

/* D440 GE_PRESETPROBE (dev, opt-in, one-shot at overlay init), T4-lite form:
 * drives the Display mode row through the real arm -> confirm activation
 * (rowActivateReset, the path F10 and the front screen use): Modern -> Original
 * N64 -> edit a bundled key -> Custom -> Modern, checking the derived value text
 * at each step. Mutates the live config like a real press -- use a throwaway
 * data dir. */
static void valueText(int i, char *out, int n);
static void presetProbe(void)
{
    int bad = 0;
    int i = rowIndexByKey("__DisplayMode");
    char val[32] = "";
    if (i < 0) { bad++; } else {
        if (displayModeNow() != 0) rowAdjust(&rows[i], +1);   /* start from Modern whatever the ini held */
        valueText(i, val, sizeof(val));
        if (strcmp(val, "Modern") != 0) bad++;
        rowAdjust(&rows[i], +1);                              /* Modern -> Original N64 */
        valueText(i, val, sizeof(val));
        if (strcmp(val, "Original N64") != 0 || !videoPresetIsActive(VIDEO_PRESET_N64)) bad++;
        int tf = rowIndexByKey("Video.TextureFilter");        /* edit one bundled key */
        if (tf >= 0) rowAdjust(&rows[tf], +1);
        valueText(i, val, sizeof(val));
        if (strcmp(val, "Custom") != 0) bad++;
        ddOpen(i);                                            /* dropdown path: pick Modern */
        if (s_ddRow != i || s_ddN != 3 || s_ddCur != 2) bad++;
        ddConfirm(0);
        valueText(i, val, sizeof(val));
        if (strcmp(val, "Modern") != 0 || !videoPresetIsActive(VIDEO_PRESET_PORT)) bad++;
        sysLogPrintf(LOG_INFO, "GE_PRESETPROBE: Modern -> N64 -> edit -> Custom -> (dropdown) Modern, last '%s', bad=%d", val, bad);
    }
    sysLogPrintf(bad ? LOG_ERROR : LOG_INFO, "GE_PRESETPROBE: %s", bad ? "FAIL" : "PASS");
}

/* ------------------------------------------------------------------------ */

/* GE_OVNAV test step (called from the file-select hook, game thread):
 * O toggle, D/U move, A open header/link, B back one level, P log the place. */
static SDL_SpinLock s_ovLock;   /* tentative: the definition below is the same object */
void optionsOverlayTestStep(char c)
{
    SDL_AtomicLock(&s_ovLock);
    if (c == 'O') {
        SDL_AtomicUnlock(&s_ovLock);
        optionsOverlayToggle();
        return;
    }
    if (!s_open) { SDL_AtomicUnlock(&s_ovLock); return; }
    overlayUpdateVisible();
    if (c == 'D') s_sel = overlayStepSel(s_sel, 1);
    else if (c == 'U') s_sel = overlayStepSel(s_sel, -1);
    else if (c == 'A' && s_sel >= 0 && s_sel < s_visN) {
        int i = s_visIdx[s_sel];
        if (rows[i].kind == ROW_HEADER) { if (s_section < 0) overlayOpenHeader(i); }
        else if (optionsRowChildHeader(i) >= 0) overlayOpenLink(i);
    } else if (c == 'B') { if (s_section >= 0) overlayBackOne(); }
    overlayUpdateVisible();
    overlayUpdateScroll();
    if (c == 'P') {
        sysLogPrintf(LOG_INFO, "ovnav: section=%s sel=%s scroll=%d seat=%d vis=%d",
                     s_section >= 0 ? rows[s_section].key : "(root)",
                     (s_sel >= 0 && s_sel < s_visN) ? rows[s_visIdx[s_sel]].key : "-", s_scroll, s_padSeat, s_visN);
        for (int q = 0; q < s_visN; q++)
            if (rows[s_visIdx[q]].kind == ROW_BOND_FILE) sysLogPrintf(LOG_INFO, "ovnav: chooser row visible at %d", q);
    }
    SDL_AtomicUnlock(&s_ovLock);
}

void optionsOverlayToggle(void)
{
    overlayInit();
    if (s_open) {
        if (optionsBindingCaptureActive()) return; /* Escape cancels modal */
        int pending = SDL_AtomicSet(&s_dragWatchField, 0);
        if (pending > 0) watchSettingsCommit(pending - 1);
        optionsAdjustCommitPending();   /* D489 */
        overlayUpdateVisible();
        s_memValid = 1;
        s_memSec = s_section;
        s_memRow = (s_sel >= 0 && s_sel < s_visN) ? s_visIdx[s_sel] : -1;
        s_memScroll = s_scroll;
        s_memSeat = s_padSeat;
    }
    s_open = !s_open;
    ddClose();   /* closing (or reopening) the menu cancels a pending pick */
    if (s_open) {
        s_section = -1;
        s_sel = s_scroll = 0;
        s_scrollSnap = 1;
        if (s_memValid && s_memSec >= 0 && s_memSec < NUM_ROWS && rows[s_memSec].kind == ROW_HEADER) {
            int seat = s_padSeat;
            s_section = s_memSec;
            if (s_memSeat >= 0 && s_memSeat < 4) padSetSeat(s_memSeat);
            overlayUpdateVisible();
            if (s_visN <= 1) {   /* nothing shown there in this context: root */
                s_section = -1;
                padSetSeat(seat);
                overlayUpdateVisible();
            }
        } else if (s_memValid) {
            overlayUpdateVisible();
        }
        if (s_memValid && s_memRow >= 0) {
            int first = s_section >= 0 ? 1 : 0, hit = -1;
            for (int q = first; q < s_visN; q++) if (s_visIdx[q] == s_memRow) { hit = q; break; }
            if (hit >= 0) { s_sel = hit; s_scroll = s_memScroll < 0 ? 0 : s_memScroll; }
            else s_sel = first;   /* row not shown here: top of its section */
            overlayUpdateScroll();
        }
        s_mouseActive = 0;
        s_hover = -1;
        s_seedPrev = 1;
    }
    SDL_AtomicSet(&s_backPending, 0);
    sysLogPrintf(LOG_INFO, "optionsoverlay: %s", s_open ? "opened" : "closed");
    if (!s_open) {
        optionsResetClear();   /* D356: closing the overlay disarms a pending reset */
        configSave();
    }
}

void optionsOverlayBack(void)
{
    /* Called from SDL's host event pump; navigation is scheduler-owned. */
    if (s_open) SDL_AtomicSet(&s_backPending, 1);
}

int optionsOverlayMouseActive(void) { return s_mouseActive; }   /* D567 */

int optionsOverlayIsOpen(void)
{
    if (!s_inited) {
        overlayInit();
    }
    return s_open;
}

void optionsOverlayScroll(int dir)
{
    if (!s_inited) {
        overlayInit();
    }
    if (!s_open || dir == 0) {
        return;
    }
    /* D314: this runs on the host thread (SDL wheel event), while HandleInput
     * and Emit rebuild s_visIdx/s_visN/s_scroll on the scheduler thread; the
     * old direct overlayUpdateVisible() + s_sel write here could leave Emit
     * drawing a half-rebuilt list. Queue the notches instead (D287 pattern);
     * optionsOverlayHandleInput() applies them on the scheduler thread. */
    SDL_AtomicAdd(&s_wheelPending, dir);
}

/* Scheduler thread: apply wheel notches queued by optionsOverlayScroll().
 * wheel-up -> move up the list; clamps at both ends like a normal PC settings
 * list (wrapping read as a duplicate) and skips D237 headers. */
static void overlayApplyWheel(void)
{
    int n = SDL_AtomicSet(&s_wheelPending, 0);
    if (n != 0) s_mouseActive = 0;   /* the wheel moves the selection: cue follows it */
    while (n != 0) {
        if (s_ddRow >= 0) ddMove((n > 0) ? -1 : 1);
        else
        s_sel = overlayStepSel(s_sel, (n > 0) ? -1 : 1);
        n += (n > 0) ? -1 : 1;
    }
}

/* Set a slider row from an overlay-space x inside its value bar, snapped to
 * the row's step. */
static void sliderSetFromX(struct Row *r, double ox)
{
    double lo = rowLo(r), hi = rowHi(r);
    if (hi <= lo) {
        return;
    }
    s32 bx0, bx1;
    sliderBarSpan(&bx0, &bx1);
    double f = (ox - bx0) / (double)(bx1 - bx0);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    double v = rowValueAtFraction(r, f);
    double step = (r->step > 0.0) ? r->step : 1.0;
    v = lround(v / step) * step;
    rowSetCommit(r, v, 0); /* slider drag: save only on release */
}

/* D314 (findings.md): F10 menu items flicker/mis-land on the file-select
 * screen, mechanism unpinned. Ruled out: viGetX/viGetY (stable per-screen,
 * set once by front.c) and non-determinism in the display list. This logs
 * the remaining suspects -- viGetY(), s_visN, s_scroll, s_sel -- once per
 * frame while the overlay is open, to catch whichever one oscillates during
 * a file-select repro. Env-gated, cached once (not re-queried per frame --
 * see D302: a hot-path getenv() regression cost a real bug before). */
static int s_d314Enabled = -1;   /* -1 = not yet resolved */

/* Shared by paint and mouse input: the area right of a setting's name
 * changes it; its name only selects it. Reset actions use the whole row. */
static int overlayControlSpan(int i, s32 *x0, s32 *x1);

/* D481: HandleInput runs on the scheduler thread (controller poll) and Emit on
 * the render worker; both rebuild s_visIdx/s_visN/s_scroll/s_sel. They used to
 * share one thread, so serialise them now that rendering has its own. */
static SDL_SpinLock s_ovLock;

static void overlayHandleInputLocked(void);
void optionsOverlayHandleInput(void)
{
    SDL_AtomicLock(&s_ovLock);
    overlayHandleInputLocked();
    SDL_AtomicUnlock(&s_ovLock);
}

static void overlayHandleInputLocked(void)
{
    static int prevUp, prevDn, prevLf, prevRt, prevLmb, prevRmb, prevPadBack;
    /* D347: hold-to-repeat state (18/4-frame cadence, same as the options
     * screen's D345(e)/(f) blocks). */
    static int navDir = 0, navTimer = 0, adjDir = 0, adjTimer = 0;

    if (!s_open) {
        s_latchArm = 1;   /* D563: whatever is held when it opens is latched */
        prevUp = prevDn = prevLf = prevRt = prevLmb = prevRmb = 0;
        s_dragRow = -1;
        SDL_AtomicSet(&s_dragWatchField, 0);
        optionsAdjustCommitPending();   /* D489: closed by the host mid-hold */
        navDir = adjDir = 0;
        SDL_AtomicSet(&s_wheelPending, 0);
        return;
    }

    if (s_d314Enabled < 0) {
        s_d314Enabled = getenv("GE_D314") ? 1 : 0;
    }
    if (s_d314Enabled) {
        fprintf(stderr, "GE_D314 viGetY=%d visN=%d scroll=%d sel=%d\n",
                (int)viGetY(), s_visN, s_scroll, s_sel);
    }

    /* The overlay owns the mouse while it is open: force the OS cursor free +
     * visible (a stage poll would otherwise leave it locked/hidden). */
    inputSuspendForOverlay();
    if (optionsBindingCaptureTick()) { s_rmbBlock = 1; return; }   /* D544: a right-click bound here must not also go back */

    /* D361: regression probe on the ACTUAL joyPoll/F10 thread (not the
     * game-thread GE_WSPROBE_FRONT hook). A front-end write used to block
     * forever in joyDisablePoll waiting for this very poll to acknowledge it.
     * Opt-in only, once after the file-select save becomes available. */
    {
        static int probe = -1, readyTicks = 0;
        if (probe < 0) probe = getenv("GE_WSPROBE_F10FRONT") ? 1 : 0;
        if (probe == 1 && current_menu == MENU_FILE_SELECT && watchSettingsAvailable() &&
            ++readyTicks >= 120) {
            probe = 2;
            sysLogPrintf(LOG_INFO, "wsf10front: queue Music+FX from controller poll");
            watchSettingsSet(WATCH_SETTING_MUSIC, 4096, 1);
            watchSettingsSet(WATCH_SETTING_FX, 4096, 1);
        }
    }

    /* D356: a pending reset arm expires after 3 s or when the selection
     * leaves the armed row; the overlay's selection is s_visIdx[s_sel]. */
    overlayUpdateVisible();
    /* D541: Steam's Desktop-Mode layout types Escape for the Deck's B while SDL
     * also reads the pad, so one B press arrived as both back paths (Esc event
     * here, pad-B edge below) and backed out two pages. One back per 150 ms. */
    static Uint32 s_lastBackAt = 0;
    if (SDL_AtomicSet(&s_backPending, 0) && SDL_GetTicks() - s_lastBackAt >= 150) {
        s_lastBackAt = SDL_GetTicks();
        if (s_ddRow >= 0) {
            ddClose();   /* Esc cancels the pick, not the page */
        } else if (s_section >= 0) {
            overlayBackOne();
        } else {
            optionsOverlayToggle();
            return;
        }
    }
    optionsResetMaintain(s_section < 0 ? -1 : s_visIdx[s_sel]);

    /* % rows may have appeared/vanished after a setting change. */
    overlayApplyWheel();      /* D314: wheel notches queued by the host thread */
    overlayUpdateScroll();

    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    int mx = 0, my = 0;
    Uint32 mb = SDL_GetMouseState(&mx, &my);
    int lmb = (mb & SDL_BUTTON(SDL_BUTTON_LEFT))  ? 1 : 0;
    int rmb = (mb & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? 1 : 0;

    /* D396: B backs out one F10 page (closes from root); A/X accept/toggle
     * on an edge, not slider adjustment. Only D-pad/left-stick X (or keyboard
     * left/right) adjust sliders, with hold-repeat. The pad is swallowed by
     * input.c while the overlay owns it. Start/Select still close it. */
    int gUp = inputPadButton(inputOverlayOwnerPad(), SDL_CONTROLLER_BUTTON_DPAD_UP)
           || inputPadAxis(inputOverlayOwnerPad(), SDL_CONTROLLER_AXIS_LEFTY) < -12000;
    int gDn = inputPadButton(inputOverlayOwnerPad(), SDL_CONTROLLER_BUTTON_DPAD_DOWN)
           || inputPadAxis(inputOverlayOwnerPad(), SDL_CONTROLLER_AXIS_LEFTY) > 12000;
    int gLf = inputPadButton(inputOverlayOwnerPad(), SDL_CONTROLLER_BUTTON_DPAD_LEFT)
            || inputPadAxis(inputOverlayOwnerPad(), SDL_CONTROLLER_AXIS_LEFTX) < -12000;
    int gRt = inputPadButton(inputOverlayOwnerPad(), SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
            || inputPadAxis(inputOverlayOwnerPad(), SDL_CONTROLLER_AXIS_LEFTX) > 12000;
    int up = ks[SDL_SCANCODE_UP]    || ks[SDL_SCANCODE_KP_8] || gUp;
    int dn = ks[SDL_SCANCODE_DOWN]  || ks[SDL_SCANCODE_KP_2] || gDn;
    int lf = ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_KP_4] || gLf;
    int rightNav = ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_KP_6] || gRt;
    int accept = ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER] ||
                 inputPadButton(inputOverlayOwnerPad(), SDL_CONTROLLER_BUTTON_A) ||
                 inputPadButton(inputOverlayOwnerPad(), SDL_CONTROLLER_BUTTON_X);
    {   /* D541: release hold-off -- Enter (Steam desktop layout) + pad A from
         * one press must not leave a gap that reads as a second accept. */
        static Uint32 s_acceptDownAt = 0;
        Uint32 now = SDL_GetTicks();
        if (accept) s_acceptDownAt = now;
        else if (s_acceptDownAt && now - s_acceptDownAt < 70) accept = 1;
    }
    int padBack = inputPadButton(inputOverlayOwnerPad(), SDL_CONTROLLER_BUTTON_B) || ks[SDL_SCANCODE_BACKSPACE];   /* QoL: Backspace goes back like B */
    if (!rmb) s_rmbBlock = 0;
    if (rmb && !s_rmbBlock && !optionsBindingCaptureActive()) padBack = 1;   /* D544 QoL: right-click goes back like Esc (closes a dropdown first) */
    {   /* D563: held-input latch (see s_latchArm) */
        unsigned cur = (up ? 1u : 0) | (dn ? 2u : 0) | (lf ? 4u : 0) | (rightNav ? 8u : 0) |
                       (accept ? 16u : 0) | (padBack ? 32u : 0);
        if (s_latchArm) { s_latchArm = 0; s_latchMask = cur; }
        s_latchMask &= cur;
        if (s_latchMask & 1u)  up = 0;
        if (s_latchMask & 2u)  dn = 0;
        if (s_latchMask & 4u)  lf = 0;
        if (s_latchMask & 8u)  rightNav = 0;
        if (s_latchMask & 16u) accept = 0;
        if (s_latchMask & 32u) padBack = 0;
    }
    int rt = rightNav || accept;
    if (s_seedPrev) {   /* D519: whatever opened the overlay (A / Enter / click) is still held: not a press */
        s_seedPrev = 0;
        prevUp = up; prevDn = dn; prevLf = lf; prevRt = rt;
        prevLmb = lmb; prevRmb = rmb; prevPadBack = padBack;
    }

    static int prevStart = 0;   /* not reset while closed: a Start held across
                                  close must not re-close on the next open */
    int startNow = inputPadButton(inputOverlayOwnerPad(), SDL_CONTROLLER_BUTTON_START);
    if (startNow && !prevStart) {
        prevStart = startNow;
        optionsOverlayToggle();   /* Start closes */
        return;
    }
    prevStart = startNow;
    if (padBack && !prevPadBack && SDL_GetTicks() - s_lastBackAt < 150) {
        prevPadBack = padBack;   /* D541: same press already backed out via Esc */
        prevUp = up; prevDn = dn; prevLf = lf; prevRt = rt;
        return;
    }
    if (padBack && !prevPadBack) {
        s_lastBackAt = SDL_GetTicks();
        prevPadBack = padBack;
        navDir = adjDir = 0;
        prevUp = up; prevDn = dn; prevLf = lf; prevRt = rt;
        prevLmb = lmb; prevRmb = rmb;   /* the right-click that backed out is not also a row click */
        if (s_ddRow >= 0) ddClose();
        else if (s_section >= 0) overlayBackOne();
        else optionsOverlayToggle();
        return;
    }
    prevPadBack = padBack;

    /* D469: Y or Delete clears the selected pad-binding slot (one press, no modal). */
    {
        static int prevYClr = 0, prevDelClr = 0;
        int yNow = inputPadButton(inputOverlayOwnerPad(), SDL_CONTROLLER_BUTTON_Y), delNow = ks[SDL_SCANCODE_DELETE];
        if (s_section >= 0 && s_sel > 0 && ((yNow && !prevYClr) || (delNow && !prevDelClr))) {
            struct Row *cr = &rows[s_visIdx[s_sel]];
            if (cr->kind == ROW_PADBIND) {
                inputPadBindingSet(s_padSeat, inputPadActionForKey(cr->key), cr->bindSlot, -1);
                configSave();
            }
        }
        prevYClr = yNow; prevDelClr = delNow;
    }

    /* ---- keyboard / D-pad nav (clamped at the ends; scroll follows).
     * D347: hold-to-repeat so a controller (and held keys) can traverse the
     * list without one-press-per-row. */
    {
        int vdir = (dn && !prevDn) ? +1 : (up && !prevUp) ? -1 : 0;
        if (vdir != 0) {
            s_mouseActive = 0;
            navDir = vdir;
            navTimer = 18;
            if (s_ddRow >= 0) ddMove(vdir);
            else s_sel = overlayStepSel(s_sel, vdir);   /* skips headers */
        } else if ((up || dn) && navDir != 0) {
            if (--navTimer <= 0) {
                navTimer = 4;
                if (s_ddRow >= 0) ddMove(navDir);
                else s_sel = overlayStepSel(s_sel, navDir);
            }
        } else {
            navDir = 0;
        }
    }
    overlayUpdateScroll();
    /* ---- value adjust (D347: D-pad/stick left-right wired in; hold repeats) ---- */
    {
        int dir = (rt && !prevRt) ? +1 : (lf && !prevLf) ? -1 : 0;
        if (dir != 0) {
            s_mouseActive = 0;
            adjDir = dir;
            adjTimer = 18;
            if (s_ddRow >= 0) {
                if (accept && !(dir < 0)) ddConfirm(s_ddSel);   /* Enter/A picks; left/right do nothing while open */
                adjDir = 0;
            } else if (s_section < 0) {
                struct Row *hr = &rows[s_visIdx[s_sel]];
                /* D563: on the root hub pages and actions open only on A / Enter
                 * (or a click) -- never on Left/Right/stick/D-pad. */
                if (!accept) {
                } else if (hr->kind == ROW_HEADER)
                    overlayOpenHeader(s_visIdx[s_sel]);
                else if (dir > 0 && !isSepRow(hr))
                    rowAdjust(hr, +1);   /* hub actions: Restart game / Quit to desktop */
                adjDir = 0;
            } else {
                int child = optionsRowChildHeader(s_visIdx[s_sel]);
                if (child >= 0) {
                    if (dir > 0) overlayOpenLink(s_visIdx[s_sel]);
                    adjDir = 0;
                } else if (isBackRow(&rows[s_visIdx[s_sel]])) {
                    if (dir > 0) overlayBackOne();   /* Back row: activate only */
                    adjDir = 0;
                } else {
                struct Row *r = &rows[s_visIdx[s_sel]];
                int keyConfirm = ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER];
                if (r->kind == ROW_PADBIND && dir > 0 && accept && !rightNav) {
                    bindingBegin(r);   /* D469: A/X/Enter starts pad capture; D-pad right changes the slot */
                    adjDir = 0;
                } else if (r->kind == ROW_BIND && dir > 0 && keyConfirm) {
                    bindingBegin(r);
                    adjDir = 0;
                } else if (r->kind == ROW_BIND && dir > 0 && accept && !rightNav) {
                    /* Pad accept does nothing on keyboard-only binding rows;
                     * D-pad/stick right still changes the displayed slot. */
                    adjDir = 0;
                } else if (rowIsDropdown(r) && dir > 0 && accept && !rightNav) {
                    ddOpen(s_visIdx[s_sel]);   /* Enter/A opens the popup; left/right still step */
                    adjDir = 0;
                } else if (r->kind == ROW_SLIDER && dir > 0 && accept && !rightNav) {
                    /* Accept never moves a slider; left/right own detents. */
                    adjDir = 0;
                } else {
                    rowAdjust(r, dir);
                    if (accept && !rightNav) adjDir = 0; /* one-shot toggles */
                }
                }
            }
        } else if ((rt || lf) && adjDir != 0 && s_ddRow < 0) {
            if (--adjTimer <= 0) {
                adjTimer = 4;
                /* D356: reset actions are edge-triggered -- the held-repeat
                 * re-fires adjust rows but must never re-fire a reset (a held
                 * key could arm AND confirm, or commit repeatedly). */
                if (s_section >= 0 && s_sel != 0 && !isArmRow(&rows[s_visIdx[s_sel]]))
                    rowAdjust(&rows[s_visIdx[s_sel]], adjDir);
            }
        } else {
            adjDir = 0;
        }
        /* D489: save a stepped watch slider once, on release (also covers
         * mouse click/right-click steps, which never hold left/right). */
        if (!(rt || lf)) optionsAdjustCommitPending();
    }

    /* ---- mouse ----
     * D316: mx/my are raw window pixels, but the overlay's own 2D content
     * is drawn into whatever on-window rect the safe-area crop currently
     * maps the logical (viGetX() x viGetY()) canvas to -- NOT the full
     * window whenever that rect is inset (default-on: any in-game "Full"
     * viewport insets it). Map through the real forward transform's rect
     * (gfx_get_ui_screen_rect) instead of a naive window-size scale so a
     * click lands on the same row it visually appears over. */
    int32_t rx = 0, ry = 0, rw = 0, rh = 0;
    gfx_get_ui_screen_rect(&rx, &ry, &rw, &rh);
    if (rw > 0 && rh > 0) {
        double ox = (double)(mx - rx) * (double)ovW() / rw;
        {   /* D335b/D472: the card is drawn into a centred 4:3 region under native widescreen */
            f32 na = portNativeAspect();
            if (na > 1.3334f) {
                double vis = (4.0 / 3.0) / (double)na;
                double fx = (double)(mx - rx) / rw;
                ox = ((fx - (1.0 - vis) * 0.5) / vis) * (double)ovW();
            }
        }
        double oy = (double)(my - ry) * (double)ovH() / rh;
        int hoverVis = overlayRowAtY(ox, oy);
        {   /* R2: hover gets the focus cue, never the selection (D304 feedback loop) */
            static int prevMx = -1, prevMy = -1;
            if (prevMx < 0) { prevMx = mx; prevMy = my; }
            int moved = (mx != prevMx || my != prevMy);
            if (moved || (lmb && !prevLmb) || (rmb && !prevRmb)) s_mouseActive = 1;
            prevMx = mx; prevMy = my;
            s_hover = s_mouseActive ? hoverVis : -1;
            if (s_ddRow >= 0) {   /* open dropdown owns the pointer: hover picks, click confirms, outside/right cancels */
                int hit = ddHit(ox, oy);
                if (hit >= 0 && moved) s_ddSel = hit;
                if (lmb && !prevLmb) { if (hit >= 0) ddConfirm(hit); else ddClose(); }
                else if (rmb && !prevRmb) ddClose();
                hoverVis = -1; s_hover = -1;
            }
        }
        /* Opt-in diagnostic for D391: distinguish hit-band/scroll errors
         * from logical SDL mouse coords vs GL drawable pixel coords. */
        static int pointerLog = -1, pointerSamples = 0;
        if (pointerLog < 0) pointerLog = getenv("GE_OVPOINTERMAP") != NULL;
        if (pointerLog && ((lmb && !prevLmb) || ++pointerSamples == 120)) {
            if (pointerSamples >= 120) pointerSamples = 0;
            int winW = 0, winH = 0, drawW = 0, drawH = 0;
            SDL_Window *win = SDL_GetMouseFocus();
            if (!win) win = SDL_GetKeyboardFocus();
            if (win) {
                SDL_GetWindowSize(win, &winW, &winH);
                SDL_GL_GetDrawableSize(win, &drawW, &drawH);
            }
            sysLogPrintf(LOG_INFO, "ovpointer: mouse=%d,%d window=%d,%d drawable=%d,%d rect=%d,%d,%d,%d vi=%d,%d logical=%.1f,%.1f section=%d scroll=%d sel=%d hit=%d key=%s click=%d",
                         mx, my, winW, winH, drawW, drawH, rx, ry, rw, rh,
                         viGetX(), viGetY(), ox, oy, s_section, s_scroll, s_sel,
                         hoverVis, hoverVis >= 0 ? rows[s_visIdx[hoverVis]].key : "-",
                         lmb && !prevLmb);
        }
        /* No hover-to-highlight: merely moving the mouse must not move the
         * selection or scroll the window (hovering near a list edge fed the
         * new row back into the cursor and made the bottom twitch/echo).
         * Selection moves only by click, wheel, or arrows. */

        /* Clicking the name selects; anywhere to its right changes the
         * setting. Reset actions use the whole row. */
        /* D556: scrollbar. Press on the thumb = drag it; press on the track = page up/down
         * (toward the pointer); the drag follows the pointer until release. */
        static int sbDrag = 0, sbGrab = 0;
        int sbTaken = 0;
        struct OvScrollbar sb;
        if (s_ddRow < 0 && overlayScrollbar(&sb)) {
            if (lmb && !prevLmb && ox >= sb.hx0 && ox < sb.hx1 && oy >= sb.ty0 && oy < sb.ty1) {
                if (oy >= sb.th0 && oy < sb.th1) { sbDrag = 1; sbGrab = (int)(oy - sb.th0); }
                else overlayScrollTo(s_scroll + (oy < sb.th0 ? -1 : 1) * maxVisibleRows());
                sbTaken = 1;
            } else if (lmb && sbDrag) {
                double f = ((oy - sbGrab) - sb.ty0) / (double)((sb.ty1 - sb.ty0) - (sb.th1 - sb.th0));
                if (f < 0) f = 0;
                if (f > 1) f = 1;
                overlayScrollTo(sb.first + (int)lround(f * sb.span));
            }
            if (sbDrag) { sbTaken = 1; s_hover = -1; hoverVis = -1; }
        }
        if (!lmb) sbDrag = 0;
        if (lmb && !prevLmb && !sbTaken) {
            if (hoverVis >= 0 && !isSepRow(&rows[s_visIdx[hoverVis]])) {
                s_sel = hoverVis;         /* explicit click -> select */
                overlayUpdateScroll();
                if (rows[s_visIdx[hoverVis]].kind == ROW_HEADER) {
                    if (s_section < 0) overlayOpenHeader(s_visIdx[hoverVis]);
                } else if (optionsRowChildHeader(s_visIdx[hoverVis]) >= 0) {
                    overlayOpenLink(s_visIdx[hoverVis]);
                } else if (isBackRow(&rows[s_visIdx[hoverVis]])) {
                    overlayBackOne();
                } else {
                    int i = s_visIdx[hoverVis];
                    s32 x0, x1;
                    if (overlayControlSpan(i, &x0, &x1) && ox >= x0 && ox < x1) {
                        struct Row *r = &rows[i];
                        if (r->kind == ROW_SLIDER && r->found &&
                            ox >= overlayLayout().barX0 && ox <= overlayLayout().barX1) {
                            sliderSetFromX(r, ox);
                            s_dragRow = i;
                            int field = watchSettingsFieldForKey(r->key);
                            SDL_AtomicSet(&s_dragWatchField, field + 1);
                        } else if (r->kind == ROW_BIND || r->kind == ROW_PADBIND) {
                            bindingBegin(r); /* mouse selects slot via arrows; click captures */
                        } else {
                            if (rowIsDropdown(r)) ddOpen(i);
                            else rowAdjust(r, +1);   /* toggle / cycle forward (wraps) */
                        }
                    }
                }
            }
        }
        /* drag a slider */
        if (lmb && s_dragRow >= 0 && rows[s_dragRow].kind == ROW_SLIDER) {
            sliderSetFromX(&rows[s_dragRow], ox);
        }
        if (!lmb) {
            if (s_dragRow >= 0) {
                int field = watchSettingsFieldForKey(rows[s_dragRow].key);
                if (field >= 0) watchSettingsCommit(field);
            }
            s_dragRow = -1;
            SDL_AtomicSet(&s_dragWatchField, 0);
        }
    }

    prevUp = up; prevDn = dn; prevLf = lf; prevRt = rt;
    prevLmb = lmb; prevRmb = rmb;
}

/* ------------------------------------------------------------------------ */

#define OV_BUF_CMDS 16384
static Gfx s_buf[OV_BUF_CMDS];

static Gfx *fillRect(Gfx *gdl, s32 x0, s32 y0, s32 x1, s32 y1,
                     u8 r, u8 g, u8 b, u8 a)
{
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gdl++, 0, 0, r, g, b, a);
    gDPFillRectangle(gdl++, x0, y0, x1, y1);
    return gdl;
}

static void valueText(int i, char *out, int n)
{
    const struct Row *r = &rows[i];
    double v = rowGet(r);
    if (r->kind == ROW_BOND_FILE) {
        int f = watchSettingsFolder();
        if (f < 0) snprintf(out, n, "Select a profile");
        else snprintf(out, n, "%d", f + 1);
        return;
    }
    if (watchSettingsFieldForKey(r->key) == WATCH_SETTING_CONTROL) {   /* D516 */
        if (v < 0) snprintf(out, n, "Not in a mission");
        else snprintf(out, n, "%s", kOrigStyle[v > 7 ? 7 : (int)v]);
        return;
    }
    if (watchSettingsFieldForKey(r->key) >= 0 && !watchSettingsAvailable()) {
        snprintf(out, n, "Select a profile");
        return;
    }
    if (r->kind == ROW_RES) {
        if (videoIsFullscreen()) {
            snprintf(out, n, "(fullscreen)");
        } else if (s_resFitN <= 0) {
            snprintf(out, n, "(n/a)");
        } else {
            int i2 = s_resFit[s_resSel];
            snprintf(out, n, "%d x %d", kResList[i2][0], kResList[i2][1]);
        }
        return;
    }
    if (r->kind == ROW_ACTION && !strcmp(r->key, "__DisplayMode")) {
        snprintf(out, n, "%s", kDisplayModeName[displayModeNow()]);
        return;
    }
    if (r->kind == ROW_ACTION) {
        if (isSepRow(r) || isBackRow(r)) {
            if (n > 0) out[0] = 0;
        } else if (!strcmp(r->key, "__RestartGame")) {
            if (n > 0) out[0] = 0;   /* was a sentence fragment; the "(restart)" tag explains itself */
        } else if (optionsRowChildHeader(i) >= 0) {
            /* PD's selectable submenu rows use a title such as "Stick
             * Settings...", not a second "Open >" value. The link label
             * above carries that same affordance on both GE PC menus. */
            if (n > 0) out[0] = 0;
        } else if (isResetRow(r)) {
            /* D356: the label is the button; the value column shows the
             * armed state so the confirm step is discoverable. */
            snprintf(out, n, s_resetArmedRow == i ? "Confirm" : "");
        } else if (isPresetRow(r)) {
            /* D440: same arm/confirm cue, plus "Active" while every
             * preset key already holds that preset's value. */
            snprintf(out, n, "%s", s_resetArmedRow == i ? "Confirm" : "");
        } else {
            if (n > 0) out[0] = 0;
        }
        return;
    }
    if (r->kind == ROW_AUDIODEV) {
        const char *cur = audioDeviceCurrentName();
        if (!cur[0]) snprintf(out, n, "Default");
        else if (strlen(cur) > 24) snprintf(out, n, "%.22s..", cur);
        else snprintf(out, n, "%s", cur);
        return;
    }
    if (r->kind == ROW_PADSEAT) {
        snprintf(out, n, "Pad %d%s", s_padSeat + 1, inputPadSeatPresent(s_padSeat) ? "" : " (none)");
        return;
    }
    if (r->kind == ROW_PADBIND) {
        if (optionsBindingCaptureActive() && i == s_bindCaptureRow) {
            snprintf(out, n, "Press a button...");
        } else {
            static const char *const kSlotName[INPUT_BIND_SLOTS] = { "Primary", "Secondary" };
            char nm[32];
            inputPadBindingText(s_padSeat, inputPadActionForKey(r->key), r->bindSlot, nm, sizeof(nm));
            static const struct { const char *a, *b; } pretty[] = {
                { "leftshoulder", "LB" }, { "rightshoulder", "RB" }, { "lefttrigger", "LT" },
                { "righttrigger", "RT" }, { "leftstick", "L-Click" }, { "rightstick", "R-Click" },
                { "dpup", "D-Up" }, { "dpdown", "D-Down" }, { "dpleft", "D-Left" }, { "dpright", "D-Right" },
            };
            const char *show = nm;
            for (size_t k = 0; k < sizeof(pretty) / sizeof(pretty[0]); k++)
                if (!strcmp(nm, pretty[k].a)) show = pretty[k].b;
            {   /* D471: PlayStation / Nintendo names for the pad being edited */
                const char *fam = inputPadSourceFamilyName(s_padSeat, nm);
                if (fam) show = fam;
            }
            snprintf(out, n, "%s: %s", kSlotName[r->bindSlot], show);
        }
        return;
    }
    if (r->kind == ROW_BIND) {
        if (optionsBindingCaptureActive() && i == s_bindCaptureRow)
            snprintf(out, n, "Press a key...");
        else if (s_bindError && i == s_bindCaptureRow)
            snprintf(out, n, "Binding too long");
        else {
            static const char *const kSlotName[INPUT_BIND_SLOTS] = {
                "Primary", "Secondary",
            };
            snprintf(out, n, "%s: %s", kSlotName[r->bindSlot],
                     inputBindingSlot(r->key, r->bindSlot));
        }
        return;
    }
    if (r->kind == ROW_MSAA) {
        if ((int)lround(v) <= 1) snprintf(out, n, "None");
        else                     snprintf(out, n, "%dx", (int)lround(v));
        return;
    }
    if ((r->kind == ROW_TOGGLE || r->kind == ROW_ENUM) && r->names) {
        int idx = (int)lround(v);
        int cnt = 0;
        while (r->names[cnt]) cnt++;
        if (idx >= 0 && idx < cnt) {
            snprintf(out, n, "%s", r->names[idx]);
            return;
        }
    }
    if (!strcmp(r->key, "Video.FovScale")) {   /* D546: vertical degrees, 60 = N64 */
        snprintf(out, n, "%d deg", (int)lround(v * 0.6));
        return;
    }
    if (!strcmp(r->key, "Input.MouseSensitivity")) {   /* D443/D357: raw 100 = 1.0x */
        snprintf(out, n, "%.1fx", v / 100.0);
        return;
    }
    if (!strcmp(r->key, "Game.ScreenShakeIntensity")) {   /* D181: shown as whole % */
        snprintf(out, n, "%d%%", (int)lround(v * 100.0));
        return;
    }
    if (!strcmp(r->key, "Input.RumbleScale")) {   /* D357: whole %, like deadzone/volume; storage stays 0..1 */
        snprintf(out, n, "%d%%", (int)lround(v * 100.0));
        return;
    }
    if (r->kind == ROW_SLIDER && r->type == CONFIG_OPT_FLOAT) {
        snprintf(out, n, "%.2f", v);
        return;
    }
    if (r->kind == ROW_FPSCAP) {
        int fps = (int)lround(v);
        if (fps <= 0) snprintf(out, n, "Uncapped");
        else          snprintf(out, n, "%d FPS", fps);
        return;
    }
    /* D506: multipliers (raw 100 = 1.0x): vertical mouse and controller look sensitivity. */
    if (!strcmp(r->key, "Input.MouseYScale") || !strcmp(r->key, "Input.PadLookSensX") ||
        !strcmp(r->key, "Input.PadLookSensY")) {
        snprintf(out, n, "%.1fx", v / 100.0);
        return;
    }
    /* D506: controller look smoothing, raw 0-10 -> 0..100 %. */
    if (!strcmp(r->key, "Input.PadLookSmooth")) {
        snprintf(out, n, "%d%%", (int)lround(v) * 10);
        return;
    }
    /* D506: draw / LOD distance are multiples of the authored distance
     * (ini 100..800 %): 2.5x by default. */
    if (strcmp(r->key, "Video.DrawDistance") == 0 ||
        strcmp(r->key, "Video.FogDistance") == 0 ||
        strcmp(r->key, "Video.LodDistance") == 0) {
        char t[16];
        snprintf(t, sizeof(t), "%.2f", v / 100.0);
        size_t tl = strlen(t);
        if (tl > 1 && t[tl - 1] == '0') t[tl - 1] = 0;   /* 2.50 -> 2.5, 2.75 stays */
        snprintf(out, n, "%sx", t);
        return;
    }
    /* D346: integer sliders -- optional raw->display divide + unit suffix. */
    if (r->dispDiv > 0) {
        v = (double)(int)lround(v / (double)r->dispDiv);
    }
    if (r->unit) {
        snprintf(out, n, "%d%s", (int)lround(v), r->unit);
    } else {
        snprintf(out, n, "%d", (int)lround(v));
    }
}

/* D558: textRenderGlyph (src/game/textrelated.c) drops any glyph whose UNSCALED x is
 * > viGetX() or whose y is > viGetY() -- the game's current 2D canvas: 320x240 in a level,
 * 440x330 on the front end. The overlay lays out on its own canvas (ovW() x ovH(), up to
 * 427 wide at HUD 75) and draws body text through the OV_TEXT_PCT span scale about the
 * string start, so the glyph x the game tests is the start plus the UNscaled advance (up
 * to 1/0.78 of the drawn width). In a level that cut every long tip at x = 320 (front end:
 * 440, so it never showed). Widen the canvas the game tests for the duration of the one
 * textRender call (limits maxX/maxY in unscaled space); restored right after. */
static Gfx *textRenderWide(Gfx *gdl, s32 *px, s32 *py, char *str, struct fontchar *chars,
                           struct font *font, u32 colour, s32 lw, s32 lh, s32 maxX, s32 maxY)
{
    s16 ox = viGetX(), oy = viGetY();
    s16 nx = ox, ny = oy;
    if (maxX + 1 > nx) nx = (s16)(maxX + 1);
    if (maxY + 1 > ny) ny = (s16)(maxY + 1);
    if (nx != ox || ny != oy) viSetXY(nx, ny);
    gdl = textRender(gdl, px, py, str, chars, font, colour, lw, lh, 0, 0);
    if (nx != ox || ny != oy) viSetXY(ox, oy);
    return gdl;
}

static Gfx *drawText(Gfx *gdl, s32 x, s32 y, const char *str, u32 colour)
{
    s32 px = x, py = y;
    /* width/height are the on-screen CLIP rect textRenderGlyph tests against
     * (clipX=start x, clipY=start y, +clipWidth/+clipHeight), NOT the text's
     * own measured size -- passing the measured w/h clipped every glyph out
     * (baseline+height > measured h => nothing drawn).  Match the game: pass
     * the full 2D viewport, like bondview2.c's debug-text path. */
    return textRenderWide(gdl, &px, &py, (char *)str, ptrFontBankGothicChars,
                          ptrFontBankGothic, colour, ovW(), ovH(), ovW(), ovH());
}

static s32 measureText(const char *str)
{
    s32 h = 0, w = 0;
    textMeasure(&h, &w, (char *)str, ptrFontBankGothicChars, ptrFontBankGothic, 0);
    return w;
}

/* Right-aligned: the string ends at xr. */
static Gfx *drawTextR(Gfx *gdl, s32 xr, s32 y, const char *str, u32 colour)
{
    return drawText(gdl, xr - measureText(str), y, str, colour);
}

/* The N64 watch pages use Bank Gothic with a dim/bright green palette.
 * Reuse that same loaded font here, without invoking watch rendering. */
static struct font *bodyFont(void)
{
    /* R5: Zurich Bold is GE's only mixed-case face; Bank Gothic (caps-only) is the fallback. */
    return ptrFontZurichBold && ptrFontZurichBoldChars ? ptrFontZurichBold : ptrFontBankGothic;
}
static struct fontchar *bodyChars(void)
{
    return ptrFontZurichBold && ptrFontZurichBoldChars ? ptrFontZurichBoldChars : ptrFontBankGothicChars;
}
static s32 bodyWidth(const char *str)
{
    s32 h = 0, w = 0;
    if (!bodyFont() || !bodyChars()) return (s32)strlen(str) * 5;   /* fonts not loaded yet (early poll tick) */
    textMeasure(&h, &w, (char *)str, bodyChars(), bodyFont(), 0);
    return w * OV_TEXT_PCT / 100;
}
/* Zurich Bold is ~12 units tall, too big for PD-sized rows: each string is drawn
 * through the HUD span scale about its own origin (fast3d G_HUDSCALE_EXT), so
 * bodyWidth() reports the scaled width. */
static Gfx *drawBody(Gfx *gdl, s32 x, s32 y, const char *str, u32 colour)
{
    s32 px = x, py = y;
    gSPHudScaleEXT(gdl++, OV_TEXT_PCT * 256 / 100, x * 4, y * 4);
    /* D554: textRender clips against its width/height in UNscaled glyph space, but the
     * string is drawn through the OV_TEXT_PCT span scale about (x, y). Pass the limits
     * mapped back into unscaled space, else a string starting right of centre is cut
     * early when ovW() is small (HUD 150: 213) even though it fits on screen. */
    s32 lw = x + (ovW() - x) * 100 / OV_TEXT_PCT, lh = y + (ovH() - y) * 100 / OV_TEXT_PCT;
    gdl = textRenderWide(gdl, &px, &py, (char *)str, bodyChars(), bodyFont(),
                         colour, lw, lh, ovW() * 100 / OV_TEXT_PCT, ovH() * 100 / OV_TEXT_PCT);
    gSPHudScaleEXT(gdl++, 256, 0, 0);
    return gdl;
}
static Gfx *drawBodyR(Gfx *gdl, s32 xr, s32 y, const char *str, u32 colour)
{
    return drawBody(gdl, xr - bodyWidth(str), y, str, colour);
}

/* No per-value boxes: the full strip after the rendered setting name is
 * active, even if there's space between its name and its bar/value. Its
 * boundary is capped at the label's clipping limit for long names. */
static int overlayControlSpan(int i, s32 *x0, s32 *x1)
{
    struct OvLayout o = overlayLayout();
    const struct Row *r = &rows[i];
    char val[64];
    if (r->kind == ROW_HEADER || (r->kind == ROW_SLIDER && !r->found) || isSepRow(r))
        return 0;
    *x1 = o.right - 7;
    if (isArmRow(r) || isBackRow(r) || r->hub) {
        *x0 = o.left + 7; /* reset/preset is an action, not a setting name */
    } else {
        valueText(i, val, sizeof(val));
        s32 limit = r->kind == ROW_SLIDER ? o.barX0 :
                    o.valueR - bodyWidth(val) - 7;
        if (r->restart) limit = o.barX0 - 6;
        *x0 = o.labelX + bodyWidth(r->label) + 4;
        if (*x0 > limit) *x0 = limit;
    }
    return *x1 > *x0;
}
static int sectionIsMixedScope(int header)
{
    if (header < 0) return 0;
    int scoped = 0, global = 0;
    for (int i = header + 1; i < NUM_ROWS && rows[i].kind != ROW_HEADER; i++) {
        if (rows[i].kind == ROW_ACTION) continue;
        if (rows[i].saveScoped) scoped = 1;
        else global = 1;
    }
    return scoped && global;
}

/* ---- menu PD parity: palette + drawing helpers ----
 * Palette = PD's own green dialog colours (g_MenuColours[], src/game/menu.c:110,
 * PD is MIT, Ryan Dwyer). RGBA, alpha as PD stores it (0x7f = ~50 %). */
#define PD_DIALOG_BORDER1   0x00bf007fu   /* left/lower border, title gradient start */
#define PD_TITLEBG          0x0050007fu   /* title gradient middle */
#define PD_DIALOG_BORDER2   0x00ff007fu   /* right border, title gradient end */
#define PD_TITLE_TEXT       0xffff00ffu   /* yellow */
#define PD_BODY             0x002f00c8u   /* PD stores 0x7f; raised after the first look (maintainer: easier to read) */
#define PD_ITEM_UNFOCUSED   0x55ff55ffu
#define PD_ITEM_DISABLED    0x006f00afu
#define PD_FOCUS_INNER      0xffffffffu
#define PD_FOCUS_OUTER      0x004400ffu
static const u8 PAL_TITLE_L[3]  = {   0, 191,   0 };   /* border1 */
static const u8 PAL_TITLE_M[3]  = {   0,  80,   0 };   /* titlebg */
static const u8 PAL_TITLE_R[3]  = {   0, 255,   0 };   /* border2 */

static u8 lerpU8(int a, int b, int num, int den)
{
    return (u8)(a + (b - a) * num / den);
}

static Gfx *fillRectC(Gfx *gdl, s32 x0, s32 y0, s32 x1, s32 y1, u32 rgba)
{
    return fillRect(gdl, x0, y0, x1, y1, (u8)(rgba >> 24), (u8)(rgba >> 16), (u8)(rgba >> 8), (u8)rgba);
}

/* Horizontal 3-stop gradient (PD title strip), drawn as thin slices. */
static Gfx *fillGradH3(Gfx *gdl, s32 x0, s32 y0, s32 x1, s32 y1,
                       const u8 *c0, const u8 *cm, const u8 *c1, u8 a)
{
    s32 w = x1 - x0;
    if (w <= 0) return gdl;
    s32 n = (w + 5) / 6;
    if (n > 64) n = 64;
    for (s32 k = 0; k < n; k++) {
        s32 sx0 = x0 + w * k / n, sx1 = x0 + w * (k + 1) / n;
        int t = (int)(2 * k + 1), d = (int)(2 * n);   /* slice centre, 0..1 */
        u8 r, g, b;
        if (2 * t < d) {
            r = lerpU8(c0[0], cm[0], 2 * t, d); g = lerpU8(c0[1], cm[1], 2 * t, d); b = lerpU8(c0[2], cm[2], 2 * t, d);
        } else {
            r = lerpU8(cm[0], c1[0], 2 * t - d, d); g = lerpU8(cm[1], c1[1], 2 * t - d, d); b = lerpU8(cm[2], c1[2], 2 * t - d, d);
        }
        gdl = fillRect(gdl, sx0, y0, sx1, y1, r, g, b, a);
    }
    return gdl;
}

/* PD menugfxDrawShimmer: a 40-unit-wide soft streak sweeping along a bar edge.
 * Wall-clock driven (render-rate independent); clipped to [x0, x1). */
static Gfx *drawShimmer(Gfx *gdl, s32 x0, s32 x1, s32 y0, s32 y1, int periodMs, int phaseMs)
{
    s32 span = (x1 - x0) + 80;
    s32 head = (s32)(((sysGetMicroseconds() / 1000 + (uint64_t)phaseMs) % (uint64_t)periodMs) * (uint64_t)span / (uint64_t)periodMs) - 40;
    for (int k = 0; k < 10; k++) {
        s32 sx0 = x0 + head + k * 4, sx1 = sx0 + 4;
        int d = k < 5 ? k : 9 - k;   /* triangle 0..4..0 */
        if (sx1 <= x0 || sx0 >= x1) continue;
        if (sx0 < x0) sx0 = x0;
        if (sx1 > x1) sx1 = x1;
        gdl = fillRect(gdl, sx0, y0, sx1, y1, 200, 255, 200, (u8)(24 + d * 22));
    }
    return gdl;
}

/* PD focused-item colour: the unfocused green pulses toward white
 * (menuGetSinOscFrac(40): 40-tick period = 2/3 s). Wall-clock cosine. */
static u32 pulseInk(void)
{
    int ph = (int)((sysGetMicroseconds() / 1000) % 667);       /* 0..666 ms */
    int w = ph < 333 ? ph : 667 - ph;                           /* 0..333..0 */
    int t = w * 1000 / 333;                                     /* 0..1000 */
    t = t * t * (3000 - 2 * t) / 1000000;                       /* smoothstep */
    u8 r = lerpU8(0x55, 0xff, t, 1000), g = lerpU8(0xff, 0xff, t, 1000), b = lerpU8(0x55, 0xff, t, 1000);
    return ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | 0xffu;
}

/* "KEY BINDINGS" -> "Key Bindings"; digits/punctuation/parentheses untouched. */
static void titleCase(const char *in, char *out, int n)
{
    int i = 0, startWord = 1, lower = 0;
    for (const char *q = in; *q; q++) lower |= (*q >= 'a' && *q <= 'z');
    if (lower) { snprintf(out, n, "%s", in); return; }   /* already mixed case ("PC Options", "Quit to desktop") */
    for (; in[i] && i < n - 1; i++) {
        char c = in[i];
        if (c >= 'A' && c <= 'Z') { if (!startWord) c = (char)(c - 'A' + 'a'); }
        else if (c >= 'a' && c <= 'z') { if (startWord) c = (char)(c - 'a' + 'A'); }
        startWord = (c == ' ' || c == '(');
        out[i] = c;
    }
    out[i] = 0;
}

/* PD textRenderProjected focus glow: the label drawn once per axis neighbour in
 * the dark outline colour under the real text. */
static Gfx *drawBodyOutlined(Gfx *gdl, s32 x, s32 y, const char *str, u32 colour)
{
    gdl = drawBody(gdl, x - 1, y, str, PD_FOCUS_OUTER);
    gdl = drawBody(gdl, x + 1, y, str, PD_FOCUS_OUTER);
    gdl = drawBody(gdl, x, y - 1, str, PD_FOCUS_OUTER);
    gdl = drawBody(gdl, x, y + 1, str, PD_FOCUS_OUTER);
    return drawBody(gdl, x, y, str, colour);
}

/* PD checkbox (menugfxDrawCheckbox): a square at the row's right edge, outlined
 * in the item colour, filled when on. */
static Gfx *drawCheckbox(Gfx *gdl, s32 xr, s32 rowY, int on, u32 ink)
{
    s32 x0 = xr - 9, y0 = rowY + 2, x1 = xr, y1 = rowY + 11;
    gdl = fillRectC(gdl, x0, y0, x1, y0 + 1, ink);
    gdl = fillRectC(gdl, x0, y1 - 1, x1, y1, ink);
    gdl = fillRectC(gdl, x0, y0 + 1, x0 + 1, y1 - 1, ink);
    gdl = fillRectC(gdl, x1 - 1, y0 + 1, x1, y1 - 1, ink);
    if (on) gdl = fillRectC(gdl, x0 + 2, y0 + 2, x1 - 2, y1 - 2, ink);
    return gdl;
}

/* D520: antialiased overlay shapes. The overlay lays out on a 320-wide canvas
 * scaled to the window (ovScale), so rect-built diagonals step in whole canvas
 * pixels that grow with the window; PD draws these as triangles that MSAA
 * smooths. Here each edge pixel gets its coverage as alpha instead. One
 * render-state setup per shape, then 2 commands per piece (DL budget). */
static Gfx *aaBegin(Gfx *gdl)
{
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    return gdl;
}

static Gfx *aaPiece(Gfx *gdl, s32 x0, s32 y0, s32 x1, s32 y1, u32 rgba, double cov)
{
    int a = (int)((rgba & 0xff) * cov + 0.5);
    if (a < 6 || x1 <= x0 || y1 <= y0) return gdl;
    gDPSetPrimColor(gdl++, 0, 0, (u8)(rgba >> 24), (u8)(rgba >> 16), (u8)(rgba >> 8), (u8)(a > 255 ? 255 : a));
    gDPFillRectangle(gdl++, x0, y0, x1, y1);
    return gdl;
}

/* Downward triangle: top edge `w` canvas px wide centred on cx at y0, apex
 * at y0 + rows. Per-pixel area coverage from 4 sub-rows. */
static Gfx *drawTriDownAA(Gfx *gdl, double cx, s32 y0, s32 rows, double w, u32 ink)
{
    gdl = aaBegin(gdl);
    for (s32 r = 0; r < rows; r++) {
        double cov[16] = {0};
        s32 px0 = (s32)(cx - w / 2) - 1;
        for (int s = 0; s < 4; s++) {
            double half = w / 2 * (1.0 - (r + (s + 0.5) / 4.0) / rows);
            double l = cx - half, rr = cx + half;
            for (int k = 0; k < 16; k++) {
                double a = px0 + k, b = a + 1;
                double o = (rr < b ? rr : b) - (l > a ? l : a);
                if (o > 0) cov[k] += o / 4.0;
            }
        }
        for (int k = 0; k < 16; k++)
            gdl = aaPiece(gdl, px0 + k, y0 + r, px0 + k + 1, y0 + r + 1, ink, cov[k]);
    }
    return gdl;
}

/* PD slider (menugfxRenderSlider): a ramp-triangle track rising to the right,
 * the passed part bright, plus a small triangle marker under the track.
 * D520: solid body as runs of equal whole height, plus a coverage-alpha top
 * pixel per column, so the ramp is smooth at any window size. */
static Gfx *drawWedge(Gfx *gdl, s32 x0, s32 x1, s32 baseY, double f, u32 ink)
{
    const s32 H = 8;
    s32 span = x1 - x0;
    if (span < 8) return gdl;
    s32 mx = x0 + (s32)(span * f);
    gdl = aaBegin(gdl);
    s32 runX = x0, runH = -1;
    u32 runC = 0;
    for (s32 x = x0; x <= x1; x++) {
        double h = 1.0 + (H - 1) * ((x - x0) + 0.5) / span;
        s32 hi = (s32)h;
        u32 c = x < mx ? ink : PD_ITEM_DISABLED;
        if (x == x1 || hi != runH || c != runC) {   /* flush the solid run */
            if (runH > 0) gdl = aaPiece(gdl, runX, baseY - runH, x, baseY, runC, 1.0);
            runX = x; runH = hi; runC = c;
        }
        if (x < x1) gdl = aaPiece(gdl, x, baseY - hi - 1, x + 1, baseY - hi, c, h - hi);
    }
    return drawTriDownAA(gdl, mx + 0.5, baseY + 1, 3, 6.0, PD_FOCUS_INNER);
}

static Gfx *overlayEmitLocked(void);

/* D555 rework: Input.CrosshairCursor -- the mouse pointer over the overlay is the
 * game's own crosshair sprite (crosshairimage, 32x32), drawn through the same
 * texSelect + display_image_at_position the front end's frontDrawCursor uses
 * (white, alpha 220). Size: 16 half-extent in the game's own VI canvas units
 * (frontDrawCursor: image->width/2; gunDrawSight: 16.0), i.e. 16/ovScale() in
 * this 320-wide overlay canvas, with gunDrawSight's native-widescreen x fix.
 * Drawn after the card (and outside the centred-aspect block) so it floats over
 * everything and follows the real mouse across the whole window. */
static Gfx *drawCrosshairPointer(Gfx *gdl, s32 W, s32 H)
{
    if (!inputCrosshairCursorOn() || !crosshairimage || !SDL_GetMouseFocus()) return gdl;
    /* D562: only while the mouse is the active input (moved/clicked since the last
     * pad/keyboard nav); a pad/keyboard-driven menu has no pointer to block rows.
     * The OS cursor stays hidden regardless (inputApplyMouseRequests). */
    if (!s_mouseActive) return gdl;
    double px, py;
    f32 halfedxy[2];
    double fx, fy, xs;
    if (current_menu != MENU_RUN_STAGE && current_menu != MENU_INVALID &&
        inputFrontEndCursorUiFrac(&fx, &fy, &xs) && viGetX() > 0 && viGetY() > 0) {
        /* Front end: sit exactly over the game's own cursor (frontDrawCursor draws at
         * floor(cursor_pos + 0.5), half-extent image->width/2 in the vi canvas). */
        px = fx * W; py = fy * H;
        halfedxy[0] = (f32)((crosshairimage->width * 0.5) / viGetX() * W * xs);
        halfedxy[1] = (f32)((crosshairimage->height * 0.5) / viGetY() * H);
    } else {
        int mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        int32_t rx = 0, ry = 0, rw = 0, rh = 0;
        gfx_get_ui_screen_rect(&rx, &ry, &rw, &rh);
        if (rw <= 0 || rh <= 0) return gdl;
        px = (double)(mx - rx) * (double)W / rw;
        py = (double)(my - ry) * (double)H / rh;
        f32 half = 16.0f / ovScale();
        halfedxy[0] = halfedxy[1] = half;
        extern float portNativeAspect(void);
        if (portNativeAspect() > 0.0f) halfedxy[0] *= (4.0f / 3.0f) / portNativeAspect();
    }
    if (px < 0.0) px = 0.0; else if (px > W) px = W;
    if (py < 0.0) py = 0.0; else if (py > H) py = H;
    f32 xypos[2] = { (f32)(s32)(px + 0.5), (f32)(s32)(py + 0.5) };
    {   /* D570: drawn at non-integer scales; clamp so the edge never wraps */
        struct sImageTableEntry clamped = *crosshairimage;
        clamped.flagsS = G_TX_NOMIRROR | G_TX_CLAMP;
        clamped.flagsT = G_TX_NOMIRROR | G_TX_CLAMP;
        texSelect(&gdl, &clamped, 4, 0, 0);
    }
    display_image_at_position(&gdl, xypos, halfedxy, 32, 32, 0, 0, 1, 255, 255, 255, 220,
                              (crosshairimage->level > 0), 0);
    return gdl;
}
static int tipWrap(const char *tip, s32 av, char *l1, size_t n1, char *l2, size_t n2);   /* D554 */
/* D554: every control-hint line has a full form (card budget 296) and a compact form
 * used only when the full one is wider than the card (HUD 150: 189). Pad button
 * placeholders stay; hintFit() also falls back to Xbox letters as a last resort. */
struct HintLine { const char *full, *compact; };
static const struct HintLine kHint[] = {
    /*  0 */ { "Press a pad button, or hold {B} to bind {B}", "Press a button   Hold {B}: Bind {B}" },
    /*  1 */ { "Tap {B}/Esc: Cancel   Hold {BACK}: Clear",    "Tap {B}: Cancel   Hold {BACK}: Clear" },
    /*  2 */ { "Press a key or mouse button (1-5)",           "Press key or mouse 1-5" },
    /*  3 */ { "{B}/Esc: Cancel   Del: Clear",                "{B}: Cancel   Del: Clear" },
    /*  4 */ { "{A}/Enter: Select",                           "{A}: Select" },
    /*  5 */ { "{B}/Esc/F10: Close",                          "{B}/Esc/F10: Close" },
    /*  6 */ { "{A}/Enter: Bind   Left/Right: Slot   {Y}: Clear", "{A}: Bind   L/R: Slot   {Y}: Clear" },
    /*  7 */ { "{B}/Esc: Back   F10: Close",                  "{B}: Back   F10: Close" },
    /*  8 */ { "Keyboard and mouse only   Left/Right: Slot",  "Keys/mouse only   L/R: Slot" },
    /*  9 */ { "Enter: Bind   {B}/Esc: Back",                 "Enter: Bind   {B}: Back" },
    /* 10 */ { "{A}/Enter: Select   Left/Right: Change",      "{A}: Select   L/R: Change" },
};
enum { H_PADCAP1, H_PADCAP2, H_KEYCAP1, H_KEYCAP2, H_HUB1, H_HUB2, H_CTRL1, H_BACK, H_BIND1, H_BIND2, H_SEC1 };
static void tipSelfCheck(void);

Gfx *optionsOverlayEmit(void)
{
    SDL_AtomicLock(&s_ovLock);   /* D481: see optionsOverlayHandleInput */
    Gfx *dl = overlayEmitLocked();
    SDL_AtomicUnlock(&s_ovLock);
    return dl;
}

static Gfx *overlayEmitLocked(void)
{
    if (!s_inited) {
        overlayInit();
    }

    fpsTick();

    {   /* D558 test hook: GE_OPTIONSOVERLAY_ATFRAME=<n> re-applies the open request at emit n (read once). */
        static int s_atFrame = -2, s_emitN = 0;
        if (s_atFrame == -2) {
            const char *af = getenv("GE_OPTIONSOVERLAY_ATFRAME");
            s_atFrame = af ? atoi(af) : -1;
        }
        if (s_atFrame >= 0 && s_emitN++ == s_atFrame) {
            const char *oe = getenv("GE_OPTIONSOVERLAY");
            if (oe && atoi(oe) != 0) {
                s_section = -1;
                overlayApplyTestRequest(oe);
                sysLogPrintf(LOG_INFO, "optionsoverlay: GE_OPTIONSOVERLAY_ATFRAME applied at emit %d", s_emitN - 1);
            }
        }
    }

    if (!s_open) {
        if (!s_showFps || !s_fpsText[0]) {
            return NULL;   /* nothing appended -> golden dumps byte-identical */
        }
        /* D213: FPS-only mini DL (top-right), panel closed. */
        const s32 fw = ovW();
        const s32 fh = ovH();
        gfx_set_overlay_scale(ovScale());   /* D510 */
        Gfx *fgdl = s_buf;
        gDPPipeSync(fgdl++);
        gDPSetCycleType(fgdl++, G_CYC_1CYCLE);
        gDPSetTexturePersp(fgdl++, G_TP_NONE);
        gDPSetScissor(fgdl++, G_SC_NON_INTERLACE, 0, 0, viGetX(), viGetY());
        fgdl = microcode_constructor(fgdl);
        fgdl = drawTextR(fgdl, fw - 6, 6, s_fpsText, 0x40ff60ff);
        gDPPipeSync(fgdl++);
        gSPEndDisplayList(fgdl++);
        return s_buf;
    }

    overlayUpdateVisible();
    overlayUpdateScroll();
    if (s_scrollSnap) {
        s_scrollF = s_scroll;
        s_scrollSnap = 0;
    } else {   /* PD SMOOTHSCROLLABLE: ease toward the target row */
        double d = (double)s_scroll - s_scrollF;
        s_scrollF = (d > -0.02 && d < 0.02) ? (double)s_scroll : s_scrollF + d * 0.3;
    }

    const s32 W = ovW();
    const s32 H = ovH();
    gfx_set_overlay_scale(ovScale());   /* D510 */
    struct OvLayout o = overlayLayout();
    const int first = s_section >= 0 ? 1 : 0;
    int p0 = (int)s_scrollF;
    if (p0 < first) p0 = first;
    int pLast = p0 + o.maxRows;   /* one extra row while gliding; the scissor trims it */
    if (pLast >= s_visN) pLast = s_visN - 1;
    const s32 bx0 = o.barX0, bx1 = o.barX1;
    /* Focus cue row: the hovered row while the mouse drives, else the selection. */
    const int cueP = (s_mouseActive && s_hover >= 0 && s_hover < s_visN) ? s_hover : s_sel;
    Gfx *gdl = s_buf;

    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetTexturePersp(gdl++, G_TP_NONE);
    gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, viGetX(), viGetY());

    /* Menu PD parity (R1/R4): PD dialog anatomy in PD's own green palette -- a
     * content-sized centred box, gradient title bar (2 units wider each side)
     * with shimmer streaks and a shadowed yellow title, flat ~50 % body, 1 px
     * borders. No projected walls / grow-in / background blur (logged gaps). */
    gdl = fillRect(gdl, 0, 0, W, H, 0, 0, 0, 40);   /* light dim only; the frozen game shows through the body */
    /* D335b/D472: the card + text are drawn under the centre aspect mode so
     * native widescreen shows an undistorted, centred 4:3 panel (no emission at
     * <= 4:3: byte-identical there). */
    PORT_HUD_ASPECT(gdl, GE_HUD_ASPECT_CENTER);
    {
        gdl = fillGradH3(gdl, o.left - 2, o.top, o.right + 2, o.titleB,
                         PAL_TITLE_L, PAL_TITLE_M, PAL_TITLE_R, 127);
        gdl = drawShimmer(gdl, o.left - 2, o.right + 2, o.top, o.top + 1, 2400, 0);
        gdl = drawShimmer(gdl, o.left - 2, o.right + 2, o.titleB - 1, o.titleB, 2400, 1200);
        gdl = fillRectC(gdl, o.left, o.titleB, o.right, o.titleB + 1, PD_DIALOG_BORDER1);   /* line under the title bar */
        gdl = fillRectC(gdl, o.left, o.titleB + 1, o.right, o.bottom, PD_BODY);
        gdl = fillRectC(gdl, o.left, o.titleB + 1, o.left + 1, o.bottom, PD_DIALOG_BORDER1);
        gdl = fillRectC(gdl, o.right - 1, o.titleB + 1, o.right, o.bottom, PD_DIALOG_BORDER2);
        gdl = fillGradH3(gdl, o.left, o.bottom - 1, o.right, o.bottom, PAL_TITLE_L, PAL_TITLE_L, PAL_TITLE_R, 127);
    }

    /* Hint texts (computed here so the hint panel fills before any text draws). */
    const struct HintLine *help1 = NULL, *help2 = NULL;
    if (optionsBindingCaptureActive() && s_padCapMode) {
        help1 = &kHint[H_PADCAP1];
        help2 = &kHint[H_PADCAP2];
    } else if (optionsBindingCaptureActive()) {
        help1 = &kHint[H_KEYCAP1];
        help2 = &kHint[H_KEYCAP2];
    } else {   /* D556: control hints are always drawn (never tied to the pointer) */
        if (s_section < 0) {
            help1 = &kHint[H_HUB1];
            help2 = &kHint[H_HUB2];
        } else if (!strcmp(rows[s_section].key, "__HdrController")) {
            help1 = &kHint[H_CTRL1];
            help2 = &kHint[H_BACK];
        } else if (!strcmp(rows[s_section].key, "__HdrBindings")) {
            help1 = &kHint[H_BIND1];
            help2 = &kHint[H_BIND2];
        } else {
            help1 = &kHint[H_SEC1];
            help2 = &kHint[H_BACK];
        }
    }
    /* D507: description of the focused row (any input source), first line under
     * the box; the key hints, when shown, move down one line to make room. */
    /* D544 QoL: with the mouse in use, the hovered row's tip (hover is a cue, never the selection). */
    int helpVis = (s_mouseActive && s_hover >= 0 && s_hover < s_visN) ? s_hover : s_sel;
    const char *rowHelp = (s_section >= 0 && helpVis >= 0 && helpVis < s_visN && !optionsBindingCaptureActive())
                              ? optionsRowHelp(s_visIdx[helpVis]) : NULL;
    /* D554: the tip wraps to a second centred line instead of being chopped with "..". */
    char tipL1[160], tipL2[160];
    int tipLines = 0;
    tipSelfCheck();   /* once, cached: logs tips that would need > 2 lines at HUD 150 */
    if (rowHelp) tipLines = tipWrap(rowHelp, o.right - o.left - 8, tipL1, sizeof(tipL1), tipL2, sizeof(tipL2));
    if (tipLines > 2) tipLines = 2;   /* line 2 is already chopped with ".." by tipWrap when it still overflows */
    const s32 tipArea = s_section >= 0 ? 24 : 0;
    {   /* D519: the hint text sits in its own panel (box-body style) under the card, so it never
         * draws bare over the game's own bar (file select's Copy/Erase) */
        /* D556: fixed panel height per page: a section page reserves two tip lines (a row without
         * a tip leaves them empty) so the key hints never move; the hub has no tips. 2 + 2 lines
         * = 50 <= OV_HINT_H 53. */
        int hintLines = tipArea / 12 + (help1 ? 2 : 0);
        if (hintLines) {
            s32 py1 = o.hintY + hintLines * 12 + 2;
            gdl = fillRectC(gdl, o.left, o.bottom + 1, o.right, py1, PD_BODY);
            gdl = fillRectC(gdl, o.left, o.bottom + 1, o.left + 1, py1, PD_DIALOG_BORDER1);
            gdl = fillRectC(gdl, o.right - 1, o.bottom + 1, o.right, py1, PD_DIALOG_BORDER1);
            gdl = fillRectC(gdl, o.left, py1 - 1, o.right, py1, PD_DIALOG_BORDER1);
        }
    }
    /* Rows only draw while their whole band is inside the body, so the eased
     * scroll never paints over the title bar or border (the scissor does not
     * clip the 2D rects here). */
    const s32 bodyTop = o.contentY, bodyBot = o.contentY + o.maxRows * OV_LINE;

    /* ---- pass 1: row fills (separators, checkboxes, wedge sliders, swatch) ---- */
    for (int p = p0; p <= pLast; p++) {
        const struct Row *r = &rows[s_visIdx[p]];
        s32 rowY = o.contentY + (s32)lround((p - s_scrollF) * OV_LINE);
        if (rowY < bodyTop || rowY + OV_LINE > bodyBot) continue;
        int isCue = (p == cueP);
        u32 fillInk = !r->found ? PD_ITEM_DISABLED :
                      isCue ? pulseInk() : PD_ITEM_UNFOCUSED;
        if (isSepRow(r)) {   /* PD menuitemSeparatorRender: a 1 px line */
            gdl = fillRectC(gdl, o.left + 8, rowY + OV_LINE / 2, o.right - 8, rowY + OV_LINE / 2 + 1, PD_ITEM_DISABLED);
            continue;
        }
        if (s_section < 0) continue;   /* hub rows are text only */
        if (strcmp(r->key, "Video.CrosshairColor") == 0 && r->found) {
            extern void portCrosshairPreview(s32 *, s32 *, s32 *);
            char val[48];
            s32 red, green, blue;
            valueText(s_visIdx[p], val, sizeof(val));
            portCrosshairPreview(&red, &green, &blue);
            s32 x = o.valueR - bodyWidth(val) - 14;
            gdl = fillRect(gdl, x, rowY + 3, x + 8, rowY + 11, red, green, blue, 255);
        }
        if (r->kind == ROW_SLIDER && r->found && rowHi(r) > rowLo(r))
            gdl = drawWedge(gdl, bx0, bx1, rowY + 8, rowFractionAt(r, rowGet(r)), fillInk);
        else if (rowIsDropdown(r)) {   /* PD dropdown tick: a small down triangle before the value */
            char val[48];
            valueText(s_visIdx[p], val, sizeof(val));
            s32 tx = o.valueR - bodyWidth(val) - 10;
            gdl = drawTriDownAA(gdl, tx + 2.5, rowY + 6, 3, 6.0, fillInk);   /* D520 */
        } else if (overlayRowIsCheckbox(r) && r->found) {
            char val[16];
            valueText(s_visIdx[p], val, sizeof(val));
            gdl = drawCheckbox(gdl, o.valueR, rowY, !strncmp(val, "On", 2) || !strncmp(val, "ON", 2), fillInk);
        }
    }

    {   /* D556: mouse scrollbar (only while the page overflows): dim track + bright thumb in the card margin */
        struct OvScrollbar sb;
        if (s_section >= 0 && overlayScrollbar(&sb)) {
            gdl = fillRectC(gdl, sb.x0, sb.ty0, sb.x1, sb.ty1, PD_ITEM_DISABLED);
            gdl = fillRectC(gdl, sb.x0, sb.th0, sb.x1, sb.th1, PD_ITEM_UNFOCUSED);
        }
    }

    /* ---- pass 2: text ---- */
    gdl = microcode_constructor(gdl);

    for (int p = p0; p <= pLast; p++) {
        const struct Row *r = &rows[s_visIdx[p]];
        s32 rowY = o.contentY + (s32)lround((p - s_scrollF) * OV_LINE) + OV_TEXT_DY;
        if (rowY - OV_TEXT_DY < bodyTop || rowY - OV_TEXT_DY + OV_LINE > bodyBot) continue;
        if (isSepRow(r)) continue;
        int isCue = (p == cueP);
        u32 ink = !r->found ? PD_ITEM_DISABLED :
                  isCue ? pulseInk() : PD_ITEM_UNFOCUSED;
        u32 valueInk = !r->found ? PD_ITEM_DISABLED : PD_ITEM_UNFOCUSED;   /* PD: values stay unfocused */
        char label[64];
        if (s_section < 0) titleCase(r->label, label, sizeof(label));
        else snprintf(label, sizeof(label), "%s", r->label);
        char val[48];
        if (s_section < 0) {   /* PD hub: normal-size rows, no value column */
            gdl = isCue ? drawBodyOutlined(gdl, o.labelX, rowY, label, ink)
                        : drawBody(gdl, o.labelX, rowY, label, ink);
            continue;
        }
        valueText(s_visIdx[p], val, sizeof(val));
        int checkbox = overlayRowIsCheckbox(r);
        s32 valueW = (r->kind == ROW_SLIDER || checkbox) ? 0 : bodyWidth(val);
        s32 labelEnd = r->kind == ROW_SLIDER ? bx0 - 4 :
                       checkbox ? o.valueR - 14 : o.valueR - valueW - 8;
        if (strcmp(r->key, "Video.CrosshairColor") == 0) labelEnd -= 14;
        {
            char fit[80];
            snprintf(fit, sizeof(fit), "%s", label);
            if (bodyWidth(fit) > labelEnd - o.labelX) {   /* ellipsize */
                int n = (int)strlen(fit);
                while (n > 1) {
                    fit[--n] = 0;
                    if (n + 2 < (int)sizeof(fit)) { fit[n] = '.'; fit[n + 1] = '.'; fit[n + 2] = 0; }
                    if (bodyWidth(fit) <= labelEnd - o.labelX) break;
                    fit[n] = 0;
                }
            }
            gdl = isCue ? drawBodyOutlined(gdl, o.labelX, rowY, fit, ink)
                        : drawBody(gdl, o.labelX, rowY, fit, ink);
        }
        /* The mixed-scope Game page tags per-profile rows with a subdued word. */
        if (r->saveScoped && sectionIsMixedScope(s_section)) {
            s32 tagX = o.labelX + bodyWidth(r->label) + 4;
            if (tagX + bodyWidth("(per profile)") < labelEnd - 2)
                gdl = drawBody(gdl, tagX, rowY, "(per profile)", PD_ITEM_DISABLED);
        }
        if (!r->found) {
            gdl = drawBodyR(gdl, o.valueR, rowY, "(n/a)", PD_ITEM_DISABLED);
            continue;
        }
        if (r->restart) {
            s32 tagX = o.labelX + bodyWidth(r->label) + 4;
            if (tagX + bodyWidth("(restart)") < labelEnd - 2)
                gdl = drawBody(gdl, tagX, rowY, "(restart)", PD_ITEM_DISABLED);
        }
        if (!checkbox && val[0])   /* sliders: PD shows the value text right of the wedge */
            gdl = drawBodyR(gdl, o.valueR, rowY, val,
                            isArmRow(r) && !strncmp(val, "Confirm", 7) ? PD_TITLE_TEXT : valueInk);
    }

    {   /* late pass (PD): the open dropdown's popup list over everything */
        s32 dx0, dy0, dx1, dy1; int vis;
        if (ddGeom(&dx0, &dy0, &dx1, &dy1, &vis)) {
            gdl = fillRectC(gdl, dx0, dy0, dx1, dy1, 0x002800eeu);
            gdl = fillRectC(gdl, dx0, dy0, dx1, dy0 + 1, PD_DIALOG_BORDER2);
            gdl = fillRectC(gdl, dx0, dy1 - 1, dx1, dy1, PD_DIALOG_BORDER2);
            gdl = fillRectC(gdl, dx0, dy0, dx0 + 1, dy1, PD_DIALOG_BORDER2);
            gdl = fillRectC(gdl, dx1 - 1, dy0, dx1, dy1, PD_DIALOG_BORDER2);
            for (int k = 0; k < vis; k++) {
                int it = s_ddTop + k;
                if (it == s_ddCur)   /* marks the current value */
                    gdl = fillRectC(gdl, dx0 + 4, dy0 + 1 + k * OV_LINE + 5, dx0 + 8, dy0 + 1 + k * OV_LINE + 9, PD_ITEM_UNFOCUSED);
            }
            gdl = microcode_constructor(gdl);
            for (int k = 0; k < vis; k++) {
                int it = s_ddTop + k;
                s32 ty = dy0 + 1 + k * OV_LINE + OV_TEXT_DY;
                gdl = it == s_ddSel ? drawBodyOutlined(gdl, dx0 + 11, ty, s_ddLab[it], pulseInk())
                                    : drawBody(gdl, dx0 + 11, ty, s_ddLab[it], PD_ITEM_UNFOCUSED);
            }
            if (s_ddN > vis)
                gdl = drawBodyR(gdl, dx1 - 3, dy1 - 12, s_ddTop + vis < s_ddN ? "v" : "^", PD_TITLE_TEXT);
        }
    }
    {
        char raw[64], title[64];
        if (s_section >= 0) optionsRowTitle(s_section, raw, sizeof(raw));
        else snprintf(raw, sizeof(raw), "PC Options");
        titleCase(raw, title, sizeof(title));
        gdl = drawBody(gdl, o.left + 9, o.top + 2, title, 0x000000ff);   /* PD: title shadow pass */
        gdl = drawBody(gdl, o.left + 8, o.top + 1, title, PD_TITLE_TEXT);
        if (s_section >= 0 && rows[s_section].saveScoped) {
            int f = watchSettingsActiveFolder();
            char note[24];
            if (f < 0) snprintf(note, sizeof(note), "(no profile)");
            else       snprintf(note, sizeof(note), "(Profile %d)", f + 1);
            gdl = drawBody(gdl, o.left + 8 + bodyWidth(title) + 6, o.top + 1, note, PD_ITEM_UNFOCUSED);
        }
    }
    if (s_visN - first > o.maxRows)
        gdl = drawBodyR(gdl, o.right - 8, o.top + 1,
                        s_scroll > first ? "^ v" : "v", PD_TITLE_TEXT);

    if (s_section < 0 && configUnknownKeyCount() > 0) {   /* D472: non-blocking ini warning */
        char w[64];
        snprintf(w, sizeof(w), "%d unknown setting%s in the ini, see the log", configUnknownKeyCount(),
                 configUnknownKeyCount() == 1 ? "" : "s");
        gdl = drawBodyR(gdl, o.right - 8, o.top + 1, w, 0xe0b050ff);
    }

    /* Hint line under the box (R4): only while the last input came from a pad or
     * the keyboard (or a capture is waiting for one); it keeps D471's per-family
     * button names. Two short lines, centred. */
    s32 hintDy = 0;
    if (rowHelp) {
        const char *tl[2] = { tipL1, tipL2 };
        for (int L = 0; L < tipLines; L++) {
            s32 hw = bodyWidth(tl[L]), hx = (o.left + o.right) / 2 - hw / 2;
            s32 hy = o.hintY + L * 12;
            gdl = drawBody(gdl, hx + 1, hy + 1, tl[L], 0x000000ff);
            gdl = drawBody(gdl, hx, hy, tl[L], PD_ITEM_UNFOCUSED);
        }
    }
    hintDy = tipArea;
    if (help1) {
        static char h1buf[80], h2buf[80];
        inputPadHelpFmt(h1buf, sizeof(h1buf), help1->full);   /* D471: pad names for the menu pad's family */
        inputPadHelpFmt(h2buf, sizeof(h2buf), help2->full);
        {
            /* Too wide for the card: use the line's compact form, then the Xbox letters. */
            const struct HintLine *tm[2] = { help1, help2 };
            char *bf[2] = { h1buf, h2buf };
            s32 av = o.right - o.left - 8;
            for (int L = 0; L < 2; L++) {
                if (bodyWidth(bf[L]) <= av) continue;
                inputPadHelpFmt(bf[L], 80, tm[L]->compact);
                if (bodyWidth(bf[L]) > av) inputPadHelpFmtFam(bf[L], 80, tm[L]->compact, 0);
            }
        }
        s32 cx = (o.left + o.right) / 2;
        s32 y1 = o.hintY + hintDy, y2 = o.hintY + hintDy + 12;
        s32 w1 = bodyWidth(h1buf), w2 = bodyWidth(h2buf);
        gdl = drawBody(gdl, cx - w1 / 2 + 1, y1 + 1, h1buf, 0x000000ff);
        gdl = drawBody(gdl, cx - w1 / 2, y1, h1buf, PD_ITEM_UNFOCUSED);
        gdl = drawBody(gdl, cx - w2 / 2 + 1, y2 + 1, h2buf, 0x000000ff);
        gdl = drawBody(gdl, cx - w2 / 2, y2, h2buf, PD_ITEM_UNFOCUSED);
    }
    PORT_HUD_ASPECT(gdl, GE_HUD_ASPECT_NONE);
    gdl = drawCrosshairPointer(gdl, W, H);   /* D555 rework: last, so it is above the card */
    gDPPipeSync(gdl++);
    gSPEndDisplayList(gdl++);

    if ((gdl - s_buf) > OV_BUF_CMDS) {
        sysLogPrintf(LOG_ERROR, "optionsoverlay: DL overflow (%d)", (int)(gdl - s_buf));
    }
    return s_buf;
}

/* ------------------------------------------------------------------------ */
/* D343 (M2): row API for the file-select options screen (frontoptions.c).   */
/* The screen shares this file's rows, value text and live-apply logic; it   */
/* keeps its own page/selection state. Both UIs call these from one thread   */
/* at a time: the screen gets no input while the F10 overlay is open.        */
/* ------------------------------------------------------------------------ */

static struct Row *rowAt(int i)
{
    overlayInit();
    return (i >= 0 && i < NUM_ROWS) ? &rows[i] : NULL;
}

int optionsRowCount(void)
{
    overlayInit();
    return NUM_ROWS;
}

int optionsRowIsHeader(int i)
{
    struct Row *r = rowAt(i);
    return r && r->kind == ROW_HEADER;
}

/* Registered and not hidden by its "auto" toggle. */
int optionsRowIsShown(int i)
{
    struct Row *r = rowAt(i);
    return r && r->found && !(r->hidePtr && *r->hidePtr) &&
           !isSepRow(r) && !isBackRow(r) &&   /* F10-only furniture */
           (!r->showPtr || *r->showPtr == r->shownValue) && rowSchemeOk(r) &&
           (r->kind != ROW_BOND_FILE || current_menu == MENU_FILE_SELECT);   /* front-end profile chooser */
}

/* D507: short descriptions of the less obvious rows (key -> text), shown for
 * the focused row under the box. D554: a tip wraps onto a second line when it is
 * wider than the card (budget 296 at HUD 75/100, 189 at HUD 150); keep each tip
 * <= ~55 characters so it is two lines at most (tipSelfCheck logs offenders).
 * Rows with self-explanatory labels have no entry (D556: standard PC settings such as
 * resolution, VSync, anti-aliasing, volumes and sensitivities carry no tip). */
static const struct { const char *key, *help; } kRowHelp[] = {
    { "__DisplayMode", "Modern, original or custom graphics presets." },
    { "Video.AspectMode", "Fill window fits the picture to the whole window." },   /* D560: shown only while Fill window is selected (optionsRowHelp) */
    { "Video.CrosshairHide", "When off, the crosshair stays hidden, even with Sight on screen." },
    { "Video.CrosshairPersistent", "Shows the crosshair all the time. The N64 shows it only while aiming. Off by default." },
    { "Video.CrosshairColor", "Original keeps the game's red sight. Custom: set Red, Green and Blue (0-255)." },
    { "Video.CrosshairHealthColor", "The crosshair shifts from green to red as your health drops." },
    { "Video.WidescreenAuto", "This only applies when Native widescreen is off." },
    { "Game.ScreenShakeIntensity", "Sets how strongly explosions shake the screen." },
    { "Input.AimMode", "Original lets the crosshair move. Centered keeps it in the middle." },
    { "Input.AimRange", "Sets how far the crosshair can move from the center." },
    { "Input.CrosshairCursor", "Uses the crosshair as the mouse pointer in this menu." },
    { "Input.ControlScheme", "Choose the port's modern controls or the original game's." },
    { "Bond.Control", "The original game's control presets." },
    { "Input.PadPreset", "Xbox Series-style presets, or your own custom layout." },
    { "Input.PadSouthpaw", "Swaps the fire and aim triggers." },
    { "Bond.AutoAim", "Bond automatically aims at nearby targets." },
    { "Bond.LookAhead", "Tilts the view up or down on slopes." },
    { "Bond.Sight", "Shows or hides the game's own crosshair, saved per profile." },
    { "Video.FovScale", "Sets the vertical view angle in degrees. 60 is the original." },
    { "Game.AIWideView", "Extends guard behavior to any widescreen view." },
    { "Game.NoHitFlash", "Disables the white flash when you take damage." },
    { "Game.HudScale", "Scales ammo, pickup text, dialogue and this menu." },
    { "Game.SkipIntro", "Skips the intro and opens the file select screen." },
    { "Game.AllUnlocked", "Unlocks all levels, 007 mode and every cheat." },
    { "Game.CheckUpdates", "Notifies you when a new version is available." },
};


/* D554: greedy two-line wrap of a tip at width av. Returns 1 (fits), 2 (split at the last
 * space that fits; line 2 fits) or 3 (needs more than 2 lines: line 2 is chopped with ".."
 * so the caller still has something drawable). */
static int tipWrap(const char *tip, s32 av, char *l1, size_t n1, char *l2, size_t n2)
{
    snprintf(l1, n1, "%s", tip);
    l2[0] = 0;
    if (bodyWidth(l1) <= av) return 1;
    size_t len = strlen(l1), cut = 0;
    for (size_t i = 1; i < len; i++) {
        if (l1[i] != ' ') continue;
        char c = l1[i]; l1[i] = 0;
        s32 w = bodyWidth(l1);
        l1[i] = c;
        if (w > av) break;
        cut = i;
    }
    if (!cut) {   /* no space fits: hard-chop line 1 with ".." (should not happen for real tips) */
        for (int n = (int)len; n > 3 && bodyWidth(l1) > av; ) {
            l1[--n] = 0;
            if (n >= 2) { l1[n - 1] = '.'; l1[n - 2] = '.'; }
        }
        return 3;
    }
    snprintf(l2, n2, "%s", tip + cut + 1);
    l1[cut] = 0;
    if (bodyWidth(l2) <= av) return 2;
    for (int n = (int)strlen(l2); n > 3 && bodyWidth(l2) > av; ) {
        l2[--n] = 0;
        if (n >= 2) { l2[n - 1] = '.'; l2[n - 2] = '.'; }
    }
    return 3;
}

/* D554: one-time startup check (log only). The narrowest budget is HUD scale 150:
 * overlay width 320*100/150 = 213, card = min(304, 213 - 16) = 197, av = 189. Runs
 * on the first emit that has the fonts; cached, so never per frame. */
static void tipSelfCheck(void)
{
    static int s_done = 0;
    if (s_done || !bodyFont() || !bodyChars()) return;
    s_done = 1;
    const s32 av = (320 * 100 / 150 - 16) - 8;
    char a[160], b[160];
    int bad = 0;
    for (size_t k = 0; k < sizeof(kRowHelp) / sizeof(kRowHelp[0]); k++) {
        if (tipWrap(kRowHelp[k].help, av, a, sizeof(a), b, sizeof(b)) > 2) {
            sysLogPrintf(LOG_WARNING, "optionsoverlay: tip %s needs more than 2 lines at HUD 150 (budget %d)",
                         kRowHelp[k].key, (int)av);
            bad++;
        }
    }
    sysLogPrintf(LOG_INFO, "optionsoverlay: tip wrap check: %d tips, %d over 2 lines at HUD 150",
                 (int)(sizeof(kRowHelp) / sizeof(kRowHelp[0])), bad);
    /* Hint lines: the full form (Xbox names) must fit the HUD 100/75 budget (296), the compact
     * form (Xbox names, the last fallback) the HUD 150 budget. */
    const s32 avWide = 304 - 8;
    int hbad = 0;
    for (size_t k = 0; k < sizeof(kHint) / sizeof(kHint[0]); k++) {
        inputPadHelpFmtFam(a, sizeof(a), kHint[k].full, 0);
        if (bodyWidth(a) > avWide) {
            sysLogPrintf(LOG_WARNING, "optionsoverlay: hint %d full form too wide at HUD 100 (%d > %d): %s",
                         (int)k, (int)bodyWidth(a), (int)avWide, a);
            hbad++;
        }
        inputPadHelpFmtFam(b, sizeof(b), kHint[k].compact, 0);
        if (bodyWidth(b) > av) {
            sysLogPrintf(LOG_WARNING, "optionsoverlay: hint %d compact form too wide at HUD 150 (%d > %d): %s",
                         (int)k, (int)bodyWidth(b), (int)av, b);
            hbad++;
        }
    }
    sysLogPrintf(LOG_INFO, "optionsoverlay: hint check: %d lines, %d over budget", (int)(sizeof(kHint) / sizeof(kHint[0])), hbad);
}

const char *optionsRowHelp(int i)
{
    struct Row *r = rowAt(i);
    if (!r) return NULL;
    /* D560: the Aspect ratio tip describes Fill window only, so it shows only for that value. */
    if (!strcmp(r->key, "Video.AspectMode") && (int)rowGet(r) != 0) return NULL;
    for (size_t k = 0; k < sizeof(kRowHelp) / sizeof(kRowHelp[0]); k++)
        if (!strcmp(kRowHelp[k].key, r->key)) return kRowHelp[k].help;
    return NULL;
}

const char *optionsRowLabel(int i)
{
    struct Row *r = rowAt(i);
    return r ? r->label : "";
}

/* Page title: the label, except Controller which carries the seat chosen on
 * the Select player page ("PLAYER 2 CONTROLLER"). */
void optionsRowTitle(int i, char *out, int n)
{
    struct Row *r = rowAt(i);
    if (!r || n <= 0) { if (n > 0) out[0] = 0; return; }
    if (!strcmp(r->key, "__HdrController"))
        snprintf(out, n, "PLAYER %d %s", s_padSeat + 1, r->label);
    else if (!strcmp(r->key, "__HdrSelPlayerC"))
        snprintf(out, n, "SELECT PLAYER");
    else
        snprintf(out, n, "%s", r->label);
}

/* Page-kind queries for the front screen's help lines / scope tags (it must not
 * match on header labels: the hub label CONTROLLER now names two pages). */
int optionsRowIsBindingsPage(int hdr)
{
    struct Row *r = rowAt(hdr);
    return r && !strcmp(r->key, "__HdrBindings");
}

int optionsRowIsPadPage(int hdr)
{
    struct Row *r = rowAt(hdr);
    return r && !strcmp(r->key, "__HdrController");
}

int optionsRowSectionMixedScope(int hdr)
{
    overlayInit();
    return sectionIsMixedScope(hdr);
}

/* Called by a UI right before it opens the child page of link row i: a
 * "Player N" row on a Select player page picks the seat the next page edits. */
void optionsRowLinkOpened(int i)
{
    struct Row *r = rowAt(i);
    int seat = r ? selPlayerSeat(r) : -1;
    if (seat >= 0) padSetSeat(seat);   /* D518 */
}

/* Seat (0-3) of a "Player N" row on a Select player page, else -1. */
int optionsRowSeat(int i)
{
    struct Row *r = rowAt(i);
    return r ? selPlayerSeat(r) : -1;
}

/* Seat that the Select player page should preselect. */
int optionsPadSeat(void)
{
    return s_padSeat;
}

int optionsRowIsSlider(int i)
{
    struct Row *r = rowAt(i);
    return r && r->kind == ROW_SLIDER && rowHi(r) > rowLo(r);
}

int optionsRowIsBind(int i)
{
    struct Row *r = rowAt(i);
    return r && (r->kind == ROW_BIND || r->kind == ROW_PADBIND);
}

int optionsRowIsPadBind(int i)
{
    struct Row *r = rowAt(i);
    return r && r->kind == ROW_PADBIND;
}

void optionsRowBeginBind(int i)
{
    bindingBegin(rowAt(i));
}

/* D353: the explicit Bond-file chooser row (front options screen only). */
int optionsRowIsBondChooser(int i)
{
    struct Row *r = rowAt(i);
    return r && r->kind == ROW_BOND_FILE;
}

/* D356: content rows carry the literal per-file flag from the table; header
 * rows were recomputed at init to "this section contains per-file rows".
 * Both drive UI decoration (the "(per profile)" tag, the "(Profile N)" annotation).
 * The F10 overlay and the front options screen are the only consumers. */
int optionsRowIsSaveScoped(int i)
{
    struct Row *r = rowAt(i);
    return r && r->saveScoped;
}

int optionsRowNeedsRestart(int i)
{
    struct Row *r = rowAt(i);
    return r && r->restart;
}

double optionsRowFraction(int i)
{
    struct Row *r = rowAt(i);
    if (!r || rowHi(r) <= rowLo(r)) {
        return 0.0;
    }
    return rowFractionAt(r, rowGet(r));
}

/* D356: raw value (the fraction accessors are bar geometry only); the
 * reset probe verifies against raw defaults. */
double optionsRowGetValue(int i)
{
    struct Row *r = rowAt(i);
    return r ? rowGet(r) : 0.0;
}

void optionsRowSetFraction(int i, double f)
{
    struct Row *r = rowAt(i);
    if (!r || !r->found || rowHi(r) <= rowLo(r)) {
        return;
    }
    if (f < 0.0) f = 0.0;
    if (f > 1.0) f = 1.0;
    double step = (r->step > 0.0) ? r->step : 1.0;
    double v = rowValueAtFraction(r, f);
    rowSetCommit(r, lround(v / step) * step, 0);
}

void optionsRowCommit(int i)
{
    struct Row *r = rowAt(i);
    if (r) {
        int field = watchSettingsFieldForKey(r->key);
        if (field >= 0) watchSettingsCommit(field);
    }
}

void optionsRowValueText(int i, char *out, int n)
{
    struct Row *r = rowAt(i);
    if (!r || n <= 0) {
        return;
    }
    valueText(i, out, n);
}

void optionsRowAdjust(int i, int dir)
{
    struct Row *r = rowAt(i);
    if (r) {
        rowAdjust(r, dir);
    }
}

/* ------------------------------------------------------------------------ */
/* D356 GE_WSPROBE_RESET dev hook (env-gated; driven from the game-thread   */
/* hook in watchSettingsGameTick, so the dispatch runs on the right        */
/* thread in both contexts). Drives the REAL arm -> confirm -> dispatch    */
/* UI path for EVERY section's reset row and verifies each section's       */
/* values after ITS dispatch (plan §5.8): watch rows vs BLANKSAVEDATA,     */
/* ini rows vs the defaults table, __Resolution untouched, and (front) a   */
/* second file's bytes unchanged (scope isolation).                        */
/* ------------------------------------------------------------------------ */

#define PROBE_MAX_SECTIONS 12 /* reset-owning sections including HUD and bind leaves */
static int probeSectionIdxs[PROBE_MAX_SECTIONS];
static int probeSectionN = 0;
static int probeResSelBefore = -2;
static int probeOtherFolder = -1;
static save_data probeOtherBytes;

/* The header + end of the section a reset row belongs to. Written in
 * plain local-variable form on purpose: the pointer-increment style
 * (`while (*hdr >= 0 && ...) *hdr--;`) miscompiled under -O2 (the loop
 * pointer walked the stack and the faulted on garbage rows[] indices). */
static void probeSectionRange(int iReset, int *hdr, int *end)
{
    int h, e;
    if (iReset <= 0 || iReset >= NUM_ROWS) {
        sysLogPrintf(LOG_ERROR, "wsresetprobe: bad reset row index %d (NUM_ROWS %d)", iReset, NUM_ROWS);
        *hdr = -1; *end = -1;
        return;
    }
    h = iReset - 1;
    while (h >= 0 && rows[h].kind != ROW_HEADER) h--;
    e = h + 1;
    while (e < NUM_ROWS && rows[e].kind != ROW_HEADER) e++;
    *hdr = h;
    *end = e;
}

/* Verify every row of section iReset is at its default. Returns the
 * failure count. Watch rows compare against BLANKSAVEDATA via
 * watchSettingsRead (front: the saved file, stage: the post-drain
 * snapshot); ini rows against the defaults table. */
static int probeVerifySection(int iReset)
{
    int hdr, end;
    probeSectionRange(iReset, &hdr, &end);
    if (hdr < 0) {
        sysLogPrintf(LOG_WARNING, "wsresetprobe: section %d: no header", iReset);
        return 1;
    }
    int bad = 0;
    for (int j = hdr + 1; j < end; j++) {
        struct Row *r = &rows[j];
        if (r->kind == ROW_ACTION) continue;
        if (strcmp(r->key, "__Resolution") == 0) continue;   /* documented exclusion */
        if (strcmp(r->key, "Bond.Control") == 0) continue;   /* D516 */
        int field = watchSettingsFieldForKey(r->key);
        if (field >= 0) {
            int want = watchSettingsBlankValue((enum WatchSettingField)field);
            int got = watchSettingsRead((enum WatchSettingField)field);
            if (got != want) {
                sysLogPrintf(LOG_WARNING, "wsresetprobe: section '%s': %s = %d, want %d (BLANKSAVEDATA)",
                             rows[hdr].label, r->key, got, want);
                bad++;
            }
        } else if (r->found && r->ptr) {
            double want = kResetDefault(r->key);
            if (want < 0.0) continue;   /* no default (the handler skips it too) */
            double got = optionsRowGetValue(j);
            if (got < want - 0.001 || got > want + 0.001) {
                sysLogPrintf(LOG_WARNING, "wsresetprobe: section '%s': %s = %g, want %g (default table)",
                             rows[hdr].label, r->key, got, want);
                bad++;
            }
        }
    }
    return bad;
}

/* Front context (tick 1): dirty one value per section, then arm + confirm
 * every section's reset row in order, verifying each section right after
 * its own dispatch (the front commit is direct). Finally: resolution
 * untouched + a second file's bytes unchanged (scope isolation). */
void optionsResetProbePrepare(void)
{
    overlayInit();   /* idempotent: resolves the ini rows (found + ptr) so
                     * the front probe dispatches every declared row, not
                     * just the watch rows */
    probeSectionN = 0;
    for (int i = 0; i < NUM_ROWS; i++)
        if (isResetRow(&rows[i]) && probeSectionN < PROBE_MAX_SECTIONS)
            probeSectionIdxs[probeSectionN++] = i;
    probeResSelBefore = s_resSel;
    int active = watchSettingsActiveFolder();
    sysLogPrintf(LOG_INFO, "wsresetprobe: front prepare start (active folder %d, %d sections)", active, probeSectionN);
    probeOtherFolder = -1;
    for (int f = FOLDER1; f < MAX_FOLDER_COUNT; f++) {
        if (f == active) continue;
        save_data *s = fileGetSaveForFoldernum((u32)f);
        if (s) {
            probeOtherFolder = f;
            probeOtherBytes = *s;
            break;
        }
    }
    sysLogPrintf(LOG_INFO, "wsresetprobe: other-folder snapshot done (folder %d)", probeOtherFolder);
    /* 1. dirty a distinct value in each section (its first adjustable row). */
    for (int s = 0; s < probeSectionN; s++) {
        int iR = probeSectionIdxs[s];
        int hdr, end;
        probeSectionRange(iR, &hdr, &end);
        if (hdr < 0) continue;
        int did = 0;
        for (int j = hdr + 1; j < end && !did; j++) {
            struct Row *r = &rows[j];
            if (r->kind == ROW_ACTION || strcmp(r->key, "__Resolution") == 0) continue;
            int field = watchSettingsFieldForKey(r->key);
            if (field >= 0) {
                int blank = watchSettingsBlankValue((enum WatchSettingField)field);
                int want = (blank > 1) ? 0 : 1;   /* distinct from the blank */
                if (watchSettingsRead((enum WatchSettingField)field) == want) continue;
                watchSettingsSet((enum WatchSettingField)field, want, 1);
                did = 1;
            } else if (r->found && r->ptr && (r->kind == ROW_TOGGLE || r->kind == ROW_ENUM ||
                       r->kind == ROW_SLIDER || r->kind == ROW_MSAA)) {
                optionsRowAdjust(j, 1);   /* one step off the default */
                did = 1;
            }
        }
        sysLogPrintf(LOG_INFO, "wsresetprobe: section '%s' dirty=%d", rows[hdr].label, did);
    }
    /* 2. arm + confirm every section, verifying each after its dispatch. */
    for (int s = 0; s < probeSectionN; s++) {
        int iReset = probeSectionIdxs[s];
        int hdr, end;
        probeSectionRange(iReset, &hdr, &end);
        if (hdr < 0) continue;
        optionsRowActivateReset(iReset);   /* arm */
        sysLogPrintf(LOG_INFO, "wsresetprobe: section %d armed", s);
        optionsRowActivateReset(iReset);   /* confirm -> commit */
        int bad = probeVerifySection(iReset);
        sysLogPrintf(LOG_INFO, "wsresetprobe: section %d confirmed + verified", s);
        if (s_resSel != probeResSelBefore) {
            sysLogPrintf(LOG_WARNING, "wsresetprobe: __Resolution changed (%d -> %d)", probeResSelBefore, s_resSel);
            bad++;
        }
        sysLogPrintf(LOG_INFO, "wsresetprobe: section '%s' verified, failures=%d", rows[hdr].label, bad);
    }
    if (probeOtherFolder >= 0) {
        save_data *s = fileGetSaveForFoldernum((u32)probeOtherFolder);
        if (!s || memcmp(&probeOtherBytes, s, sizeof(save_data)) != 0) {
            sysLogPrintf(LOG_WARNING, "wsresetprobe: file %d bytes CHANGED (scope isolation broken)", probeOtherFolder + 1);
        } else {
            sysLogPrintf(LOG_INFO, "wsresetprobe: file %d unchanged (scope isolation)", probeOtherFolder + 1);
        }
    } else {
        sysLogPrintf(LOG_INFO, "wsresetprobe: no second folder to compare");
    }
}

/* In stage (tick 2): arm + confirm every section. The file rows enter the
 * D352 command queue (applied + persisted by the game thread at the next
 * drain); log the queue depth so the dispatch is visible. */
void optionsResetProbeDispatchStage(void)
{
    if (probeSectionN == 0) {
        for (int i = 0; i < NUM_ROWS; i++)
            if (isResetRow(&rows[i]) && probeSectionN < PROBE_MAX_SECTIONS)
                probeSectionIdxs[probeSectionN++] = i;
    }
    probeResSelBefore = s_resSel;
    for (int s = 0; s < probeSectionN; s++) {
        optionsRowActivateReset(probeSectionIdxs[s]);   /* arm */
        optionsRowActivateReset(probeSectionIdxs[s]);   /* confirm */
    }
    int q = watchSettingsQueueCount();
    if (q < 8) {
        sysLogPrintf(LOG_WARNING, "wsresetprobe: stage dispatch: queue depth %d, want >= 8 (6 GAME + 2 AUDIO watch rows)", q);
    } else {
        sysLogPrintf(LOG_INFO, "wsresetprobe: stage dispatch: %d command(s) queued (game thread drains next tick)", q);
    }
}

/* In stage (tick 3, after the drain + persist): verify every section. */
void optionsResetProbeVerifyStage(void)
{
    for (int s = 0; s < probeSectionN; s++) {
        int iReset = probeSectionIdxs[s];
        int hdr, end;
        probeSectionRange(iReset, &hdr, &end);
        int bad = probeVerifySection(iReset);
        if (s_resSel != probeResSelBefore) {
            sysLogPrintf(LOG_WARNING, "wsresetprobe: __Resolution changed (%d -> %d)", probeResSelBefore, s_resSel);
            bad++;
        }
        sysLogPrintf(LOG_INFO, "wsresetprobe: stage: section '%s' verified, failures=%d", rows[hdr].label, bad);
    }
    /* The persisted bytes must hold the BLANKSAVEDATA raw values too. */
    save_data *save = fileGetSaveForFoldernum(selected_folder_num);
    if (!save) {
        sysLogPrintf(LOG_WARNING, "wsresetprobe: stage: no save for folder %d", (int)selected_folder_num);
    } else if (save->music_vol != 0xFF || save->sfx_vol != 0xFF) {
        sysLogPrintf(LOG_WARNING, "wsresetprobe: stage: saved music=%d fx=%d, want 0xFF/0xFF",
                     (int)save->music_vol, (int)save->sfx_vol);
    } else {
        sysLogPrintf(LOG_INFO, "wsresetprobe: stage: saved bytes OK (music/sfx = 0xFF)");
    }
}
