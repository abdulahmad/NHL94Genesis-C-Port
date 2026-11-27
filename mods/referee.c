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
 *    cartridge's rink, skateto every 12 ticks with EvadePlayers, doplayeracc's turn rate (his facing turns a
 *    step of 45 degrees in 5 to 11 ticks, never flipping), playeracc's push and MaxSpeed, dostop / stopna,
 *    updateplayers' glide and checkwallcoll / wallcoll's boards and corners. At a face off he stands 15 off the
 *    dot (assrefatdot) and after the drop waits 24 frames (the drop's frames).
 *
 * Against 95 PC, what is not the PC's (the cartridge's game has needs the PC's does not):
 *  - between plays (RULE_HOLD) he walks to the puck and carries it to the dot on mods/hold.c's walk, a fixed time
 *    from the game's state, not the PC's assrefwhistle / assrefgoalpa / assrefdrop / SkateToSpot steps; a puck out
 *    of play is not fetched (assrefgoalpa), he goes to the timekeeper's side for one (mods/hold.c). The PC's stop
 *    at the timekeeper (assrefwhistle to (0A0h, 0)) and his signals (assrefsignal, assrefgoalline) are not done.
 *  - a fight (the PC has none): he watches it from 50h (NHL 93's assfwatch), never between the two.
 *  - his collisions with skaters and the puck are the cartridge's checkcx / puckbody (above). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "rules.h"
#include "anim_gen.h"
#include "symbols_gen.h"

#define REF_WEIGHT 7                                   /* (95 PC's findpc: SortCords[SCref].weight = 7) */
#define REF_FALL 48                                    /* (SPAreffallback: 10 + 28 + 10 ticks) */
#define REF_DROP 24                                    /* (the puck drop's frames before he skates) */
#define PEN_REFEREE 0x1A                               /* (PenRoughing: the cartridge's nearest to 95 PC's 16h, Abuse of Official) */

static struct SortObj ref;                             /* (only the fields below are his: Xpos ... impact, nopuck[0]) */
static struct referee_extra { uint8_t mode, drop, fall, fdir, hits, faller; } rx;   /* mode 0 off the ice, 1 at the dot, 2 in play */

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

/* A fight (mods/fight.c; not in 95 PC, which has no fights): the players not in it watch from 50h away (NHL 93's assfwatch: a spot
 * 50h from the fight, xc1 / yc1, on the line from it to them, at most 7Eh across). He does the same (where the boards cut that line,
 * along them, 50h off), so he never stands between the two, and the spot is kept inside the rink (ref_inside) */
static void ref_inside(int32_t *x, int32_t *y);
static void fight_spot(const struct SortObj *o, int16_t *tx, int16_t *ty)
{
    int16_t fx = (int16_t)g.xc1, fy = (int16_t)g.yc1;
    int32_t dx = o->Xpos - fx, dy = o->Ypos - fy;
    if (!dx && !dy) dx = fx > 0 ? -1 : 1;                         /* (on the spot: away from the boards) */
    uint32_t d = isqrt64((uint64_t)(dx * dx + dy * dy));
    if (!d) d = 1;
    int32_t x = dx * 0x50 / (int32_t)d + fx, y = dy * 0x50 / (int32_t)d + fy;
    if (x > 0x7E || x < -0x7E) {                                  /* (the boards: along them instead, still 50h off) */
        x = x > 0 ? 0x7E : -0x7E;
        int32_t gx = x - fx, gy2 = 0x50 * 0x50 - gx * gx;
        int32_t gy = gy2 > 0 ? (int32_t)isqrt64((uint64_t)gy2) : 0;
        y = fy + (dy < 0 ? -gy : gy);
    }
    ref_inside(&x, &y);
    *tx = (int16_t)x; *ty = (int16_t)y;
}

/* ==== NHL 95 PC's skating, for him: assreffollow (4E0BD) -> skateto (5E93B) with EvadePlayers (5E4C4) -> doplayeracc (5E16D) /
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
#define PCX(v) ((v) * 0x88 / 0xA0)                      /* (a PC x on the cartridge's rink) */
#define PCY(v) ((v) <= 0xE8 ? (v) * 0x108 / 0xE8 : 0x108 + ((v) - 0xE8) * 0x22 / 0x20)   /* (a PC y, 0 or more) */
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

/* EvadePlayers (5E4C4): the skateto routine of assreffollow. Behind a net (|y| over 0F2h (PC), |x| under 32h): round it. Else the
 * nearest of the skaters and the puck (the puck only while none is within 50h; not the nets, sort objects 12 and 13) whose gaps to
 * him a little ahead (the places plus the high bytes of the speeds) are both within 28h: vtoa of the gap (away from it), and by his
 * place across the rink a sidestep of it (near the side boards up or down the ice, in the middle to the side). Two fighters are not
 * in it (not in 95 PC: he watches a fight from 50h, fight_spot). d: skateto's direction; the result: his */
static int ref_evade(const struct SortObj *p, int d)
{
    int x = p->Xpos, y = p->Ypos;
    if (iabs(y) > PCY(0xF2) && iabs(x) < PCX(0x32)) {
        if (y > 0) d = p->Yvel > 0 ? (x < 0 ? 7 : 1) : (x < 0 ? 6 : 2);
        else d = p->Yvel < 0 ? (x < 0 ? 5 : 3) : (x < 0 ? 6 : 2);
        return d;
    }
    int best = 0x64;
    for (int i = 0; i <= puckscnum; i++) {
        if (i == 12 || i == 13) continue;
        if (i == puckscnum && best <= 0x50) continue;
        const struct SortObj *o = sortobj(i);
        if (i < 12 && (o->pflags2 & (1 << pf2fight))) continue;
        int dx = (int16_t)(x - o->Xpos), dy = (int16_t)(y - o->Ypos);
        int gx = iabs(dx + (p->Xvel >> 8) - (o->Xvel >> 8));
        if (gx > 0x28) continue;
        int gy = iabs((p->Yvel >> 8) - (o->Yvel >> 8) + dy);
        if (gy > 0x28) continue;
        if (gx + gy >= best) continue;
        best = gx + gy;
        d = vtoa((int16_t)dx, (int16_t)dy);
        if (x < -PCX(0x82)) {
            if (d <= 4 || d >= 8) continue;
            d = o->Yvel ? 4 : 0;
        } else if (x > PCX(0x82)) {
            if (d <= 0 || d >= 4) continue;
            d = o->Yvel ? 4 : 0;
        } else if (x > -PCX(0x64) && x < 0) {
            if (d <= 0 || d >= 4) continue;
            d = 8 - d;
            if (d == 6 && o->Yvel) d = dy < 0 ? 5 : 4;
            else d = dy <= 0 ? 0 : 7;
        } else {
            if (x >= PCX(0x64) || x < 0) continue;
            if (d <= 4 || d >= 8) continue;
            d = 8 - d;
            if (d == 2 && o->Yvel) d = dy >= 0 ? 4 : 3;
            else d = dy > 0 ? 1 : 0;
        }
    }
    return d;
}

/* skateto (5E93B): every 12 ticks (the countdown in temp2's high byte) avdgoal (the game's own, src/logic.c, its scratch put back;
 * the PC's avdgoal leaves a stopped clock's referee alone on his own side of the goal line), the aim led by his speed's high byte,
 * 9 (close enough) within 0Ch, else vtoa of it, then EvadePlayers, kept in temp2's low byte; no direction and still, a step toward
 * the puck (a fight: xc1 / yc1). Every tick doplayeracc with the kept direction */
static void ref_skateto(struct SortObj *o, int16_t x, int16_t y)
{
    int8_t tk = (int8_t)((uint16_t)o->temp2 >> 8);
    tk = (int8_t)(tk - elapsed);
    o->temp2 = (int16_t)(((uint16_t)(uint8_t)tk << 8) | ((uint16_t)o->temp2 & 0xFF));
    if (tk < 0) {
        o->temp2 = (int16_t)(((uint16_t)(uint8_t)(tk + 0xC) << 8) | ((uint16_t)o->temp2 & 0xFF));
        int16_t ox = x, oy = y;
        if (!((g.gmode & (1 << gmclock)) && iabs(y) < 0x108 && (y ^ o->Ypos) >= 0)) {
            uint16_t sdx = g.deltax, sdy = g.deltay;
            avdgoal(&x, &y, o);
            g.deltax = sdx; g.deltay = sdy;
        }
        int px = o->Xpos + (o->Xvel >> 8), py = o->Ypos + (o->Yvel >> 8);
        int a0 = (int16_t)(x - px), a1 = (int16_t)(y - py), dx = iabs((int16_t)(ox - px)), dy = iabs((int16_t)(oy - py));
        int d;
        if (iabs(a0) <= 0xC && iabs(a1) <= 0xC && (dx <= 0xC || o->Xvel) && (dy <= 0xC || o->Yvel)) d = 9;
        else d = vtoa((int16_t)a0, (int16_t)a1);
        d = ref_evade(o, d);
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

/* he comes on at x, y: at rest, facing centre ice (StartHL2's facing 2 on the PC's left), skateto's first look at once */
void referee_skate_start(struct SortObj *o, int16_t x, int16_t y)
{
    o->Xpos = x; o->Xfrac = 0; o->Ypos = y; o->Yfrac = 0; o->Xvel = o->Yvel = 0;
    o->facedir = x > 0 ? 6 : 2; o->_pad56 = 0;
    o->temp2 = 9; o->temp3 = x; o->temp4 = y;
    o->position = 1;                                              /* (not a goalie: avdgoal leaves a goalie alone) */
    o->SPA = REF_GLIDE; o->SPAcnt = 0;
}

/* assreffollow (4E0BD): the spot by the puck's zone, then skateto there. The puck by the net (|x| under 32h, |y| from 0ACh to the goal
 * line): beside the net behind the goal line (46h, 0EDh); deep in an end (|y| over 62h) with the puck loose or its carrier attacking
 * that end: by the corner on the goal line (78h, 0E8h); else beside the play by the boards (90h), keeping his y till the view is
 * 32h off it. On his own side of the rink always. A fight (not in 95 PC): fight_spot. The clock run out: he glides (doplayeracc 8) */
void referee_follow(struct SortObj *o)
{
    const struct SortObj *pk = sortobj(puckscnum);
    int16_t px = pk->Xpos, py = pk->Ypos, cy = (int16_t)g.Vpos;
    int ax = iabs(px), ay = iabs(py), side = o->Xpos > 0;
    if (!g.gameclock) { ref_doacc(o, 8); return; }
    if (pk->pflags2 & (1 << pf2fight)) { int16_t fx, fy; fight_spot(o, &fx, &fy); o->temp3 = fx; o->temp4 = fy; }
    else if (ax < PCX(0x32) && ay > PCY(0xAC) && ay < PCY(0xE8)) { o->temp3 = (int16_t)(side ? PCX(0x46) : -PCX(0x46)); o->temp4 = (int16_t)(py > 0 ? PCY(0xED) : -PCY(0xED)); }
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
    int32_t p = POS32(o->Xpos, o->Xfrac) + (int32_t)o->Xvel * d4; o->Xpos = (int16_t)(p >> 16); o->Xfrac = (uint16_t)p;
    p = POS32(o->Ypos, o->Yfrac) + (int32_t)o->Yvel * d4; o->Ypos = (int16_t)(p >> 16); o->Yfrac = (uint16_t)p;
    if (o->SPAcnt < 0xFFFF) o->SPAcnt = (uint16_t)(o->SPAcnt + elapsed);
    int16_t c, s;
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
        int wx = POS32(ref.Xpos, ref.Xfrac) >> 8, wy = POS32(ref.Ypos, ref.Yfrac) >> 8;
        if (hold_skate_ref_step(&wx, &wy)) {                     /* (RULE_HOLD: he skates there, fetching the puck, mods/hold.c) */
            ref.Xpos = (int16_t)(wx >> 8); ref.Xfrac = (uint16_t)((uint32_t)wx << 8);
            ref.Ypos = (int16_t)(wy >> 8); ref.Yfrac = (uint16_t)((uint32_t)wy << 8);
        } else if (rx.mode && hold_skate_pending()) {
            /* (RULE_HOLD: the face off is set up a frame before the walk is: he stays where he is that frame. Put at the dot he was
             * gone from the view, and the walk then started from the dot: PR #193's walk, and he showed again only as it neared
             * the view) */
        } else {
            ref.Xpos = (int16_t)(fx > 0 ? fx + 15 : fx - 15); ref.Xfrac = 0;
            ref.Ypos = fy; ref.Yfrac = 0;
        }
        ref.Xvel = ref.Yvel = 0;
        ref.facedir = fx > 0 ? 6 : 2; ref._pad56 = 0; ref.temp2 = 9;
        ref_anim(&ref, REF_GLIDE);
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
    else referee_follow(&ref);                                    /* (assreffollow: 95 PC's skating, above) */
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
    v->at = ref.SPAcnt;
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
    }
    return REFEREE_STATE_SIZE;
}

/* a state's n bytes, or (in NULL) none: a state from before the referee, which starts him off the ice. A state from before his
 * skating (version 6 to 9 of the rules' part: REFEREE_STATE_SIZE_9 bytes) starts it afresh where he is */
int referee_state_load(const uint8_t *in, size_t n)
{
    memset(&ref, 0, sizeof ref);
    memset(&rx, 0, sizeof rx);
    if (!in) return 0;
    if (n != REFEREE_STATE_SIZE && n != REFEREE_STATE_SIZE_9) return -1;
    if (n == REFEREE_STATE_SIZE && (in[22] > 7 || (in[23] > 9) || (int8_t)in[24] < -0xC || (int8_t)in[24] > 0xC)) return -1;
    if (in[15] > 2 || in[16] > REF_DROP || in[17] > REF_FALL || in[18] > 7 || in[19] > 2 || in[20] > 0xB || in[21]) return -1;   /* (a state is checked before it is used) */
    int16_t w[7];
    for (int k = 0; k < 7; k++) w[k] = (int16_t)(in[2 * k] | in[2 * k + 1] << 8);
    ref.Xpos = w[0]; ref.Xfrac = (uint16_t)w[1]; ref.Ypos = w[2]; ref.Yfrac = (uint16_t)w[3];
    ref.Xvel = w[4]; ref.Yvel = w[5]; ref.impact = w[6];
    ref.nopuck[0] = in[14]; rx.mode = in[15]; rx.drop = in[16]; rx.fall = in[17]; rx.fdir = in[18]; rx.hits = in[19]; rx.faller = in[20];
    int16_t x = ref.Xpos, y = ref.Ypos, vx = ref.Xvel, vy = ref.Yvel;
    referee_skate_start(&ref, x, y);
    ref.Xfrac = (uint16_t)w[1]; ref.Yfrac = (uint16_t)w[3]; ref.Xvel = vx; ref.Yvel = vy;
    if (n == REFEREE_STATE_SIZE) {
        ref.facedir = in[22]; ref.temp2 = (int16_t)(in[23] | in[24] << 8);
        ref._pad56 = (uint16_t)(in[25] | in[26] << 8); ref.temp4 = (int16_t)(in[27] | in[28] << 8);
    }
    ref.SCnum = 0x10;
    ref.weight = REF_WEIGHT;
    return 0;
}
