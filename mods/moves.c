/* NHL 95's moves in the 94 game: the fake shot, the drop pass and blocking shots, each an option (src/rules.h) with NHL 95 PC's way
 * (1) or NHL 95 Genesis's (2); 0 is the cartridge's, which has none of them (native/vanilla/mods.c has its answers).
 *
 * NHL 95 Genesis (NHL95Genesis src/engine): input95.asm doinput's A button and SetShotMode / doshot, dopass and passtoa0 (GameFlags
 * bits 0 and 1, cleared by middle95.asm CheckNewCarrier whenever someone else has the puck), logic95.asm check4check and Holdplayer
 * with replay95.asm CanBlock and chkblock (GameFlags bit 2, ShotTimer and shotpflags from doshot).
 * NHL 95 PC (NHL95PC src/engine): puckphys/ShotMode.c, pass/dopass.c with PassLaneChk.c and ScatterPass.c (penalty.asm ShotLaneOpen),
 * input/Holdplayer.c with CanBlockShot.c and OppInReach, checking/TryBlockShot.c (from check4check) and BlockShotDive.c, puckstick's
 * block test. The PC's ratings are half the Genesis' (its check4check's 14h - checking against the cartridge's 28h), so its formulas
 * take the cartridge's ratings halved. NHL 95 PC's computer players drop pass too (chk4pass); here only a pad player's pass does.
 *
 * The animations are mods/anims.c's (the blocks in the PC's pictures). 95G's flags and shot timer are this file's own state, kept in
 * every state (MOVES_STATE_SIZE, native/rules.c). */
#include "pccoord.h"
#include "game.h"
#include "rom.h"
#include "rules.h"
#include "symbols_gen.h"
#include "anim_gen.h"
#include "anims.h"
#include <stdlib.h>

#define PFLAG(o, b) ((o)->pflags & (1 << (b)))
#define BUT_B 0x10
#define BUT_A 0x40

static struct {
    uint8_t flags;                                        /* 95G's GameFlags: bit 0 a fake shot, 1 a drop pass, 2 a shot blocked */
    int16_t lastpuckc;                                    /* (CheckNewCarrier's lastpuckc) */
    int16_t shottimer;                                    /* frames since the last shot from $1E down; negative: none */
    uint8_t shotpflags;                                   /* the shooter's pflags */
} M = { 0, -1, -1, 0 };

static int16_t puckx(void) { return (int16_t)g.puckx[0]; }
static int16_t pucky(void) { return (int16_t)g.pucky; }
static int16_t puckc(void) { return (int16_t)puck_carrier(); }
static int dist(int dx, int dy) { return Sroot((uint32_t)(dx * dx + dy * dy)); }   /* (the PC's vecdist) */
/* places on the PC's rink (mods/pccoord.h): 95 PC's tests below measure there, with the PC's own numbers */
#define PX(o) cart_pc_x((o)->Xpos)
#define PY(o) cart_pc_y((o)->Ypos)
static int ppx(void) { return cart_pc_x(puckx()); }
static int ppy(void) { return cart_pc_y(pucky()); }

void moves_frame(void)
{
    if (M.shottimer >= 0) M.shottimer = (int16_t)(M.shottimer - elapsed);
    if (M.lastpuckc != puckc()) M.flags &= (uint8_t)~3;   /* (CheckNewCarrier) */
    M.lastpuckc = puckc();
}

/* ---- the fake shot ---- */

/* NHL 95 PC (ShotMode): early in the wind up (before its fifth frame) a new press of B or A ends it, the shot mode off, in the
 * fake's follow through (from the same frame on) */
int moves_fake_pc(uint16_t d1, struct SortObj *o)
{
    if (RULE(RULE_FAKESHOT) != 1) return 0;
    if (o->SPA != SPAshotf && o->SPA != SPAshotb) return 0;
    if ((int16_t)o->SPAnum >= 0x14 || !(d1 & (BUT_B | BUT_A))) return 0;
    g.sflags &= (uint16_t)~HI(sfssdir);
    o->pflags |= 1 << pfalock;
    o->SPA = MA_SPA(o->SPA == SPAshotf ? MA_FAKEF_PC : MA_FAKEB_PC);
    return 1;
}

/* NHL 95 Genesis (doinput): A let go with the puck in the attacking zone (y $56 on) is a fake shot, the wind up of a shot that does
 * not go; once started, A again does nothing until it is over */
int moves_fake_g(struct SortObj *o)
{
    if (RULE(RULE_FAKESHOT) != 2) return 0;
    int16_t y = pucky();
    if (y < 0x56 && y > -0x56) return 0;
    if (!PFLAG(o, pfgoal)) y = (int16_t)-y;
    if (y < 0) return 0;
    if (M.flags & 1) return 1;
    M.flags |= 1;
    SetShotMode(o);
    return 1;
}

int moves_fake_pending(void) { return RULE(RULE_FAKESHOT) == 2 && (M.flags & 1); }

uint16_t moves_shot_spa(uint16_t spa)
{
    if (!moves_fake_pending()) return spa;
    return MA_SPA(spa == SPAshotf ? MA_FAKEF_G : MA_FAKEB_G);
}

/* doshot: 95G's fake ends gliding with the puck, no shot; a shot starts 95G's shot timer (the blocker's chkblock) */
int moves_fake_end(struct SortObj *o)
{
    if (M.flags & 1) {
        M.flags &= (uint8_t)~1;
        if (RULE(RULE_FAKESHOT) == 2) {
            g.sflags &= (uint16_t)~HI(sfssdir);
            SetSPA(o, SPAglide);
            return 1;
        }
    }
    M.shottimer = 0x1E;
    M.shotpflags = o->pflags;
    return 0;
}

/* ---- the drop pass ---- */

/* NHL 95 Genesis (doinput): A pressed with the pad pulled back (away from the goal he attacks) starts a drop pass */
int moves_drop_g(uint16_t d3, struct SortObj *o)
{
    if (RULE(RULE_DROPPASS) != 2) return 0;
    if (!(d3 & (PFLAG(o, pfgoal) ? 2 : 1))) return 0;
    M.flags |= 2;
    setpassmode(o);
    return 1;
}

int moves_dropping(void) { return RULE(RULE_DROPPASS) == 2 && (M.flags & 2); }

uint16_t moves_pass_spa(uint16_t spa)
{
    if (!moves_dropping()) return spa;
    if (spa == SPApassf) return MA_SPA(MA_DROP_G);
    if (spa == SPApassb) return MA_SPA(MA_DROPB_G);
    return spa;
}

/* passtoa0: a drop pass takes 4 times as long to the meeting, on the ice */
int moves_drop_time(int16_t *t)
{
    if (!moves_dropping()) return 0;
    *t = (int16_t)(*t << 2);
    return 1;
}

/* dopass with nobody that way: a quarter of the speed, on the ice */
void moves_drop_loose(void)
{
    if (!moves_dropping()) return;
    g.puckvz[0] = 0;
    g.puckvx = (uint16_t)((int16_t)g.puckvx >> 2);
    g.puckvy = (uint16_t)((int16_t)g.puckvy >> 2);
}

/* NHL 95 PC's ShotLaneOpen: 1 when no opposing skater is ahead of the puck carrier toward the net he attacks; 0 when there is,
 * when he is in his own half, or when nobody has the puck (as in dopass, which has let it go) */
static int lane_open(void)
{
    if (puckc() < 0) return 0;
    const struct SortObj *c = sortobj(puckc());
    int up = PFLAG(c, pfgoal) != 0, y = PY(c);
    if (up ? y < 0 : y > 0) return 0;
    int gy = up ? 0xE8 : -0xE8, lim = up ? y + 4 : y - 4;
    int mine = dist(PX(c), y - gy);
    const struct SortObj *q = sortobj(c->SCnum < 6 ? 6 : 0);
    for (int i = 0; i < 6; i++, q++) {
        if (q->position <= 0) continue;
        if (up ? PY(q) > lim : PY(q) < lim) return 0;
        if (up ? q->Yvel < 0 : q->Yvel > 0) continue;
        if (dist(PX(q), PY(q) - gy) <= mine) return 0;
    }
    return 1;
}

/* NHL 95 PC's PassLaneChk: p's pass to o is dropped when p is in the attacking zone (past the blue line, not deeper than $76), o trails
 * him skating the same way within $3C and $28, and o is heading at him */
static int pass_lane(const struct SortObj *p, const struct SortObj *o)
{
    int side = PFLAG(p, pfgoal) != 0;
    if (p->position == 0) return 0;
    if (lane_open()) return 0;
    int oy = PY(o), y = PY(p);
    if ((y > (side ? 0x4E : -0x4E)) ^ side) return 0;
    if (abs(y) <= abs(oy) || abs(y) > 0x76) return 0;
    if (side ? (o->Yvel < 0 || p->Yvel < 0) : (o->Yvel > 0 || p->Yvel > 0)) return 0;
    if (((y - 0x4E) ^ (cart_pc_y(o->Ypos + (int8_t)((uint16_t)o->Yvel >> 8)) - 0x4E)) < 0) return 0;   /* (where he is a frame on) */
    if (abs(oy - y) > 0x3C || abs(PX(p) - PX(o)) > 0x28) return 0;
    int a = vtoa(o->Xvel, o->Yvel);
    if (a == 8) a = o->facedir;
    a -= vtoa((int16_t)(PX(p) - PX(o)), (int16_t)(y - oy));
    return ((a + 1) & 7) < 3;
}

/* NHL 95 PC (dopass, ScatterPass): a pad player's pass to a team-mate trailing him in the zone is left on the ice for him, barely
 * moving; no swing, no sound */
int moves_scatter(struct SortObj *o, struct SortObj *rcv)
{
    if (RULE(RULE_DROPPASS) != 1 || !PFLAG(o, pfjoycon) || !pass_lane(o, rcv)) return 0;
    o->nopuck[0] = 0x28;
    loadTeamStruct(o, NULL)->tmpasses++;
    g.passReceiverPlayerNum = (uint16_t)rcv->SCnum;
    assinsert(apassrec, rcv);
    g.puckvz[0] = 0;
    int r = (0x12 - o->passacc / 2) * 20;
    if (r < 1) r = 1;
    g.puckvx = randomd0((uint16_t)r);
    g.puckvy = randomd0((uint16_t)r);
    g.puckx_nopuck = 0;
    return 1;
}

/* ---- blocking shots ---- */

/* NHL 95 Genesis's CanBlock: a computer skater in his own zone, the other team's carrier winding up, standing in the line from his
 * goal to the puck on its side of the ice within $B2, facing it (95G's own test of the facings next to it masks with 3) */
static int canblock_g(const struct SortObj *o)
{
    if (PFLAG(o, pfalock)) return 0;
    if (!(g.sflags & HI(sfssdir))) M.flags &= (uint8_t)~4;
    if (M.flags & 4) return 0;
    int16_t y = o->Ypos;
    if (PFLAG(o, pfgoal)) y = (int16_t)-y;
    if (y < 0x56 || puckc() < 0) return 0;
    if (!((sortobj(puckc())->pflags ^ o->pflags) & (1 << pfteam))) return 0;
    if (!(g.sflags & HI(sfssdir))) return 0;
    uint16_t to = vtoa((int16_t)(puckx() - o->Xpos), (int16_t)(pucky() - o->Ypos));
    int16_t gy = PFLAG(o, pfgoal) ? -0x10B : 0x10B;
    uint16_t d0 = vtoa(puckx(), (int16_t)(pucky() - gy));
    if (d0 != to) return 0;
    if ((int16_t)(o->Xpos ^ puckx()) < 0) return 0;
    if (abs(pucky() - o->Ypos) > 0xB2) return 0;
    if (d0 == (uint16_t)o->facedir) return 1;
    d0 = (uint16_t)((d0 + 1) & 3);
    if (d0 == (uint16_t)o->facedir) return 1;
    d0 = (uint16_t)((d0 - 2) & 3);
    return d0 == (uint16_t)o->facedir;
}

static int shooting(uint16_t spa)
{
    return spa == SPAshotf || spa == SPAshotb || spa == MA_SPA(MA_FAKEF_PC) || spa == MA_SPA(MA_FAKEB_PC)
        || spa == MA_SPA(MA_FAKEF_G) || spa == MA_SPA(MA_FAKEB_G);
}

/* NHL 95 PC's CanBlockShot: may p go down to block a shot? The puck $4E-$E8 out, within $50 of him and coming at his goal, the shooter
 * (the carrier, or the last shooter while it is loose) in a shot; p between the puck and his goal, nearer the goal than it, facing
 * it (a computer player not straight beside the net, $80 deep, within $64 of the middle). 0, or the dive: $A (straight) or the way
 * + 1, the way the slide goes in *slide */
static int canblockshot(const struct SortObj *p, int *slide)
{
    int pc = puckc();
    if (pc >= 0 && (pc < 6) == (p->SCnum < 6)) return 0;
    int e = abs(ppy());
    if (e > 0xE8 || e < 0x4E) return 0;
    int pd = dist(ppx() - PX(p), ppy() - PY(p));
    if (pd > 0x50) return 0;
    int side = PFLAG(p, pfgoal) != 0;
    const struct SortObj *net = sortobj(12 + side);
    if ((int16_t)g.puckcross[side].frames < 10) return 0;
    int k;
    if (pc >= 0) k = pc;
    else {
        if (side ? (int16_t)g.puckvy > 0 : (int16_t)g.puckvy < 0) return 0;
        int s = (int16_t)g.shotplayer;
        if (s < 0 || !((p->SCnum < 6) ^ (s < 6))) return 0;
        k = s;
    }
    if (!shooting(sortobj(k)->SPA)) return 0;
    int dx = ppx() - PX(net), y = ppy() - PY(net);
    int far = dist(dx, y), dir = vtoa((int16_t)dx, (int16_t)y);
    y = PY(p);
    if (!(side && y < -0x4E && y > -0xE8 && y < ppy()))
        if (side || y <= 0x4E || y >= 0xE8 || y <= ppy()) return 0;
    dx = PX(p) - PX(net);
    y -= PY(net);
    int near = dist(dx, y), a = vtoa((int16_t)dx, (int16_t)y);
    int joy = PFLAG(p, pfjoycon) != 0;
    if (!joy && (a == 2 || a == 6 || abs(PY(p)) < 0x80 || abs(PX(p)) > 0x64)) return 0;
    int to = vtoa((int16_t)(ppx() - PX(p)), (int16_t)(ppy() - PY(p)));
    int b = (to - p->facedir + 2) & 7;
    a = (dir - a + 2) & 7;
    if (((to - dir + 1) & 7) > 2) return 0;
    if (!(joy && b <= 4 && a <= 4)) {
        if (joy || b < 1 || a < 1 || b > 3 || a > 3) return 0;
    }
    if (near >= far || near + pd >= far + far) return 0;
    if (b == 2 && a == b) return 10;
    *slide = dir;
    return a + 1;
}

/* the PC's slide speeds by facing (X, Y; game_saveload.asm blockspd) */
static const int16_t blockspd[8][2] = { { 0x1092, 0 }, { 0x0BB8, -0x0BB8 }, { 0, -0x1092 }, { -0x0BB8, -0x0BB8 },
                                        { -0x1092, 0 }, { -0x0BB8, 0x0BB8 }, { 0, 0x1092 }, { 0x0BB8, 0x0BB8 } };

static int16_t damp(int16_t v, int16_t add)
{
    if ((add ^ v) < 0) v = (int16_t)(v >> 3);
    else v = (int16_t)(abs(v) > abs(add) ? v >> 1 : v >> 3);
    return (int16_t)(v + add);
}

/* NHL 95 PC's BlockShotDive: turn to the puck; straight at him a block in front, on a knee for a low slow puck; else a slide along
 * the ice the way it goes */
static void blockdive(struct SortObj *p, int dir, int slide)
{
    p->pflags |= 1 << pfalock;
    int to = vtoa((int16_t)(puckx() - p->Xpos), (int16_t)(pucky() - p->Ypos));
    if (to < 8) p->facedir = (int16_t)to;
    uint16_t spa;
    if (dir == 10 || dir == 3)
        spa = MA_SPA((int16_t)g.puckz[0] < 3 && (int16_t)g.puckvz[0] < 0x7D0 ? MA_BLOCKK : MA_BLOCKF);
    else {
        int f = slide & 7, neg = dir < 3;
        p->facedir = (int16_t)f;
        p->Yvel = damp(p->Yvel, (int16_t)(neg ? -blockspd[f][1] : blockspd[f][1]));
        p->Xvel = damp(p->Xvel, (int16_t)(neg ? -blockspd[f][0] : blockspd[f][0]));
        spa = MA_SPA(((p->handed[0] & 1) == 0) ^ neg ? MA_BLOCKL : MA_BLOCKR);
    }
    SetSPA(p, spa);
}

/* NHL 95 PC's OppInReach: an opponent in front of him within $1E (the PC measures the y gap as the x gap again, kept) */
static int opp_in_reach(const struct SortObj *p)
{
    const struct SortObj *q = sortobj(p->SCnum < 6 ? 6 : 0);
    for (int i = 0; i < 6; i++, q++) {
        int dx = PX(q) - PX(p);
        int t = vtoa((int16_t)dx, (int16_t)dx);
        if (((t - p->facedir + 2) & 7) <= 4 && abs(dx) < 0x1E) return 1;
    }
    return 0;
}

/* Holdplayer with no one to hold: 95 PC's hook an opponent in reach, else a block when one can be made; 95G's chkblock, a block while
 * the other team's carrier winds up or just after its shot */
int moves_block_hold(struct SortObj *o)
{
    int r = RULE(RULE_SHOTBLOCK);
    if (r == 1) {
        int slide = 0, d;
        if (opp_in_reach(o)) return 0;
        if (!(d = canblockshot(o, &slide))) return 0;
        blockdive(o, d, slide);
        return 1;
    }
    if (r == 2) {
        int other;
        if (g.sflags & HI(sfssdir)) {
            if (puckc() < 0) return 0;
            other = (sortobj(puckc())->pflags ^ o->pflags) & (1 << pfteam);
        } else {
            if (M.shottimer < 0) return 0;
            other = (M.shotpflags ^ o->pflags) & (1 << pfteam);
        }
        if (!other) return 0;
        SetSPA(o, MA_SPA(MA_BLOCK_G));
        return 1;
    }
    return 0;
}

/* check4check's start (95G): a computer skater blocks when CanBlock lets him, once a shot */
int moves_block_cpu(struct SortObj *o)
{
    if (RULE(RULE_SHOTBLOCK) != 2 || PFLAG(o, pfjoycon) || !canblock_g(o)) return 0;
    SetSPA(o, MA_SPA(MA_BLOCK_G));
    M.flags |= 4;
    o->pflags |= 1 << pfalock;
    return 1;
}

/* check4check's end, nobody to check (95 PC's TryBlockShot): a chance by his defence rating to go down for a block */
void moves_block_try(struct SortObj *o)
{
    if (RULE(RULE_SHOTBLOCK) != 1) return;
    int n = (8 + 15 - o->aidef / 2) / 2;
    if (n < 1) n = 1;
    if (randomd0((uint16_t)n) > 1) return;
    int slide = 0, d = canblockshot(o, &slide);
    if (d) blockdive(o, d, slide);
}

/* puckstick: a PC block's stick turns the puck away (no steal, no catch) */
int moves_blocking(const struct SortObj *o)
{
    int n = ma_of(o->SPA);
    return RULE(RULE_SHOTBLOCK) == 1 && n >= MA_BLOCKL && n <= MA_BLOCKK;
}

/* ---- the state ---- */

size_t moves_state_save(uint8_t *out)
{
    out[0] = M.flags;
    out[1] = (uint8_t)((uint16_t)M.lastpuckc >> 8); out[2] = (uint8_t)M.lastpuckc;
    out[3] = (uint8_t)((uint16_t)M.shottimer >> 8); out[4] = (uint8_t)M.shottimer;
    out[5] = M.shotpflags;
    return MOVES_STATE_SIZE;
}

int moves_state_load(const uint8_t *in)
{
    if (!in) { M.flags = 0; M.lastpuckc = -1; M.shottimer = -1; M.shotpflags = 0; return 0; }
    if (in[0] & 0xF8) return -1;
    M.flags = in[0];
    M.lastpuckc = (int16_t)(in[1] << 8 | in[2]);
    M.shottimer = (int16_t)(in[3] << 8 | in[4]);
    M.shotpflags = in[5];
    return 0;
}
