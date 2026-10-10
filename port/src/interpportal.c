/* interpportal.c -- D578: replay bg.c's portal traversal for an interpolated
 * camera (port side, read-only on game data).
 *
 * Why: bgDetermineVisibleRooms clips every drawn room to the screen rectangle
 * of the portal chain that reached it, for the CURRENT camera. An in-between
 * (interpolated) pass draws the same display list at an earlier camera, so the
 * narrowed rectangle clips the room ahead (Dam tunnel, walking forward). The
 * rectangle is a path-dependent intersection (portal descent, near-plane
 * clipping, per-portal projection), so it cannot be fixed per portal in the
 * gfx layer; instead this file re-runs the same algorithm with the
 * interpolated world-to-view matrix and reports each room's new box.
 *
 * Threading: the render worker draws frame N while the game thread already
 * runs frame N+1 (D481), so the live game state is NOT frame N's. Everything
 * the replay reads is therefore captured on the game thread at the moment the
 * frame's graphics task is queued (interpPortalOnSend, from osSendMesg to the
 * scheduler, i.e. rspGfxTaskStart), into a small ring keyed by the display
 * list pointer. The static level data (portal polygons, the vis command list)
 * is read in place.
 *
 * The replay is a transcription of src/game/bg.c (US/JP path):
 *   rp_visit_count      <- bgIncrementRoomPortalVisitCount
 *   rp_queue            <- bgQueuePortalTraversal
 *   rp_add_room         <- sub_GAME_7F0B39BC
 *   rp_portal_points    <- sub_GAME_7F0B5528
 *   rp_portal_box       <- sub_GAME_7F0B5864 (+ the PORT D106/D271 guard)
 *   rp_room_on_screen   <- bgIsRoomOnScreen / bgProjectRoomCoordToScreen
 *   rp_descend          <- sub_GAME_7F0B7F84
 *   rp_parse_vis        <- parse_global_vis_command_list
 *   rp_run              <- bgDetermineVisibleRoomsImpl (the final, extended pass)
 * Nothing in the game is written: all traversal state (visit counts, room
 * mask, portal cache, queue, vis stack, drawn-room list) is a private copy.
 *
 * Camera: GE loads the room projection field_10E0 = lookat(scaled pos) *
 * perspective (row vectors). With the perspective matrix known
 * (player->projmatrixf) the lookat of any blended projection is
 * L = P * inverse(persp); its rotation is the camera rotation and its
 * translation gives the camera position (in the room-scaled space, / D_800364CC
 * back to world). The world-to-view matrix bg.c uses (field_10CC) is rebuilt
 * from that. Exact passes (blended == raw) use field_10CC itself.
 *
 * Env: see port/include/interpportal.h. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "PR/os.h"
#include <PR/sptask.h>
#include "bondtypes.h"
#include "bondconstants.h"
#include "bg.h"
#include "bondview.h"
#include "port_math.h"
#include "system.h"   /* D583: sysLogPrintf (Game Mode drops stderr) */
#include "math_ceil.h"
#include "interpportal.h"

extern struct player *g_CurrentPlayer;
extern void viGetZRange(f32 *zrange);
extern u32 videoGetFrameCount(void);

extern s32 g_BgNumberOfRoomsDrawn;
extern s_bound_info dword_CODE_bss_8007FFA0[];
extern s32 *dword_CODE_bss_8007FF90;
extern s32 g_BgStack[];
extern s32 g_BgStackCount;
extern s32 g_BgCurrentRoom;
extern s32 levelentry_index;
extern f32 mCurrentLevelVisibilityScale;
extern f32 room_data_float1;
extern s32 D_8004489C;
extern f32 D_800364CC;
extern void mtx4TransformVecInPlace(Mtxf *matrix, struct coord3d *vector);
extern coord3d *bondviewGetCurrentPlayersPosition(void);
extern OSMesgQueue *sched_cmdQ;          /* src/init.c */
extern void *g_gfxTaskSettingsList;      /* src/game/rsp.c: struct GfxInfo_s *, task is the first member */

#define RP_QLEN      500    /* BG_PORTAL_QUEUE_LEN (US/JP) */
#define RP_STACK     20     /* BG_STACK_SIZE */
#define RP_MAXDRAWN  256
#define RP_SLOTS     4

static int rp_env(const char *name, int *cache)
{
    if (*cache < 0) { *cache = getenv(name) != NULL; }
    return *cache;
}

/* ------------------------------------------------------------------------- */
/* Frame snapshot (game thread)                                               */
/* ------------------------------------------------------------------------- */

typedef struct {
    const void *dl;                 /* key: the frame's display list */
    int valid;
    unsigned frame;                 /* snapshot serial, for logs */

    /* camera / view of g_CurrentPlayer */
    Mtxf V;                         /* field_10CC: world-to-view */
    Mtxf persp;                     /* projmatrixf */
    coord3d ppos;                   /* bondviewGetCurrentPlayersPosition() */
    coord3d model_pos;              /* current_model_pos */
    f32 c_screenleft, c_halfwidth, c_screentop, c_halfheight, c_recipscalex, c_recipscaley;
    s16 viewleft, viewtop, viewx, viewy;
    bbox2d screensize;

    /* level scalars */
    f32 zrange[2];
    f32 vis_scale, rdf1, rdf2, d364cc;
    s32 curroom, levelentry, d4489c, maxrooms;

    /* vis command list state */
    s32 *vislist;
    s32 stack[RP_STACK];
    s32 stackcount;

    /* portals (geometry is read in place via `portals`) */
    bg_portal_data_entry *portals;
    int nportals;
    u8 cb1[PORTMAX], cb2[PORTMAX], conn1[PORTMAX], conn2[PORTMAX];

    /* rooms */
    coord3d minb[MAXROOMCOUNT], maxb[MAXROOMCOUNT];
    u32 vphys[MAXROOMCOUNT];        /* OS_K0_TO_PHYSICAL(vertices), 0 = none */

    /* the game's own result for this frame */
    int ndrawn;
    s32 d_room[RP_MAXDRAWN];
    bbox2d d_bbox[RP_MAXDRAWN];
} IpSnap;

static IpSnap s_ring[RP_SLOTS];
static int s_ringNext;
static const IpSnap *S;             /* snapshot of the frame being drawn (render thread) */
static unsigned s_snapSerial;

typedef struct IpSchedTask {        /* mirror of OSScTask (src/sched.h) up to `list` */
    void *next;
    u32 state;
    u32 flags;
    void *framebuffer;
    OSTask list;
} IpSchedTask;

static void ip_snapshot(IpSnap *s, const void *dl)
{
    struct player *p = g_CurrentPlayer;
    int i, n;

    s->valid = 0;
    s->dl = dl;
    if (p == NULL || p->field_10CC == NULL || p->projmatrixf == NULL || g_BgPortals == NULL) {
        return;
    }
    s->frame = ++s_snapSerial;
    s->V = *p->field_10CC;
    s->persp = *p->projmatrixf;
    s->ppos = *bondviewGetCurrentPlayersPosition();
    s->model_pos = p->current_model_pos;
    s->c_screenleft = p->c_screenleft;
    s->c_halfwidth = p->c_halfwidth;
    s->c_screentop = p->c_screentop;
    s->c_halfheight = p->c_halfheight;
    s->c_recipscalex = p->c_recipscalex;
    s->c_recipscaley = p->c_recipscaley;
    s->viewleft = p->viewleft; s->viewtop = p->viewtop; s->viewx = p->viewx; s->viewy = p->viewy;
    s->screensize = p->screensize;

    viGetZRange(s->zrange);
    s->vis_scale = mCurrentLevelVisibilityScale;
    s->rdf1 = room_data_float1;
    s->rdf2 = room_data_float2;
    s->d364cc = D_800364CC;
    s->curroom = g_BgCurrentRoom;
    s->levelentry = levelentry_index;
    s->d4489c = D_8004489C;
    s->maxrooms = g_MaxNumRooms;

    s->vislist = dword_CODE_bss_8007FF90;
    memcpy(s->stack, g_BgStack, sizeof(s->stack));
    s->stackcount = g_BgStackCount;

    s->portals = g_BgPortals;
    for (n = 0; n < PORTMAX && g_BgPortals[n].offset_portal != NULL; n++) {
        s->cb1[n] = g_BgPortals[n].controlbytes1;
        s->cb2[n] = g_BgPortals[n].controlbytes2;
        s->conn1[n] = g_BgPortals[n].connectedRoom1;
        s->conn2[n] = g_BgPortals[n].connectedRoom2;
    }
    s->nportals = n;

    for (i = 0; i < MAXROOMCOUNT; i++) {
        s->minb[i] = g_BgRoomInfo[i].minbounds;
        s->maxb[i] = g_BgRoomInfo[i].maxbounds;
        s->vphys[i] = (g_BgRoomInfo[i].vertices != NULL) ? (u32) OS_K0_TO_PHYSICAL(g_BgRoomInfo[i].vertices) : 0;
    }

    n = g_BgNumberOfRoomsDrawn;
    if (n < 0) { n = 0; }
    if (n > RP_MAXDRAWN) { n = RP_MAXDRAWN; }
    s->ndrawn = n;
    for (i = 0; i < n; i++) {
        s->d_room[i] = dword_CODE_bss_8007FFA0[i].roomid;
        s->d_bbox[i] = dword_CODE_bss_8007FFA0[i].bbox;
    }
    s->valid = 1;
}

/* osSendMesg hook (game thread). */
void interpPortalOnSend(void *mq, void *msg)
{
    static int env_off = -1;
    IpSchedTask *t;
    int i;

    if (mq != (void *) sched_cmdQ || msg == NULL || msg != g_gfxTaskSettingsList) { return; }
    if (rp_env("GE_INTERPPORTAL_OFF", &env_off)) { return; }
    t = (IpSchedTask *) msg;
    for (i = 0; i < RP_SLOTS; i++) {          /* same display list again: reuse its slot */
        if (s_ring[i].dl == (const void *) t->list.t.data_ptr) { break; }
    }
    if (i == RP_SLOTS) {
        i = s_ringNext;
        s_ringNext = (s_ringNext + 1) % RP_SLOTS;
    }
    ip_snapshot(&s_ring[i], (const void *) t->list.t.data_ptr);
}

/* ------------------------------------------------------------------------- */
/* Replay state                                                               */
/* ------------------------------------------------------------------------- */

typedef struct { s32 roomid, unk1, next; bbox2d bbox; } RpRoom;
typedef struct { s32 roomnum, portalnum, depth; f32 box[4]; } RpQ;
typedef struct { s32 count; bbox2d bbox; } RpPortalCache;

typedef struct {
    int ok;                 /* camera derived */
    int exact;              /* raw == blended: V is the game's own matrix */
    Mtxf V;                 /* world-to-view (field_10CC equivalent) */
    coord3d ppos;           /* player position */
    double rotdiff;         /* |derived R - field_10CC R| (diagnostic) */
    double scalediff;       /* |(cam - model_pos) * scale - derived scaled pos| */
} RpCam;

static struct {
    Mtxf V;
    coord3d ppos;
    RpRoom rooms[RP_MAXDRAWN];
    int nrooms;
    u8 visit[MAXROOMCOUNT];
    u8 mask[MAXROOMCOUNT];
    RpQ q[RP_QLEN];
    int qw, qr;
    RpPortalCache cache[PORTMAX];
    s32 stack[RP_STACK];
    s32 stackcount;
    s32 curvis;
    bbox2d vbox;            /* dword_CODE_bss_80081600.unk0 (a function static in the game) */
    s32 vflag;              /* ...unk10 */
} rp;

/* ------------------------------------------------------------------------- */
/* Transcription of bg.c (reads the snapshot S, never live state)             */
/* ------------------------------------------------------------------------- */

static bbox2d rp_screen(void)
{
    return S->screensize;
}

/* transform3Dto2DWithZScaling with the snapshot's player scale */
static void rp_xf2d(const coord3d *in, coord3d *out)
{
    f32 inv_z;

    if (in->z == 0.0f) {
        inv_z = -100000000000000000000.0f;
    } else {
        inv_z = 1.0f / in->z;
    }
    out->y = in->y * inv_z * S->c_recipscaley + (S->c_screentop + S->c_halfheight);
    out->x = (S->c_screenleft + S->c_halfwidth) - in->x * inv_z * S->c_recipscalex;
}

/* sub_GAME_7F0B9990 */
static f32 rp_portal_scale(s32 portalnum)
{
    s32 value = S->cb2[portalnum];
    s32 shift = (value >> 4) & 0xf;
    f32 result = (value & 0xf) * 0.25f;

    while (shift != 0) {
        result += result;
        shift--;
    }
    return result;
}

static int rp_visit_count(s32 roomnum)
{
    u8 count, out, tmp;
    if (roomnum < 0 || roomnum >= MAXROOMCOUNT) { return 0; }
    count = rp.visit[roomnum];
    out = count;
    if ((s32) count < 0xFF) {
        tmp = count + 1;
        rp.visit[roomnum] = tmp;
        out = tmp & 0xFF;
    }
    return out;
}

static void rp_queue(s32 roomnum, s32 portalnum, s32 depth, const f32 *box)
{
    RpQ *e = &rp.q[rp.qw < 0 ? 0 : rp.qw];
    if (depth >= 2) {
        if (rp_visit_count((S->conn2[portalnum] ^ S->conn1[portalnum]) ^ roomnum) >= 9) {
            return;
        }
    }
    e->roomnum = roomnum;
    e->portalnum = portalnum;
    e->depth = depth;
    e->box[0] = box[0]; e->box[1] = box[1]; e->box[2] = box[2]; e->box[3] = box[3];
    rp.qw++;
    if (rp.qw == RP_QLEN) { rp.qw = 0; }
    if (rp.qw == rp.qr) { rp.qw--; if (rp.qw < 0) { rp.qw = RP_QLEN - 1; } }
}

static s32 rp_add_room(s32 curroom, s32 unk1, bbox2d *screensize, s32 next)
{
    int i;
    s32 temp;

    if (curroom < 0 || curroom >= MAXROOMCOUNT) { return 0; }
    if (rp.mask[curroom] != 0) {
        return 0;
    }
    for (i = 0; i < rp.nrooms; i++) {
        if (curroom == rp.rooms[i].roomid) {
            bbox2d *rb = &rp.rooms[i].bbox;
            if (rp.rooms[i].unk1 < unk1) { rp.rooms[i].unk1 = unk1; }
            /* bgRectOutersect(screensize, &rooms[i].bbox): union into *screensize */
            screensize->min.x = (screensize->min.x < rb->min.x) ? screensize->min.x : rb->min.x;
            screensize->min.y = (screensize->min.y < rb->min.y) ? screensize->min.y : rb->min.y;
            screensize->max.x = (screensize->max.x > rb->max.x) ? screensize->max.x : rb->max.x;
            screensize->max.y = (screensize->max.y > rb->max.y) ? screensize->max.y : rb->max.y;
            temp = rp.rooms[i].next;
            rb->min.x = screensize->min.x;
            rb->min.y = screensize->min.y;
            rb->max.x = screensize->max.x;
            rb->max.y = screensize->max.y;
            rp.rooms[i].next = temp | next;
            return temp;
        }
    }
    if (rp.nrooms >= RP_MAXDRAWN) { return 0; }
    i = rp.nrooms;
    rp.rooms[i].roomid = curroom;
    rp.rooms[i].unk1 = unk1;
    rp.rooms[i].bbox.min.x = screensize->min.x;
    rp.rooms[i].bbox.min.y = screensize->min.y;
    rp.rooms[i].bbox.max.x = screensize->max.x;
    rp.rooms[i].bbox.max.y = screensize->max.y;
    rp.rooms[i].next = next;
    rp.nrooms = i + 1;
    return 0;
}

static s32 rp_rect_intersect(bbox2d *a, bbox2d *b)
{
    a->min.x = a->min.x > b->min.x ? a->min.x : b->min.x;
    a->min.y = a->min.y > b->min.y ? a->min.y : b->min.y;
    a->max.x = b->max.x > a->max.x ? a->max.x : b->max.x;
    a->max.y = b->max.y > a->max.y ? a->max.y : b->max.y;
    if (a->min.x >= a->max.x) {
        a->min.x = a->max.x;
        return FALSE;
    }
    if (a->max.y <= a->min.y) {
        a->min.y = a->max.y;
        return FALSE;
    }
    return TRUE;
}

static void rp_rect_union(bbox2d *a, const bbox2d *b)
{
    a->min.x = (a->min.x < b->min.x) ? a->min.x : b->min.x;
    a->min.y = (a->min.y < b->min.y) ? a->min.y : b->min.y;
    a->max.x = (a->max.x > b->max.x) ? a->max.x : b->max.x;
    a->max.y = (a->max.y > b->max.y) ? a->max.y : b->max.y;
}

static s32 rp_portal_points(s32 portalnum, f32 arg1, coord3d *arg2)
{
    Mtxf *matrix = &rp.V;
    bg_portal_entry *portal = S->portals[portalnum].offset_portal;
    coord3d *point;
    s32 i, next, len;
    f32 zrange1;
    s32 allbehind = 1;
    struct PortalMetric metric;

    zrange1 = S->zrange[1] / S->vis_scale;

    for (i = 0; i < portal->numPoints; i++) {
        point = &arg2[i];

        point->x = (&portal->point)[i].x;
        point->y = (&portal->point)[i].y;
        point->z = (&portal->point)[i].z;

        if (arg1 != 0.0f) {
            sub_GAME_7F0B96CC(portalnum, (f32 *) &metric);   /* pure: static portal polygon */

            point->x += metric.normal.x * arg1;
            point->y += metric.normal.y * arg1;
            point->z += metric.normal.z * arg1;
        }

        point = &arg2[i];
        point->x *= S->rdf2;
        point->y *= S->rdf2;
        point->z *= S->rdf2;

        mtx4TransformVecInPlace(matrix, point);

        if (-zrange1 * 0.9f < point->z) {
            allbehind = 0;
        }
    }

    if (allbehind) {
        return 0;
    }

    len = portal->numPoints;

    for (i = 0; i < portal->numPoints; i++) {
        point = &arg2[i];
        next = (i + 1) % portal->numPoints;

        if ((point->z > 0.0f && arg2[next].z <= 0.0f) || (point->z <= 0.0f && arg2[next].z > 0.0f)) {
            f32 scale = -point->z / (arg2[next].z - point->z);

            arg2[len].x = point->x + ((arg2[next].x - point->x) * scale);
            arg2[len].y = point->y + ((arg2[next].y - point->y) * scale);
            point = &arg2[i];
            arg2[len].z = 0.0f;
            len++;
        }
    }

    return len;
}

static s32 rp_portal_box(s32 portalnum, bbox2d *bbox)
{
    f32 scale;
    RpPortalCache *cache;
    s32 j, i, pointcount, onscreencount;
    coord3d points[64];
    coord3d screenpos;
    bbox2d bounds;

    cache = &rp.cache[portalnum];

    if (cache->count >= 0) {
        bbox->min.x = cache->bbox.min.x;
        bbox->min.y = cache->bbox.min.y;
        bbox->max.x = cache->bbox.max.x;
        bbox->max.y = cache->bbox.max.y;
        return cache->count;
    }

    memset(&bounds, 0, sizeof(bounds));
    scale = rp_portal_scale(portalnum);
    pointcount = rp_portal_points(portalnum, scale, points);

    if (scale > 0.0f) {
        pointcount += rp_portal_points(portalnum, -scale, &points[pointcount]);
    }

    onscreencount = 0;
    i = 0;

    if (pointcount > 0) {
        j = 0;
        do {
            if (points[j].z <= 0.0f) {
                rp_xf2d(&points[j], &screenpos);

                if (onscreencount == 0) {
                    bounds.min.x = (bounds.max.x = screenpos.x);
                    bounds.min.y = (bounds.max.y = screenpos.y);
                } else {
                    if (screenpos.x < bounds.min.x) { bounds.min.x = screenpos.x; }
                    if (bounds.max.x < screenpos.x) { bounds.max.x = screenpos.x; }
                    if (screenpos.y < bounds.min.y) { bounds.min.y = screenpos.y; }
                    if (bounds.max.y < screenpos.y) { bounds.max.y = screenpos.y; }
                }
                onscreencount++;
            }
            i++;
            j++;
        } while (i != pointcount);
    }

    if (onscreencount == 0) {
        bounds.max.y = (bounds.min.x = 0.0f);
        bounds.min.y = 0.0f;
        bounds.max.x = 0.0f;
    } else {
        s32 degenerate = (bounds.max.x <= bounds.min.x) || (bounds.max.y <= bounds.min.y);
        const f32 lim = 1e38f;
        if (!(bounds.min.x > -lim && bounds.min.x < lim) ||
            !(bounds.min.y > -lim && bounds.min.y < lim) ||
            !(bounds.max.x > -lim && bounds.max.x < lim) ||
            !(bounds.max.y > -lim && bounds.max.y < lim)) {
            degenerate = 1;
        }
        if (degenerate) {
            bbox2d sc = rp_screen();
            bounds.min.x = sc.min.x;
            bounds.min.y = sc.min.y;
            bounds.max.x = sc.max.x;
            bounds.max.y = sc.max.y;
        }
    }

    bbox->min.x = bounds.min.x;
    bbox->min.y = bounds.min.y;
    bbox->max.x = bounds.max.x;
    bbox->max.y = bounds.max.y;

    cache->bbox.min.x = bbox->min.x;
    cache->bbox.min.y = bbox->min.y;
    cache->bbox.max.x = bbox->max.x;
    cache->bbox.max.y = bbox->max.y;
    cache->count = onscreencount;

    return onscreencount;
}

static int rp_room_on_screen(s32 roomID, const bbox2d *sb)
{
    s32 i;
    coord3d projected, corner;
    s32 count_z = 0, count_failed_projection = 0, count_left = 0, count_right = 0, count_top = 0, count_bottom = 0;
    f32 zrange1;
    const f32 left = sb->min.x, up = sb->min.y, right = sb->max.x, down = sb->max.y;

    if (roomID < 0 || roomID >= MAXROOMCOUNT) { return 0; }

    zrange1 = S->zrange[1] / S->vis_scale;

    for (i = 0; i < 8; i++) {
        corner.x = (i & 1) ? S->minb[roomID].x : S->maxb[roomID].x;
        corner.y = (i & 2) ? S->minb[roomID].y : S->maxb[roomID].y;
        corner.z = (i & 4) ? S->minb[roomID].z : S->maxb[roomID].z;

        /* bgProjectRoomCoordToScreen */
        projected.x = corner.x * S->rdf2;
        projected.y = corner.y * S->rdf2;
        projected.z = corner.z * S->rdf2;
        mtx4TransformVecInPlace(&rp.V, &projected);
        rp_xf2d(&projected, &projected);

        if (!(projected.z > 0.0f)) {
            if (zrange1 <= -projected.z) { count_z++; }
            if (left <= projected.x) { count_left++; }
            if (projected.x <= right) { count_right++; }
            if (up <= projected.y) { count_top++; }
            if (projected.y <= down) { count_bottom++; }
            count_failed_projection++;
        } else {
            if (zrange1 <= -projected.z) { count_z++; }
            if (projected.x <= left) { count_left++; } else if (right <= projected.x) { count_right++; }
            if (projected.y <= up) { count_top++; } else if (down <= projected.y) { count_bottom++; }
        }
    }

    if (count_failed_projection == 8 || count_z == 8 || count_left == 8 ||
        count_right == 8 || count_top == 8 || count_bottom == 8) {
        return 0;
    }
    return 1;
}

static void rp_descend(s32 roomnum, s32 portalnum, s32 depth, f32 *parentbox)
{
    bbox2d screenbox;
    coord3d *playerpos;
    s32 otherroom, i;
    f32 metric[5];   /* normal x,y,z, min, max (struct PortalMetric) */
    f32 playermetric, portalmetric;

    if (depth >= 101) { return; }
    if (S->d4489c < depth) { return; }
    if (depth >= 16) { return; }
    if (S->cb1[portalnum] & PORTALFLAG_DISABLED) { return; }

    playerpos = &rp.ppos;
    sub_GAME_7F0B96CC(portalnum, metric);
    playermetric = ((metric[2] * playerpos->z) + ((metric[0] * playerpos->x) + (metric[1] * playerpos->y))) * S->rdf1;
    portalmetric = rp_portal_scale(portalnum);

    if (roomnum == S->conn1[portalnum]) {
        otherroom = S->conn2[portalnum];
        if (metric[4] <= (playermetric - portalmetric)) { return; }
    } else {
        otherroom = S->conn1[portalnum];
        if ((playermetric + portalmetric) <= metric[3]) { return; }
    }

    if (((metric[3] - portalmetric) < playermetric) && (playermetric < (metric[4] + portalmetric))) {
        screenbox = rp_screen();
    } else {
        if (S->cb1[portalnum] & PORTALFLAG_SPECIAL) {
            if (!rp_portal_box(portalnum, &screenbox)) { return; }
            otherroom = (S->conn1[portalnum] ^ S->conn2[portalnum]) ^ roomnum;
            if (!rp_room_on_screen(otherroom, &screenbox)) { return; }
            screenbox = rp_screen();
        } else {
            bbox2d pb, sc;
            if (!rp_portal_box(portalnum, &screenbox)) { return; }
            pb.min.x = parentbox[0]; pb.min.y = parentbox[1]; pb.max.x = parentbox[2]; pb.max.y = parentbox[3];
            rp_rect_intersect(&screenbox, &pb);
            sc = rp_screen();
            rp_rect_intersect(&screenbox, &sc);
        }
        if ((screenbox.max.x <= screenbox.min.x) || (screenbox.max.y <= screenbox.min.y)) { return; }
    }

    if ((screenbox.min.x < screenbox.max.x) && (screenbox.min.y < screenbox.max.y)) {
        if (rp_add_room(otherroom, depth, &screenbox, S->cb1[portalnum] & PORTALFLAG_SPECIAL)) {
            return;
        }
    } else {
        return;
    }

    for (i = 0; i < S->nportals; i++) {
        if (i != portalnum) {
            if ((otherroom == S->conn1[i]) || (otherroom == S->conn2[i])) {
                f32 b[4];
                b[0] = screenbox.min.x; b[1] = screenbox.min.y; b[2] = screenbox.max.x; b[3] = screenbox.max.y;
                rp_queue(otherroom, i, depth + 1, b);
            }
        }
    }
}

/* ---- global vis command list (sub_GAME_7F0B8A24 / parse_global_vis_command_list) ---- */

typedef struct RpVisCmd { u8 type; u8 length; u8 padding[2]; s32 arg; } RpVisCmd;

enum {
    RV_END = 0x00, RV_PUSH = 0x01, RV_POP = 0x02, RV_AND = 0x03, RV_OR = 0x04, RV_NOT = 0x05, RV_XOR = 0x06,
    RV_PUSH_IF_ROOM_IN_RANGE = 0x14, RV_FORCE_VISIBLE = 0x1e, RV_MATCH_PORTAL_VIS = 0x1f,
    RV_ADD_VISIBLE_ROOM = 0x20, RV_REMOVE_VIS = 0x21, RV_VISIBLE_IF_SEEN = 0x22, RV_NOT_VISIBLE_IF_SEEN = 0x23,
    RV_DISABLE_ROOM = 0x24, RV_DISABLE_ROOM_RANGE = 0x25, RV_PRELOAD_ROOM = 0x26, RV_PRELOAD_ROOM_RANGE = 0x27,
    RV_IF = 0x50, RV_DONT_EXEC = 0x51, RV_ENDIF_CONTINUE = 0x52, RV_IF_PULL = 0x5a, RV_TOGGLE_EXEC = 0x5b,
    RV_ENDIF = 0x5c
};

static void rp_push(s32 v) { rp.stack[rp.stackcount] = v; rp.stackcount = (rp.stackcount + 1) % RP_STACK; }
static s32 rp_pop(void)
{
    rp.stackcount = (rp.stackcount + (RP_STACK - 1)) % RP_STACK;
    return rp.stack[rp.stackcount];
}

static RpVisCmd *rp_parse_vis(RpVisCmd *cmd, s32 execute)
{
    RpVisCmd *ret;
    s32 value;
    bbox2d sp68, sp58, sc;

    rp.vflag = FALSE;
    if (cmd == NULL) { return cmd; }

    while (TRUE) {
        switch (cmd->type) {
            case RV_END:
                return cmd;
            case RV_PUSH:
                if (execute) { rp_push(cmd->arg); }
                cmd += cmd->length;
                break;
            case RV_POP:
                if (execute) { rp_pop(); }
                cmd += cmd->length;
                break;
            case RV_AND:
                if (execute) { value = rp_pop(); rp_push(rp_pop() & value); }
                cmd += cmd->length;
                break;
            case RV_OR:
                if (execute) { value = rp_pop(); rp_push(rp_pop() | value); }
                cmd += cmd->length;
                break;
            case RV_NOT:
                if (execute) { rp_push(rp_pop() == 0); }
                cmd += cmd->length;
                break;
            case RV_XOR:
                if (execute) { value = rp_pop(); rp_push(rp_pop() ^ value); }
                cmd += cmd->length;
                break;
            case RV_PUSH_IF_ROOM_IN_RANGE:
                if (execute) {
                    s32 curroom = S->curroom;
                    rp_push(cmd[1].arg <= curroom && curroom <= cmd[2].arg);
                }
                cmd += cmd->length;
                break;
            case RV_FORCE_VISIBLE:
                if (execute) {
                    rp.vbox = rp_screen();
                    rp.curvis = FALSE;
                }
                cmd += cmd->length;
                break;
            case RV_MATCH_PORTAL_VIS:
                if (execute) {
                    sc = rp_screen();
                    if (rp_portal_box(cmd[1].arg, &rp.vbox) == 0) {
                        rp.curvis = TRUE;
                    } else if (rp_rect_intersect(&rp.vbox, &sc) == 0) {
                        rp.curvis = TRUE;
                    } else {
                        rp.curvis = FALSE;
                    }
                }
                cmd += cmd->length;
                break;
            case RV_VISIBLE_IF_SEEN:
                if (execute) {
                    sc = rp_screen();
                    if (rp_portal_box(cmd[1].arg, &sp68) && rp_rect_intersect(&sp68, &sc)) {
                        if (rp.curvis) {
                            rp.vbox = sp68;
                            rp.curvis = FALSE;
                        } else {
                            rp_rect_union(&rp.vbox, &sp68);
                        }
                    }
                }
                cmd += cmd->length;
                break;
            case RV_NOT_VISIBLE_IF_SEEN:
                if (execute && !rp.curvis) {
                    sc = rp_screen();
                    if (!rp_portal_box(cmd[1].arg, &sp58)) {
                        rp.curvis = TRUE;
                    } else if (!rp_rect_intersect(&sp58, &sc)) {
                        rp.curvis = TRUE;
                    } else if (!rp_rect_intersect(&rp.vbox, &sp58)) {
                        rp.curvis = TRUE;
                    }
                }
                cmd += cmd->length;
                break;
            case RV_ADD_VISIBLE_ROOM:
                if (execute && !rp.curvis) {
                    if (rp_room_on_screen(cmd[1].arg, &rp.vbox)) {
                        rp_add_room(cmd[1].arg, 0, &rp.vbox, 0);
                    }
                }
                cmd += cmd->length;
                break;
            case RV_DISABLE_ROOM:
                if (execute && cmd[1].arg >= 0 && cmd[1].arg < MAXROOMCOUNT) { rp.mask[cmd[1].arg] = TRUE; }
                cmd += cmd->length;
                break;
            case RV_DISABLE_ROOM_RANGE:
                if (execute) {
                    s32 room = cmd[1].arg;
                    while (room <= cmd[2].arg) {
                        if (room >= 0 && room < MAXROOMCOUNT) { rp.mask[room] = TRUE; }
                        room++;
                    }
                }
                cmd += cmd->length;
                break;
            case RV_PRELOAD_ROOM:
            case RV_PRELOAD_ROOM_RANGE:
                /* bgCheckIfRoomModelNeedsLoad has side effects and no bearing on boxes */
                cmd += cmd->length;
                break;
            case RV_REMOVE_VIS:
                if (execute) { rp.curvis = TRUE; }
                cmd += cmd->length;
                break;
            case RV_IF:
                ret = rp_parse_vis(cmd + cmd->length, execute);
                ret += ret->length;
                cmd = ret;
                break;
            case RV_ENDIF_CONTINUE:
                cmd += cmd->length;
                rp.vflag = FALSE;
                return cmd;
            case RV_DONT_EXEC:
                cmd += cmd->length;
                if (execute) { execute = FALSE; rp.vflag = TRUE; } else { execute = FALSE; }
                break;
            case RV_IF_PULL:
                value = rp_pop();
                ret = rp_parse_vis(cmd + cmd->length, value & execute);
                cmd = ret;
                if (!rp.vflag) { continue; }
                execute = FALSE;
                break;
            case RV_TOGGLE_EXEC:
                execute ^= TRUE;
                cmd += cmd->length;
                break;
            case RV_ENDIF:
                cmd += cmd->length;
                return cmd;
            default:
                return cmd;
        }
    }
}

static void rp_run(void)
{
    int i, var_s0;
    bbox2d sc = rp_screen();
    f32 screenbounds[4];

    screenbounds[0] = sc.min.x; screenbounds[1] = sc.min.y; screenbounds[2] = sc.max.x; screenbounds[3] = sc.max.y;

    rp.nrooms = 0;
    memset(rp.visit, 0, sizeof(rp.visit));
    memset(rp.mask, 0, sizeof(rp.mask));
    for (i = 0; i < PORTMAX; i++) { rp.cache[i].count = -1; }
    rp.qw = rp.qr = 0;
    memcpy(rp.stack, S->stack, sizeof(rp.stack));
    rp.stackcount = S->stackcount;
    rp.curvis = 0;

    if (S->vislist != NULL) {
        rp_parse_vis((RpVisCmd *) S->vislist, 1);
    }

    if ((S->levelentry == LEVEL_INDEX_CRAD) || S->nportals == 0) {
        if (S->levelentry == LEVEL_INDEX_CRAD) {
            bbox2d b = rp_screen();
            rp_add_room(9, 0, &b, 1);
        }
        for (var_s0 = 1; var_s0 < S->maxrooms; var_s0++) {
            bbox2d b = rp_screen();
            if (rp_room_on_screen(var_s0, &b) != 0) {
                bbox2d b2 = rp_screen();
                rp_add_room(var_s0, 0, &b2, 1);
            }
        }
    } else {
        bbox2d b = rp_screen();
        rp_add_room(S->curroom, 0, &b, 1);

        for (var_s0 = 0; var_s0 < S->nportals; var_s0++) {
            if ((S->curroom == S->conn1[var_s0]) || (S->curroom == S->conn2[var_s0])) {
                rp_queue(S->curroom, var_s0, 1, screenbounds);
            }
        }
        while (rp.qr != rp.qw) {
            RpQ *e = &rp.q[rp.qr];
            rp_descend(e->roomnum, e->portalnum, e->depth, e->box);
            rp.qr++;
            if (rp.qr == RP_QLEN) { rp.qr = 0; }
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Camera derivation                                                          */
/* ------------------------------------------------------------------------- */

static int inv4(const double m[4][4], double out[4][4])
{
    double a[4][8];
    int i, j, k;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) { a[i][j] = m[i][j]; a[i][j + 4] = (i == j) ? 1.0 : 0.0; }
    }
    for (i = 0; i < 4; i++) {
        int piv = i;
        double best = fabs(a[i][i]);
        for (k = i + 1; k < 4; k++) { if (fabs(a[k][i]) > best) { best = fabs(a[k][i]); piv = k; } }
        if (best < 1e-12) { return 0; }
        if (piv != i) { for (j = 0; j < 8; j++) { double t = a[i][j]; a[i][j] = a[piv][j]; a[piv][j] = t; } }
        { double d = 1.0 / a[i][i]; for (j = 0; j < 8; j++) { a[i][j] *= d; } }
        for (k = 0; k < 4; k++) {
            if (k != i) {
                double f = a[k][i];
                if (f != 0.0) { for (j = 0; j < 8; j++) { a[k][j] -= f * a[i][j]; } }
            }
        }
    }
    for (i = 0; i < 4; i++) { for (j = 0; j < 4; j++) { out[i][j] = a[i][j + 4]; } }
    return 1;
}

static int inv3(const double m[3][3], double out[3][3], double *det_out)
{
    double a = m[0][0], b = m[0][1], c = m[0][2], d = m[1][0], e = m[1][1], f = m[1][2], g = m[2][0], h = m[2][1], k = m[2][2];
    double det = a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
    double id;
    if (det_out) { *det_out = det; }
    if (fabs(det) < 1e-12) { return 0; }
    id = 1.0 / det;
    out[0][0] = (e * k - f * h) * id; out[0][1] = (c * h - b * k) * id; out[0][2] = (b * f - c * e) * id;
    out[1][0] = (f * g - d * k) * id; out[1][1] = (a * k - c * g) * id; out[1][2] = (c * d - a * f) * id;
    out[2][0] = (d * h - e * g) * id; out[2][1] = (b * g - a * h) * id; out[2][2] = (a * e - b * d) * id;
    return 1;
}

/* v (row) * M : out_j = sum_i v_i * M[i][j] */
static void rowvec_mul3(const double v[3], const double M[3][3], double out[3])
{
    int j;
    for (j = 0; j < 3; j++) { out[j] = v[0] * M[0][j] + v[1] * M[1][j] + v[2] * M[2][j]; }
}

static void mul44(const float a[4][4], const double b[4][4], double out[4][4])
{
    int i, j, k;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            double s = 0.0;
            for (k = 0; k < 4; k++) { s += (double) a[i][k] * b[k][j]; }
            out[i][j] = s;
        }
    }
}

/* Derive the camera for one projection pair. ok = 0 when this is not the
 * room (field_10E0) projection. */
static void rp_camera(const float raw[4][4], const float bl[4][4], RpCam *cam)
{
    const Mtxf *Vc = &S->V;
    double Pi[4][4], Lc[4][4], Lb[4][4];
    double Rc[3][3], Rb[3][3], RcI[3][3], RbI[3][3], RvI[3][3], det;
    double tc[3], tb[3], sc_[3], sb_[3], cam_c[3], cam_b[3], d[3], tv[3];
    int i, j;

    memset(cam, 0, sizeof(*cam));
    {   /* persp is a float 4x4; keep it in double for the inverse */
        double Pd[4][4];
        for (i = 0; i < 4; i++) { for (j = 0; j < 4; j++) { Pd[i][j] = S->persp.m[i][j]; } }
        if (!inv4(Pd, Pi)) { return; }
    }
    mul44(raw, Pi, Lc);
    mul44(bl, Pi, Lb);

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) { Rc[i][j] = Lc[i][j]; Rb[i][j] = Lb[i][j]; }
        tc[i] = Lc[3][i]; tb[i] = Lb[3][i];
    }
    /* the derived rotation must be the game's camera rotation, else this is
     * not the room projection (pure perspective, HUD, menus) */
    {
        double md = 0.0;
        for (i = 0; i < 3; i++) { for (j = 0; j < 3; j++) { double x = fabs(Rc[i][j] - Vc->m[i][j]); if (x > md) { md = x; } } }
        cam->rotdiff = md;
        if (md > 0.05) { return; }
    }
    if (!inv3(Rc, RcI, &det) || !inv3(Rb, RbI, &det)) { return; }

    /* camera position in the room-scaled space: x with x*R + t = 0 */
    for (i = 0; i < 3; i++) { tv[i] = -tc[i]; }
    rowvec_mul3(tv, RcI, sc_);
    for (i = 0; i < 3; i++) { tv[i] = -tb[i]; }
    rowvec_mul3(tv, RbI, sb_);

    /* world camera of the game's matrix: x*Vr + Vt = 0 */
    {
        double Vr[3][3];
        for (i = 0; i < 3; i++) { for (j = 0; j < 3; j++) { Vr[i][j] = Vc->m[i][j]; } }
        if (!inv3(Vr, RvI, &det)) { return; }
        for (i = 0; i < 3; i++) { tv[i] = -(double) Vc->m[3][i]; }
        rowvec_mul3(tv, RvI, cam_c);
    }
    {   /* scale / model-origin consistency (diagnostic) */
        double md = 0.0;
        for (i = 0; i < 3; i++) {
            double x = fabs((cam_c[i] - (double) S->model_pos.f[i]) * S->d364cc - sc_[i]);
            if (x > md) { md = x; }
        }
        cam->scalediff = md;
    }

    cam->exact = (memcmp(raw, bl, sizeof(float) * 16) == 0);
    cam->ppos = S->ppos;
    if (cam->exact) {
        cam->V = *Vc;
    } else {
        double k, dt, Rn[3][3];
        if (!(S->d364cc > 1e-9f)) { return; }
        for (i = 0; i < 3; i++) { d[i] = (sb_[i] - sc_[i]) / (double) S->d364cc; cam_b[i] = cam_c[i] + d[i]; }
        /* normalise the lerped rotation (no cos-shrink) */
        inv3(Rb, RbI, &dt);
        k = cbrt(fabs(dt));
        if (!(k > 1e-6)) { return; }
        for (i = 0; i < 3; i++) { for (j = 0; j < 3; j++) { Rn[i][j] = Rb[i][j] / k; } }
        for (i = 0; i < 3; i++) { for (j = 0; j < 3; j++) { cam->V.m[i][j] = (f32) Rn[i][j]; } }
        for (j = 0; j < 3; j++) {
            cam->V.m[3][j] = (f32) -(cam_b[0] * Rn[0][j] + cam_b[1] * Rn[1][j] + cam_b[2] * Rn[2][j]);
        }
        for (i = 0; i < 3; i++) { cam->V.m[i][3] = 0.0f; }
        cam->V.m[3][3] = 1.0f;
        cam->ppos.x = (f32) (S->ppos.x + d[0]);
        cam->ppos.y = (f32) (S->ppos.y + d[1]);
        cam->ppos.z = (f32) (S->ppos.z + d[2]);
    }
    cam->ok = 1;
}

/* ------------------------------------------------------------------------- */
/* Public API (render thread)                                                 */
/* ------------------------------------------------------------------------- */

static float s_note_raw[4][4], s_note_bl[4][4];
static int s_note_have, s_note_dirty;

static float s_world_raw[4][4], s_world_bl[4][4];   /* the derivable (room) pair */
static int s_world_have;
static float s_non_raw[4][4], s_non_bl[4][4];       /* last pair that was not the room projection */
static int s_non_have;
static int s_replay_valid;
static int s_replay_blend_pass;   /* D583: a replay at a blended camera ran in this pass */
static RpCam s_cam;

static unsigned s_cnt_shrink;   /* D583: replayed box < half the game's */
static unsigned s_cnt_replace, s_cnt_gatefail, s_cnt_fullview, s_cnt_noroom, s_cnt_unreached, s_cnt_nocam, s_cnt_replays, s_cnt_nosnap;

void interpPortalPassBegin(const void *dl)
{
    int i;
    S = NULL;
    for (i = 0; i < RP_SLOTS; i++) {
        if (s_ring[i].valid && s_ring[i].dl == dl) { S = &s_ring[i]; break; }
    }
    s_world_have = 0;
    s_non_have = 0;
    s_replay_valid = 0;
    s_replay_blend_pass = 0;
    s_note_have = 0;
    s_note_dirty = 0;
}

void interpPortalNoteProj(const float raw[4][4], const float blended[4][4])
{
    memcpy(s_note_raw, raw, sizeof(s_note_raw));
    memcpy(s_note_bl, blended, sizeof(s_note_bl));
    s_note_have = 1;
    s_note_dirty = 1;
}

/* bgScissorCurrentPlayerViewF + bgScissorCurrentPlayerView clamping */
static void rp_rect(const bbox2d *b, int *l, int *t, int *r, int *bt)
{
    s32 left = (s32) b->min.x, top = (s32) b->min.y;
    s32 width = ceilFloatToInt(b->max.x), height = ceilFloatToInt(b->max.y);
    if (left < (s32) S->viewleft) { left = (s32) S->viewleft; }
    if (top < (s32) S->viewtop) { top = (s32) S->viewtop; }
    if (S->viewleft + S->viewx < width) { width = S->viewleft + S->viewx; }
    if (S->viewtop + S->viewy < height) { height = S->viewtop + S->viewy; }
    *l = left; *t = top; *r = width; *bt = height;
}

static int rp_box_ok(const bbox2d *b)
{
    return isfinite(b->min.x) && isfinite(b->min.y) && isfinite(b->max.x) && isfinite(b->max.y) &&
           fabsf(b->min.x) < 1e6f && fabsf(b->min.y) < 1e6f && fabsf(b->max.x) < 1e6f && fabsf(b->max.y) < 1e6f;
}

static int rp_game_index(s32 room)
{
    int i;
    for (i = 0; i < S->ndrawn; i++) {
        if (S->d_room[i] == room) { return i; }
    }
    return -1;
}

/* D578: the game's rect for `room` in the snapshot one game frame before S
 * (same level), or 0 if that frame is not in the ring or did not draw it. */
static int rp_prev_game_rect(s32 room, bbox2d *out)
{
    int i, j;
    for (i = 0; i < RP_SLOTS; i++) {
        const IpSnap *p = &s_ring[i];
        if (!p->valid || p == S || p->frame + 1 != S->frame || p->portals != S->portals) { continue; }
        for (j = 0; j < p->ndrawn; j++) {
            if (p->d_room[j] == room) { *out = p->d_bbox[j]; return 1; }
        }
        return 0;
    }
    return 0;
}

/* D583: 1 = the snapshot one game frame before S drew `room`, 0 = it did
 * not, -1 = that frame is not in the ring (or another level). */
static int rp_prev_game_drew(s32 room)
{
    int i, j;
    for (i = 0; i < RP_SLOTS; i++) {
        const IpSnap *p = &s_ring[i];
        if (!p->valid || p == S || p->frame + 1 != S->frame || p->portals != S->portals) { continue; }
        for (j = 0; j < p->ndrawn; j++) {
            if (p->d_room[j] == room) { return 1; }
        }
        return 0;
    }
    return -1;
}

static int rp_replay_index(s32 room)
{
    int i;
    for (i = 0; i < rp.nrooms; i++) {
        if (rp.rooms[i].roomid == room) { return i; }
    }
    return -1;
}

/* D578 (tunnel holes): rooms the replay reaches at the interpolated camera that
 * the game's list for the CURRENT camera never drew. Such a room is simply
 * absent from the display list, so the in-between pass shows background where
 * the exact frame has wall. Counted per replay, taken by the pass loop. */
static int s_extra_rooms;
static unsigned s_cnt_extra;
static u32 s_extra_vphys[IP_MAXEXTRA];   /* D583: vertex base of each extra room */
static int s_extra_prev[IP_MAXEXTRA];    /* D583: previous snapshot drew it: 1 / 0 / -1 unknown */

static void rp_replay_now(void)
{
    int i, extra = 0;
    rp.V = s_cam.V;
    rp.ppos = s_cam.ppos;
    rp_run();
    s_replay_valid = 1;
    s_cnt_replays++;
    if (!s_cam.exact && S != NULL) {
        s_replay_blend_pass = 1;
        for (i = 0; i < rp.nrooms; i++) {
            int l, t, r, b;
            if (rp_game_index(rp.rooms[i].roomid) >= 0 || !rp_box_ok(&rp.rooms[i].bbox)) { continue; }
            rp_rect(&rp.rooms[i].bbox, &l, &t, &r, &b);
            if (r - l >= 3 && b - t >= 3) {   /* ignore sub-3px slivers */
                /* D583: the caller decides from the display lists whether it is
                 * a hole (vertex base = the room's SPSEGMENT_BG_VTX value). */
                if (extra < IP_MAXEXTRA) {
                    const s32 room = rp.rooms[i].roomid;
                    s_extra_vphys[extra] = (room >= 0 && room < MAXROOMCOUNT) ? S->vphys[room] : 0;
                    s_extra_prev[extra] = rp_prev_game_drew(room);
                }
                extra++;
            }
        }
    }
    s_extra_rooms = extra;
    if (extra) { s_cnt_extra++; }
}

int interpPortalReplayedThisPass(void)
{
    return s_replay_blend_pass;
}

int interpPortalTakeExtraRooms(uint32_t *vphys, int *prevdrew, int max)
{
    int i, n = s_extra_rooms;
    for (i = 0; i < n && i < max && i < IP_MAXEXTRA; i++) {
        vphys[i] = s_extra_vphys[i];
        prevdrew[i] = s_extra_prev[i];
    }
    s_extra_rooms = 0;
    return n;
}

unsigned interpPortalExtraCount(void)
{
    return s_cnt_extra;
}

void interpPortalCounts(unsigned out[8])
{
    out[0] = s_cnt_replace; out[1] = s_cnt_replays; out[2] = s_cnt_fullview; out[3] = s_cnt_gatefail;
    out[4] = s_cnt_noroom; out[5] = s_cnt_unreached; out[6] = s_cnt_nocam; out[7] = s_cnt_nosnap;
}

int interpPortalScissor(uint32_t seg14, float rl, float rt, float rr, float rb,
                        int replace, int *ol, int *ot, int *or_, int *ob)
{
    static int env_union = -1;
    int room = -1, i, gi, ri, gl, gt, gr, gb;
    bbox2d rb2;

    if (!replace) { return 0; }
    if (S == NULL) { s_cnt_nosnap++; return 0; }
    if (!s_note_have) { return 0; }
    if (S->portals != g_BgPortals) { return 0; }   /* level changed since the snapshot */

    /* classify the latest projection pair (room projection or not) */
    if (s_note_dirty) {
        s_note_dirty = 0;
        if (s_world_have && !memcmp(s_note_raw, s_world_raw, sizeof(s_note_raw)) && !memcmp(s_note_bl, s_world_bl, sizeof(s_note_bl))) {
            /* same pair as the cached camera */
        } else if (s_non_have && !memcmp(s_note_raw, s_non_raw, sizeof(s_note_raw)) && !memcmp(s_note_bl, s_non_bl, sizeof(s_note_bl))) {
            /* known not to be the room projection */
        } else {
            RpCam cam;
            rp_camera(s_note_raw, s_note_bl, &cam);
            if (cam.ok) {
                s_cam = cam;
                memcpy(s_world_raw, s_note_raw, sizeof(s_world_raw));
                memcpy(s_world_bl, s_note_bl, sizeof(s_world_bl));
                s_world_have = 1;
                s_replay_valid = 0;
            } else {
                memcpy(s_non_raw, s_note_raw, sizeof(s_non_raw));
                memcpy(s_non_bl, s_note_bl, sizeof(s_non_bl));
                s_non_have = 1;
            }
        }
    }
    if (!s_world_have) { s_cnt_nocam++; return 0; }

    if (!s_replay_valid) {
        rp_replay_now();
    }

    if (!replace || s_cam.exact) { return 0; }

    /* which room does this scissor belong to? */
    for (i = 0; i < MAXROOMCOUNT; i++) {
        if (S->vphys[i] != 0 && S->vphys[i] == seg14) {
            room = i;
            break;
        }
    }
    if (room < 0) { s_cnt_noroom++; return 0; }
    gi = rp_game_index(room);
    if (gi < 0) { s_cnt_noroom++; return 0; }
    rp_rect(&S->d_bbox[gi], &gl, &gt, &gr, &gb);
    if (fabsf((float) gl - rl) > 1.01f || fabsf((float) gt - rt) > 1.01f ||
        fabsf((float) gr - rr) > 1.01f || fabsf((float) gb - rb) > 1.01f) {
        /* not a room scissor (HUD / full view / props between rooms) or another room's */
        if (rl <= (float) S->viewleft + 1.01f && rr >= (float) (S->viewleft + S->viewx) - 1.01f &&
            rt <= (float) S->viewtop + 1.01f && rb >= (float) (S->viewtop + S->viewy) - 1.01f) {
            s_cnt_fullview++;
        } else {
            s_cnt_gatefail++;
        }
        return 0;
    }
    ri = rp_replay_index(room);
    if (ri < 0 || !rp_box_ok(&rp.rooms[ri].bbox)) { s_cnt_unreached++; return 2; }   /* D583: caller decides */
    rb2 = rp.rooms[ri].bbox;
    {
        /* D583: replayed box much smaller than the game's (a room the replay
         * clips that the exact frame shows whole: suspected Dam tower flicker). */
        const bbox2d *g = &S->d_bbox[gi];
        const float ga = (g->max.x - g->min.x) * (g->max.y - g->min.y);
        const float ra = (rb2.max.x - rb2.min.x) * (rb2.max.y - rb2.min.y);
        if (ga > 64.0f && ra < 0.5f * ga) {
            static int slog;
            s_cnt_shrink++;
            if (slog++ < 40 || (slog % 500) == 0) {
                sysLogPrintf(LOG_NOTE, "D583 SHRINK frame %u room %d: replay %.0fx%.0f at (%.0f,%.0f), game %.0fx%.0f at (%.0f,%.0f)",
                             videoGetFrameCount(), room, rb2.max.x - rb2.min.x, rb2.max.y - rb2.min.y, rb2.min.x, rb2.min.y,
                             g->max.x - g->min.x, g->max.y - g->min.y, g->min.x, g->min.y);
            }
        }
        /* D583: never tighter than the exact frame's box. The replay alone
         * clipped distant Dam guard towers on in-between presents (they
         * flickered with distance; maintainer Deck A/B: gone with the union, no
         * overdraw seen). The lim clamp below still bounds it. */
        rp_rect_union(&rb2, g);
    }
    if (rp_env("GE_INTERPPORTAL_UNION", &env_union)) {
        rp_rect_union(&rb2, &S->d_bbox[gi]);
    }
    {
        /* D578 (Dam tunnel overdraw): the in-between camera lies between
         * the previous and current exact cameras, so a room's true rect lies
         * near its game rects in those two frames. The replay can return a
         * near-full-view rect (seen: room 131, 128..1164 x 33..600 slot px,
         * vs a small portal opening), and a room drawn without depth then
         * paints over nearer rooms. Clamp the replayed rect to the union of
         * the two game rects, padded. */
        const float pad = 8.0f;
        bbox2d lim = S->d_bbox[gi], prev;
        if (rp_prev_game_rect(room, &prev)) { rp_rect_union(&lim, &prev); }
        lim.min.x -= pad; lim.min.y -= pad; lim.max.x += pad; lim.max.y += pad;
        if (rb2.min.x < lim.min.x) { rb2.min.x = lim.min.x; }
        if (rb2.min.y < lim.min.y) { rb2.min.y = lim.min.y; }
        if (rb2.max.x > lim.max.x) { rb2.max.x = lim.max.x; }
        if (rb2.max.y > lim.max.y) { rb2.max.y = lim.max.y; }
        if (rb2.max.x <= rb2.min.x || rb2.max.y <= rb2.min.y) { s_cnt_unreached++; return 2; }
    }
    rp_rect(&rb2, ol, ot, or_, ob);
    if (*or_ <= *ol || *ob <= *ot) { s_cnt_unreached++; return 2; }
    s_cnt_replace++;
    if ((s_cnt_replace % 4000) == 1) {
        fprintf(stderr, "D578 PORTAL stats: replaced=%u replays=%u fullview=%u gatefail=%u noroom=%u unreached=%u nocam=%u nosnap=%u\n",
                s_cnt_replace, s_cnt_replays, s_cnt_fullview, s_cnt_gatefail, s_cnt_noroom, s_cnt_unreached, s_cnt_nocam, s_cnt_nosnap);
    }
    return 1;
}
