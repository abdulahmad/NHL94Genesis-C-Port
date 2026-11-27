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
 *  - where he skates: as the host's picture of him did (assreffollow's spots for the cartridge's rink: beside
 *    the play at x 7Ch, in a corner, behind the net), easing in at most 2 a frame; at a face off he stands 15 off
 *    the dot and after the drop waits 24 frames (the drop's frames). */
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
    rx.fdir = (uint8_t)(vtoa(ref.Xvel, ref.Yvel) & 7);
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

/* A fight (mods/fight.c): the players not in it watch from 50h away (NHL 93's assfwatch: a spot 50h from the fight, xc1 / yc1, on
 * the line from it to them, at most 7Eh across). He does the same (where the boards cut that line, along them, 50h off), so he never stands between the two (the beside the play spot is
 * at the boards, at the view's y: a fight along the boards had him in the middle of it) */
static void fight_spot(int16_t *tx, int16_t *ty)
{
    int16_t fx = (int16_t)g.xc1, fy = (int16_t)g.yc1;
    int32_t dx = ref.Xpos - fx, dy = ref.Ypos - fy;
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
    *tx = (int16_t)x; *ty = (int16_t)y;
}

/* 95 PC's EvadePlayers, which his assreffollow skates with (skateto's evade routine): every skater (not the fighters, and not one out
 * of play) and the puck, their gaps to him a little ahead (the places plus the high bytes of the speeds); the nearest within 28h on
 * both axes (the puck only when no skater is within 50h) turns his way aside. The PC's turns him to one of 8 directions from that
 * gap by where he is on the rink; here the speed he wants (wx, wy, at most m) is turned to the side of the gap and away from it, and
 * kept off the boards he stands by. Integers only: every machine of a session moves him alike */
#define EVADE_GAP 0x28
static void evade(int32_t *wx, int32_t *wy, int32_t m)
{
    int best = 0x64, bx = 0, by = 0;
    for (int i = 0; i <= puckscnum; i++) {
        if (i >= 12 && i != puckscnum) continue;
        const struct SortObj *q = sortobj(i);
        if (i < 12 && (q->position < 0 || (q->pflags & (1 << pfnc)) || (q->pflags2 & ((1 << 5) | (1 << pf2fight))))) continue;
        if (i == puckscnum && ((int16_t)puck_carrier() >= 0 || best <= 0x50)) continue;
        int gx = ref.Xpos + (ref.Xvel >> 8) - q->Xpos - (q->Xvel >> 8);
        int gy = ref.Ypos + (ref.Yvel >> 8) - q->Ypos - (q->Yvel >> 8);
        if (gx > EVADE_GAP || gx < -EVADE_GAP || gy > EVADE_GAP || gy < -EVADE_GAP) continue;
        int d = (gx < 0 ? -gx : gx) + (gy < 0 ? -gy : gy);
        if (d >= best) continue;
        best = d; bx = gx; by = gy;
    }
    if (best >= 0x64) return;
    if (!bx && !by) bx = ref.Xpos > 0 ? -1 : 1;
    uint32_t bl = isqrt64((uint64_t)((int64_t)bx * bx + (int64_t)by * by));
    int32_t ux = (int32_t)((int64_t)bx * m / (int32_t)bl), uy = (int32_t)((int64_t)by * m / (int32_t)bl);   /* (away from it, at his top speed) */
    int64_t ahead = (int64_t)*wx * -bx + (int64_t)*wy * -by;      /* (his way toward it) */
    int32_t sx = ux, sy = uy;
    if (ahead > 0) {                                              /* (it is in his way: round it, to the side it is not on) */
        int32_t px = -*wy, py = *wx;
        if ((int64_t)px * bx + (int64_t)py * by < 0) { px = -px; py = -py; }
        uint32_t pl = isqrt64((uint64_t)((int64_t)px * px + (int64_t)py * py));
        if (pl) { px = (int32_t)((int64_t)px * m / (int32_t)pl); py = (int32_t)((int64_t)py * m / (int32_t)pl); }
        sx = (px + ux) / 2; sy = (py + uy) / 2;
    } else {                                                      /* (beside or behind him: on his way, and a step off it) */
        sx = (*wx + ux) / 2; sy = (*wy + uy) / 2;
    }
    if ((ref.Xpos >= 0x76 && sx > 0) || (ref.Xpos <= -0x76 && sx < 0)) { sy += (sy >= 0 ? 1 : -1) * (sx < 0 ? -sx : sx); sx = 0; }   /* (not into the boards: along them) */
    if ((ref.Ypos >= 0x100 && sy > 0) || (ref.Ypos <= -0x100 && sy < 0)) { sx += (sx >= 0 ? 1 : -1) * (sy < 0 ? -sy : sy); sy = 0; }
    uint32_t l = isqrt64((uint64_t)((int64_t)sx * sx + (int64_t)sy * sy));
    if ((int32_t)l > m) { sx = (int32_t)((int64_t)sx * m / (int32_t)l); sy = (int32_t)((int64_t)sy * m / (int32_t)l); }
    *wx = sx; *wy = sy;
}

/* once a pass, after the skaters (updateplayers) */
void referee_update(void)
{
    if (!active()) { rx.mode = 0; if (!g.demoflag) rx.hits = 0; return; }   /* (out of a game: the next counts from 0, as 95 PC's ResetGameVars) */
    const struct SortObj *pk = sortobj(puckscnum);
    int asn = pk->asslist[pk->assnum & 7];
    int16_t fx = (int16_t)g.fox, fy = (int16_t)g.foy;
    if (asn == pfaceoff || asn == pfaceoff2) {                   /* (assrefatdot: 15 off the dot, at rest) */
        int wx = POS32(ref.Xpos, ref.Xfrac) >> 8, wy = POS32(ref.Ypos, ref.Yfrac) >> 8;
        if (hold_skate_ref_step(&wx, &wy)) {                     /* (RULE_HOLD: he skates there, fetching the puck, mods/hold.c) */
            ref.Xpos = (int16_t)(wx >> 8); ref.Xfrac = (uint16_t)((uint32_t)wx << 8);
            ref.Ypos = (int16_t)(wy >> 8); ref.Yfrac = (uint16_t)((uint32_t)wy << 8);
        } else {
            ref.Xpos = (int16_t)(fx > 0 ? fx + 15 : fx - 15); ref.Xfrac = 0;
            ref.Ypos = fy; ref.Yfrac = 0;
        }
        ref.Xvel = ref.Yvel = 0;
        rx.mode = 1; rx.drop = 0; rx.fall = 0;
        return;
    }
    int K = g.PalMode ? 0x16 : 0x11;
    int16_t px = pk->Xpos, py = pk->Ypos, cy = (int16_t)g.Vpos;
    if (rx.mode == 0) { ref.Xpos = (int16_t)(px > 0 ? 0x7C : -0x7C); ref.Xfrac = 0; ref.Ypos = cy; ref.Yfrac = 0; ref.Xvel = ref.Yvel = 0; rx.fall = 0; }
    if (rx.mode == 1) rx.drop = REF_DROP;
    rx.mode = 2;
    ref.SCnum = 0x10;
    ref.weight = REF_WEIGHT;
    if (ref.nopuck[0]) ref.nopuck[0]--;
    ref.impact = (int16_t)(ref.impact - elapsed * 2);
    if (ref.impact < 0) ref.impact = 0;
    if (rx.fall) {
        rx.fall = (uint8_t)(rx.fall > elapsed ? rx.fall - elapsed : 0);
        if (ref.Xvel) { int16_t d = (int16_t)(ref.Xvel >> 6); if (!d) d = 1; ref.Xvel = (int16_t)(ref.Xvel - d); }   /* (down: he slides, */
        if (ref.Yvel) { int16_t d = (int16_t)(ref.Yvel >> 6); if (!d) d = 1; ref.Yvel = (int16_t)(ref.Yvel - d); }   /* as a skater does on the ice) */
    } else if (rx.drop) {
        rx.drop--;
        ref.Xvel = ref.Yvel = 0;
    } else {
        int16_t rx_ = ref.Xpos, ry = ref.Ypos, tx, ty;
        int16_t ax = px < 0 ? (int16_t)-px : px, ay = py < 0 ? (int16_t)-py : py;
        if (pk->pflags2 & (1 << pf2fight)) fight_spot(&tx, &ty);                                                /* a fight: watching it */
        else if (ax < 0x32 && ay > 0xAC && ay < 0xE8) { tx = rx_ > 0 ? 0x46 : -0x46; ty = py > 0 ? 0xED : -0xED; }  /* behind the net */
        else if (ax > 0x62 && ay > 0x80) { tx = rx_ > 0 ? 0x6A : -0x6A; ty = py > 0 ? 0xE8 : -0xE8; }           /* in a corner */
        else { tx = rx_ > 0 ? 0x7C : -0x7C; ty = (ry - cy < 0 ? cy - ry : ry - cy) > 0x32 ? cy : ry; }       /* beside the play */
        int32_t dx = (int32_t)(((uint32_t)(uint16_t)tx << 16)) - POS32(ref.Xpos, ref.Xfrac);
        int32_t dy = (int32_t)(((uint32_t)(uint16_t)ty << 16)) - POS32(ref.Ypos, ref.Yfrac);
        int32_t wx = dx / (12 * K), wy = dy / (12 * K), m = 2 * 65536 / K;   /* (the speed he wants: easing in, at most 2 a frame) */
        uint32_t l = isqrt64((uint64_t)((int64_t)wx * wx + (int64_t)wy * wy));
        if ((int32_t)l > m) { wx = (int32_t)((int64_t)wx * m / (int32_t)l); wy = (int32_t)((int64_t)wy * m / (int32_t)l); }
        evade(&wx, &wy, m);                                       /* (out of the skaters' way, and the puck's: 95 PC's EvadePlayers) */
        ref.Xvel = (int16_t)(ref.Xvel + (wx - ref.Xvel) / 6);
        ref.Yvel = (int16_t)(ref.Yvel + (wy - ref.Yvel) / 6);
    }
    int16_t lim = (int16_t)(4 * 65536 / K);                       /* (at most 4 a frame) */
    if (ref.Xvel > lim) ref.Xvel = lim;
    if (ref.Xvel < -lim) ref.Xvel = (int16_t)-lim;
    if (ref.Yvel > lim) ref.Yvel = lim;
    if (ref.Yvel < -lim) ref.Yvel = (int16_t)-lim;
    int16_t ox = ref.Xpos, oy = ref.Ypos;
    uint16_t oxf = ref.Xfrac, oyf = ref.Yfrac;
    int32_t d4 = K * elapsed;
    int32_t p = POS32(ref.Xpos, ref.Xfrac) + (int32_t)ref.Xvel * d4; ref.Xpos = (int16_t)(p >> 16); ref.Xfrac = (uint16_t)p;
    p = POS32(ref.Ypos, ref.Yfrac) + (int32_t)ref.Yvel * d4; ref.Ypos = (int16_t)(p >> 16); ref.Yfrac = (uint16_t)p;
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
    }
    return REFEREE_STATE_SIZE;
}

/* a state's bytes, or (in NULL) none: a state from before the referee, which starts him off the ice */
int referee_state_load(const uint8_t *in)
{
    memset(&ref, 0, sizeof ref);
    memset(&rx, 0, sizeof rx);
    if (!in) return 0;
    if (in[15] > 2 || in[16] > REF_DROP || in[17] > REF_FALL || in[18] > 7 || in[19] > 2 || in[20] > 0xB || in[21]) return -1;   /* (a state is checked before it is used) */
    int16_t w[7];
    for (int k = 0; k < 7; k++) w[k] = (int16_t)(in[2 * k] | in[2 * k + 1] << 8);
    ref.Xpos = w[0]; ref.Xfrac = (uint16_t)w[1]; ref.Ypos = w[2]; ref.Yfrac = (uint16_t)w[3];
    ref.Xvel = w[4]; ref.Yvel = w[5]; ref.impact = w[6];
    ref.nopuck[0] = in[14]; rx.mode = in[15]; rx.drop = in[16]; rx.fall = in[17]; rx.fdir = in[18]; rx.hits = in[19]; rx.faller = in[20];
    ref.SCnum = 0x10;
    ref.weight = REF_WEIGHT;
    return 0;
}
