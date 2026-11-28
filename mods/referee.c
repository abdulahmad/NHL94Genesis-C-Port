/* RULE_REFEREE: NHL 95 PC's referee in play. The cartridge has no referee on the ice; NHL 95 PC's is SortCords 10h,
 * an object of the game like a skater: skaters bounce off him (checkplcoll / checkcx take SCnum 10h), the puck
 * bounces off him (checkpuckcoll_ccx / puckbody), a check knocks him down (checkcheck / FallDown), and every third
 * knock down is a penalty on the checker (checkcheck: refhitcount, AddPenalty 16h, "Abuse of Official").
 *
 * Here he is not in the game's RAM (its 16 sort objects are the cartridge's); he is this file's own object, moved
 * only by the game's state (the puck, the face off spot, the camera, the skaters) and kept in every state (the
 * RULE part, native/rules.c: referee_state_save), so every machine of a session, and a rollback, sees the same
 * referee. The host draws him from here when the rule is on (native/pcspr.c); with the rule off nothing here runs
 * and nothing of the game changes.
 *
 * What is the PC's and what is not:
 *  - the collision (checkcx): a skater within 10h on the sort axis and 100h squared, closing, adds the impact
 *    (at least 5) to both, plays the check, and the two trade velocity weighted by weight + 8Ch (the cartridge's
 *    checkcx's exchange, the PC's is the same with evalue 0); the mover keeps its old place (checkcoll's
 *    collflag). The referee's weight is the PC's 7 (findpc). Not the PC's: impactp is not set to him (the
 *    cartridge's code reads impactp as a sort object of its own 16), and checkint / holdcheck / Bcheck / fights do
 *    not take him (in the PC a hold or a hook can catch him; here only body checks do).
 *  - the check (checkcheck): a bursting skater that hits him with an impact of 14h or more takes the check
 *    animation, and the cartridge's CCStart odds decide whether the hit lands (the PC's own odds are its own
 *    checkcheck's; the cartridge's are kept so a hit on him lands as a hit on a skater would). A hit that lands
 *    knocks him down only when the checker is a pad player's (the PC's pflags bit 3); each such hit counts and
 *    every third is a penalty on the checker. The PC calls it 16h, Abuse of Official; the cartridge has no such
 *    penalty, so it is Roughing (PenRoughing 1Ah), the cartridge's nearest (the PC's own penalty list names 9 and
 *    0Dh Roughing too). It goes through AddPenalty, so it is called, shown and said as any other penalty.
 *  - the fall (FallDown, SCnum 10h): SPAreffallback, 48 ticks (10, 28, 10), the crowd up 1F4h (to at most 3E8h),
 *    excitement +0Fh, a check sound. He slides while down.
 *  - the puck (checkpuckcoll_ccx, puckbody): a puck at most 10h high and not carried, within 8 of him on both
 *    axes and 40h squared, bounces off along the gap (no touch: he never has the puck), its height speed cleared;
 *    then 8 frames before he takes it again. A high, fast puck (over 0Ch, the cartridge's fast speed) knocks him
 *    down.
 *  - where and how he skates: NHL 95 PC's own routines (below, referee_follow): assreffollow's spots put on the
 *    cartridge's rink, skateto every 12 ticks (not EvadePlayers: he dodges only out of a fight, assfwatchref), doplayeracc's turn rate (his facing turns a
 *    step of 45 degrees in 5 to 11 ticks, never flipping), playeracc's push and MaxSpeed, dostop / stopna,
 *    updateplayers' glide and checkwallcoll / wallcoll's boards and corners. At a face off he stands 15 off the
 *    dot (assrefatdot) and after the drop waits 24 frames (the drop's frames).
 *
 * Against 95 PC, what is not the PC's (the cartridge's game has needs the PC's does not):
 *  - between plays (RULE_HOLD) he walks to the puck and carries it to the dot on mods/hold.c's walk, a fixed time
 *    from the game's state, not the PC's assrefwhistle / assrefgoalpa / assrefdrop / SkateToSpot steps, round the nets
 *    as their aims take him (referee_way); a puck out of play is not fetched (assrefgoalpa): he goes to the timekeeper's
 *    side for one, turns to facing 2 and points (assrefwhistle: SPArefpointgoa). assrefgoalline is not done.
 *  - a fight (the PC has none): assfwatchref, NHLPA 93's assfwatch for him: he watches it from 50h, never between the two.
 *  - no EvadePlayers in play (95 PC's referee sidesteps skaters closing on him; by Abdul's direction he only gets out of a fight).
 *  - his collisions with skaters and the puck are the cartridge's checkcx / puckbody (above). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "rules.h"
#include "anim_gen.h"
#include "symbols_gen.h"
#include "pccoord.h"

#define REF_WEIGHT 7                                   /* (95 PC's findpc: SortCords[SCref].weight = 7) */
#define REF_FALL 48                                    /* (SPAreffallback: 10 + 28 + 10 ticks) */
#define REF_DROP 24                                    /* (the puck drop's frames before he skates) */
#define PEN_REFEREE 0x1A                               /* (PenRoughing: the cartridge's nearest to 95 PC's 16h, Abuse of Official) */

static struct SortObj ref;                             /* (only the fields below are his: Xpos ... impact, nopuck[0]) */
static struct referee_extra { uint8_t mode, drop, fall, fdir, hits, faller, call, sig, st; uint16_t ct; } rx;   /* mode 0 off the ice, 1 at the dot, 2 in play;
                                                                 * call (RULE_PENCALL): 0 none, 1 skating to the spot, 2 turning to face the call, 3 the
                                                                 * signal (sig, st its frames), 4 made; ct the frames since the whistle */

static int logging(void) { static int l = -1; if (l < 0) l = getenv("REFEREE_LOG") != NULL; return l; }

static int active(void)
{
    return rules[RULE_REFEREE] && g.demoflag && !(g.sflags & HI(sfhor)) && !(g.gmode2 & 1) && !(g.gmode & (1 << gmhl));
}

static uint32_t isqrt64(uint64_t v)
{
    uint64_t r = 0, b = (uint64_t)1 << 62;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1;
        b >>= 2;
    }
    return (uint32_t)r;
}

/* he falls (95 PC FallDown for SCnum 10h); dir: the way he faced */
static void fall(void)
{
    if (rx.fall) return;
    rx.fall = REF_FALL;
    rx.fdir = (uint8_t)(ref.facedir & 7);                  /* (SPAreffallback by his facedir) */
    if (g.Crowdlevel <= 0x3E8) {
        g.Crowdlevel = (uint16_t)(g.Crowdlevel + 0x1F4);
        if (g.Crowdlevel > 0x3E8) g.Crowdlevel = 0x3E8;
    }
    g.CwdExciteLvl = (uint16_t)(g.CwdExciteLvl + 0xF);
    newcheck();
    if (logging()) fprintf(stderr, "referee: knocked down at %d,%d (vblank %u)\n", ref.Xpos, ref.Ypos, (unsigned)g.Vcount);
}

/* skater a3 (bursting) checks him (the cartridge's CCStart with 95 PC's checkcheck for SortCords 10h) */
static void checked(struct SortObj *a3, int16_t impact)
{
    if (a3->SPA != SPAburst) return;
    if (g.gmode & (1 << gmclock)) { a3->impact = 0x100; ref.impact = 0x100; }
    uint16_t dir = (uint16_t)((vtoa((int16_t)(ref.Xpos - a3->Xpos), (int16_t)(ref.Ypos - a3->Ypos)) - a3->facedir) & 7);
    if (a3->attribute & HI(3)) dir = (uint16_t)((8 - dir) & 7);
    a3->pflags |= 1 << pfalock;
    SetSPA(a3, rd16(rom_ptr(SYM_checkinglist) + dir * 2));
    if (impact < 0x14) return;
    uint16_t d0 = 0x78;
    if (a3->pflags & (1 << pfjoycon)) d0 <<= 1;
    ref.weight = REF_WEIGHT;
    d0 = CheckWeightOdds(d0, &ref, a3);
    d0 >>= 1;
    wallcollduringcheck(a3, &ref);
    d0 = (uint16_t)(d0 - ref.impact);
    if ((int16_t)d0 > 0 && !(a3->pflags3 & (1 << pf3wall))) {
        uint16_t r = randomd0(d0);
        g.TempWord1 = a3->checking;
        g.TempWord1 >>= 1;
        if ((int8_t)r > (int8_t)g.TempWord1) return;             /* (the hit does not land: no charging on him) */
    }
    if (!(a3->pflags & (1 << pfjoycon))) return;                 /* (95 PC: only a pad player's check knocks him down) */
    if (++rx.hits >= 3) {
        rx.hits = 0;
        if (logging()) fprintf(stderr, "referee: third knock down, penalty on %d%s (vblank %u)\n", a3->SCnum, (g.gmode & (1 << gmclock)) ? " (the clock is stopped: none)" : "", (unsigned)g.Vcount);
        AddPenalty(PEN_REFEREE, a3);
    }
    rx.faller = (uint8_t)a3->SCnum;
    fall();
}

/* collide94.asm:checkcx for a pair with the referee in it: o moves (its x on the sort axis, dy the gap on the other),
 * p is the other */
static void pair(struct SortObj *o, struct SortObj *p, int16_t x, int16_t dy)
{
    struct SortObj *sk = o == &ref ? p : o;
    int16_t dx = (int16_t)(p->Xpos - x);
    if (dx < -0x10 || dx > 0x10) return;
    if ((int32_t)dy * dy + (int32_t)dx * dx > 0x100) return;
    int16_t vx = (int16_t)(o->Xvel - p->Xvel);
    int16_t vy = (int16_t)(o->Yvel - p->Yvel);
    int16_t Dx = (int16_t)(p->Xpos - o->Xpos);
    int16_t Dy = (int16_t)(p->Ypos - o->Ypos);
    int32_t v1n = (int32_t)vy * Dy + (int32_t)vx * Dx;
    if (v1n < 0) return;
    v1n >>= 4;
    uint16_t imp = (uint16_t)v1n >> 8;
    if ((int16_t)imp <= 5) imp = 5;
    o->impact = (int16_t)(o->impact + imp);
    p->impact = (int16_t)(p->impact + imp);
    if ((int16_t)imp >= 0x14 && puck_carrier() == (uint16_t)sk->SCnum) newcheck();
    if (logging()) fprintf(stderr, "referee: %d bumps him at %d,%d impact %d%s%s (vblank %u)\n", sk->SCnum, ref.Xpos, ref.Ypos, (int16_t)imp,
                           sk->SPA == SPAburst ? " bursting" : "", (sk->pflags & (1 << pfjoycon)) ? " pad" : "", (unsigned)g.Vcount);
    checked(sk, (int16_t)imp);
    int16_t v1nw = (int16_t)v1n;
    if ((int16_t)g.collflag < 0) return;
    int16_t v1t = (int16_t)(((int32_t)vx * Dy - (int32_t)vy * Dx) >> 4);
    uint16_t m1 = (uint16_t)(o->weight + 0x8C);
    uint16_t m2 = (uint16_t)(p->weight + 0x8C);
    uint16_t msum = (uint16_t)(m1 + m2);
    int16_t v1n2 = (int16_t)m68k_divs((int32_t)v1nw * (int16_t)m1, (int16_t)msum);
    int16_t v2n = v1n2;
    o->Xvel = (int16_t)((((int32_t)v1t * Dy + (int32_t)v1n2 * Dx) >> 4) + p->Xvel);
    o->Yvel = (int16_t)((((int32_t)v1n2 * Dy - (int32_t)v1t * Dx) >> 4) + p->Yvel);
    p->Xvel = (int16_t)(p->Xvel + (((int32_t)v2n * Dx) >> 4));
    p->Yvel = (int16_t)(p->Yvel + (((int32_t)v2n * Dy) >> 4));
    ST_W(g.collflag);
}

/* checkplcoll's walk takes him too: skater o moved to x, y */
void referee_cx(int16_t x, int16_t y, struct SortObj *o)
{
    if (rx.mode != 2 || !active()) return;
    if (o->pflags2 & (1 << pf2fight)) return;                    /* (a fight is the two fighters': he never comes between them) */
    int16_t dy = (int16_t)(ref.Ypos - y);
    if (dy > 0x10 || dy < -0x10) return;
    ref.weight = REF_WEIGHT;
    pair(o, &ref, x, dy);
}

/* checkpuckcoll's walk takes him too (checkpuckcoll_ccx for SortCords 10h, then puckbody) */
void referee_puck(struct SortObj *puck)
{
    if (rx.mode != 2 || !active()) return;
    if ((int16_t)puck_carrier() >= 0) return;                    /* (a carried puck goes where its carrier takes it) */
    if (g.gmode & (1 << gmclock)) return;                         /* (a dead puck is not played: the PC's referee picks it up, here the walk to the dot,
                                                                     mods/hold.c; bounced off him every 8 frames it went along at his feet) */
    if (ref.nopuck[0]) return;
    int16_t d0 = (int16_t)(ref.Xpos - puck->Xpos), d1 = (int16_t)(ref.Ypos - puck->Ypos);
    if (d0 > 8 || d0 < -8 || d1 > 8 || d1 < -8) return;
    uint32_t dsq = (uint32_t)((int32_t)d0 * d0 + (int32_t)d1 * d1);
    if (dsq > 0x40) return;
    if (puck->Zpos <= 8 && (dsq & 0xF) != 0) return;
    sfx(0x24);                                                    /* SFXpuckbody */
    puck->Zvel = 0;
    ref.nopuck[0] = 8;
    d0 = (int16_t)-d0; d1 = (int16_t)-d1;                         /* (the puck from him) */
    if (d1 == 0) d1 = puck->Ypos >= 0 ? 1 : -1;                   /* (GetHot: he has no hot spot here) */
    int16_t vx = puck->Xvel, vy = puck->Yvel;
    puck->Xvel = (int16_t)((puck->Xvel & 0xFF) | ((uint8_t)d0 << 8));
    puck->Yvel = (int16_t)((puck->Yvel & 0xFF) | ((uint8_t)d1 << 8));
    puckflip(d0, puck);
    if (logging()) fprintf(stderr, "referee: the puck bounces off him at %d,%d, now %d,%d (vblank %u)\n", ref.Xpos, ref.Ypos, puck->Xvel, puck->Yvel, (unsigned)g.Vcount);
    if (puck->Zpos > 0xC && (uint32_t)((int32_t)vx * vx + (int32_t)vy * vy) > 0x9000000) fall();
}

static void ref_inside(int32_t *x, int32_t *y);

/* ==== NHL 95 PC's skating, for him: assreffollow (4E0BD) -> skateto (5E93B) -> doplayeracc (5E16D) /
 * noturn0 / playeracc (5EDAD) / dostop (5F745) / stopna (5F82A), then updateplayers' (5C40F) glide and checkwallcoll's boards.
 * The same code moves the host's picture of him (native/pcspr.c, the Visual only referee) on its own SortObj: it reads the game
 * and changes nothing of it (no sound, the game's avdgoal scratch put back). The fields of a SortObj it uses: Xpos/Xfrac, Ypos/Yfrac,
 * Xvel, Yvel (the cartridge's units: x += Xvel x 11h a frame), facedir with _pad56 its fraction (the PC's facedir is the high word
 * of a 16.16, pad_34), temp2 (skateto's countdown in the high byte, its kept direction in the low: TEMP2_TICKS / TEMP2_DIR),
 * temp3 / temp4 (assreffollow's spot; temp4 is kept: beside the play he keeps his y until the view is 32h off it), impact; SPA /
 * SPAcnt are his picture's (REF_GLIDE ... and the frames in it), not the game's.
 *
 * The rink: the PC's is 0A0h across and 108h long to the boards with its goal line at 0E8h, the cartridge's 88h, 12Ah and 108h. The
 * PC's spots and zones are put on the cartridge's rink by those: x times 88h / 0A0h, y times 108h / 0E8h to the goal line and
 * beyond it the end's 22h / 20h. His speeds are the PC's (MaxSpeed, dirtab, the stop step), in the cartridge's units (the PC's
 * updateplayers moves Xvel x 10h a tick, the cartridge's Xvel x 11h a frame: PC = cartridge x 11h / 10h). */
#define REF_LEGSTR 0x0F                                 /* (findpc: the referee's legstr = legspd = 0Fh) */
#define REF_MAX 0x0434A900u                             /* (MaxSpeed[0Fh]: playeracc's fixed index for him) */
#define REF_MAX_STOP 0x03B19000u                        /* (MaxSpeed[0Ch]: the clock stopped and he is 74h (PC) or more from centre) */
#define PCX(v) pc_x(v)                                  /* (a PC x on the cartridge's rink: mods/pccoord.h) */
#define PCY(v) pc_y(v)
static const int16_t pc_dirtab[8][2] = { { 0, 0xC8 }, { 0x8D, 0x8D }, { 0xC8, 0 }, { 0x8D, -0x8D }, { 0, -0xC8 }, { -0x8D, -0x8D }, { -0xC8, 0 }, { -0x8D, 0x8D } };
static const int8_t pc_ftab[8] = { 0, 16, 16, 16, 0, -16, -16, -16 };   /* (doplayeracc_ftab) */

static int kfr(void) { return g.PalMode ? 0x16 : 0x11; }
static int32_t pcv(int32_t v) { return v * kfr() / 16; }                 /* (a cartridge speed in the PC's units) */
static int32_t cartv(int32_t a) { return a * 16 / kfr(); }               /* (a PC speed step in the cartridge's) */
static int iabs(int v) { return v < 0 ? -v : v; }

/* SetSPA for his picture: a new one starts from its first frame */
static void ref_anim(struct SortObj *o, int a) { if (o->SPA != a) { o->SPA = (uint16_t)a; o->SPAcnt = 0; } }
/* pf2aip: a turn (SPArefturnl / r: one 8 tick frame) or the stop (SPArefstop: loops while he stops) is under way */
static int ref_aip(const struct SortObj *o)
{
    if (o->SPA == REF_TURNL || o->SPA == REF_TURNR) return o->SPAcnt < 8;
    return o->SPA == REF_STOP && (o->Xvel | o->Yvel);
}

/* checkwallcoll's boards (src/collide.c) at x, y for a skater's radii (8, 4): the side boards at 80h, the ends at 126h, the corners
 * a circle of 40h about (+-40h, +-0E6h). 1 and wallcoll's c, s (the boards' normal, 100h long) when he is past them */
static int ref_walls(int16_t x, int16_t y, int16_t *c, int16_t *s)
{
    const int16_t d4 = 0x88 - 8, d5 = 0x12A - 4, cx = (int16_t)(d4 - 0x40), cy = (int16_t)(d5 - 0x40);
    if ((y <= -cy || y >= cy) && (x < -cx || x > cx)) {
        int16_t qx = (int16_t)(x - (x < 0 ? -cx : cx)), qy = (int16_t)(y - (y < 0 ? -cy : cy));
        uint32_t d2 = (uint32_t)((int32_t)qx * qx + (int32_t)qy * qy);
        if (d2 > 0x1000) {
            int32_t dist = (int32_t)isqrt64(d2);
            *c = (int16_t)((int32_t)qy * 256 / dist); *s = (int16_t)(-(int32_t)qx * 256 / dist);
            return 1;
        }
        return 0;
    }
    if (y >= d5) { *c = 0x100; *s = 0; return 1; }
    if (y <= -d5) { *c = -0x100; *s = 0; return 1; }
    if (x >= d4) { *c = 0; *s = -0x100; return 1; }
    if (x <= -d4) { *c = 0; *s = 0x100; return 1; }
    return 0;
}

/* a spot of his kept 8 inside those boards (his spots are the PC's put on this rink; a fight's can be anywhere) */
static void ref_inside(int32_t *x, int32_t *y)
{
    const int32_t lx = 0x80 - 8, ly = 0x126 - 8, cx = 0x40, cy = 0xE6, r = 0x40 - 8;
    if (*x > lx) *x = lx;
    if (*x < -lx) *x = -lx;
    if (*y > ly) *y = ly;
    if (*y < -ly) *y = -ly;
    int32_t ax = *x < 0 ? -*x : *x, ay = *y < 0 ? -*y : *y;
    if (ax > cx && ay > cy) {
        int32_t qx = ax - cx, qy = ay - cy;
        uint32_t d = isqrt64((uint64_t)(qx * qx + qy * qy));
        if ((int32_t)d > r) { ax = cx + qx * r / (int32_t)d; ay = cy + qy * r / (int32_t)d; *x = *x < 0 ? -ax : ax; *y = *y < 0 ? -ay : ay; }
    }
}

/* wallcoll (src/collide.c) for him, as for a skater: the speed into the boards is turned back, a quarter of it and at least 3E8h;
 * 2 for a hard hit (the cartridge's SFXplayerwall), 1 a hit, 0 none */
static int ref_bounce(struct SortObj *o, int16_t c, int16_t s)
{
    int16_t v1n = (int16_t)-(int16_t)(((int32_t)c * o->Yvel - (int32_t)s * o->Xvel) >> 8);
    int16_t v1t = (int16_t)(((int32_t)c * o->Xvel + (int32_t)s * o->Yvel) >> 8);
    if (v1n > 0x3E8) return 0;
    int hard = v1n <= -0x1000 && o->impact >= 0xA;
    v1n = (int16_t)(v1n >> 2);
    if (v1n >= -0x384) v1n = -0x3E8;
    o->Xvel = (int16_t)(((int32_t)c * v1t - (int32_t)s * v1n) >> 8);
    o->Yvel = (int16_t)(((int32_t)s * v1t + (int32_t)c * v1n) >> 8);
    return hard ? 2 : 1;
}

/* playeracc (5EDAD) for SCnum 10h: dirtab's step times (legstr + 30h) / 64, none into the boards he is against (Wallsin / Wallcos),
 * times 20h - weight / 8 + legstr over 32; the new speed is kept only while under MaxSpeed (his fixed 0Fh, 0Ch with the clock
 * stopped 74h from centre). No energy: the referee does not tire */
static void ref_acc(struct SortObj *o, int dir)
{
    int32_t xa = pc_dirtab[dir & 7][0] * (REF_LEGSTR + 0x30) / 64, ya = pc_dirtab[dir & 7][1] * (REF_LEGSTR + 0x30) / 64;
    int16_t c, s;
    if (ref_walls(o->Xpos, o->Ypos, &c, &s)) {
        if (s && (xa ^ s) < 0) xa = 0;
        if (c && (ya ^ c) >= 0) ya = 0;
    }
    int32_t f = 0x20 - (REF_WEIGHT >> 3) + REF_LEGSTR;
    int32_t nx = o->Xvel + cartv(xa * f >> 5), ny = o->Yvel + cartv(ya * f >> 5);
    int64_t sx = pcv(nx), sy = pcv(ny);
    uint32_t mx = (g.gmode & (1 << gmclock)) && iabs(o->Ypos) >= PCY(0x74) ? REF_MAX_STOP : REF_MAX;
    if ((uint64_t)(sx * sx + sy * sy) <= mx) { o->Xvel = (int16_t)nx; o->Yvel = (int16_t)ny; }
}

/* dostop (5F745): over 1000h (PC) on an axis, SPArefstop; stopna (5F82A): a step of 200 + legstr off each axis, not past 0 */
static void ref_stop(struct SortObj *o)
{
    if (iabs(pcv(o->Xvel)) > 0x1000 || iabs(pcv(o->Yvel)) > 0x1000) ref_anim(o, REF_STOP);
    int16_t st = (int16_t)cartv(200 + REF_LEGSTR);
    if (o->Xvel < 0) { o->Xvel = (int16_t)(o->Xvel + st); if (o->Xvel > 0) o->Xvel = 0; } else { o->Xvel = (int16_t)(o->Xvel - st); if (o->Xvel < 0) o->Xvel = 0; }
    if (o->Yvel < 0) { o->Yvel = (int16_t)(o->Yvel + st); if (o->Yvel > 0) o->Yvel = 0; } else { o->Yvel = (int16_t)(o->Yvel - st); if (o->Yvel < 0) o->Yvel = 0; }
}

/* doplayeracc (5E16D) for SCnum 10h (he never skates backwards: pfrev is not his). dir 8: glide; 9: stop when moving. Facing dir
 * (doplayeracc_ftab 0): noturn0 skates and accelerates; dir behind him: noturn0 stops when he is still going his way, else turns one
 * step at once and glides. Else he turns: facedir as a 16.16 moves 16 x (300h - speed squared >> 16, at least 180h) a tick (a step
 * of 45 degrees in about 5 ticks from rest, 11 at his top speed), with SPArefturnl / r above speed 14h, and accelerates the way he
 * faces above speed 2 */
static void ref_doacc(struct SortObj *o, int dir)
{
    dir &= 0xF;
    if (dir > 7) {
        if (dir == 9 && (o->Xvel | o->Yvel)) { ref_stop(o); return; }
        if (!ref_aip(o)) ref_anim(o, REF_GLIDE);
        return;
    }
    int t = (dir - o->facedir) & 7;
    if (!pc_ftab[t]) {                                            /* noturn0 */
        if (t) {
            uint16_t a = vtoa(o->Xvel, o->Yvel);
            if (!(a & 8) && ((a - o->facedir + 2) & 7) < 4) { ref_stop(o); return; }
            o->facedir = (int16_t)((o->facedir + 1) & 7);
            ref_anim(o, REF_GLIDE);
            return;
        }
        if (!ref_aip(o)) ref_anim(o, REF_SKATE);
        ref_acc(o, o->facedir);
        return;
    }
    int64_t vx = pcv(o->Xvel), vy = pcv(o->Yvel);
    int32_t s2 = (int32_t)((vx * vx + vy * vy) >> 16);
    int32_t r = 0x300 - s2;
    if (r < 0x180) r = 0x180;
    uint32_t f = ((uint32_t)(o->facedir & 7) << 16 | o->_pad56) + (uint32_t)(r * pc_ftab[t]);
    o->facedir = (int16_t)((f >> 16) & 7);
    o->_pad56 = (uint16_t)f;
    ref_anim(o, s2 > 0x14 ? (pc_ftab[t] < 0 ? REF_TURNL : REF_TURNR) : REF_GLIDE);
    if (s2 > 2) ref_acc(o, o->facedir);
}

/* (95 PC's assreffollow skates with EvadePlayers (5E4C4), which turns him aside from any skater or the puck closing within 28h. It is not
 * used here: by Abdul's direction he keeps his line in play, as the cartridge's own skaters do, and only steps out of a fight's way,
 * assfwatchref below) */

/* skateto (5E93B): every 12 ticks (the countdown in temp2's high byte) avdgoal (the PC's: ref_aim, below; it leaves a stopped clock's
 * referee alone on his own side of the goal line), the aim led by his speed's high byte,
 * 9 (close enough) within 0Ch, else vtoa of it (no evade routine: see above), kept in temp2's low byte; no direction and still, a step toward
 * the puck (a fight: xc1 / yc1). Every tick doplayeracc with the kept direction */
static void ref_aim(int x, int y, int tx, int ty, int stopped, int spot, int *ax, int *ay);
static void ref_skate(struct SortObj *o, int16_t x, int16_t y, int stop)
{
    int8_t tk = (int8_t)((uint16_t)o->temp2 >> 8);
    tk = (int8_t)(tk - elapsed);
    o->temp2 = (int16_t)(((uint16_t)(uint8_t)tk << 8) | ((uint16_t)o->temp2 & 0xFF));
    if (tk < 0) {
        o->temp2 = (int16_t)(((uint16_t)(uint8_t)(tk + 0xC) << 8) | ((uint16_t)o->temp2 & 0xFF));
        int16_t ox = x, oy = y;
        { int ax, ay; ref_aim(o->Xpos, o->Ypos, x, y, (g.gmode & (1 << gmclock)) != 0, 0, &ax, &ay); x = (int16_t)ax; y = (int16_t)ay; }
        int px = o->Xpos + (o->Xvel >> 8), py = o->Ypos + (o->Yvel >> 8);
        int a0 = (int16_t)(x - px), a1 = (int16_t)(y - py), dx = iabs((int16_t)(ox - px)), dy = iabs((int16_t)(oy - py));
        int d;
        if (stop || (iabs(a0) <= 0xC && iabs(a1) <= 0xC && (dx <= 0xC || o->Xvel) && (dy <= 0xC || o->Yvel))) d = 9;
        else d = vtoa((int16_t)a0, (int16_t)a1);
        o->temp2 = (int16_t)(((uint16_t)o->temp2 & 0xFF00) | (uint8_t)d);
        if (d > 7 && !(o->Xvel | o->Yvel)) {
            const struct SortObj *pk = sortobj(puckscnum);
            int16_t qx = pk->Xpos, qy = pk->Ypos;
            if (pk->pflags2 & (1 << pf2fight)) { qx = (int16_t)g.xc1; qy = (int16_t)g.yc1; }
            int16_t r = (int16_t)(o->facedir - vtoa((int16_t)(qx - o->Xpos), (int16_t)(qy - o->Ypos)));
            if (r) o->facedir = (int16_t)((o->facedir + ((r & 4) >> 1) - 1) & 7);
        }
    }
    ref_doacc(o, (uint8_t)o->temp2);
}

static void ref_skateto(struct SortObj *o, int16_t x, int16_t y) { ref_skate(o, x, y, 0); }

/* he comes on at x, y: at rest, facing centre ice (StartHL2's facing 2 on the PC's left), skateto's first look at once */
void referee_skate_start(struct SortObj *o, int16_t x, int16_t y)
{
    o->Xpos = x; o->Xfrac = 0; o->Ypos = y; o->Yfrac = 0; o->Xvel = o->Yvel = 0;
    o->facedir = x > 0 ? 6 : 2; o->_pad56 = 0;
    o->temp2 = 9; o->temp3 = x; o->temp4 = y;
    o->temp1 = 0; o->temp5 = 0;                                   /* (not watching a fight) */
    o->position = 1;                                              /* (not a goalie: avdgoal leaves a goalie alone) */
    o->SPA = REF_GLIDE; o->SPAcnt = 0;
}

/* RULE_PENCALL: NHL 95 PC's penalty call. Stop4Pen's whistle stops play and InProgress calls SetPA, which gives the referee assrefsignal
 * (Asstab 20h, goalie/assgoalie.c): he skates to (A0h, 0), the timekeeper's side at the red line (SkateToSpot, until within 0Ch in y
 * and no more than 14h short in x), then every 8 ticks glides (SPArefglide), stops and turns a step toward the call's way (refsigdir:
 * 4, facing the viewer); facing it he makes the signal (refsigspa_m2 by RefPen: SPArefrough ... one picture of 12 ticks, the
 * signal's two or more, back to 847) and goes on to assrefgoalpa, which holds the stance while the penalty's overlay runs. Here the
 * cartridge's penalty screen is that overlay: chkprogress waits for pencall_ready, then cuts to it with no fade (and the face off
 * set up cuts back: hold_nofade). The cartridge's penalty number (a word offset into Penaltylist) is the PC's RefPen times 2 for
 * roughing (9) to holding (12h); its fights are called with roughing's signal and the penalty shot versions with SPArefpenshot. */
static const uint8_t sig_len[REF_SIGS] = { 60, 96, 60, 60, 60, 60, 72, 72, 68 };   /* (frames.asm: the pictures' ticks, the last held) */
static int call_pen(void)                                         /* the first queued penalty with minutes (Penaltylist's byte 1), or -1 */
{
    const uint8_t *t = rom_ptr(SYM_Penaltylist);
    for (const uint8_t *a = g.PenBuf; a < g.PenBuf + sizeof g.PenBuf - 1 && (a[0] | a[1]); a += 2) {
        uint16_t pen = a[0];
        if ((int8_t)(t + rd16(t + pen))[penmins] > 0) return pen;
    }
    return -1;
}
static int sig_of(int pen)
{
    switch (pen >> 1) {
    case 0x09: case 0x0D: case 0x13: case 0x14: return REF_SIG_ROUGH;      /* (roughing, both; fighting: the PC has no fight) */
    case 0x0B: return REF_SIG_CHARGE;
    case 0x0C: return REF_SIG_SLASH;
    case 0x0E: return REF_SIG_XCHECK;
    case 0x0F: return REF_SIG_HOOK;
    case 0x10: return REF_SIG_TRIP;
    case 0x11: return REF_SIG_INTERFER;
    case 0x12: return REF_SIG_HOLD;
    default: return pen >= 0x2E ? REF_SIG_PENSHOT : REF_SIG_ROUGH;
    }
}
static int call_on(void)
{
    return RULE(RULE_PENCALL) && (g.gmode & (1 << gmpen)) && (int16_t)g.PenCntDwn >= 0 && call_pen() >= 0;
}
static void assrefsignal(struct SortObj *o)
{
    if (!rx.call) { rx.call = 1; rx.sig = (uint8_t)sig_of(call_pen()); rx.st = 0; rx.ct = 0; o->temp1 = 0; o->temp5 = 0; }
    if (rx.ct < 0xFFFF) rx.ct = (uint16_t)(rx.ct + elapsed);
    int32_t tx = PCX(0xA0), ty = 0;
    ref_inside(&tx, &ty);
    g.sflags |= HI(sfslock);                                       /* (95 PC's checkwindow while RefStep is set: the view follows him) */
    g.yc1 = (uint16_t)o->Ypos; g.xc1 = (uint16_t)o->Xpos; g.yleader = 0;
    if (rx.call == 1) {
        if (iabs(o->Ypos - (int16_t)ty) > 0xC || o->Xpos < tx - 0x14) { ref_skateto(o, (int16_t)tx, (int16_t)ty); return; }
        rx.call = 2; o->temp1 = 0;
    }
    if (rx.call == 2) {
        if (o->Xvel | o->Yvel) { ref_skate(o, o->Xpos, o->Ypos, 1); return; }   /* (SkateToSpot's stop there; then the turn alone) */
        o->temp1 = (int16_t)(o->temp1 - elapsed);
        if (o->temp1 >= 0) return;
        o->temp1 = (int16_t)(o->temp1 + 8);
        if (o->Xvel | o->Yvel) return;
        int r = (4 - o->facedir) & 7;
        if (r) { o->facedir = (int16_t)((o->facedir + (r < 5 ? 1 : -1)) & 7); o->_pad56 = 0; ref_anim(o, REF_GLIDE); return; }
        rx.call = 3; rx.st = 0;
        ref_anim(o, REF_SIGNAL);
        return;
    }
    o->Xvel = o->Yvel = 0;
    if (rx.call == 3) {
        rx.st = (uint8_t)(rx.st + elapsed > 0xFF ? 0xFF : rx.st + elapsed);
        if (rx.st >= sig_len[rx.sig]) rx.call = 4;
    }
}

/* chkprogress: the cartridge's way (0), the call still being made (-1), or made: cut to the penalty screen (1). Not after 600 frames
 * (a referee who cannot get there: the call is made where he is) */
int pencall_ready(void)
{
    if (!RULE(RULE_PENCALL) || !active() || call_pen() < 0) return 0;
    if (rx.call != 4 && rx.ct < 600) return -1;
    rx.call = 4;                                                  /* (made: kept through the penalty screen, for the cut back, hold_nofade) */
    return 1;
}
int referee_call_made(void) { return rx.call == 4; }

/* assfwatchref: a fight (mods/fight.c; 95 PC has none) is the two fighters'; he gets out of it and watches, as every other skater does
 * with NHLPA 93's assfwatch (logic93.asm): on taking it (temp5 set) and then every 60 frames (temp1 the countdown) a spot 50h from the
 * fight's centre (xc1, yc1) on the line from it to him, x at most 7Eh across, here kept 8 inside the boards and their corners
 * (ref_inside; where that leaves him within 48h of it, along the boards to 50h off); on the centre itself, toward the nearer side
 * boards. He skates there with 95 PC's skating (skateto with no evade
 * routine, as assfwatch's skateto with rtss); once 48h clear of the centre (temp5 2) he stops and faces the fight, and goes again
 * if it comes within 40h of him. Back to assreffollow when the fight is over */
static void assfwatchref(struct SortObj *o)
{
    int32_t cx = o->Xpos - (int16_t)g.xc1, cy = o->Ypos - (int16_t)g.yc1, r2 = cx * cx + cy * cy;
    if (!o->temp5 || (o->temp5 == 2 && r2 < 0x40 * 0x40)) { o->temp5 = 1; o->temp1 = 0; }   /* (new, or the fight come to him: a spot at once) */
    if (o->temp5 == 1 && r2 >= 0x48 * 0x48) {                    /* (clear of it: he stops there and watches; not assfwatch's, whose */
        o->temp5 = 2;                                             /*  skater keeps skating to his spot, and with 95 PC's turning would */
        o->temp2 = (int16_t)((uint16_t)o->temp2 & 0xFF);          /*  circle it at speed) */
    }
    if (o->temp5 == 2) { ref_skate(o, o->Xpos, o->Ypos, 1); return; }
    o->temp1 = (int16_t)(o->temp1 - elapsed);
    if (o->temp1 < 0) {
        o->temp1 = (int16_t)(o->temp1 + 0x3C);
        int32_t fx = (int16_t)g.xc1, fy = (int16_t)g.yc1, dx = o->Xpos - fx, dy = o->Ypos - fy;
        if (!dx && !dy) dx = fx >= 0 ? 1 : -1;                    /* (on the centre: toward the nearer side boards) */
        int32_t d = (int32_t)isqrt64((uint64_t)(dx * dx + dy * dy));
        if (!d) d = 1;
        int32_t x = dx * 0x50 / d + fx, y = dy * 0x50 / d + fy;
        if (x > 0x7E) x = 0x7E;
        if (x < -0x7E) x = -0x7E;
        ref_inside(&x, &y);
        int32_t gx = x - fx, gy = y - fy;
        if (gx * gx + gy * gy < 0x48 * 0x48) {
            /* (not assfwatch's: a fight by the boards or in a corner, where the line from it to him meets the boards inside 50h, would
             * leave him in it. Along the boards instead, still 50h off: up or down the ice his side of it, else the other way) */
            int sg = dy ? (dy < 0 ? -1 : 1) : (fy > 0 ? -1 : 1);
            for (int k = 0; k < 2; k++, sg = -sg) {
                int32_t h2 = 0x50 * 0x50 - gx * gx, h = h2 > 0 ? (int32_t)isqrt64((uint64_t)h2) : 0;
                int32_t ty = fy + sg * h, tx = x;
                ref_inside(&tx, &ty);
                if ((tx - fx) * (tx - fx) + (ty - fy) * (ty - fy) >= 0x48 * 0x48) { x = tx; y = ty; break; }
            }
        }
        o->temp3 = (int16_t)x; o->temp4 = (int16_t)y;
    }
    ref_skateto(o, o->temp3, o->temp4);
}

/* assreffollow (4E0BD): the spot by the puck's zone, then skateto there. The puck by the net (|x| under 32h, |y| from 0ACh to the goal
 * line): beside the net behind the goal line (46h, 0EDh); deep in an end (|y| over 62h) with the puck loose or its carrier attacking
 * that end: by the corner on the goal line (78h, 0E8h); else beside the play by the boards (90h), keeping his y till the view is
 * 32h off it. On his own side of the rink always. A fight: assfwatchref. The clock run out: he glides (doplayeracc 8) */
void referee_follow(struct SortObj *o)
{
    const struct SortObj *pk = sortobj(puckscnum);
    int16_t px = pk->Xpos, py = pk->Ypos, cy = (int16_t)g.Vpos;
    int ax = iabs(px), ay = iabs(py), side = o->Xpos > 0;
    if (!g.gameclock) { ref_doacc(o, 8); return; }
    if (pk->pflags2 & (1 << pf2fight)) { assfwatchref(o); return; }
    o->temp5 = 0;
    if (ax < PCX(0x32) && ay > PCY(0xAC) && ay < PCY(0xE8)) { o->temp3 = (int16_t)(side ? PCX(0x46) : -PCX(0x46)); o->temp4 = (int16_t)(py > 0 ? PCY(0xED) : -PCY(0xED)); }
    else if (ay > PCY(0x62) && ((int16_t)puck_carrier() < 0 || (((sortobj((int16_t)puck_carrier())->pflags & (1 << pfgoal)) != 0) ^ (py < 0)))) {
        o->temp3 = (int16_t)(side ? PCX(0x78) : -PCX(0x78)); o->temp4 = (int16_t)(py > 0 ? PCY(0xE8) : -PCY(0xE8));
    } else {
        o->temp3 = (int16_t)(side ? PCX(0x90) : -PCX(0x90));
        if (iabs(o->Ypos - cy) > PCY(0x32)) o->temp4 = cy;
    }
    int32_t tx = o->temp3, ty = o->temp4;
    ref_inside(&tx, &ty);
    ref_skateto(o, (int16_t)tx, (int16_t)ty);
}

/* assreffollow's spot (above) for a referee on that side of the play as it is now, beside the play keeping the view's y: where the
 * picture's own referee (the Visual only one, which the game does not know) is taken to be at the whistle, for his walk's time */
void referee_follow_spot(int side, int *x, int *y)
{
    const struct SortObj *pk = sortobj(puckscnum);
    int16_t px = pk->Xpos, py = pk->Ypos;
    int ax = iabs(px), ay = iabs(py);
    int32_t tx, ty;
    if (ax < PCX(0x32) && ay > PCY(0xAC) && ay < PCY(0xE8)) { tx = side ? PCX(0x46) : -PCX(0x46); ty = py > 0 ? PCY(0xED) : -PCY(0xED); }
    else if (ay > PCY(0x62) && ((int16_t)puck_carrier() < 0 || (((sortobj((int16_t)puck_carrier())->pflags & (1 << pfgoal)) != 0) ^ (py < 0)))) {
        tx = side ? PCX(0x78) : -PCX(0x78); ty = py > 0 ? PCY(0xE8) : -PCY(0xE8);
    } else { tx = side ? PCX(0x90) : -PCX(0x90); ty = (int16_t)g.Vpos; }
    ref_inside(&tx, &ty);
    *x = (int)tx; *y = (int)ty;
}

/* the game's referee as the whistle left him (RULE_REFEREE): place, speed (the cartridge's units), facing */
int referee_walk_from(int *x, int *y, int *vx, int *vy, int *dir)
{
    if (!rules[RULE_REFEREE] || !rx.mode) return 0;
    *x = ref.Xpos; *y = ref.Ypos; *vx = ref.Xvel; *vy = ref.Yvel; *dir = ref.facedir & 7;
    return 1;
}

/* his skating's rates in the cartridge's units: ref_acc's step along a straight way (dirtab's C8h), MaxSpeed's (0Fh, or 0Ch with the
 * clock stopped 74h from centre: far), ref_stop's step */
void referee_rates(int far, int *acc, int *top, int *brake)
{
    int32_t a = 0xC8 * (REF_LEGSTR + 0x30) / 64;
    *acc = (int)cartv(a * (0x20 - (REF_WEIGHT >> 3) + REF_LEGSTR) >> 5);
    *top = (int)cartv((int32_t)isqrt64(far ? REF_MAX_STOP : REF_MAX));
    *brake = (int)cartv(200 + REF_LEGSTR);
}

/* ==== The nets. NHL 95 PC keeps its referee out of them two ways, as it does its skaters:
 *  - avdgoal (5F151, PC's own: skateto calls it every 12 ticks, so it steers him in play and between plays): when the line from
 *    him to where he skates to crosses a net's box (y E8h - 23h to FCh, x within 2Ch of the middle), the aim is moved beside a
 *    post (x 2Ch + 0Ah) at the goal mouth (y C4h) or behind the net (FDh), by a 13 way switch on which sides of the box he and
 *    the aim are; with the clock stopped it leaves him alone when the aim is in front of the goal line on his own side.
 *    Between plays SkateToSpot (4E292) adds two of its own: behind the net (|x| 1Eh or less, |y| F2h or more) to an aim in
 *    front, he aims first for behind the post on the aim's side (28h, FCh); beside it at the goal line, for 28h, DCh.
 *  - the net's frame (checkwallcoll -> checkgoal -> checkgoalp, which takes SCnum 10h, without CheckBump): inside the oval about
 *    the net (32 across, 11 deep: dx^2 / 4 + 2 dy^2 up to 256) he bounces off it as off the boards (wallcoll with nethit: a
 *    player at the side posts or past the net's back keeps his old place on that axis).
 * The cartridge has the same oval for its skaters (src/collide.c checkgoalp: dx^2 / 1024 + dy^2 / 121 up to 1, about its nets
 * at 0, 10Ch), but not avdgoal's box: its avdgoal (src/logic.c) only nudges the aim off the goal line. Here the PC's avdgoal and
 * SkateToSpot's rules on the PC's rink (mods/pccoord.h), and the cartridge's oval about its nets where they are */
static int avd_box(int px, int py, int tx, int ty, int x1, int x2, int y, int *ps, int *pt)   /* (avdgoal_box: x1, x2 are y limits, y the x half width) */
{
    *ps = *pt = 0;
    if (x1 > py) { *ps = 4; if (ty < x1) return 0; } else if (ty < x1) *pt = 4;
    if (x2 > py) { if (ty >= x2) *pt = 1; } else { *ps = 1; if (ty >= x2) return 0; }
    if (y > px) { if (y <= tx) *pt |= 2; } else { *ps |= 2; if (y <= tx) return 0; }
    if (-y <= px) { if (-y > tx) *pt |= 8; } else { *ps |= 8; if (-y > tx) return 0; }
    return 1;
}

/* the goalie in the net at that end (PC units; 0: none) */
static int net_goalie(int top, int *gx, int *gy)
{
    const struct SortObj *o = sortobj(0);
    for (int i = 0; i < 12; i++, o++)
        if (o->position == 0 && o->frame && (o->Ypos > 0) == top) { *gx = cart_pc_x(o->Xpos); *gy = cart_pc_y(o->Ypos); return 1; }
    return 0;
}

/* avdgoal for him (PC units): p his place, *tx / *ty the aim, changed in place. stopped: the clock is stopped */
static void pc_avdgoal(int px, int py, int *ox, int *oy, int stopped)
{
    int ps, pt, gl, yl, yb, gk, gx = 0, gy = 0, tx = *ox, ty = *oy;
    const int m = 0xA;                                  /* (not a goalie, not assignment 27h: the wide box) */
    if (stopped && (ty < 0 ? -ty : ty) < 0xE8 && (ty ^ py) >= 0) return;
    if (avd_box(px, py, tx, ty, 0xE8 - 0x23, 0xFC, 0x2C, &ps, &pt)) {
        yb = 0xFD; yl = 0xE8 - 0x24; gl = 0xE8;
        py = -py; ty = -ty;
        gk = net_goalie(1, &gx, &gy);
        if (ps & 5) ps ^= 5;
        if (pt & 5) pt ^= 5;
    } else {
        if (!avd_box(px, py, tx, ty, -0xFC, 0x23 - 0xE8, 0x2C, &ps, &pt)) return;
        yb = -0xFD; yl = 0x24 - 0xE8; gl = -0xE8;
        gk = net_goalie(0, &gx, &gy);
    }
    int apx = px < 0 ? -px : px;
    if (pt == 0) {
        if (py >= -0xE8) {
            if (ty < -0xE8) {
                *oy = yl;
                if (gk && ((tx - gx) ^ (px - tx)) < 0) *ox = px < gx ? -0x2C - m : m + 0x2C;
                else {
                    if (apx >= m + 0x2C) return;
                    *ox = px < 0 ? -0x2C - m : m + 0x2C;
                }
                if (apx > 0x26) *oy = yb;
                return;
            }
            if (!gk) return;
            if (px < tx) { if (px >= gx) return; } else if (px < gy) return;   /* (the PC's own: its goalie's y here) */
            *oy = yl;
            return;
        }
        if (ty < -0xE8) return;
        if (px < -0x2C - m + 1) *ox = -0x2C - m;
        else if (px >= m + 0x2C) *ox = 0x2C + m;
        else {
            *oy = yb;
            if (tx >= 0) { if (m + 0x2C > tx) *ox = 0x2C + m; } else if (-0x2C - m + 1 <= tx) *ox = -0x2C - m;
        }
        if (apx > 0x26) *oy = yl;
        return;
    }
    switch (ps) {
    case 0:
        if (py < -0xE8) {
            if (px < -0x2C - m + 1) *ox = -0x2C - m;
            else if (px >= m + 0x2C) *ox = m + 0x2C;
            else {
                *oy = yb;
                if (tx >= 0) { if (m + 0x2C > tx) *ox = m + 0x2C; } else if (-0x2C - m + 1 <= tx) *ox = -0x2C - m;
            }
            return;
        }
        if (ty >= -0xE8) {
            if (gk && ((tx - gx) ^ (px - tx)) < 0) *ox = px < gx ? -0x2C - m : m + 0x2C;
            else if (apx < m + 0x2C) *ox = px < 0 ? -0x2C - m : m + 0x2C;
            if (apx > 0x26) *oy = yb;
            return;
        }
        *oy = gl;
        if (gk) *ox = px < gy ? -0x2C - m : m + 0x2C;    /* (the PC's own: its goalie's y here) */
        else if (tx >= 0) { if (m + 0x2C > tx) *ox = m + 0x2C; } else if (-0x2C - m + 1 <= tx) *ox = -0x2C - m;
        return;
    case 1:
        *oy = yl;
        if (pt & 4) return;
        if (tx >= 0) { if (tx < m + 0x2C) *ox = m + 0x2C; } else if (tx >= -0x2C - m + 1) *ox = -0x2C - m;
        return;
    case 2: *ox = m + 0x2C; if (pt & 5) return; *oy = yl; return;
    case 3: if (pt == 8) *oy = yl; else *ox = m + 0x2C; return;
    case 4:
        *oy = yb;
        if (!(pt & 1)) return;
        if (tx >= 0) { if (tx < m + 0x2C) *ox = m + 0x2C; } else if (tx >= -0x2C - m + 1) *ox = -0x2C - m;
        return;
    case 6: if (pt != 8) *ox = m + 0x2C; else *oy = yb; return;
    case 8: *ox = -0x2C - m; if (!(pt & 5)) return; *oy = yl; return;
    case 9: if (pt == 2) *oy = yl; else *ox = -0x2C - m; return;
    case 12: if (pt == 2) *oy = yb; return;
    }
}

/* where he aims from x, y for the spot tx, ty (the cartridge's units): avdgoal, and between plays (spot 1) SkateToSpot's own two */
static void ref_aim(int x, int y, int tx, int ty, int stopped, int spot, int *ax, int *ay)
{
    int px = cart_pc_x(x), py = cart_pc_y(y), qx = cart_pc_x(tx), qy = cart_pc_y(ty), ax2 = qx, ay2 = qy;
    int apx = px < 0 ? -px : px, apy = py < 0 ? -py : py, aqy = qy < 0 ? -qy : qy;
    if (spot && apx <= 0x1E && apy >= 0xF2 && aqy < 0xF2) { ay2 = py < 0 ? -0xFC : 0xFC; ax2 = qx < 0 ? -0x28 : 0x28; }
    else if (spot && apx >= 0x10 && apx <= 0x1E && apy >= 0xE8 && (py ^ qy) >= 0 && aqy < 0xE8 && aqy > 0xE0 && (qx < 0 ? -qx : qx) < 0x14) {
        ay2 = py < 0 ? -0xDC : 0xDC; ax2 = px < 0 ? -0x28 : 0x28;
    }
    pc_avdgoal(px, py, &ax2, &ay2, stopped);
    if (ax2 == qx && ay2 == qy) { *ax = tx; *ay = ty; return; }   /* (unchanged: the spot itself, not its round trip) */
    *ax = pc_x(ax2); *ay = pc_y(ay2);
}

/* does the straight way from a to b (the cartridge's units) cross a net's box (avdgoal's: PC x within 2Ch, y E8h - 23h to FCh)? the
 * net's end (1 top, -1 bottom) or 0. Every 2 pixels */
static int way_box(int ax, int ay, int bx, int by, int frame)
{
    int dx = bx - ax, dy = by - ay, d = (int)isqrt64((uint64_t)((int64_t)dx * dx + (int64_t)dy * dy)) / 2 + 1;
    for (int k = 0; k <= d; k++) {
        int x = ax + dx * k / d, y = ay + dy * k / d;
        if (frame) { if (referee_in_net(x, y, NULL, NULL)) return y > 0 ? 1 : -1; continue; }
        int px = cart_pc_x(x), py = cart_pc_y(y), apy = py < 0 ? -py : py;
        if (px > -0x2C && px < 0x2C && apy >= 0xE8 - 0x23 && apy < 0xFC) return py > 0 ? 1 : -1;
    }
    return 0;
}

/* his way between plays from a to b (the clock stopped: SkateToSpot, skateto, avdgoal), the corners of it, b last; at most n; the count.
 *  - behind the net (PC |x| 1Eh or less, |y| F2h or more) to a spot in front: first behind the post on the spot's side (28h, FCh),
 *    SkateToSpot's own aim;
 *  - a way through a net's box goes round it by avdgoal's aims, beside the post (x 2Ch + 0Ah) at the goal mouth (C4h) and behind the
 *    net (FDh), on his side of it (the spot's when he is straight in front or behind). With the clock stopped avdgoal leaves him alone
 *    to a spot in front of the goal line on his own side; then only a way into the net's frame goes round (the PC's checkgoalp would
 *    bounce him off it).
 * Not avdgoal's every case: looked at as he skates (12 ticks), its aim straight in front of the net for a spot behind it is the goal
 * mouth, where he stops: the PC's referee waits there until assrefgoalpa's 258h ticks give him the puck. Here he goes round */
int referee_way(int ax, int ay, int bx, int by, int *wx, int *wy, int n)
{
    int k = 0, x = ax, y = ay, side = 0;
    int px = cart_pc_x(x), py = cart_pc_y(y), qx = cart_pc_x(bx), qy = cart_pc_y(by);
    int apx = px < 0 ? -px : px, apy = py < 0 ? -py : py, aqy = qy < 0 ? -qy : qy;
    if (apx <= 0x1E && apy >= 0xF2 && aqy < 0xF2 && k < n - 1) {
        x = wx[k] = pc_x(qx < 0 ? -0x28 : 0x28); y = wy[k] = pc_y(py < 0 ? -0xFC : 0xFC); k++;
        side = qx < 0 ? -1 : 1;
    }
    while (k < n - 1) {
        int e = way_box(x, y, bx, by, 0);
        if (!e) break;
        int sy = cart_pc_y(y) * e, sty = cart_pc_y(by) * e;   /* (his and the spot's PC y toward that net's end) */
        if (sy < 0xE8 && sty < 0xE8 && (y ^ (e > 0 ? 1 : -1)) >= 0 && (by ^ (e > 0 ? 1 : -1)) >= 0 && !way_box(x, y, bx, by, 1)) break;   /* (the stopped clock's leave) */
        if (!side) { int sx = cart_pc_x(x); side = sx > 4 ? 1 : sx < -4 ? -1 : bx < 0 ? -1 : 1; }
        int cx = pc_x(side * (0x2C + 0xA)), lx = cart_pc_x(x) * side, front = pc_y(e * (0xE8 - 0x24)), back = pc_y(e * 0xFD);
        int ty;
        if (lx >= 0x2C + 0xA - 2) ty = sty >= 0xEC ? back : front;   /* (beside the net: along it to the spot's side of it, the net's
                                                         * middle at ECh: the cartridge's 10Ch) */
        else ty = sy < 0xEC ? front : back;             /* (in front of it or behind it: to the post's corner on that side) */
        if (cx == x && ty == y) break;
        x = wx[k] = cx; y = wy[k] = ty; k++;
    }
    wx[k] = bx; wy[k] = by;
    return k + 1;
}

/* in a net's frame at x, y (the cartridge's oval, src/collide.c checkgoalp, about the net's place): 1 and the way out (wallcoll's
 * c, s, 100h long) */
int referee_in_net(int x, int y, int16_t *c, int16_t *s)
{
    for (int i = 12; i <= 13; i++) {
        const struct SortObj *net = sortobj(i);
        int dx = x - net->Xpos, dy = y - net->Ypos;
        if (dx < -32 || dx > 32 || dy < -11 || dy > 11) continue;
        if ((uint32_t)(dx * 16) * (uint32_t)(dx * 16) / 0x400 + (uint32_t)(dy * 16) * (uint32_t)(dy * 16) / 0x79 > 0x100) continue;
        if (c) {
            int d = (int)isqrt64((uint64_t)(dx * dx + dy * dy)) + 1;
            *c = (int16_t)(-dy * 256 / d); *s = (int16_t)(dx * 256 / d);
        }
        return 1;
    }
    return 0;
}

/* updateplayers (5C40F) for him: on the ice a 64th of the speed off a frame, the move, the frames of his picture; then checkwallcoll's
 * boards. 2: he hit them hard (the game's referee plays SFXplayerwall, the picture's does not) */
int referee_glide(struct SortObj *o)
{
    if (o->Xvel) { int16_t d = (int16_t)(o->Xvel >> 6); if (!d) d = 1; o->Xvel = (int16_t)(o->Xvel - d); }
    if (o->Yvel) { int16_t d = (int16_t)(o->Yvel >> 6); if (!d) d = 1; o->Yvel = (int16_t)(o->Yvel - d); }
    int16_t lim = (int16_t)(4 * 65536 / kfr());                   /* (at most 4 a frame: a skater's bump can be hard) */
    if (o->Xvel > lim) o->Xvel = lim;
    if (o->Xvel < -lim) o->Xvel = (int16_t)-lim;
    if (o->Yvel > lim) o->Yvel = lim;
    if (o->Yvel < -lim) o->Yvel = (int16_t)-lim;
    int32_t d4 = kfr() * elapsed;
    int16_t ox = o->Xpos, oy = o->Ypos;
    uint16_t oxf = o->Xfrac, oyf = o->Yfrac;
    int32_t p = POS32(o->Xpos, o->Xfrac) + (int32_t)o->Xvel * d4; o->Xpos = (int16_t)(p >> 16); o->Xfrac = (uint16_t)p;
    p = POS32(o->Ypos, o->Yfrac) + (int32_t)o->Yvel * d4; o->Ypos = (int16_t)(p >> 16); o->Yfrac = (uint16_t)p;
    if (o->SPAcnt < 0xFFFF) o->SPAcnt = (uint16_t)(o->SPAcnt + elapsed);
    int16_t c, s;
    if (referee_in_net(o->Xpos, o->Ypos, &c, &s)) {     /* (checkgoalp: off the net's frame, wallcoll with nethit: no sound, and at the side posts
                                                         * or past the net's back his old place on that axis) */
        ref_bounce(o, c, s);
        if ((ox <= -PCX(0x18) && o->Xvel > 0) || (ox >= PCX(0x18) && o->Xvel < 0)) { o->Xvel = 0; o->Xpos = ox; o->Xfrac = oxf; }
        int ny = oy < 0 ? -PCY(0xEC) : PCY(0xEC);
        if ((oy > ny + PCY(0xA) && o->Yvel < 0) || (oy < ny - PCY(6) && o->Yvel > 0)) { o->Yvel = 0; o->Ypos = oy; o->Yfrac = oyf; }
        return 1;
    }
    return ref_walls(o->Xpos, o->Ypos, &c, &s) ? ref_bounce(o, c, s) : 0;
}

/* once a pass, after the skaters (updateplayers) */
void referee_update(void)
{
    if (!active()) { rx.mode = 0; if (!g.demoflag) rx.hits = 0; return; }   /* (out of a game: the next counts from 0, as 95 PC's ResetGameVars) */
    const struct SortObj *pk = sortobj(puckscnum);
    int asn = pk->asslist[pk->assnum & 7];
    int16_t fx = (int16_t)g.fox, fy = (int16_t)g.foy;
    if (asn == pfaceoff || asn == pfaceoff2) {                   /* (assrefatdot 4DFF7: 15 off the dot, at rest, facing 2 / 6) */
        struct hold_refpose wp;
        if (hold_skate_ref_pose(0, 0, 0, 0, 0, &wp)) {           /* (RULE_HOLD: he skates there with 95 PC's skating, fetching the puck, mods/hold.c) */
            ref.Xpos = (int16_t)(wp.x >> 8); ref.Xfrac = (uint16_t)((uint32_t)wp.x << 8);
            ref.Ypos = (int16_t)(wp.y >> 8); ref.Yfrac = (uint16_t)((uint32_t)wp.y << 8);
            ref.Xvel = (int16_t)wp.vx; ref.Yvel = (int16_t)wp.vy;
            ref.facedir = (int16_t)wp.dir; ref._pad56 = 0; ref.temp2 = 9;
            ref_anim(&ref, wp.anim); ref.SPAcnt = (uint16_t)wp.at;
        } else if (rx.mode && hold_skate_pending()) {
            /* (RULE_HOLD: the face off is set up a frame before the walk is: he stays where he is that frame, his speed and facing kept
             * for the walk, which goes on from them. Put at the dot he was gone from the view, and the walk then started from the
             * dot: PR #193's walk, and he showed again only as it neared the view) */
            ref_anim(&ref, REF_GLIDE);
        } else {
            ref.Xpos = (int16_t)(fx > 0 ? fx + 15 : fx - 15); ref.Xfrac = 0;
            ref.Ypos = fy; ref.Yfrac = 0;
            ref.Xvel = ref.Yvel = 0;
            ref.facedir = fx > 0 ? 6 : 2; ref._pad56 = 0; ref.temp2 = 9;
            ref_anim(&ref, REF_GLIDE);
        }
        rx.mode = 1; rx.drop = 0; rx.fall = 0;
        return;
    }
    int16_t cy = (int16_t)g.Vpos;
    if (rx.mode == 0) { referee_skate_start(&ref, (int16_t)(pk->Xpos > 0 ? PCX(0x90) : -PCX(0x90)), cy); rx.fall = 0; }
    if (rx.mode == 1) { rx.drop = REF_DROP; ref.temp4 = ref.Ypos; }
    rx.mode = 2;
    ref.SCnum = 0x10;
    ref.weight = REF_WEIGHT;
    ref.position = 1;
    if (ref.nopuck[0]) ref.nopuck[0]--;
    ref.impact = (int16_t)(ref.impact - elapsed * 2);
    if (ref.impact < 0) ref.impact = 0;
    if (rx.fall) rx.fall = (uint8_t)(rx.fall > elapsed ? rx.fall - elapsed : 0);   /* (down: he slides, as a skater does on the ice) */
    else if (rx.drop) { rx.drop--; ref.Xvel = ref.Yvel = 0; }
    else if (call_on()) assrefsignal(&ref);                       /* (RULE_PENCALL: the penalty call, NHL 95 PC's assrefsignal) */
    else referee_follow(&ref);
    if (!call_on()) rx.call = 0;                                    /* (assreffollow: 95 PC's skating, above) */
    int16_t ox = ref.Xpos, oy = ref.Ypos;
    uint16_t oxf = ref.Xfrac, oyf = ref.Yfrac;
    if (referee_glide(&ref) == 2) sfx(0x20);                      /* (SFXplayerwall, as wallcoll plays it for a skater) */
    if (ref.Xpos == ox && ref.Ypos == oy) return;
    uint16_t cf = g.collflag;                                     /* (his own checkcoll: the game's flag as it was after) */
    g.collflag = 0;
    for (int i = 0; i < 12; i++) {                                /* (checkplcoll for SortCords 10h) */
        struct SortObj *q = sortobj(i);
        if (q->position < 0 || (q->pflags & (1 << pfnc)) || (q->pflags2 & ((1 << 5) | (1 << pf2fight)))) continue;
        int16_t gy = (int16_t)(q->Ypos - ref.Ypos);
        if (gy > 0x10 || gy < -0x10) continue;
        pair(&ref, q, ref.Xpos, gy);
    }
    if (g.collflag) { ref.Xpos = ox; ref.Xfrac = oxf; ref.Ypos = oy; ref.Yfrac = oyf; }
    g.collflag = cf;
}

int referee_get(struct referee_view *v)
{
    if (!rules[RULE_REFEREE] || rx.mode == 0) return 0;
    v->x = POS32(ref.Xpos, ref.Xfrac);
    v->y = POS32(ref.Ypos, ref.Yfrac);
    int K = g.PalMode ? 0x16 : 0x11;
    v->vx = ref.Xvel * K;
    v->vy = ref.Yvel * K;
    v->at_dot = rx.mode == 1;
    v->drop = rx.drop;
    v->fall = rx.fall ? REF_FALL - rx.fall : -1;
    v->fall_dir = rx.fdir;
    v->dir = ref.facedir & 7;
    v->anim = ref.SPA;
    v->at = ref.SPA == REF_SIGNAL ? rx.st : ref.SPAcnt;
    v->sig = rx.sig;
    return 1;
}

/* the state's bytes (little-endian, the same on every host) */
size_t referee_state_save(uint8_t *out)
{
    if (out) {
        int16_t w[7] = { ref.Xpos, (int16_t)ref.Xfrac, ref.Ypos, (int16_t)ref.Yfrac, ref.Xvel, ref.Yvel, ref.impact };
        for (int k = 0; k < 7; k++) { out[2 * k] = (uint8_t)w[k]; out[2 * k + 1] = (uint8_t)((uint16_t)w[k] >> 8); }
        uint8_t *b = out + 14;
        b[0] = ref.nopuck[0]; b[1] = rx.mode; b[2] = rx.drop; b[3] = rx.fall; b[4] = rx.fdir; b[5] = rx.hits; b[6] = rx.faller; b[7] = 0;
        b[8] = (uint8_t)(ref.facedir & 7); b[9] = (uint8_t)ref.temp2; b[10] = (uint8_t)((uint16_t)ref.temp2 >> 8);   /* (version 10: his skating) */
        b[11] = (uint8_t)ref._pad56; b[12] = (uint8_t)(ref._pad56 >> 8); b[13] = (uint8_t)ref.temp4; b[14] = (uint8_t)((uint16_t)ref.temp4 >> 8);
        b[15] = (uint8_t)ref.temp5; b[16] = (uint8_t)ref.temp1; b[17] = (uint8_t)ref.temp3; b[18] = (uint8_t)((uint16_t)ref.temp3 >> 8);   /* (version 11: his fight watch) */
        b[19] = rx.call; b[20] = rx.sig; b[21] = rx.st; b[22] = (uint8_t)rx.ct; b[23] = (uint8_t)(rx.ct >> 8);   /* (version 12: his penalty call) */
    }
    return REFEREE_STATE_SIZE;
}

/* a state's n bytes, or (in NULL) none: a state from before the referee, which starts him off the ice. A state from before his
 * skating (version 6 to 9 of the rules' part: REFEREE_STATE_SIZE_9 bytes) starts it afresh where he is; one from before his fight
 * watch (version 10: REFEREE_STATE_SIZE_10) has him not watching one; one from before his penalty call (version 11: REFEREE_STATE_SIZE_11) has him not making one */
int referee_state_load(const uint8_t *in, size_t n)
{
    memset(&ref, 0, sizeof ref);
    memset(&rx, 0, sizeof rx);
    if (!in) return 0;
    if (n != REFEREE_STATE_SIZE && n != REFEREE_STATE_SIZE_11 && n != REFEREE_STATE_SIZE_10 && n != REFEREE_STATE_SIZE_9) return -1;
    if (n >= REFEREE_STATE_SIZE_11 && (in[29] > 2 || in[30] > 0x3C)) return -1;
    if (n == REFEREE_STATE_SIZE && (in[33] > 4 || in[34] >= REF_SIGS)) return -1;
    if (n >= REFEREE_STATE_SIZE_10 && (in[22] > 7 || (in[23] > 9) || (int8_t)in[24] < -0xC || (int8_t)in[24] > 0xC)) return -1;
    if (in[15] > 2 || in[16] > REF_DROP || in[17] > REF_FALL || in[18] > 7 || in[19] > 2 || in[20] > 0xB || in[21]) return -1;   /* (a state is checked before it is used) */
    int16_t w[7];
    for (int k = 0; k < 7; k++) w[k] = (int16_t)(in[2 * k] | in[2 * k + 1] << 8);
    ref.Xpos = w[0]; ref.Xfrac = (uint16_t)w[1]; ref.Ypos = w[2]; ref.Yfrac = (uint16_t)w[3];
    ref.Xvel = w[4]; ref.Yvel = w[5]; ref.impact = w[6];
    ref.nopuck[0] = in[14]; rx.mode = in[15]; rx.drop = in[16]; rx.fall = in[17]; rx.fdir = in[18]; rx.hits = in[19]; rx.faller = in[20];
    int16_t x = ref.Xpos, y = ref.Ypos, vx = ref.Xvel, vy = ref.Yvel;
    referee_skate_start(&ref, x, y);
    ref.Xfrac = (uint16_t)w[1]; ref.Yfrac = (uint16_t)w[3]; ref.Xvel = vx; ref.Yvel = vy;
    if (n >= REFEREE_STATE_SIZE_11) { ref.temp5 = in[29]; ref.temp1 = in[30]; ref.temp3 = (int16_t)(in[31] | in[32] << 8); }
    if (n == REFEREE_STATE_SIZE) { rx.call = in[33]; rx.sig = in[34]; rx.st = in[35]; rx.ct = (uint16_t)(in[36] | in[37] << 8); if (rx.call == 3) ref.SPA = REF_SIGNAL; }
    if (n >= REFEREE_STATE_SIZE_10) {
        ref.facedir = in[22]; ref.temp2 = (int16_t)(in[23] | in[24] << 8);
        ref._pad56 = (uint16_t)(in[25] | in[26] << 8); ref.temp4 = (int16_t)(in[27] | in[28] << 8);
    }
    ref.SCnum = 0x10;
    ref.weight = REF_WEIGHT;
    return 0;
}
