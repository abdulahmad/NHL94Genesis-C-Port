/* RULE_GOALIE: the goalie's saves, by another game's rules. Each value takes over goaliesave, the cartridge's choice of save once
 * the goalie has decided to make one (when he does, and where he goes, stay the cartridge's):
 *
 * 1 NHL 95 PC (goalie/assgoalie.c, the save stances): the side from where the puck will be next (puckx + puckvx >> 10), a puck straight
 *   at him the side he faces; a slow puck straight at him falls him back (SPAgofallback), a low puck wide of him drops him to the stick
 *   or glove side (SPAgofallstick, SPAgofallglove), a low one a stick save, the pad stack for one far off; no shoulder saves.
 * 2 NHL 96 Genesis (puck.asm goaliesave): the cartridge's choice from its own table, then a puck at most 2 from him across, not rising
 *   fast, blocked (SPAgblock) or caught low (SPAgcatchlow), a slow puck 10 or more from him the reach out saves, then a quarter turn.
 * 3 NHL 95 Genesis (logic95.asm goaliesave): aimed at where the puck crosses his line (Xvel), the side it passes him, 95's own table;
 *   a pad stack facing up or down during a shot becomes a dive (SPAgofallglove, or SPAgofallback 40 in 100), a raised puck facing up
 *   or down the high saves.
 * 4 NHL 96 PC (faceoff.asm goaliesave): 95 PC's, and a loose puck straight at him in front, low, the toe save (SPAgsavetoe), higher
 *   the kick save (SPAgsavekick).
 *
 * The saves the 94 cartridge has no animation for are mods/anims.c's, in NHL 95 PC's pictures (with PC player sprites on) or the
 * cartridge's closest save (off): falling back the stack save, the falls to the side the stick saves, the block and catches the knee
 * save. NHL 96 PC's toe save is the knee save, its kick the stick save; 96 Genesis's reach out blocker the blocker save. The save odds are
 * those of the cartridge frame each picture stands for. 1, 2 and 4 also have the goalie's own hop over the boards (SPAgwallright,
 * SPAgwallleft: 95 Genesis has none). 0 is the cartridge's (native/vanilla/mods.c). */
#include "game.h"
#include "rules.h"
#include "anim_gen.h"
#include "anims.h"
#include <stdlib.h>

#define PFLAG(o, b) ((o)->pflags & (1 << (b)))

static int16_t pz(void) { return (int16_t)g.puckz[0]; }
static int16_t pvz(void) { return (int16_t)g.puckvz[0]; }
static int16_t pvx(void) { return (int16_t)g.puckvx; }
static int16_t pvy(void) { return (int16_t)g.puckvy; }
static int16_t puckx_of(void) { return (int16_t)g.puckx[0]; }
static int16_t pucky_of(void) { return (int16_t)g.pucky; }

/* the save's crowd, the cartridge's (logic94.asm goaliesave) */
static void crowd(void) { g.Crowdlevel += 0x96; g.CwdExciteLvl += 0xA; }

/* the cartridge's pad stack (shared by 94, 95 Genesis and 96 Genesis): the goalie slid 6 toward the middle, d0 by net and hand */
static int16_t pad_stack(int16_t d0, struct SortObj *gk)
{
    gk->Xvel = 0x1000;
    d0 |= 1;
    if (!PFLAG(gk, pfgoal)) d0 ^= 1;
    int16_t dx = -6;
    if (gk->Xvel >= 0) gk->Xvel = (int16_t)-gk->Xvel;
    if (gk->Xpos < 0) {
        if (gk->Xvel < 0) gk->Xvel = (int16_t)-gk->Xvel;
        dx = 6;
        d0 ^= 1;
    }
    gk->Xpos = (int16_t)(gk->Xpos + dx);
    if (gk->handed[0] & 1) d0 ^= 1;
    return d0;
}

/* the height and distance part the cartridge, 95 Genesis and 96 Genesis share: d0 the side (0 or 1) -> the table index */
static int16_t cart_type(int16_t d0, const struct PuckCross *pc, struct SortObj *gk)
{
    int16_t dx = (int16_t)(gk->Xpos - pc->x);
    if (pz() > 8 || pvz() > 0x800) return dx <= 0x10 && dx >= -0x10 ? (int16_t)(d0 + 6) : d0;
    d0 = (int16_t)(d0 + 4);
    if (pc->frames <= 8) return dx > 0x10 || dx <= -0x10 ? (int16_t)(d0 + 4) : d0;
    if (gk->facedir == 2 || gk->facedir == 6 || (int16_t)puck_carrier() < 0) return d0;
    return pad_stack((int16_t)(d0 - 2), gk);
}

/* NHL 95 PC and NHL 96 PC: the save stance (95 PC assgoalie.c; 96 PC goaliesave, the same with the toe and kick saves) */
static uint16_t save_pc(const struct PuckCross *pc, struct SortObj *gk, int pc96)
{
    static const uint16_t anim[6] = { SPAgglover, SPAgglovel, SPAgstackr, SPAgstackl, SPAgstickr, SPAgstickl };   /* (6, 7, 8: the falls) */
    gk->Xvel = (int16_t)(gk->Xvel >> 1);
    gk->Yvel = (int16_t)(gk->Yvel >> 1);
    int16_t x = (int16_t)(puckx_of() + (pvx() >> 10) - gk->Xpos);  /* (the PC's +48h bit 1, out of the crease: its puckdir, not kept here) */
    int16_t y = (int16_t)(pucky_of() + (pvy() >> 10) - gk->Ypos);
    int a = vtoa(x, y);
    int16_t d3 = a == 8 ? 0 : (int16_t)((a - gk->facedir) & 7);
    int16_t d0;
    if (d3 == 4 && abs(pvy()) + abs(pvx()) <= 0x1200) return MA_SPA(MA_GFALLB);
    if (pc96 && d3 == 0 && (int16_t)puck_carrier() < 0 && abs(x) < 5 && pz() < 0x10 && pvz() < 0x800)
        return pz() < 8 ? MA_SPA(MA_GKNEE) : ((gk->attribute & HI(3)) ? SPAgstickl : SPAgstickr);   /* (the toe save, the kick save) */
    if (d3 == 0 || d3 == 4) {
        int f = gk->facedir;
        if (f == 0) d3 = x >= 0 ? 1 : 7;
        else if (f == 4) d3 = x >= 0 ? 7 : 1;
        else if (f == 1 || f == 5) d3 = abs(x) >= abs(y) ? 1 : 7;
        else if (f == 3 || f == 7) d3 = abs(x) >= abs(y) ? 7 : 1;
        else d3 = (int16_t)randomd0(pc96 ? 8 : 2);
    }
    d0 = (int16_t)(d3 >> 2);
    if (gk->attribute & HI(3)) d0 = d0 == 0;
    if (d3 >= 4) d3 = (int16_t)(7 - d3);
    if (d3 >= 2 && pz() < 0xA && pvz() < 0x800) d0 = (int16_t)(d0 + 6);
    else if (pz() < 8 && pvz() < 0x800) {
        d0 = (int16_t)(d0 + 4);
        if (pc->frames >= 9 && gk->facedir != 2 && gk->facedir != 6 && (int16_t)puck_carrier() >= 0) d0 = (int16_t)(d0 - 2);
    }
    if (d0 == 6) return MA_SPA(MA_GFALLS);
    if (d0 == 7) return MA_SPA(MA_GFALLG);
    return anim[d0];
}

/* NHL 96 Genesis: the cartridge's choice, its table, then the block, the low catch and the reach out saves */
static uint16_t save_96g(const struct PuckCross *pc, int16_t goalline, struct SortObj *gk)
{
    static const uint16_t anim[10] = { SPAgglover, SPAgglovel, SPAgstackr, SPAgstackl, SPAgstickr, SPAgstickl,
                                       SPAgglover, SPAgglovel, SPAgstickr, SPAgstickl };
    int16_t d3 = (int16_t)((vtoa((int16_t)(pc->x - gk->Xpos), (int16_t)(goalline - gk->Ypos)) - gk->facedir) & 7);
    int16_t side = (int16_t)(d3 >> 2);
    if (gk->attribute & HI(3)) side ^= 1;
    uint16_t spa = anim[cart_type(side, pc, gk)];
    int16_t dx = (int16_t)abs((int16_t)(gk->Xpos - pc->x));
    if (dx <= 2 && pvz() <= 0xC00) spa = MA_SPA(pvz() > 0x600 ? MA_GFALLB : MA_GKNEE);
    else if (pvz() <= 0x400 && dx >= 0xA) spa = (side & 1) ? SPAgglovel2 : SPAgglover;   /* (96's reach out saves: its blocker one is the blocker save) */
    else if (spa == SPAgglovel && (d3 & 3) && (int8_t)gk->aggress >= 0xB) spa = SPAgglovel2;   /* (96's GGSleft, the cartridge's aggress) */
    return spa;
}

/* 96 Genesis's quarter turn after the save, toward the side boards */
static void turn_96g(struct SortObj *gk)
{
    int f = gk->facedir;
    if (PFLAG(gk, pfgoal)) { if (f == 3) gk->facedir = 2; else if (f == 5) gk->facedir = 6; }
    else { if (f == 7) gk->facedir = 6; else if (f == 1) gk->facedir = 2; }
}

/* NHL 95 Genesis: aimed at the crossing, the side the puck passes, 95's table, the dives and the high saves */
static uint16_t save_95g(const struct PuckCross *pc, int16_t goalline, struct SortObj *gk)
{
    static const uint16_t anim[10] = { SPAgglover, SPAgglovel, SPAgstackl, SPAgstackr, SPAgstickr, SPAgstickl,
                                       SPAgglover, SPAgglovel, SPAgstickr, SPAgstickl };
    int16_t t = (int16_t)(gk->Ypos - pucky_of());                    /* (frames until the puck reaches his y) */
    if (pvy() == 0) t = 1;
    else if (!(t == INT16_MIN && pvy() == -1)) t = (int16_t)(t / pvy());
    int16_t x = (int16_t)((int16_t)(pvx() * t) + puckx_of() - gk->Xpos);
    int16_t n = t ? t : 1;
    gk->Xvel = (int16_t)((int16_t)(x / n) << 8);
    if (!PFLAG(gk, pfgoal)) x = (int16_t)-x;
    int16_t d0 = x < 0 ? 1 : 0;
    if (gk->attribute & HI(3)) d0 ^= 1;
    (void)goalline;
    uint16_t spa = anim[cart_type(d0, pc, gk)];
    int up = gk->facedir == 0 || gk->facedir == 4;
    if ((spa == SPAgstackl || spa == SPAgstackr) && up && (g.sflags & HI(sfssdir)))
        spa = randomd0(100) > 0x3C ? MA_SPA(MA_GFALLB) : MA_SPA(MA_GFALLG);
    if (pz() >= 3 && up) {
        int16_t dx = (int16_t)(gk->Xpos - pc->x);
        uint16_t a = (gk->handed[0] & 1) ? SPAghighr : SPAgglovel2, b = (gk->handed[0] & 1) ? SPAgglovel2 : SPAghighr;
        if (PFLAG(gk, pfgoal)) { if (dx > 0x12) spa = a; else if (dx <= -0x12) spa = b; }
        else { if (dx > 0x12) spa = b; else if (dx < -0x12) spa = a; }
    }
    return spa;
}

int goalie_save(const struct PuckCross *pc, int16_t goalline, struct SortObj *gk)
{
    int r = RULE(RULE_GOALIE);
    if (!r) return 0;
    uint16_t spa;
    if (r == 1 || r == 4) spa = save_pc(pc, gk, r == 4);
    else if (r == 2) spa = save_96g(pc, goalline, gk);
    else spa = save_95g(pc, goalline, gk);
    SetSPA(gk, spa);
    crowd();
    if (r != 1 && r != 4) { gk->Xvel = (int16_t)(gk->Xvel >> 2); gk->Yvel = (int16_t)(gk->Yvel >> 2); }
    if (r == 3 && (g.sflags & HI(sfssdir))) gk->Xvel = 0;
    if (r == 2) turn_96g(gk);
    return 1;
}

uint16_t goalie_wall_spa(const struct SortObj *o, uint16_t spa)
{
    int r = RULE(RULE_GOALIE);
    if (!r || r == 3 || o->position != 0) return spa;
    return MA_SPA(spa == SPAwallright ? MA_GWALLR : MA_GWALLL);
}
