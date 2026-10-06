/* d318watchdog.c -- D318: port-side pin DETECTOR for the Facility
 * (level_34) Ourumov/Trevelyan execution softlock and its D320 siblings.
 *
 * DETECT-AND-LOG ONLY (strip-temp-probes pass, 2026-09-30): D329 fixed the
 * cause (a tick-granularity anim-pin divergence in model.c, Rule-2), so the
 * pin no longer occurs. The recovery action described below (re-seeding the
 * attack via sub_GAME_7F025560) has been REMOVED; the watchdog now only logs
 * `D318W:` lines if the signature ever reappears, so a regression of D329 is
 * visible in a playtest log instead of being silently papered over. The
 * history below is kept for context.
 *
 * What this is (full writeup: docs/dev/findings.md, D318):
 *
 * As-authored latent race, faithfully reproduced by the port (NOT a port
 * bug -- every link in the chain is byte-matched ROM logic):
 *
 *   1. Ourumov (chr 78, ai_22) pre-aims at kneeling Trevelyan (chr 67)
 *      during his monologue: actor_aim_at_actor with TARGET_AIM_ONLY|
 *      TARGET_DONTTURN (attacktype 0x64). chrlvInitActAttack picks a RANDOM
 *      variant from the aim-animation group (randomGetNext() % len) and
 *      modelSetAnimation's it with animlooping=0.
 *   2. The aim anim plays to its endframe H (the "hold" pose). With
 *      animlooping==0, modelTickAnim has no wrap logic; when the frame
 *      reaches endframe, modelConstrainOrWrapAnimFrame clamps BOTH framea
 *      and frameb to ceil(H), so framea==frameb, and the tail of
 *      modelSetAnimFrame2WithChrStuff (if (vb==va) animframe1 = va) then
 *      discards all sub-frame progress: the model is PINNED at H. That is
 *      how a hold pose works by design -- normally something else changes
 *      the attack state next.
 *   3. If the gas cascade sets objective bit 0x04 (combat) mid-monologue,
 *      ai_22 derails to section 0x2a and off=307 runs
 *      actor_fire_or_aim_at_target_update, which SUCCEEDS while the pre-aim
 *      is active: attacktype becomes 0x04, entityid becomes Trevelyan, and
 *      chrlvAttackActionRelated sets a NEW endframe from the variant's
 *      recoil/shoot start frame -- but it never calls modelSetAnimation.
 *   4. Branch (determined by the random variant chosen in step 1):
 *        - new endframe <= H: chrlvTickAttackCommon's "endframe <= frame"
 *          block fires next tick and its unk31 path calls modelSetAnimation
 *          again -> pin broken -> anim advances into the shoot window ->
 *          c78 live-fires Trevelyan -> he dies -> attack ends -> ACT_STAND
 *          -> the stop check at ai_22 off=324 passes -> scene continues.
 *        - new endframe > H: nothing ever calls modelSetAnimation again
 *          (verified by probe: zero calls in frozen runs). The frame stays
 *          pinned at H < shoot_start, c78 never fires, Trevelyan never
 *          dies, and ai_22's off=324 stop check (chrHasStoppedOrPatroling,
 *          TRUE only for ACT_STAND/ACT_ANIM/ACT_PATROL) can never pass
 *          while c78 is in ACT_ATTACK -> PERMANENT SOFTLOCK.
 *
 * N64 manifestation unverified (findings D318, 2026-09-22 correction): the
 * user's empirical prior is that no such freeze occurs on real hardware;
 * whether the derail window ever opens organically on N64 is open (an
 * unthrottled-emulator deliberate-timing A/B is the discriminating test).
 * This watchdog is a deliberate port-layer accommodation either way (same
 * pattern as the D202 M-66b port-side expiration). It detects the exact unrecoverable signature and,
 * after a long margin, re-initializes c78's attack via sub_GAME_7F025560 --
 * the same entry point the game itself uses for a fresh attack (chrlvTickAttack
 * type_of_motion==2 calls it with (attacktype, entityid)). That runs
 * chrlvInitActAttack: random variant pick + modelSetAnimation(start_frame),
 * which breaks the framea==frameb pin. c78 then live-fires Trevelyan exactly
 * like the natural escape path (verified: escape runs kill c67 within ~230
 * ticks of a fresh init), his attack sequence completes, he reaches
 * ACT_STAND, the stop check at off=324 passes, and ai_22 proceeds as
 * authored (the off=333 headshot lands on the corpse -- the same no-op the
 * game already handles when Trevelyan dies of gas before the execution shot).
 *
 * NOTE: merely killing Trevelyan (e.g. via handles_shot_actors) is NOT
 * enough -- a pinned attack has no internal completion event tied to target
 * death, so c78 would stay in ACT_ATTACK and the stop check would still
 * never pass (verified by probe). The attack state itself must be re-seeded.
 *
 * Signature (all must hold for D318_DEADLOCK_TICKS consecutive ticks):
 *   - chr 78 exists, is on AI list 0x0417 (ai_22), offset in [321, 333)
 *     (the sleep/stop-check loop; 333 is the headshot command itself),
 *   - chr 78 actiontype == ACT_ATTACK with act_attack.entityid == 67
 *     (targeting Trevelyan -- the derailed state; legitimate combat at this
 *     offset targets Bond, ent=1),
 *   - chr 67 exists, has a prop, and is not dead.
 *
 * In every non-deadlocked state the watchdog is inert: the escape path
 * kills Trevelyan within ~25-100 ticks (60x under the threshold) and
 * legitimate combat never targets chr 67 from this loop. One-shot per
 * OCCURRENCE: after firing, it re-arms once the signature has been absent
 * for D318_REARM_TICKS consecutive ticks (the deadlock is gone -- recovered
 * or the player moved on), so a later replay of Facility in the same
 * session (AllUnlocked) is still covered. Logs its detection and
 * intervention for playtest audit.
 *
 * D320 generalization (2026-09-22, findings D320): the identical pin was
 * force-reproduced headless on two more lists, and the watchdog is now
 * table-driven over all confirmed signatures:
 *
 *   - 0x0417 ai_22 [321,333) ent=67 c67-alive : D318 Facility execution
 *   - 0x0414 ai_19 [480,548) any-ent          : D320 Facility scientist/
 *                                               pad kneel-aim hold (label
 *                                               0x34 stop-check loop; pin
 *                                               observed at off=483)
 *   - 0x040a ai_9  [60,86)   any-ent          : D320 Control chr-0xFC aim
 *                                               hold (label 0x0b stop-check
 *                                               loop; pin observed at
 *                                               off=69, ~2/3 of variants)
 *
 * Any chr (not just c78) sitting in a listed loop range in ACT_ATTACK with
 * a LIVE-FIRE attacktype (no TARGET_AIM_ONLY bit) for D318_DEADLOCK_TICKS
 * consecutive ticks gets the same sub_GAME_7F025560 re-seed. Depot ai_12
 * (0x040d) tested clean 3/3 under forced repro and is deliberately NOT
 * covered. In every non-deadlocked state each entry is inert: the escape
 * path leaves the loop range in < 2 s (60x under the threshold), and a
 * re-holding guard's anim frame moves between cycles.
 *
 * Repeat rescues: for the D320 entries a successful re-seed plays out the
 * attack, the stop check passes, and the beat RE-ROLLS (re-aim -> wait ->
 * update) -- which can pin AGAIN immediately (verified: in the forced-repro
 * contexts every re-roll re-pinned ~240 ticks after each rescue; whether a
 * given roll pins depends on the random aim-variant and is context/RNG-
 * stream dependent, sometimes ~100%). So there is NO fire cap: while the
 * signature persists, the watchdog re-fires every D318W_REFIRE_TICKS (6 s,
 * > the ~240-tick re-roll cycle; a non-pinned guard leaves the loop range
 * well inside that). This keeps the beat alive -- the guard fires in
 * periodic bursts instead of freezing -- until an escape roll lands, the
 * target dies (the beats progress on their dying/dead checks), or player
 * action breaks the state. Full re-arm only when the signature has been
 * absent for D318_REARM_TICKS.
 *
 * Opt out (silences the detector): GE_D318W=0
 */
#include <stdlib.h>
#include <string.h>

#include "PR/os.h"
#include "bondtypes.h"
#include "chr.h"
#include "chrai.h"
#include "chraction.h"
#include "lv.h"
#include "model.h"
#include "random.h"

#ifdef PORT

/* chrai.c defines these but chrai.h does not declare them. */
extern s32 chraiGetAIListID(AIRecord *AIList, bool *isGlobalAIList);

/* src/bondaicommands.h:467 -- kept local so this port file doesn't pull in
 * the AI-command header just for one bit constant. */
#define D320_TARGET_AIM_ONLY 0x0020 /* "Aim at target instead of firing"   */

#define D318_LIST_AI22       0x0417 /* Ourumov's execution list (ai_22)      */
#define D318_OURUMOV_CHR     78
#define D318_TREVELYAN_CHR   67
#define D318_DEADLOCK_TICKS  600    /* 10 s at 60 Hz; escape takes < 2 s     */
#define D318_REARM_TICKS     600    /* signature-absent ticks before re-arm  */
#define D318W_REFIRE_TICKS   360    /* > the ~240-tick re-roll cycle         */

static ChrRecord *d318wFindChr(s16 chrnum)
{
    s32 i;

    for (i = 0; i < g_NumChrSlots; i++)
    {
        if (g_ChrSlots[i].chrnum == chrnum)
        {
            return &g_ChrSlots[i];
        }
    }
    for (i = 0; i < g_ActiveChrsCount; i++)
    {
        if (g_ActiveChrs[i].chrnum == chrnum)
        {
            return &g_ActiveChrs[i];
        }
    }
    return NULL;
}

/* Table-driven pin signatures (D318 + D320). off_max is exclusive. */
typedef struct
{
    u32         listid;
    s32         off_min, off_max;
    s32         ent;              /* required act_attack.entityid, -1 = any  */
    s32         target_alive_chr; /* must exist w/ prop and be alive, -1 n/a */
    const char *tag;              /* log tag                                 */
} D318WEntry;

#define D318W_NENTRIES 3
static const D318WEntry s_wEntries[D318W_NENTRIES] = {
    { 0x0417, 321, 333, D318_TREVELYAN_CHR, D318_TREVELYAN_CHR, "D318 Facility ai_22" },
    { 0x0414, 480, 548, -1,               -1,                 "D320 Facility ai_19" },
    { 0x040a, 60,  86,  -1,               -1,                 "D320 Control ai_9"   },
};

static bool d318wChrAlive(s32 chrnum)
{
    ChrRecord *c = d318wFindChr((s16)chrnum);

    return c && c->prop && !chrIsDead(c);
}

/* Does this chr match the entry's pin signature? */
static bool d318wMatches(const D318WEntry *en, ChrRecord *c)
{
    bool g = FALSE;

    if (!c->ailist)
    {
        return FALSE;
    }
    if (chraiGetAIListID(c->ailist, &g) != (s32)en->listid)
    {
        return FALSE;
    }
    if (c->aioffset < en->off_min || c->aioffset >= en->off_max)
    {
        return FALSE;
    }
    if (c->actiontype != ACT_ATTACK)
    {
        return FALSE;
    }
    /* live-fire only: an AIM_ONLY pre-aim holding its pose is not a pin */
    if ((s32)c->act_attack.attacktype & D320_TARGET_AIM_ONLY)
    {
        return FALSE;
    }
    if (en->ent >= 0 && (s32)c->act_attack.entityid != en->ent)
    {
        return FALSE;
    }
    return TRUE;
}

void d318WatchdogTick(void)
{
    static int   s_enabled = -1; /* -1 uncached, 0 off (GE_D318W=0), 1 on    */
    static s32   s_run[D318W_NENTRIES];   /* sig-held ticks per entry         */
    static s32   s_absent[D318W_NENTRIES];/* sig-absent ticks per entry       */
    static s32   s_cool[D318W_NENTRIES];  /* ticks since last fire (sig up)   */
    static s32   s_fires[D318W_NENTRIES]; /* reports this occurrence          */
    static bool  s_fired[D318W_NENTRIES];
    s32 e, i;

    if (s_enabled < 0)
    {
        const char *ev = getenv("GE_D318W");

        s_enabled = (ev && ev[0] == '0') ? 0 : 1;
    }
    if (!s_enabled)
    {
        return;
    }

    for (e = 0; e < D318W_NENTRIES; e++)
    {
        const D318WEntry *en  = &s_wEntries[e];
        ChrRecord        *hit = NULL;
        bool              sig = FALSE;

        for (i = 0; i < g_NumChrSlots && !hit; i++)
        {
            if (d318wMatches(en, &g_ChrSlots[i]))
            {
                hit = &g_ChrSlots[i];
            }
        }
        for (i = 0; i < g_ActiveChrsCount && !hit; i++)
        {
            if (d318wMatches(en, &g_ActiveChrs[i]))
            {
                hit = &g_ActiveChrs[i];
            }
        }

        sig = hit && (en->target_alive_chr < 0 || d318wChrAlive(en->target_alive_chr));

        if (sig)
        {
            s_absent[e] = 0;
            if (!s_fired[e])
            {
                if (s_run[e] == 1)
                {
                    osSyncPrintf("D318W: t=%d [%s] pin signature detected (c%d off=%d "
                                 "ACT_ATTACK atk=0x%x ent=%d) -- starting %d-tick confirmation window\n",
                                 (int)g_GlobalTimer, en->tag, (int)hit->chrnum,
                                 (int)hit->aioffset, (unsigned)hit->act_attack.attacktype,
                                 (int)hit->act_attack.entityid, D318_DEADLOCK_TICKS);
                }
                s_run[e]++;
                if (s_run[e] >= D318_DEADLOCK_TICKS)
                {
                    osSyncPrintf("D318W: t=%d [%s] anim pin CONFIRMED on c%d (atk=0x%x ent=%d) "
                                 "-- detect-only, no recovery; D329 should prevent this, please report\n",
                                 (int)g_GlobalTimer, en->tag, (int)hit->chrnum,
                                 (unsigned)hit->act_attack.attacktype,
                                 (int)hit->act_attack.entityid);
                    s_fired[e] = TRUE;
                    s_fires[e] = 1;
                    s_cool[e]  = 0;
                }
            }
            else if (++s_cool[e] >= D318W_REFIRE_TICKS)
            {
                /* Signature still held: periodic reminder, detect-only. */
                s_cool[e] = 0;
                osSyncPrintf("D318W: t=%d [%s] pin still held (report %d) -- c%d off=%d "
                             "atk=0x%x ent=%d\n",
                             (int)g_GlobalTimer, en->tag, s_fires[e] + 1,
                             (int)hit->chrnum, (int)hit->aioffset,
                             (unsigned)hit->act_attack.attacktype,
                             (int)hit->act_attack.entityid);
                s_fires[e]++;
            }
        }
        else
        {
            s_run[e]  = 0;
            s_cool[e] = 0;
            /* Once the signature has been clear for a while after a report,
             * re-arm so a later replay of the level in the same process
             * (AllUnlocked) is still covered. */
            if (s_fired[e] && ++s_absent[e] >= D318_REARM_TICKS)
            {
                s_fired[e] = FALSE;
                s_absent[e] = 0;
                s_fires[e]  = 0;
                osSyncPrintf("D318W: t=%d [%s] signature clear for %d ticks -- watchdog re-armed\n",
                             (int)g_GlobalTimer, en->tag, D318_REARM_TICKS);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* D318T: diagnostic timeline for the "Ourumov shoots Trevelyan the   */
/* moment his lines begin" report (2026-09-20). Env-gated, OFF by     */
/* default; enable with GE_D318T=1. Port-side read-only: logs state   */
/* transitions so a single playtest captures which combat trigger     */
/* (gas bit 0x04 vs Bond near-miss flag) derailed ai_22, and how many */
/* ticks after the monologue started it landed. No game state is      */
/* touched.                                                            */
/*                                                                     */
/* Background: the ONLY path by which c78 live-fires Trevelyan        */
/* (entityid 67) in this scene is section 0x2a -> off=307             */
/* actor_fire_or_aim_at_target_update succeeding while the pre-aim    */
/* (ACT_ATTACK + AIM_ONLY|DONTTURN) is active. That requires a combat */
/* trigger inside the monologue loop: gas bit 0x04 (off=75) or        */
/* chrIfNearMiss (off=87, CHRFLAG_NEAR_MISS -- set by Bond bullets    */
/* passing c78's bounds, cleared on c78's next tick). The freeze      */
/* (D318) and the immediate-fire are the two branches of the same     */
/* derailed state (random aim-variant endframe vs the hold frame H).  */
/* ------------------------------------------------------------------ */

void d318TimelineTick(void)
{
    static int  s_on      = -1; /* -1 uncached, 0 off, 1 on (GE_D318T=1)   */
    static s32  s_obj     = -1;  /* last logged objectiveregisters1        */
    static s32  s_off     = -1;  /* last logged c78 aioffset               */
    static s32  s_act     = -1;  /* last logged c78 actiontype             */
    static s32  s_atk     = -1;  /* last logged c78 act_attack.attacktype  */
    static s32  s_ent     = -1;  /* last logged c78 act_attack.entityid    */
    static s32  s_nm      = -1;  /* last logged c78 NEAR_MISS flag state   */
    static f32  s_dmg     = -1.0f;
    static bool s_dead    = FALSE;
    static s32  s_hb      = 0;
    static s32  s_det     = 0;  /* E1 determinism fingerprint counter       */
    ChrRecord  *c78;
    ChrRecord  *c67;

    if (s_on < 0)
    {
        const char *e = getenv("GE_D318T");

        s_on = (e && e[0] == '1') ? 1 : 0;
    }
    if (!s_on)
    {
        return;
    }

    /* Unified chr view: real guards live in g_ChrSlots[0..g_NumChrSlots) with
     * model != NULL; the background script entities (chrnum 0xFE, list IDs
     * >= 0x1000) live in g_ActiveChrs. Both are ticked by chrlvAllChrTick,
     * so both must be scanned for pins / fingerprints. (D320, 2026-09-22:
     * the detector originally walked g_ActiveChrs only and was blind to
     * every real guard.) */
    static ChrRecord *s_all[96];
    static int        s_nall = 0;
    s32               i;

    s_nall = 0;
    for (i = 0; i < g_NumChrSlots && s_nall < 96; i++)
    {
        if (g_ChrSlots[i].model != NULL)
        {
            s_all[s_nall++] = &g_ChrSlots[i];
        }
    }
    for (i = 0; i < g_ActiveChrsCount && s_nall < 96; i++)
    {
        s_all[s_nall++] = &g_ActiveChrs[i];
    }

    c78 = d318wFindChr(D318_OURUMOV_CHR);
    c67 = d318wFindChr(D318_TREVELYAN_CHR);

    /* Objective bitfield transitions (the derail triggers live here:     */
    /* 0x04 gas/combat, 0x20 surrender/monologue, 0x40 flee).             */
    if (s_obj != objectiveregisters1)
    {
        osSyncPrintf("D318T: t=%d obj bits 0x%08x -> 0x%08x\n",
                 (int)g_GlobalTimer, (unsigned)s_obj, (unsigned)objectiveregisters1);
        s_obj = objectiveregisters1;
    }

    if (c78)
    {
        s32 nm = (c78->chrflags & CHRFLAG_NEAR_MISS) != 0;

        /* D318 anim-internals line: catches the kneel-attack route          */
        /* (unk54==0 => sub_GAME_7F0256F0 "chrAttackKneel" re-inits) and the */
        /* hold/pin frame values, for the "bending down too far" report.     */
        {
            static s32 s_tom  = -1;
            static u32 s_u54  = 0xFFFFFFFFu;
            static f32 s_endf = -1.0f;

            s32 tom  = (s32)c78->act_attack.type_of_motion;
            u32 u54  = c78->act_attack.unk54;
            f32 endf = c78->model ? modelGetAnimEndFrame(c78->model) : -1.0f;

            if (s_tom != tom || s_u54 != u54 || s_endf != endf)
            {
                osSyncPrintf("D318T: t=%d c78 anim mot=%d unk54=%u endframe=%.2f frame1=%.2f (off=%d)\n",
                             (int)g_GlobalTimer, tom, (unsigned)u54,
                             (double)endf,
                             (double)(c78->model ? modelGetAnimFrame(c78->model) : 0.0f),
                             (int)c78->aioffset);
                s_tom  = tom;
                s_u54  = u54;
                s_endf = endf;
            }
        }

        if (s_off != (s32)c78->aioffset || s_act != (s32)c78->actiontype)
        {
            const char *phase = "?";

            if (c78->aioffset >= 38 && c78->aioffset < 44)      { phase = "pre-aim(off38)"; }
            else if (c78->aioffset >= 44 && c78->aioffset < 69) { phase = "wait-loop(off44)"; }
            else if (c78->aioffset >= 69 && c78->aioffset < 272){ phase = "MONOLOGUE(off69+)"; }
            else if (c78->aioffset >= 272 && c78->aioffset < 295){ phase = "execution(0x13)"; }
            else if (c78->aioffset >= 302 && c78->aioffset < 333){ phase = "COMBAT-0x2a"; }

            osSyncPrintf("D318T: t=%d c78 off=%d [%s] act=%d atk=0x%x ent=%d\n",
                     (int)g_GlobalTimer, (int)c78->aioffset, phase,
                     (int)c78->actiontype, (unsigned)c78->act_attack.attacktype,
                     (int)c78->act_attack.entityid);
            s_off = (s32)c78->aioffset;
            s_act = (s32)c78->actiontype;
        }
        if (s_atk != (s32)c78->act_attack.attacktype || s_ent != (s32)c78->act_attack.entityid)
        {
            osSyncPrintf("D318T: t=%d c78 ATTACK CHANGE atk=0x%x ent=%d (off=%d)\n",
                     (int)g_GlobalTimer, (unsigned)c78->act_attack.attacktype,
                     (int)c78->act_attack.entityid, (int)c78->aioffset);
            s_atk = (s32)c78->act_attack.attacktype;
            s_ent = (s32)c78->act_attack.entityid;
        }
        if (s_nm != nm)
        {
            osSyncPrintf("D318T: t=%d c78 NEAR_MISS flag %s (Bond bullet passed his bounds)\n",
                     (int)g_GlobalTimer, nm ? "SET" : "cleared");
            s_nm = nm;
        }
    }

    if (c67)
    {
        bool dead = chrIsDead(c67);

        if (s_dmg != c67->damage || s_dead != dead)
        {
            osSyncPrintf("D318T: t=%d c67(Trevelyan) dmg=%.2f/%.2f %s\n",
                     (int)g_GlobalTimer, (double)c67->damage, (double)c67->maxdamage,
                     dead ? "DEAD" : "");
            s_dmg = c67->damage;
            s_dead = dead;
        }
    }

    /* Heartbeat while c78 is on his execution list, so gaps are visible. */
    if (c78 && c78->ailist)
    {
        bool g = FALSE;

        if (chraiGetAIListID(c78->ailist, &g) == D318_LIST_AI22 && (++s_hb >= 60))
        {
            s_hb = 0;
            osSyncPrintf("D318T: t=%d c78 hb off=%d act=%d atk=0x%x ent=%d obj=0x%08x\n",
                     (int)g_GlobalTimer, (int)c78->aioffset, (int)c78->actiontype,
                     (unsigned)c78->act_attack.attacktype, (int)c78->act_attack.entityid,
                     (unsigned)objectiveregisters1);
        }
    }

    /* E1 determinism fingerprint: one line per second over ALL active chrs  */
    /* + the PRNG seed + objective bits. Two no-input runs that differ here */
    /* are non-deterministic in the port (foundational); identical logs     */
    /* mean run-to-run variation is input-timing-driven.                    */
    if (++s_det >= 60)
    {
        s32 h = 0x811C9DC5;

        s_det = 0;
        for (i = 0; i < s_nall; i++)
        {
            ChrRecord *c = s_all[i];
            union { f32 f; u32 u; } xf, yf, zf, df;

            xf.f = c->prop ? c->prop->pos.x : 0.0f;
            yf.f = c->prop ? c->prop->pos.y : 0.0f;
            zf.f = c->prop ? c->prop->pos.z : 0.0f;
            df.f = c->damage;
            h ^= c->chrnum;          h = (h * 0x01000193) ^ 0x9E3779B9;
            h ^= (s32)c->actiontype; h = (h * 0x01000193) ^ 0x9E3779B9;
            h ^= (s32)c->aioffset;   h = (h * 0x01000193) ^ 0x9E3779B9;
            h ^= xf.u;               h = (h * 0x01000193) ^ 0x9E3779B9;
            h ^= yf.u;               h = (h * 0x01000193) ^ 0x9E3779B9;
            h ^= zf.u;               h = (h * 0x01000193) ^ 0x9E3779B9;
            h ^= df.u;               h = (h * 0x01000193) ^ 0x9E3779B9;
        }
        osSyncPrintf("D318T: DET t=%d seed=%08x%08x obj=0x%08x h=%08x nchrs=%d\n",
                     (int)g_GlobalTimer,
                     (unsigned)(u32)(g_randomSeed >> 32), (unsigned)(u32)g_randomSeed,
                     (unsigned)objectiveregisters1, (unsigned)h, s_nall);
    }

    /* D320: generic D318-class pin DETECTION for the sweep's flagged lists
     * (Facility ai_19, Control ai_9, Depot ai_12 -- see findings D320).
     * Detection only: it logs, it never intervenes; the recovery watchdog
     * above stays hardcoded to ai_22 by design until a second list actually
     * freezes in play (D320's explicit instruction).
     *
     * Signature: a chr in ACT_ATTACK whose attacktype is LIVE FIRE (no
     * TARGET_AIM_ONLY bit -- it is supposed to be shooting) and whose
     * modelGetAnimFrame is byte-stable for 600 consecutive ticks (10 s). A
     * guard that should be firing but whose animation never advances is the
     * exact D318 frozen shape on any list. The two legitimate long-stable
     * states are excluded or self-clearing: monologue pre-aims keep the
     * AIM_ONLY bit (excluded), and a working fire cycle re-inits through
     * chrlvTickAttackCommon's "endframe <= frame" block, so its frame does
     * not stay byte-stable for 10 s. One line at first detection, then one
     * per further 600 stable ticks while it persists; a frozen run keeps
     * printing, an escaped run stops after the single line (or none). */
    {
        typedef struct { s16 chrnum; u32 framebits; s32 stable; s32 nextlog; }
                D320Pin;
        static D320Pin s_pins[8];
        static int     s_npins = 0;
        s32 i, j;

        for (i = 0; i < s_nall; i++)
        {
            ChrRecord *c = s_all[i];
            union { f32 f; u32 u; } fr;
            D320Pin   *p = NULL;

            if (c->actiontype != ACT_ATTACK || c->model == NULL)
            {
                continue;
            }
            if ((s32)c->act_attack.attacktype & D320_TARGET_AIM_ONLY)
            {
                continue; /* legitimate hold pose (monologue pre-aim) */
            }

            fr.f = modelGetAnimFrame(c->model);
            for (j = 0; j < s_npins; j++)
            {
                if (s_pins[j].chrnum == c->chrnum)
                {
                    p = &s_pins[j];
                    break;
                }
            }
            if (!p)
            {
                if (s_npins < 8)
                {
                    p = &s_pins[s_npins++];
                }
                else
                {
                    continue; /* table full: drop the newest, keep tracking */
                }
                p->chrnum    = c->chrnum;
                p->framebits = fr.u;
                p->stable    = 0;
                p->nextlog   = 600;
            }

            if (p->framebits == fr.u)
            {
                p->stable++;
            }
            else
            {
                p->framebits = fr.u;
                p->stable    = 0;
                p->nextlog   = 600;
            }

            if (p->stable >= p->nextlog)
            {
                bool g = FALSE;
                s32 aid = c->ailist ? chraiGetAIListID(c->ailist, &g) : -1;

                osSyncPrintf("D320T: t=%d PIN? c%d aiid=0x%04x%s off=%d atk=0x%x ent=%d "
                             "mot=%d unk54=%u frame=%.2f stable=%d ticks (D318-class pin candidate)\n",
                             (int)g_GlobalTimer, (int)c->chrnum,
                             (unsigned)aid, g ? "G" : "", (int)c->aioffset,
                             (unsigned)c->act_attack.attacktype,
                             (int)c->act_attack.entityid,
                             (s32)c->act_attack.type_of_motion,
                             (unsigned)c->act_attack.unk54,
                             (double)fr.f, p->stable);
                p->nextlog = p->stable + 600;
            }
        }

        /* Drop entries for chrs that left the tracked state so a later
         * re-entry starts a fresh confirmation window. */
        for (i = 0; i < s_npins; i++)
        {
            bool alive = FALSE;

            for (j = 0; j < s_nall; j++)
            {
                ChrRecord *c = s_all[j];

                if (c->chrnum == s_pins[i].chrnum && c->actiontype == ACT_ATTACK
                    && c->model != NULL
                    && !((s32)c->act_attack.attacktype & D320_TARGET_AIM_ONLY))
                {
                    alive = TRUE;
                    break;
                }
            }
            if (!alive)
            {
                s_pins[i] = s_pins[--s_npins];
                i--;
            }
        }
    }
}

#endif /* PORT */
