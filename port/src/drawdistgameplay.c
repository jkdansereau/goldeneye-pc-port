/* drawdistgameplay.c -- D466 (#125): keep Video.DrawDistance out of gameplay.
 *
 * See port/include/drawdistgameplay.h and docs/dev/findings.md D466. This file
 * only holds port state + helpers; the game-file hooks (all #ifdef PORT) are
 * in bg.c (two room sets), bgfog.c (authored fog), chr.c/propobj.c (verdict
 * recording) and the gameplay readers.
 *
 * Env (dev probes, see docs/dev/GE-ENV-PROBES.md):
 *   GE_D466_FORCE=1  run the split even at multiplier 1 (identity test)
 *   GE_D466_OFF=1    disable the split entirely (reproduces the #125 leak)
 *   GE_D466LOG=<n>   log a line every n rendered frames (room-set + chr counts)
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "PR/os.h"
#include "bondtypes.h"
#include "bondconstants.h"
#include "bg.h"
#include "chrai.h"
#include "chr.h"
#include "bondview.h"
#include "port_math.h"
#include "drawdistgameplay.h"

extern f32 portDrawDistanceMultiplier(void);

static u8 s_authRendered[MAXROOMCOUNT];
static u8 s_authNeighbor[MAXROOMCOUNT];
static int s_authoredPass;
static int s_lastVerdict = 1;

static int d466Env(const char *name, int *cache)
{
    if (*cache < 0) { *cache = getenv(name) != NULL; }
    return *cache;
}

/* D468: AI view clamp. The cartridge's widest gameplay view is its own Ratio
 * 16:9 mode at the game's FOV; with a wider window (ultrawide) or
 * Video.FovScale > 100 the rendered frustum is wider than that. Perspective is
 * linear in screen space, so the faithful frustum is exactly a centred
 * sub-rectangle of the current player's screen: fx = tanH_faithful / tanH,
 * fy = tanV_faithful / tanV. Computed once per player view in
 * bgDetermineVisibleRooms (portD468UpdateClamp) and used by the authored room
 * pass (shrunken screen box) and the posIsOnScreen gameplay verdict. Off when
 * Game.AIWideView=1 (PD-port model) or when the view is already faithful. */
extern struct player *g_CurrentPlayer;
extern f32 portFovYScaleFactor(void);
extern int portAIWideView(void);
static int s_clampOn;
static float s_clampFx = 1.0f, s_clampFy = 1.0f;

void portD468UpdateClamp(void)
{
    struct player *p = g_CurrentPlayer;
    s_clampOn = 0; s_clampFx = s_clampFy = 1.0f;
    if (portAIWideView() || p == NULL || p->c_halfheight <= 0.0f || p->c_halfwidth <= 0.0f ||
        p->c_perspfovy <= 0.0f || p->c_perspaspect <= 0.0f) {
        return;
    }
    {
        const float halfRad = 3.14159265f / 360.0f;   /* degrees -> half-angle radians */
        float k = portFovYScaleFactor();
        float tanV = tanf(p->c_perspfovy * halfRad);
        float tanVf = tanf((p->c_perspfovy / k) * halfRad);
        /* Ratio 16:9 mode: aspect = viewport w/h * 0.75 * 16/9 (bondview2.c). */
        float aspF = (p->c_halfwidth / p->c_halfheight) * 0.75f * (16.0f / 9.0f);
        float asp = p->c_perspaspect;
        if (aspF > asp) { aspF = asp; }
        if (tanV <= 0.0f) { return; }
        s_clampFy = tanVf / tanV;
        s_clampFx = (tanVf * aspF) / (tanV * asp);
        {   /* GE_D468_SCALE=<0.05..1> (dev falsifier, registered): narrow the
             * faithful box further so a headless run can prove the clamp bites. */
            static float dbg = -1.0f;
            if (dbg < 0.0f) {
                const char *e = getenv("GE_D468_SCALE");
                dbg = e ? (float)atof(e) : 0.0f;
                if (dbg != 0.0f && (dbg < 0.05f || dbg > 1.0f)) { dbg = 0.0f; }
            }
            if (dbg > 0.0f) { s_clampFx *= dbg; s_clampFy *= dbg; }
        }
        if (s_clampFx > 1.0f) { s_clampFx = 1.0f; }
        if (s_clampFy > 1.0f) { s_clampFy = 1.0f; }
        s_clampOn = (s_clampFx < 0.999f || s_clampFy < 0.999f);
    }
}

int portD468ClampActive(void) { return s_clampOn; }

/* Shrink a screen box (f[0]=min x,y; f[1]=max x,y) to its centred faithful part. */
void portD468ShrinkBox(float *minmax)
{
    float cx, cy, hx, hy;
    if (!s_clampOn) { return; }
    cx = (minmax[0] + minmax[2]) * 0.5f; hx = (minmax[2] - minmax[0]) * 0.5f * s_clampFx;
    cy = (minmax[1] + minmax[3]) * 0.5f; hy = (minmax[3] - minmax[1]) * 0.5f * s_clampFy;
    minmax[0] = cx - hx; minmax[2] = cx + hx;
    minmax[1] = cy - hy; minmax[3] = cy + hy;
}

int portD466Active(void)
{
    static int force = -1, off = -1;
    if (d466Env("GE_D466_OFF", &off)) { return 0; }
    if (portDrawDistanceMultiplier() > 1.0f || s_clampOn) { return 1; }
    return d466Env("GE_D466_FORCE", &force);
}

void portD466SetAuthoredPass(int on) { s_authoredPass = on; }
int  portD466InAuthoredPass(void)    { return s_authoredPass; }

void portD466StoreRoom(int room, int rendered, int neighbor)
{
    if (room >= 0 && room < MAXROOMCOUNT) {
        s_authRendered[room] = (u8)rendered;
        s_authNeighbor[room] = (u8)neighbor;
    }
}

int portRoomGameplayVisible(int room)
{
    if (portD466Active() && room >= 0 && room < MAXROOMCOUNT) {
        return s_authRendered[room];
    }
    return getROOMID_isRendered(room);
}

int portRoomGameplayNeighbor(int room)
{
    if (portD466Active() && room >= 0 && room < MAXROOMCOUNT) {
        return s_authNeighbor[room];
    }
    return getROOMID_isNeighborToRendered(room);
}

int portPropGameplayOnScreen(struct PropRecord *prop)
{
    PropRecord *p = (PropRecord *)prop;
    if (!(p->flags & PROPFLAG_ONSCREEN)) { return 0; }
    if (portD466Active() && (p->flags & PORT_PROPFLAG_EXTONLY)) { return 0; }
    return 1;
}

void portPropSetGameplayOnScreen(struct PropRecord *prop, int gameplayVisible)
{
    PropRecord *p = (PropRecord *)prop;
    if (gameplayVisible || !portD466Active()) {
        p->flags &= ~PORT_PROPFLAG_EXTONLY;
    } else {
        p->flags |= PORT_PROPFLAG_EXTONLY;
    }
}

void portD466SetLastPosVerdict(int v) { s_lastVerdict = v; }
int  portD466LastPosVerdict(void)     { return s_lastVerdict; }

/* ---- GE_D466LOG ------------------------------------------------------- */

extern s32 g_MaxNumRooms;
extern s32 getPlayerCount(void);

void portD466FrameLog(void)
{
    static int period = -1;
    static unsigned frame;
    if (period < 0) {
        const char *e = getenv("GE_D466LOG");
        period = e ? atoi(e) : 0;
        if (e && period <= 0) { period = 1; }
    }
    if (period == 0) { return; }
    if ((frame++ % (unsigned)period) != 0) { return; }
    {
        int i, ext = 0, auth = 0, sub = 1;
        unsigned he = 2166136261u, ha = 2166136261u;
        for (i = 0; i < g_MaxNumRooms && i < MAXROOMCOUNT; i++) {
            int e = getROOMID_isRendered(i) != 0;
            int a = s_authRendered[i] != 0;
            ext += e; auth += a;
            if (e) { he = (he ^ (unsigned)i) * 16777619u; }
            if (a) { ha = (ha ^ (unsigned)i) * 16777619u; }
            if (a && !e) { sub = 0; }
        }
        int c, nOn = 0, nGp = 0, nSeen = 0;
        unsigned hg = 2166136261u;
        for (c = 0; c < g_NumChrSlots; c++) {
            ChrRecord *chr = &g_ChrSlots[c];
            PropRecord *pr = chr->prop;
            unsigned v;
            int gp;
            if (pr == NULL || chr->model == NULL) { continue; }
            gp = portPropGameplayOnScreen((struct PropRecord *)pr);
            nOn += (pr->flags & PROPFLAG_ONSCREEN) != 0;
            nGp += gp;
            nSeen += (chr->chrflags & CHRFLAG_HAS_BEEN_ON_SCREEN) != 0;
            v = (unsigned)chr->chrnum;
            v = v * 31u + (unsigned)chr->actiontype;
            v = v * 31u + (unsigned)(int)(pr->pos.f[0] + 0.5f);
            v = v * 31u + (unsigned)(int)(pr->pos.f[1] + 0.5f);
            v = v * 31u + (unsigned)(int)(pr->pos.f[2] + 0.5f);
            v = v * 31u + (unsigned)gp;
            v = v * 31u + (unsigned)((chr->chrflags & CHRFLAG_HAS_BEEN_ON_SCREEN) != 0);
            hg = (hg ^ v) * 16777619u;
        }
        fprintf(stderr, "D466 f=%u active=%d ext=%d auth=%d subset=%d hext=%08x hauth=%08x chrOn=%d chrGp=%d seen=%d hgame=%08x\n",
                frame - 1, portD466Active(), ext, auth, sub, he, ha, nOn, nGp, nSeen, hg);
    }
}
