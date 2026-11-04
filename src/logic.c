/* logic94.asm: player logic. Turns controller input into skater actions (passing,
 * shooting, checking, line changes) and runs the computer players, with an assignment
 * per position and for the goalie, bench and penalty box. Also the puck at faceoffs,
 * offsides and icing, plus the per-frame player update and scroll window. */
#include "game.h"
#include "hal.h"
#include "rom.h"
#include "symbols_gen.h"
#include "view.h"
#include "anim_gen.h"
#include <string.h>
#include "checks_strings_gen.h"
#include "checks_tables_gen.h"
#include "input_strings_gen.h"
#include "input_tables_gen.h"
#include "replay_strings_gen.h"

#define PFLAG(o, b) ((o)->pflags & (1 << (b)))
#define HIB(w) ((int8_t)((uint16_t)(w) >> 8))
#define SET_HIB(f, b) ((f) = (int16_t)(((uint16_t)(uint8_t)(b) << 8) | ((uint16_t)(f) & 0xFF)))
#define TICK_HIB(f) (SET_HIB(f, (int8_t)(HIB(f) - elapsed)), HIB(f))
#define LOB(w) ((int8_t)(w))
#define SET_LOB(f, b) ((f) = (int16_t)(((uint16_t)(f) & 0xFF00) | (uint8_t)(b)))
#define BUT_B 0x10
#define BUT_C 0x20
#define BUT_A 0x40
#define BUT_START 0x80
#define TIMER_SET(w, pad, v) ((w) = (pad) ? (uint16_t)(((w) & 0xFF00) | (uint8_t)(v)) : (uint16_t)(((uint16_t)(uint8_t)(v) << 8) | ((w) & 0xFF)))

/* add.l d,pos: a 16.16 position update */
#define ADD_POS32(o, P, F, d) do { int32_t p_ = POS32((o)->P, (o)->F) + (d); (o)->P = (int16_t)(p_ >> 16); (o)->F = (uint16_t)p_; } while (0)

/* the puck carrier on pad 3 or 4: run doinput with the pad 1 / 2 variables swapped for the pad 3 / 4 ones */
#define FOURWAY_INPUT(CT, CT34, CP, CP34, BH, PW, PW34, AH, PCB, d0, d1, d2, d3, pad, o) do { \
        uint16_t ct_ = g.CT, cp_ = g.CP, bh_ = g.BH, pw_ = g.PW, ah_ = g.AH, pcb_ = g.PCB;   \
        g.CT = g.CT34; g.CP = g.CP34; g.BH = g.bholdtimer34; g.PW = g.PW34; g.AH = g.aholdtimer34; g.PCB = g.PadControlBits34; \
        doinput(d0, d1, d2, d3, pad, o);                                                       \
        g.PadControlBits34 = g.PCB; g.PCB = pcb_; g.aholdtimer34 = g.AH; g.AH = ah_;           \
        g.PW34 = g.PW; g.PW = pw_; g.bholdtimer34 = g.BH; g.BH = bh_;                          \
        g.CP34 = g.CP; g.CP = cp_; g.CT34 = g.CT; g.CT = ct_;                                  \
    } while (0)
static inline int16_t puckx(void) { return (int16_t)g.puckx[0]; }
static inline int16_t pucky(void) { return (int16_t)g.pucky; }
static inline int16_t puckvy(void) { return (int16_t)g.puckvy; }
static inline int16_t puckc(void) { return (int16_t)puck_carrier(); }
static inline struct Team *own_team(const struct SortObj *o) { return team_struct(PFLAG(o, pfteam)); }
static inline struct SortObj *opponents(const struct SortObj *o) { return sortobj(o->SCnum >= 6 ? 0 : 6); }
/* the other team has the puck carrier (puckc - 6 and SCnum - 6 differ in sign) */
static inline int carrier_is_opponent(int16_t carrier, const struct SortObj *o) { return (int16_t)((carrier - 6) ^ (o->SCnum - 6)) < 0; }

/* the common prologue of the positional assignments: nothing while locked in an animation,
 * donothing with the clock stopped, the bench check, nothing for a human; a new assignment
 * resets the timers. Returns 0 when the caller is done. */
static int assignment_prologue(struct SortObj *o)
{
    if (PFLAG(o, pfalock)) return 0;
    if (g.gmode & (1 << gmclock)) { donothing(o); return 0; }
    if (check4bench(o)) return 0;
    if (PFLAG(o, pfjoycon)) return 0;
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        o->temp1 = 0;
        o->temp2 = 8;
    }
    return 1;
}
/* the timer reload from aidef, one less while the crowd record is broken; 1 when it ran out */
static int aidef_timer(struct SortObj *o)
{
    if (TICK_HIB(o->temp1) >= 0) return 0;
    SET_HIB(o->temp1, o->aidef);
    if ((g.sflags7 & 0x40) && HIB(o->temp1) != 0) SET_HIB(o->temp1, HIB(o->temp1) - 1);
    return 1;
}

/* the new player takes over the object: position from newpos, his first assignment */
static void take_new_position(struct SortObj *o)
{
    o->position = (int8_t)o->newpos;
    Setplass(o);
    o->newpnum = 0xFF;
    o->newpos = 0xFF;
}

int16_t elapsed;   /* d7: frames since the last game loop pass */
static inline int16_t puckz(void) { return (int16_t)g.puckz[0]; }
static inline int16_t puckvx(void) { return (int16_t)g.puckvx; }
static inline int16_t puckvz(void) { return (int16_t)g.puckvz[0]; }
static inline struct SortObj *puck_obj(void) { return sortobj(puckscnum); }
static inline struct SortObj *teammates(const struct SortObj *o) { return sortobj(o->SCnum >= 6 ? 6 : 0); }
static inline int32_t sq(int16_t v) { return (int32_t)v * v; }
/* divs.w with the overflow flag */
static int32_t divs_ovf(int32_t dividend, int16_t divisor, int *ovf)
{
    *ovf = 0;
    if (divisor == 0) return dividend;
    int32_t q = dividend / divisor;
    if (q < -32768 || q > 32767) { *ovf = 1; return dividend; }
    return (int32_t)(((uint32_t)(uint16_t)(dividend % divisor) << 16) | (uint16_t)q);
}
/* the goalie variant: aidef / 4, one less with the crowd record broken */
static void goalie_timer_reload(struct SortObj *o)
{
    uint8_t d0 = o->aidef;
    if (d0 != 0 && (g.sflags7 & 0x40))
        d0--;
    SET_HIB(o->temp1, d0 >> 2);
}
/* count the breakaway and excite the crowd (asspuckc, assnearest, breakaway) */
static void breakaway_crowd(struct SortObj *o)
{
    struct Team *tm = own_team(o);
    tm->breakaways++;
    g.CwdExciteLvl += 0x14;
    g.crowdlevel += 0xC8;
}
/* a random point inside skating zone z of a zone table: {x base, x range, y base, y range} */
static void zone_target(const uint16_t *zones, unsigned z, struct SortObj *o)
{
    const uint16_t *e = zones + z * 4;
    o->temp3 = (int16_t)(randomd0s(e[1]) + e[0]);
    o->temp4 = (int16_t)(randomd0s(e[3]) + e[2]);
}

/* place every object for the faceoff / penalty shot: the two nets, the puck shadow, scroll to
 * the spot, reset the benches and lines */
static void reset_ice_objects(void)
{
    struct SortObj *net = sortobj(12);
    net->Xvel = 0; net->Yvel = 0; net->Xpos = 0; net->Ypos = 0x10C;
    net++;
    net->Xvel = 0; net->Yvel = 0; net->Xpos = 0; net->Ypos = -0x10C;
    struct SortObj *sh = sortobj(puckscnum + 1);
    sh->frame = SPFpuck;
    sh->SPA = 0;
    sh->attribute = 0;
    puck_obj()->attribute = 0;
    g.sflags &= (uint16_t)~HI(sfslock);
    for (int i = 0; i <= 0x64; i++) checkwindow();
    g.yleader = 0x3C;
    ResetBench();
}

/* the body of ChkGoalies for one team: d0 = team number, a2 = the team, a1 = the other */
static void chkgoalie(uint16_t d0, struct Team *a2, struct Team *a1)
{
    if ((int16_t)a2->goalie < 0) return;                  /* no goalie */
    int16_t d1 = puckc();
    if (d1 < 0) return;
    d1 = (int16_t)(d1 - 6);
    if (a2 != team_struct(0)) d1 = (int16_t)~d1;
    if (d1 >= 0) return;                                  /* the other team has the puck */
    if (g.gmode & (1 << gmpendel)) {
        ST_W(a2->goalie);                                 /* pull him on the delayed penalty */
        setpersonel(a2);
        return;
    }
    if (d0 == g.cont1team || d0 == g.cont2team) return;
    CPgoalie(pucky(), a1, a2);
}

static void returngoalie(uint16_t d0, struct Team *a2, struct Team *a1)
{
    if (a2->goalie == 0xFFFF) return;                     /* still no goalie */
    a2->goalie &= 0xFF;                                   /* the goalie returns */
    if (d0 == g.cont1team || d0 == g.cont2team) return;
    CPgoalie((int16_t)g.foy, a1, a2);
}

/* one goal line of findpc: a1 = the puckcross entry, d1 = the sideline, d4 = the goal line */
static void findpc_calc(struct PuckCross *a1, int16_t d1, int16_t d4)
{
    int16_t d0 = (int16_t)(d4 - pucky());
    int ovf;
    if (puckvy() != 0) {
        int32_t d2 = (int32_t)d0 * 4096;                  /* (swap and asr #4: d0 << 16 >> 4, exactly; a negative's shift undefined in C) */
        int32_t q = divs_ovf(d2, puckvy(), &ovf);
        if ((int16_t)q >= 0 && !ovf) {
            a1->frames = (uint16_t)q;                     /* frames until it crosses */
            int32_t p = (int32_t)d0 * puckvx();
            q = divs_ovf(p, puckvy(), &ovf);
            if (!ovf) {
                d0 = (int16_t)((int16_t)q + puckx());
                if (!(d0 < d1)) d0 = (int16_t)(-d0 + d1 + d1 + d1);   /* off the boards */
                d1 = (int16_t)-d1;
                if (!(d0 > d1)) d0 = (int16_t)(-d0 + d1 + d1 + d1);
                a1->x = d0;
                return;
            }
        }
    }
    a1->frames = 0xFFFF;
}

/* the bar check of avdgoal: d2 / d3 = y of the player and the target relative to a goal line;
 * crossing it within the net's width moves the target x off the post */
static void avdgoal_ch1(int16_t d0, int16_t d2, int16_t d3, const struct SortObj *o)
{
    if ((int16_t)(d3 ^ d2) >= 0) return;
    int32_t d4 = (int32_t)(int16_t)(d0 - o->Xpos) * d2;
    d2 = (int16_t)(d2 - d3);
    d4 = (int16_t)m68k_divs(d4, d2);
    d4 = (int16_t)(d4 + o->Xpos);
    if (d4 > 0x50) return;
    if (d4 < -0x50) return;
    int16_t w = 0x50;
    int neg = d4 != 0 ? d4 < 0 : o->Xpos < 0;
    if (neg) w = (int16_t)-w;
    g.deltax = (uint16_t)(w - d4);
}
/* the B / A hold timers hold pad 1 in the high byte and pad 2 in the low byte */
static inline uint8_t timer_get(uint16_t w, int16_t pad) { return pad ? (uint8_t)w : (uint8_t)(w >> 8); }
/* subq.b #1 then clamp at 0 */
static inline uint8_t timer_dec(uint8_t v) { int8_t r = (int8_t)(v - 1); return r < 0 ? 0 : (uint8_t)r; }
static inline uint16_t goaliemode(int16_t pad) { return pad ? g.goaliemode2 : g.goaliemode1; }

/* the assignment dispatch table (data94.asm:asstab), indexed by asslist[assnum] */
typedef void (*assfn)(struct Team *tm, struct SortObj *o);
static void ass_none(struct Team *tm, struct SortObj *o) { (void)tm; (void)o; }       /* rtss2 */
static void ass_puckunflip(struct Team *tm, struct SortObj *o) { (void)tm; puckunflip(o); }
static const assfn asstab[0x24] = {
    ass_none, assdefo, assdefd, asswingd, asswingo, asscenterd, asscentero, assscore,           /* 0-7 */
    assstanley, asseben, assepen, assbench, asspenalty, assdopen, assgoaliecpu, assgoalietopuck, /* 8-$F */
    asspuckc, assnearest, assshoot, asspassrec, assfight, assfwatch, assfaceoff, assfaceoffpl,  /* $10-$17 */
    pucknorm, puckshadow, ass_puckunflip, puckfaceoff, puckfaceoff2, assgoaliectrl, puckshootout, puckpenshot, /* $18-$1F */
    assgoaliebreakwait, chkpuckc, assbreakaway, assonetimer,                                    /* $20-$23 */
};

/* a player in a board / bench check animation is held at the boards */
static void hold_at_boards(struct SortObj *o)
{
    uint16_t spa = o->SPA;
    int16_t fx = (int16_t)g.FallXPos;
    int outside = fx > 0x56 || !(fx > -0x56);
    if (spa == SYM_SPAboardfront) o->Ypos = outside ? 0x116 : 0x124;
    else if (spa == SYM_SPAboardback) o->Ypos = (int16_t)(outside ? 0xFEEA : 0xFEDC);
    else if (spa == SYM_SPAboardright) o->Xpos = (o->attribute & HI(3)) ? (int16_t)0xFF7E : 0x82;
    else if (spa == SYM_SPAbenchcheckr) o->Xpos = (o->attribute & HI(3)) ? (int16_t)0xFF78 : 0x88;
    else if (spa == SYM_SPAboardleft) o->Xpos = (o->attribute & HI(3)) ? 0x82 : (int16_t)0xFF7E;
    else if (spa == SYM_SPAbenchcheckl) o->Xpos = (o->attribute & HI(3)) ? 0x88 : (int16_t)0xFF78;
}

/* sub.b d7 from a byte timer, stopping at 0 */
static uint8_t tick_byte(uint8_t v)
{
    uint8_t r = (uint8_t)(v - (uint8_t)elapsed);
    return (r & 0x80) ? 0 : r;
}

/* logic94.asm:updateplayers. The per-frame update of all 16 objects: puck height settling, the
 * crowd meter record bit, then for each object the old position copy, the fall-at-the-boards hold,
 * updateanim, the no-puck timers, time on ice, deceleration and movement (with the bounce sound
 * when the puck lands), the puck carrier's pad, the human controllers' input (doinput), the
 * assignment from asstab, collisions and the impact decay. d7 = elapsed frames. */
uint32_t assign_d2 = ASSIGN_D2_START;

void updateplayers(void)
{
    if (!(g.sflags & HI(sfhor))) {
        g.PadControlBits |= 0xF;
        if ((int16_t)g.puckz[0] > 5) {
            if (g.TmpPuckZ != g.puckz[0]) {
                g.TmpPuckZ = g.puckz[0];
                g.PuckZCntr = 0;
            } else if ((int16_t)++g.PuckZCntr >= 0x10) {
                g.PuckZCntr = 0;
                g.puckz[0]--;
            }
        } else {
            g.PuckZCntr = 0;
        }
    }
    g.sflags7 &= (uint8_t)~0x40;                                  /* the crowd meter record bit */
    if (!((int16_t)g.CurCrowdMeter < (int16_t)g.CrowdRecord)) g.sflags7 |= 0x40;
    SetSflags8Bit1();
    if ((int16_t)puck_carrier() >= 0) {
        g.sflags8 &= (uint8_t)~0x80;
        g.sflags6 &= (uint16_t)~HI(1);
        if (sortobj(puck_carrier())->pflags3 & (1 << pf3break)) g.gmode2 &= (uint16_t)~HI(4);
    }
    for (int i = 0; i < 16; i++) {
        struct SortObj *o = sortobj(i);
        o->OldXpos = o->Xpos; o->OldXfrac = o->Xfrac;
        o->OldYpos = o->Ypos; o->OldYfrac = o->Yfrac;
        o->OldZpos = o->Zpos; o->OldZfrac = o->Zfrac;
        if (o->pflags3 & (1 << pf3fall)) {
            o->Xvel = 0;
            o->Yvel = 0;
            if (o->SPAnum == 0) PlaceBoardFall(o);
            else hold_at_boards(o);
        }
        if (o->position >= 0) {                                   /* on the ice */
            if (o->SCnum != (int16_t)puck_carrier()) o->pflags3 &= (uint8_t)~((1 << pf3break) | (1 << pf3oside));
            updateanim(o);
            o->nopuck[0] = tick_byte(o->nopuck[0]);
            o->nopuck[1] = tick_byte(o->nopuck[1]);
            if ((g.sflags3 & 0x40) && !(o->pnum & 0x80)) {       /* the penalty timer is up: time on ice */
                struct Team *tm = loadTeamStruct(o, NULL);
                tm->ptoi[o->pnum]++;
            }
            if (o->SPA != SYM_SPAinjuryfall && o->SPA != SYM_SPAinjury1) {
                uint16_t d4 = (uint16_t)((g.PalMode ? 0x16 : 0x11) * (uint16_t)elapsed);
                if (o->Zpos == 0) {                               /* deceleration on the ice */
                    int d2 = (o->pflags & (1 << pfdoff)) ? 9 : 6;
                    assign_d2 = (uint32_t)d2;
                    int puck = i == puckscnum && rules[RULE_PUCK_SLIDE] != 100;   /* (rules: the puck's slide) */
                    int16_t d0 = o->Xvel;
                    if (d0 != 0) {
                        d0 = (int16_t)(d0 >> d2);
                        if (puck) d0 = rule_puck_slowing(d0);
                        if (d0 == 0) d0 = 1;
                        o->Xvel = (int16_t)(o->Xvel - d0);
                    }
                    d0 = o->Yvel;
                    if (d0 != 0) {
                        d0 = (int16_t)(d0 >> d2);
                        if (puck) d0 = rule_puck_slowing(d0);
                        if (d0 == 0) d0 = 1;
                        o->Yvel = (int16_t)(o->Yvel - d0);
                    }
                }
                if (o->Xvel != 0) ADD_POS32(o, Xpos, Xfrac, (int32_t)o->Xvel * (int16_t)d4);
                if (o->Yvel != 0) ADD_POS32(o, Ypos, Yfrac, (int32_t)o->Yvel * (int16_t)d4);
                if (o->Zpos >= 0 && (o->Zpos != 0 || o->Zvel != 0)) {   /* in the air: gravity */
                    d4 = (uint16_t)(d4 << 1);
                    o->Zvel = (int16_t)(o->Zvel - d4);
                    d4 = (uint16_t)(d4 << 1);
                    o->Zvel = (int16_t)(o->Zvel - d4);
                    d4 = (uint16_t)(d4 >> 2);
                    int32_t p = POS32(o->Zpos, o->Zfrac) + (int32_t)o->Zvel * (int16_t)d4;
                    o->Zpos = (int16_t)(p >> 16);
                    o->Zfrac = (uint16_t)p;
                    if (p < 0) {                                  /* landed: bounce */
                        o->Zpos = 0;
                        o->Zfrac = 0;
                        o->Zvel = (int16_t)(-o->Zvel);
                        o->Zvel = (int16_t)(o->Zvel >> 1);
                        uint8_t b = (uint8_t)(5 - (uint8_t)(o->Zvel >> 8));
                        if (b & 0x80) b = 0;
                        if (b <= 3) sfx((int16_t)(SYM_SFXpuckice + b));
                    }
                }
            }
            int16_t d6 = o->SCnum;
            if (d6 == (int16_t)puck_carrier() && !(o->pflags2 & (1 << pf2fight)) && !(g.sflags & HI(sfhor)))
                setpads(-2, o);
            if (!(g.sflags2 & HI(sf2faceoff)) && g.holdreset != 0) {
                g.holdreset--;
                if ((int16_t)g.holdreset <= 0) {
                    g.holdreset = 0;
                    g.bholdtimer = 0x1111;
                    g.bholdtimer34 = 0x1111;
                }
            }
            uint16_t d0, d1, d2, d3;
            if (d6 == (int16_t)g.c1playernum) {
                ReadJoy1(&d0, &d1, &d2, &d3);
                assign_d2 = d2;                                   /* (doinput's own d2 not followed) */
                g.joypuckcarrier = 0xFFFF;
                doinput(d0, d1, d2, d3, 0, o);
            } else if (d6 == (int16_t)g.c2playernum) {
                ReadJoy2(&d0, &d1, &d2, &d3);
                assign_d2 = d2;
                g.joypuckcarrier = 0xFFFF;
                doinput(d0, d1, d2, d3, 2, o);
            }
            if (d6 <= 0xB) {                                      /* pads 3 and 4 of the four way play */
                if (g.cont3team != 0 && d6 == (int16_t)g.c3playernum) {
                    ReadJoy3(&d0, &d1, &d2, &d3);
                    assign_d2 = d2;
                    g.joypuckcarrier = 0;
                    FOURWAY_INPUT(cont1team, cont3team, c1playernum, c3playernum, bholdtimer, padword1, padword3, aholdtimer, PadControlBits, d0, d1, d2, d3, 0, o);
                }
                if (g.cont4team != 0 && d6 == (int16_t)g.c4playernum) {
                    ReadJoy4(&d0, &d1, &d2, &d3);
                    assign_d2 = d2;
                    g.joypuckcarrier = 1;
                    FOURWAY_INPUT(cont2team, cont4team, c2playernum, c4playernum, bholdtimer, padword2, padword4, aholdtimer, PadControlBits, d0, d1, d2, d3, 2, o);
                }
            }
            struct Team *tm = loadTeamStruct(o, NULL);
            asstab[o->asslist[o->assnum]](tm, o);
            o->Wallcos = 0;
            o->Wallsin = 0;
            int16_t x = o->Xpos, y = o->Ypos;
            if (x != o->OldXpos || y != o->OldYpos) checkcoll(x, y, o);
            o->impact = (int16_t)(o->impact - elapsed * 2);
            if (o->impact < 0) o->impact = 0;
        }
        o->limpact = o->impact;
    }
}

/* logic94.asm:updateanim. Frame switch control on object a3: step SPAcnt by the elapsed frames,
 * advance SPAnum through the animation (SPAlist entry SPA, frame list by facing direction) and
 * pick the frame, with a 4 frame de-glitch minimum between frame switches. */
void updateanim(struct SortObj *o)
{
    if (o->SPA == 0) {
        o->pflags &= (uint8_t)~(1 << pfalock);
        o->pflags2 &= (uint8_t)~(1 << pf2aip);
        return;
    }
    const uint8_t *a0 = rom_ptr(SYM_SPAlist + o->SPA);
    uint16_t d1 = rd16(a0 + 0x10);                                /* animation attributes: bit 0 loops */
    int16_t d0 = o->facedir;
    if ((g.sflags & HI(sfhor)) && o->SCnum < 0xC) d0 = (int16_t)((d0 - 2) & 7);
    if (o->attribute & HI(3)) d0 = (int16_t)((-d0 + 8) & 7);
    a0 += (int16_t)rd16(a0 + d0 * 2);                             /* the frame list for this direction */
    d0 = (int16_t)o->SPAnum;
    uint16_t d2 = rd16(a0 + d0);                                  /* the frame at this index */
    int newtime = (int16_t)o->SPAcnt < 0;
    if (!newtime) {
        o->SPAcnt = (uint16_t)(o->SPAcnt - elapsed);
        if ((int16_t)o->SPAcnt < 0) {
            o->SPAnum += 4;
            d0 = (int16_t)(d0 + 4);
            if ((int16_t)rd16(a0 + d0 - 2) < 0) {                 /* the last frame's time is negative */
                d0 = 0;
                o->SPAnum = 0;
                o->pflags &= (uint8_t)~(1 << pfalock);
                o->pflags2 &= (uint8_t)~(1 << pf2aip);
                o->pflags3 &= (uint8_t)~(1 << pf3fall);
                if (!(d1 & 1)) o->SPA = 0;
            }
            newtime = 1;
        }
    }
    if (newtime) {
        int16_t t = (int16_t)rd16(a0 + d0 + 2);
        if (t < 0) t = (int16_t)-t;
        o->SPAcnt = (uint16_t)t;
    }
    o->glitch = (uint8_t)(o->glitch - (uint8_t)elapsed);
    if (o->glitch & 0x80) {
        o->glitch = 0;
        if (d2 != o->frame) {
            o->frame = d2;
            o->glitch = 4;
        }
    }
}

/* logic94.asm:freezewindow. Lock the view at the current position. */
void freezewindow(void)
{
    g.yc1 = g.Vpos;
    g.xc1 = g.Hpos;
    g.sflags |= HI(sfslock);
}

/* logic94.asm:checkwindow. Move Hpos / Vpos toward the puck (or the puck carrier, leading
 * toward the goal he attacks): the target yc1 / xc1 is followed with a 1/16 step when it leaves
 * a dead zone around the view, clamped to the rink. */
void checkwindow(void)
{
    int16_t d2 = (int16_t)g.yc1, d3 = (int16_t)g.xc1;
    if (!(g.sflags & HI(sfslock))) {
        const struct SortObj *o = sortobj(0xE);                   /* the puck */
        if ((int16_t)puck_carrier() >= 0) {
            o = sortobj(puck_carrier());
            int16_t d0 = (int16_t)(elapsed * 2);
            if (o->pflags & (1 << pfgoal)) {
                g.yleader = (uint16_t)(g.yleader + d0);
                if (!((int16_t)g.yleader < 0x32)) g.yleader = 0x32;
            } else {
                g.yleader = (uint16_t)(g.yleader - d0);
                if (!((int16_t)g.yleader > -0x32)) g.yleader = (uint16_t)-0x32;
            }
        }
        d2 = (int16_t)((o->Yvel >> 7) + o->Ypos + (int16_t)g.yleader);
        d3 = o->Xpos;
        g.yc1 = (uint16_t)d2;
        g.xc1 = (uint16_t)d3;
    }
    int16_t d0 = (int16_t)(d2 - (int16_t)g.Vpos), d1;
    int move = 1;
    if (d0 >= -0xA) {
        if (d0 <= 0xA) move = 0;
        else {
            d1 = (int16_t)(d2 - 0xA);
            if (!(d1 < 0x100)) d1 = 0x100;
        }
    } else {
        d1 = (int16_t)(d2 + 0xA);
        if (!(d1 > -0xC8)) d1 = -0xC8;
    }
    if (move) {
        d1 = (int16_t)(d1 - (int16_t)g.Vpos);
        if (d1 != 0) {
            d1 = (int16_t)(d1 >> 4);
            if (d1 == 0) d1 = 1;
            g.Vpos = (uint16_t)(g.Vpos + d1);
        }
    }
    d0 = (int16_t)(d3 - (int16_t)g.Hpos);
    move = 1;
    if (d0 >= -0x28) {
        if (d0 <= 0x28) move = 0;
        else {
            d1 = (int16_t)(d3 - 0x28);
            if (!(d1 <= view_hpos())) d1 = view_hpos();   /* (the wide view's rink: less far) */
        }
    } else {
        d1 = (int16_t)(d3 + 0x28);
        if (!(d1 >= -view_hpos())) d1 = (int16_t)-view_hpos();
    }
    if (move) {
        d1 = (int16_t)(d1 - (int16_t)g.Hpos);
        if (d1 != 0) {
            d1 = (int16_t)(d1 >> 4);
            if (d1 == 0) d1 = 1;
            g.Hpos = (uint16_t)(g.Hpos + d1);
        }
    }
}

/* logic94.asm:doinput. Process controller input for player a3: d0 = dpad, d1 = new buttons,
 * d2 = changed buttons, d3 = held buttons, d4 = controller 0 / 2 (d6 = his SCnum). B+C
 * together mark a one-timer; start pauses; then the faceoff, line change, one-timer, puck
 * carrier, player switch (B) and goalie branches. */
void doinput(uint16_t d0, uint16_t d1, uint16_t d2, uint16_t d3, int16_t d4, struct SortObj *o)
{
    if ((d1 & BUT_B) && (d1 & BUT_C)) {
        d1 &= (uint16_t)~BUT_B;
        o->pflags3 |= 0x40;
    } else {
        if (d1 & BUT_B) o->pflags3 &= (uint8_t)~0x40;
        if (d2 & BUT_B) {
            int was = o->pflags3 & 0x40;
            o->pflags3 &= (uint8_t)~0x40;
            if (was) {
                TIMER_SET(g.bholdtimer, d4, 0x11);
                d2 &= (uint16_t)~BUT_B;
            }
        }
    }
    if ((g.gmode2 & HI(7)) && (uint8_t)d0 != 8) ShortenMsgTimer(o);
    g.TempWord1 = d0 & 0xF;
    setpads(d4, o);
    if (d1 & BUT_START) {
        if ((int16_t)g.joypuckcarrier < 0) { if (d4 == 0) startpause1(); else startpause2(); }
        else { if (g.joypuckcarrier == 0) startpause3(); else startpause4(); }
        return;
    }
    if ((g.sflags & HI(sfhor)) && !(d0 & 8)) d0 = (uint16_t)((d0 + 2) & 7);
    if (g.sflags2 & HI(sf2faceoff)) { faceoffinput(d0, d1, o); return; }
    if (o->pflags2 & 8) { lineinput(d0, d1, d2, o); return; }
    if (!PFLAG(o, pfjoycon)) return;
    if ((uint16_t)o->SCnum == g.lastplayer && (int16_t)g.passplayer >= 0 && (int16_t)g.onetimerplayer < 0 && (d1 & BUT_C)) {
        struct SortObj *rcv = sortobj((int16_t)g.passplayer);   /* C after a pass: the receiver one-times it */
        if (rcv->position != 0 && !PuckOnAttackHalf(rcv) && !PFLAG(rcv, pfjoycon) && !(rcv->pflags3 & (1 << pf3onetimer))) {
            g.inputjoy = (uint16_t)d4;
            g.TmpJoyPuckCarrier = g.joypuckcarrier;
            assreplace(0x23, rcv);                        /* assonetimer */
            return;
        }
    }
    if (!(o->pflags3 & (1 << pf3onetimer)) && PFLAG(o, pfalock)) { doinput_islocked(d1, d4, o); return; }
    if (o->pflags2 & 1) return;                           /* fightinput: nothing */
    if (puckc() == o->SCnum) { doinput_ispc(d0, d1, d2, d3, d4, o); return; }
    if (o->position != 0 && !(o->pflags3 & (1 << pf3onetimer)) && (d1 & BUT_A)) { Holdplayer(o); return; }
    if (o->position != 0) {
        if (g.BA_PS_flags & HI(2)) goto noswitch;
    } else if (!(g.sflags5 & 0x40) && goaliemode(d4) != 0 && o->SCnum != puckc()) {
        changeplayer((uint16_t)d4, o);                       /* the goalie plays himself: switch off him */
        return;
    }
    /* B held / pressed / released: switch player, or (held long) take the goalie */
    if ((d3 & BUT_B) && g.holdreset == 0) {
        uint8_t t = timer_get(g.bholdtimer, d4);
        if (t == 0) goto noswitch;
        t = timer_dec(t);
        TIMER_SET(g.bholdtimer, d4, t);
        if (d4 != 0) {
            if (t != 0) goto bpress;
            if (goaliemode(d4) != 0) goto bpress;
            goto goalie;
        }
        if (goaliemode(d4) != 0) goto bpress;
        if (t == 0) goto goalie;
    }
bpress:
    if (d1 & BUT_B) {
        TIMER_SET(g.bholdtimer, d4, 0x11);
        goto noswitch;
    }
    if (!(d2 & BUT_B)) goto noswitch;
    if (d3 & BUT_B) goto noswitch;
    {
        uint16_t t = g.bholdtimer;                        /* released: a short press switches */
        TIMER_SET(g.bholdtimer, d4, 0x11);
        t &= d4 ? 0xFF : 0xFF00;
        if (t != 0 || goaliemode(d4) != 0) { changeplayer((uint16_t)d4, o); return; }
    }
goalie:
    if (g.holdreset != 0) { changeplayer((uint16_t)d4, o); return; }
    {
        int16_t gk = getGoalieSCnum(o->SCnum <= 5 ? 5 : 0xB);
        if (gk < 0) return;
        if (PFLAG(sortobj(gk), pfjoycon)) return;
        if (d4 == 0) setc1player((uint16_t)gk); else setc2player((uint16_t)gk);
        return;
    }
noswitch:
    if (o->position != 0) { doinput_onetimer(d1, d4, o); return; }
    if (!(d1 & BUT_A)) { doinput_cbut(d1, d3, o); return; }
    d0 = g.TempWord1;
    if ((uint8_t)d0 == 8) { doinput_cbut(d1, d3, o); return; }
    o->facedir = (int16_t)d0;                             /* goalie dive (A + direction) */
    o->nopuck[0] = 8;
    SetSPA(o, SPAgdive);
    o->pflags2 |= 2;
    o->pflags |= 1 << pfalock;
    g.crowdlevel += 0x96;
}

/* logic94.asm:doinput_cbut. C button (bit 5) for the goalie: a press reads the shot and
 * starts a save (goaliesave); held, it holds the save pose (SPAcnt). The shot reading repeats
 * checkanim's arithmetic for nothing but its Sroot call. */
void doinput_cbut(uint16_t d1, uint16_t d3, struct SortObj *o)
{
    if (!(d1 & BUT_C)) {
        if (d3 & BUT_C) {
            if (!(o->pflags2 & 0x80)) { doinput_chkanim(o); return; }
            if ((int8_t)(o->SPAnum & 0xFF) < 0) o->SPAcnt = 0xA;
            return;
        }
        o->pflags2 &= (uint8_t)~0x80;
        doinput_chkanim(o);
        return;
    }
    if (PFLAG(o, pfalock)) { doinput_chkanim(o); return; }
    o->facedir = (int16_t)vtoa((int16_t)(puckx() - o->Xpos), (int16_t)(pucky() - o->Ypos));
    g.BA_PS_flags &= (uint16_t)~HI(0);
    {
        int16_t d0 = (int16_t)(((g.gameclock & 7) << 4) + 0xA0);
        if (pucky() > 0xDB || pucky() <= -0xDB) d0 = (int16_t)(d0 - 0x40);
        int16_t dy = d0;
        d0 = (int16_t)((int16_t)(((int32_t)d0 * puckvx()) >> 16) + puckx());
        dy = (int16_t)((int16_t)(((int32_t)dy * puckvy()) >> 16) + pucky());
        ClampTargetY(&d0, &dy, (int16_t)d3, o);           /* d3 is the held buttons here: dead values */
        if ((uint32_t)(sq(d0) + sq(dy)) > 0x384)
            Sroot((uint32_t)(sq(d0) + sq(dy)));
    }
    if (o->pflags2 & 2) { doinput_chkanim(o); return; }
    o->pflags2 |= 2;
    const struct PuckCross *pc = &g.puckcross[0];
    int16_t line = 0x108;
    if (PFLAG(o, pfgoal)) { line = (int16_t)-line; pc = &g.puckcross[1]; }
    goaliesave(pc, line, o);
}

/* logic94.asm:ClampTargetY. Clamp the target y d1 for player a3 to the goal line d3 (d0
 * cleared when a3 has the puck). */
void ClampTargetY(int16_t *d0, int16_t *d1, int16_t d3, const struct SortObj *o)
{
    if (puckc() == o->SCnum) *d0 = 0;
    if (*d1 >= 0x103) *d1 = 0x103;
    if (*d1 <= -0x103) *d1 = -0x103;
    *d1 = (int16_t)(*d1 - d3);
}

/* logic94.asm:doinput_chkanim. Nothing while a3's animation runs; a goalie stops at the
 * edges of his crease area, else skate (doplayeracc). */
void doinput_chkanim(struct SortObj *o)
{
    if (o->pflags2 & 2) return;
    if (o->position != 0) { doplayeracc(g.TempWord1, o); return; }
    g.BA_PS_flags &= (uint16_t)~HI(1);
    if (o->Xpos > 0x24 && o->Xvel > 0) { g.BA_PS_flags |= HI(1); o->Xvel = 0; }
    if (o->Xpos < -0x24 && o->Xvel < 0) { g.BA_PS_flags |= HI(1); o->Xvel = 0; }
    if (o->Ypos <= 0xE7 && o->Ypos > -0xE7) {
        if ((int16_t)(o->Ypos ^ o->Yvel) < 0 && o->Yvel != 0) { g.BA_PS_flags |= HI(1); o->Yvel = 0; }
    }
    if (g.BA_PS_flags & HI(1)) return;
    doplayeracc(g.TempWord1, o);
}

/* logic94.asm:doinput_onetimer. a3 is set for a one-timer: C (bit 5) starts the one-timer
 * shot (a pass from a skater, puck loose on the attack half), else a burst or skating. */
void doinput_onetimer(uint16_t d1, int16_t d4, struct SortObj *o)
{
    uint16_t d0 = g.TempWord1;
    if (o->pflags3 & (1 << pf3onetimer)) return;
    if (!(d1 & BUT_C)) { doplayeracc(d0, o); return; }
    if (sortobj((int16_t)g.lastplayer)->position == 0) { burst(o); return; }
    if ((uint16_t)o->SCnum != g.passplayer) { burst(o); return; }
    if (puckc() >= 0) { burst(o); return; }
    if (PuckOnAttackHalf(o)) { burst(o); return; }
    g.inputjoy = (uint16_t)d4;
    g.TmpJoyPuckCarrier = g.joypuckcarrier;
    assreplace(0x23, o);                                  /* assonetimer */
}

/* logic94.asm:doinput_ispc. a3 has the puck: pass mode, shot mode, or start a pass / shot
 * by the buttons (B pass, A held = line change, C shot). */
void doinput_ispc(uint16_t d0, uint16_t d1, uint16_t d2, uint16_t d3, int16_t d4, struct SortObj *o)
{
    checkob(o);
    if (o->position == 0 && (o->pflags2 & 2)) return;
    if (g.sflags & HI(sfspdir)) { passmode(d0, d2, o); return; }
    if (g.sflags & HI(sfssdir)) { ShotMode(d0, d2, o); return; }
    if (d1 & BUT_B) { CountButtonPress(d4, o); setpassmode(o); return; }
    if (d1 & BUT_A) TIMER_SET(g.aholdtimer, d4, 0xF);
    if (d3 & BUT_A) {
        uint8_t t = timer_dec(timer_get(g.aholdtimer, d4));
        TIMER_SET(g.aholdtimer, d4, t);
        if (t == 0) {                                     /* A held: line change with a pass */
            g.sflags5 |= 8;
            setpassmode(o);
            SetLCmode(o);
            return;
        }
    } else if (d2 & BUT_A) {                              /* A released: a pass */
        g.sflags5 |= 8;
        setpassmode(o);
        return;
    }
    if (o->position == 0) { doplayeracc(d0, o); return; }
    if (!(d1 & BUT_C)) { doplayeracc(d0, o); return; }
    CountButtonPress(d4, o);
    SetShotMode(o);
}

/* logic94.asm:doinput_islocked. a3 is locked in an animation: B (bit 4) changes the
 * controlled player, who then has to let go of B (pflags3 bit 6). */
void doinput_islocked(uint16_t d1, int16_t d4, struct SortObj *o)
{
    if (puckc() == o->SCnum) return;
    if (!(d1 & BUT_B)) return;
    if (g.sflags5 & 0x40) return;
    changeplayer((uint16_t)d4, o);
    int16_t d0 = (int16_t)(d4 ? g.c2playernum : g.c1playernum);
    if (d0 >= 0) sortobj(d0)->pflags3 |= 0x40;
}

/* logic94.asm:getGoalieSCnum. d0 = SCnum of the goalie in the 6 sort objects up to object
 * d0, -1 if none. */
int16_t getGoalieSCnum(int16_t last)
{
    const struct SortObj *a0 = sortobj(last);
    for (int i = 0; i < 6; i++, a0--)
        if (a0->position == 0) return a0->SCnum;
    return -1;
}

/* logic94.asm:faceoffinput. Controller processing for the faceoff: the dpad goes to fodir1 /
 * fodir2 (the pull), B swings early (SPAfaceoff) else the normal swing. */
void faceoffinput(uint16_t d0, uint16_t d1, struct SortObj *o)
{
    if (g.sflags5 & 0x40) return;
    if (o->asslist[o->assnum] != 0x17) return;           /* not a faceoff player */
    if (PFLAG(o, pfgoal)) g.fodir2 = d0; else g.fodir1 = d0;
    if (o->pflags2 & (1 << pf2aip)) return;
    if (d1 & BUT_B) {
        o->pflags2 |= 2;
        SetSPA(o, SPAfaceoff);
        return;
    }
    SetSPA(o, SPAfaceoffr);
}

/* logic94.asm:SetLCmode. Initiate the line change option if available (line changes on, the
 * box not already up for his team), then draw the box. */
void SetLCmode(struct SortObj *o)
{
    if (g.OptLine != 0) return;
    if (g.sflags7 & 0x10) return;
    struct Team *tm = loadTeamStruct(o, NULL);
    int was = tm->flags & 2;
    tm->flags |= 2;
    if (was) return;
    if (!(g.sflags5 & 8)) g.sflags &= (uint16_t)~HI(sfspdir);
    g.sflags &= (uint16_t)~HI(sfssdir);
    o->pflags2 |= 8;
    SetLCmode2(tm);
}

/* logic94.asm:SetLCmode2. a2 = team struct. Draw the line change box: the frame, the three
 * line choices with their energy bars (the current line while the choice is pending), and
 * the team name. */
void SetLCmode2(struct Team *tm)
{
    uint16_t d0, d1;
    setlccords(tm, &d0, &d1);
    if ((int16_t)g.printy >= 0xF)
        g.sflags3 |= 1 << sf3llcs;
    else {
        box();
        setlccords(tm, &d0, &d1);
    }
    Framer(d0, d1);
    g.printy -= 2;
    g.printx += 1;
    for (int16_t d4 = 2; d4 >= 0; d4--) {
        int16_t line = getlchoice(d4, tm);
        if (line < 0) continue;
        if (!(tm->flags & 2)) {
            if ((uint16_t)d4 != tm->lchoice) { g.printy -= 1; continue; }
            line = (int16_t)tm->line;
        }
        uint8_t *mes = g.mesarea;                             /* "A " .. "C " */
        wr32(mes, 0x00044120);
        mes[2] = (uint8_t)(mes[2] + d4);
        print(ram_addr(mes));
        Fprintsmall(SYM_linelist, (uint16_t)line);
        linebar((uint16_t)line, tm);
        g.printx -= 5;
        g.printy -= 1;
    }
    g.printx += 2;
    print_p(roster_text(&rosters_team(tm->team)->abbr));                /* the team's abbreviation */
}

/* logic94.asm:setlccords. Set printx / printy to the coordinates of the line change box of
 * team a2 (the home side flips with gmode's gmdir). Returns d0 = 9 (width), d1 = 6 or 5. */
void setlccords(struct Team *tm, uint16_t *d0, uint16_t *d1)
{
    uint16_t y = 0;
    if (tm == team_struct(0)) y ^= 0x16;
    if (g.gmode & (1 << gmdir)) y ^= 0x16;
    printz(str_setlccords_1, sizeof str_setlccords_1);
    g.printy += y;
    *d1 = 6;
    if (getlchoice(2, tm) < 0) {
        *d1 = 5;
        g.printy += 1;
    }
    *d0 = 9;
}

/* logic94.asm:getlchoice. d0 = 0-2 (a2 = team): the line number that choice means, from
 * the current line and the power play situation; -1 = no such choice. */
int16_t getlchoice(int16_t choice, struct Team *tm)
{
    int16_t d2 = (int16_t)team_struct(tm == team_struct(0))->ap;                 /* the other team's skaters */
    return getlchoice2(choice, d2, tm);
}

/* logic94.asm:getlchoice2. Add $15 to d0 for each player more or fewer than the other team
 * has on the ice, 3 x the current line, then look it up. */
int16_t getlchoice2(int16_t d0, int16_t d2, struct Team *tm)
{
    d2 = (int16_t)(d2 - (int16_t)tm->ap);
    if (d2 != 0) {
        d0 = (int16_t)(d0 + 0x15);
        if (d2 >= 0) d0 = (int16_t)(d0 + 0x15);
    }
    int16_t d1 = (int16_t)tm->line;
    d0 = (int16_t)(d0 + d1 + d1 + d1);
    return (int8_t)lchoice_tab[d0];
}

/* logic94.asm:lineinput. Process input for line changes: d1 = new button presses (A / B / C
 * pick a line); a pass in progress goes on; a cancelled box is redrawn. */
void lineinput(uint16_t d0, uint16_t d1, uint16_t d2, struct SortObj *o)
{
    if (g.sflags5 & 8) passmode(d0, d2, o);
    struct Team *tm = team_struct(PFLAG(o, pfteam));
    if (tm->flags & 1) {
        tm->flags &= (uint8_t)~1;
        lcfound_close(tm);
        SetLCmode2(tm);
    }
    if (d1 & BUT_A) { lcfound(0, tm, o); return; }
    if (d1 & BUT_B) { lcfound(1, tm, o); return; }
    if (d1 & BUT_C) { lcfound(2, tm, o); return; }
    if (!PFLAG(o, pfjoycon)) return;
    if (PFLAG(o, pfalock)) return;
    if (!(o->pflags2 & (1 << pf2fight))) doplayeracc(d0, o);
}

/* logic94.asm:lcfound. d2 = choice made 0-2: set the team's line and close the box. */
void lcfound(uint16_t choice, struct Team *tm, struct SortObj *o)
{
    tm->lchoice = choice;
    int16_t line = getlchoice((int16_t)choice, tm);
    if (line < 0) return;
    o->pflags2 &= (uint8_t)~8;
    o->pflags |= 1 << pfjoycon;
    tm = loadTeamStruct(o, NULL);
    tm->flags &= (uint8_t)~2;
    tm->line = (uint16_t)line;
    setpersonel(tm);
    lcfound_close(tm);
}

/* logic94.asm:lcfound_close. Close the line change box of team a2. */
void lcfound_close(struct Team *tm)
{
    if (g.sflags & HI(sfhor)) return;
    uint16_t d0, d1;
    setlccords(tm, &d0, &d1);
    if ((int16_t)g.printy >= 0xF) g.sflags3 &= (uint8_t)~(1 << sf3llcs);
    eraser(d0, (uint16_t)(d1 + 1), 0x7FF);
    printscores1();
}

/* logic94.asm:burst. C button press: a burst of speed in the facing direction, paid for in
 * energy (line changes on). */
void burst(struct SortObj *o)
{
    int16_t e = getpde(o);
    if (g.OptLine == 0) {
        e = (int16_t)(e - rule_scale(RULE_TIRING, 0xCC));   /* (rules: tiring) */
        setpde(team_struct(PFLAG(o, pfteam)), (int16_t)((int8_t)o->pnum * 2), e);
        if (g.sflags7 & 0x10) e = 0x1000;
    }
    uint16_t d0 = (uint16_t)e >> 7;
    d0 = (uint16_t)rule_scale(RULE_BURST, d0);            /* (rules: the burst) */
    uint16_t d1 = d0;
    unsigned idx = (unsigned)o->facedir * 2;
    o->Xvel = (int16_t)(o->Xvel + (int16_t)((int32_t)(int16_t)d0 * (int16_t)dirtab[idx]));
    o->Yvel = (int16_t)(o->Yvel + (int16_t)((int32_t)(int16_t)d1 * (int16_t)dirtab[idx + 1]));
    o->pflags |= 1 << pfalock;
    SetSPA(o, SPAburst);
}

/* logic94.asm:Holdplayer. A button hold: hook the last player he hit (Acheck). */
void Holdplayer(struct SortObj *o)
{
    o->pflags |= 1 << pfalock;
    if (o->impact == 0) { SetSPA(o, SPAhook); return; }
    Acheck(sortobj(o->impactp), o);
}

/* logic94.asm:Acheck. Start SPAhook, or SPAHold if a0 is ahead. The computer's hold check
 * comes in here. */
void Acheck(struct SortObj *a0, struct SortObj *o)
{
    o->pflags |= 1 << pfalock;
    uint16_t d1 = SPAhook;
    if (o->impact != 0) {
        int16_t d0 = (int16_t)(o->Ypos - a0->Ypos);
        if (PFLAG(o, pfgoal)) d0 = (int16_t)-d0;
        if (d0 >= 0) d1 = SPAHold;                        /* stick in the air */
    }
    SetSPA(o, d1);
}

/* logic94.asm:setpassmode. Initialize pass mode: the pass direction starts as his facing. */
void setpassmode(struct SortObj *o)
{
    g.passdir = (uint16_t)(o->facedir & 7);
    if (g.BA_PS_flags & HI(2)) {
        g.gmode2 &= (uint16_t)~HI(2);
        int was = g.BA_PS_flags & HI(5);
        g.BA_PS_flags |= HI(5);
        if (was) return;
        g.gmode2 &= (uint16_t)~HI(5);
        g.passmodetimer = 0x64;
    }
    g.sflags |= HI(sfspdir);
}

/* logic94.asm:passmode. In pass mode: B released (or a one-timer pass) passes; the dpad sets
 * the direction and passes. */
void passmode(uint16_t d0, uint16_t d2, struct SortObj *o)
{
    if (!(d2 & BUT_B) && !(g.sflags5 & 8)) {
        if (d0 & 8) return;                               /* no direction */
        g.passdir = d0 & 7;
    }
    dopass(o);
}

/* logic94.asm:dopass. Pass the puck in passdir, to the best teammate there if any (passto),
 * else just send it that way; a one-timer pass goes to the receiver's spot. The pass speed
 * comes from his passing rating. */
void dopass(struct SortObj *o)
{
    g.sflags &= (uint16_t)~HI(sfspdir);
    puck_loose();
    o->nopuck[0] = 0x10;
    g.lastplayer = (uint16_t)o->SCnum;
    if (g.sflags5 & 8) {
        g.sflags5 &= (uint8_t)~8;
        OneTimerTarget(o);
        g.onetimerheight = 0x12;
        if (g.sflags6 & HI(2)) g.onetimerheight = 0x3A;
        OneTimerPass(o);
    } else {
        int16_t d0 = 8;
        if (o->position != 0) d0 = o->passacc;
        d0 = (int16_t)(((int16_t)(d0 << 2) >> 1) + 0xA0);
        g.passspeed = (uint16_t)d0;
        if (o->passacc & 1) g.passspeed = (uint16_t)(g.passspeed + (d0 >> 4));
        int32_t d4 = -1;
        struct SortObj *best = NULL;
        struct SortObj *a1 = teammates(o);
        int search = 1;
        if (o->SCnum >= 6 && (g.BA_PS_flags & HI(2))) search = 0;
        if (search) {
            for (int i = 0; i < 6; i++, a1++) {           /* the nearest teammate in that direction */
                if (a1 == o) continue;
                if (a1->position == 0) continue;
                if (a1->pflags2 & 4) continue;
                int16_t dx = (int16_t)(a1->Xpos - puckx()), dy = (int16_t)(a1->Ypos - pucky());
                int16_t dd = (int16_t)((vtoa(dx, dy) - g.passdir) & 7);
                dd = (int16_t)(int8_t)(dd << 5);
                dd = (int16_t)(dd * 8);                     /* (asl #3; a negative's shift undefined in C) */
                int32_t ddsq = sq(dd);
                if ((uint32_t)ddsq > 0x10000) continue;
                int32_t d2 = sq(dx) + sq(dy) + ddsq;
                if ((uint32_t)d2 > (uint32_t)d4) continue;
                d4 = d2;
                best = a1;
            }
        }
        if (d4 >= 0)
            passto(best, o);
        else {                                            /* nobody: hit it in passdir */
            unsigned idx = (unsigned)g.passdir * 2;
            int32_t v = (int32_t)(int16_t)dirtab[idx + 1] * (int16_t)g.passspeed;
            v = (int32_t)((uint32_t)v << 10);             /* (asl.l #10, wrapping as the 68k's) */
            g.puckvy = (uint16_t)((int16_t)m68k_divs(v, 0xBB8) + o->Yvel);
            v = (int32_t)(int16_t)dirtab[idx] * (int16_t)g.passspeed;
            v = (int32_t)((uint32_t)v << 10);
            g.puckvx = (uint16_t)((int16_t)m68k_divs(v, 0xBB8) + o->Xvel);
            g.puckvz[0] = randomd0(0x1000);
        }
    }
    if (o->position == 0) {                               /* the goalie clears away from his net */
        int n = puckvy() < 0;
        if (PFLAG(o, pfgoal) ? n : !n) g.puckvy = (uint16_t)-puckvy();
    }
    uint16_t dir = vtoa(puckvx(), puckvy());
    uint16_t d1 = SPAgswing;
    if (o->position != 0) {
        d1 = SPApassf;
        if (!Findhittype(dir, o)) d1 = SPApassb;
    }
    SetSPA(o, d1);
    o->pflags |= 1 << pfalock;
    uint16_t snd = (uint16_t)(uint8_t)(0xC - (uint8_t)(g.puckvz[0] >> 8));
    snd = (uint16_t)(((snd >> 2) & 3) + 0x10);            /* SFXpass, by the puck's hop */
    sfx((int16_t)snd);
}

/* logic94.asm:passto. Pass the puck to player a0 from a3: a human goalie's pass hands the
 * controller to the receiver. */
void passto(struct SortObj *rcv, struct SortObj *o)
{
    if (PFLAG(o, pfjoycon) && o->position == 0 && !PFLAG(rcv, pfjoycon)) {
        rcv->pflags3 |= 0x40;
        uint16_t d0 = (uint16_t)rcv->SCnum;
        if (g.cont1team == g.cont2team) {                 /* two pads on one team */
            if ((uint16_t)o->SCnum == g.c1playernum) setc1player(d0); else setc2player(d0);
        } else {
            uint16_t d1 = PFLAG(o, pfteam) ? 2 : 1;
            if (d1 == g.cont1team) setc1player(d0); else setc2player(d0);
        }
    }
    passtoa0(rcv, o);
}

/* logic94.asm:passtoa0. Pass the puck to player a0: he gets apassrec; the puck's velocity
 * is solved so that it meets him where he will be (passspeed pixels per second, his speed
 * per quarter second), with a hop timed to the meeting. */
void passtoa0(struct SortObj *rcv, struct SortObj *o)
{
    struct Team *tm = loadTeamStruct(o, NULL);
    tm->passes++;                                                /* pass attempts */
    g.passplayer = (uint16_t)rcv->SCnum;
    int16_t d5 = (int16_t)((int16_t)g.passspeed >> 2);
    assinsert(0x13, rcv);                                 /* apassrec */
    int16_t hx, hy;
    GetHot(rcv, &hx, &hy);
    int16_t dx = (int16_t)(hx + rcv->Xpos - puckx());
    int16_t dy = (int16_t)(hy + rcv->Ypos - pucky());
    int16_t d2 = (int16_t)(dx >> 2), d3 = (int16_t)(dy >> 2);
    int16_t vx = (int16_t)(((int32_t)rcv->Xvel * 0xF0) >> 16);   /* pixels per quarter second */
    int16_t vy = (int16_t)(((int32_t)rcv->Yvel * 0xF0) >> 16);
    int16_t j = (int16_t)((int16_t)((int16_t)((int32_t)vx * d2) + (int16_t)((int32_t)vy * d3)) * 2);
    int32_t k = sq(vx) + sq(vy) - sq(d5);
    int32_t asq = sq(d2) + sq(d3);
    int32_t kasq = (int32_t)(int16_t)asq * (int16_t)k;
    kasq = (int32_t)((uint32_t)kasq << 2);                /* (asl.l #2, wrapping) */
    int32_t disc = sq(j) - kasq;
    int16_t r = (int16_t)Sroot((uint32_t)disc);
    int16_t div = (int16_t)((int16_t)k >> 2);
    if (div == 0) div = 1;                                /* no divide by zero */
    int16_t t = 0, d0 = r;
    int zero = 0;
    for (int d3c = 1; ; ) {
        int32_t q = m68k_divs((int32_t)(int16_t)(d0 - j), div);
        t = (int16_t)q;
        zero = t == 0;
        d0 = (int16_t)-d0;
        if (t >= 0) break;
        if (--d3c == -1) break;
    }
    if (zero) t = (int16_t)(t + 1);                       /* can't be zero */
    if ((uint16_t)t > 0x18) t = 0x18;                     /* at most 3 seconds, in eighths */
    g.puckvz[0] = (uint16_t)(((uint8_t)t << 8) | (g.puckvz[0] & 0xFF));
    if (t >= 0xC) g.puckvz[0] = (uint16_t)((0xC << 8) | (g.puckvz[0] & 0xFF));
    int16_t w = (int16_t)((t << 3) - 0xA);
    rcv->temp1 = (int16_t)(((uint16_t)(uint8_t)w << 8) | ((uint16_t)rcv->temp1 & 0xFF));
    w = (int16_t)(w - 6);
    g.puckx_nopuck = (uint8_t)w;
    int32_t px = ((int32_t)vx * t) >> 1;
    px = (int32_t)((px & 0xFFFF0000u) | (uint16_t)((int16_t)px + dx));
    rcv->temp3 = (int16_t)(puckx() + (int16_t)px);
    int32_t py = ((int32_t)vy * t) >> 1;
    py = (int32_t)((py & 0xFFFF0000u) | (uint16_t)((int16_t)py + dy));
    rcv->temp4 = (int16_t)(pucky() + (int16_t)py);
    int16_t tt = (int16_t)(t * 0x78);
    uint32_t sw = ((uint32_t)(uint16_t)px << 16) | ((uint32_t)px >> 16);
    g.puckvx = (uint16_t)m68k_divs((int32_t)sw, tt);
    sw = ((uint32_t)(uint16_t)py << 16) | ((uint32_t)py >> 16);
    g.puckvy = (uint16_t)m68k_divs((int32_t)sw, tt);
}

/* logic94.asm:changeplayer. The pad d4 takes the nearest free skater (chgplayer); a3 sweep
 * checks when it is the pad's own player. */
void changeplayer(uint16_t pad, struct SortObj *o)
{
    chgplayer(pad, o);
}

/* logic94.asm:Sweepcheck. Start a sweep check on player a3. */
void Sweepcheck(struct SortObj *o)
{
    o->pflags |= 1 << pfalock;
    SetSPA(o, SPAsweepchk);
}

/* logic94.asm:setc1player / setc2player. Restore the old player and switch to the new d0
 * player on controller 1 / 2. */
void setc1player(uint16_t player)
{
    if (player == g.c1playernum) return;
    g.c1playernum = (uint16_t)restorepl((int16_t)g.c1playernum, (int16_t)player);
}
void setc2player(uint16_t player)
{
    if (player == g.c2playernum) return;
    g.c2playernum = (uint16_t)restorepl((int16_t)g.c2playernum, (int16_t)player);
}

/* logic94.asm:restorepl. d1 = the old player: give him back to the computer (unless he is
 * in a line change box); d0 = the new one takes the pad, except a goalie the pad does not
 * control in a penalty shot / shootout (the first skater instead). Returns d0. */
int16_t restorepl(int16_t old, int16_t new)
{
    if (old >= 0 && old <= 0xB) {
        struct SortObj *p = sortobj(old);
        if (p->pflags2 & 8) return old;
        p->pflags &= (uint8_t)~(1 << pfjoycon);
        if (!(p->pflags3 & 8)) p->pflags |= 1 << pfna;
    }
    if (new < 0 || new > 0xB) return new;
    struct SortObj *q = sortobj(new);
    if (((g.BA_PS_flags & HI(2)) || (g.gmode2 & HI(0))) && q->position == 0) {
        uint16_t gm = PFLAG(q, pfteam) ? g.goaliemode2 : g.goaliemode1;
        if (gm != 0) return PFLAG(q, pfteam) ? 6 : 0;
    }
    q->pflags |= 1 << pfjoycon;
    return new;
}

/* logic94.asm:Findhittype. Look for the type of swing: d0 = launch direction relative to
 * his facing and stick side. Returns 1 for a forehand (the 68k Z flag). */
int Findhittype(uint16_t dir, const struct SortObj *o)
{
    unsigned d0 = (unsigned)(o->facedir - (int16_t)dir) & 7;
    unsigned mask = (o->attribute & HI(3)) ? 0xF0 : 0x1E;
    return !((mask >> d0) & 1);
}

/* logic94.asm:SetShotMode. Initiate a shot by player a3: wind up forehand or backhand toward
 * the far boards. */
void SetShotMode(struct SortObj *o)
{
    if (g.BA_PS_flags & HI(2)) {
        g.gmode2 &= (uint16_t)~HI(2);
        int was = g.BA_PS_flags & HI(5);
        g.BA_PS_flags |= HI(5);
        if (was) return;
        g.gmode2 &= (uint16_t)~HI(5);
        g.passmodetimer = 0x64;
    }
    g.passdir = 8;
    g.sflags |= HI(sfssdir);
    int16_t d0 = 0, d1 = PFLAG(o, pfgoal) ? 0x128 : -0x128;
    d0 = (int16_t)(d0 - o->Xpos);
    d1 = (int16_t)(d1 - o->Ypos);
    uint16_t dir = vtoa(d0, d1);
    g.passspeed = 0xF;
    uint16_t spa = SPAshotf;
    if (!Findhittype(dir, o)) {
        spa = SPAshotb;
        g.passspeed = 0xF;
    }
    SetSPA(o, spa);
}

/* logic94.asm:ShotMode. Shot input: the dpad aims, the windup adds speed until the C button
 * changes (or the swing is forced past the half way point for weak shooters); at the end of
 * the animation the shot goes (prepshot). d0 = dpad, d2 = changed buttons. */
void ShotMode(uint16_t d0, uint16_t d2, struct SortObj *o)
{
    if ((int16_t)o->SPAnum >= 0x1C) { prepshot(o); return; }
    if (!(d0 & 8)) g.passdir = d0 & 7;
    if ((int16_t)o->SPAnum >= 0x10) return;
    g.passspeed = (uint16_t)(g.passspeed + elapsed);
    if ((int8_t)o->shotspd >= 0x14 || !((int16_t)o->SPAnum > 8)) {
        if (!(d2 & BUT_C)) return;
    }
    o->SPAnum = (uint16_t)(0x1C - o->SPAnum);             /* end the windup and swing through */
}

/* logic94.asm:prepshot. Check certain conditions before shooting: a shot from in close at a
 * standing goalie goes top shelf (sflags5 bit 4). */
void prepshot(struct SortObj *o)
{
    g.sflags5 &= (uint8_t)~0x10;
    int16_t gk = getGoalieSCnum(PFLAG(o, pfteam) ? 5 : 0xB);
    int standing = 0;
    if (gk >= 0) {
        uint16_t spa = sortobj(gk)->SPA;
        standing = spa != SPAgstackr && spa != SPAgstackl;
    }
    if (!standing && ((int16_t)g.passdir <= 1 || g.passdir == 7)) {
        int16_t d0 = pucky();
        if (!PFLAG(o, pfgoal)) d0 = (int16_t)-d0;
        if (d0 >= 0xD8) g.sflags5 |= 0x10;
    }
    doshot(o);
}

/* logic94.asm:doshot. The stick is at the puck: launch it toward the goal. The speed comes
 * from his shot power, energy and the windup; the aim (shotsets) is perfect in close with
 * luck by his accuracy, else scattered; the height from the distance. */
void doshot(struct SortObj *o)
{
    g.gmode2 &= (uint16_t)~HI(4);
    if (o->pflags3 & (1 << pf3break)) g.gmode2 |= HI(4);
    shotdiradj(o);
    int16_t snd = SYM_SFXshotwiff;
    g.shotplayer = (uint16_t)o->SCnum;
    g.sflags &= (uint16_t)~HI(sfssdir);
    o->pflags |= 1 << pfalock;
    if (!(o->pflags3 & (1 << pf3onetimer)) && puckc() != o->SCnum) {
        sfx(snd);                                         /* whiffed */
        return;
    }
    snd = 0x18;                                           /* SFXshotfh */
    g.sflags2 |= HI(sf2shot);
    if (o->SPA == SPAshotb) {
        snd = 0x14;                                       /* SFXshotbh */
        g.passspeed = (uint16_t)(g.passspeed - (g.passspeed >> 2));
    }
    if (o->pflags3 & (1 << pf3onetimer)) g.passspeed = 0x1F;
    uint16_t d0 = makepde((int8_t)(o->shotspd >> 1), o);
    d0 = (uint16_t)(d0 + 0x14);
    uint32_t prod = (uint32_t)d0 * g.passspeed;
    prod = (uint32_t)(uint16_t)prod * 0x5249u;
    d0 = (uint16_t)(prod >> 16);
    d0 = (uint16_t)rule_scale(RULE_SHOT_POWER, d0);       /* (rules: shot power) */
    g.passspeed = d0;
    if (o->shotspd & 1) d0 = (uint16_t)(((int16_t)d0 >> 4) + g.passspeed);
    d0 = (uint16_t)(d0 >> 4);
    int16_t sadd = (int16_t)(3 - (int16_t)d0);
    if (sadd < 0) sadd = 0;
    snd = (int16_t)(snd + sadd);
    puck_loose();
    o->nopuck[0] = 0x10;
    g.lastplayer = (uint16_t)o->SCnum;
    int16_t dy = PFLAG(o, pfgoal) ? 0x108 : -0x108;
    unsigned idx = (unsigned)g.passdir * 2;
    int16_t dx = (int16_t)((int16_t)shotsets[idx] - puckx());
    int16_t dz = (int16_t)shotsets[idx + 1];
    dy = (int16_t)(dy - pucky());
    int16_t d3 = (int16_t)Sroot((uint32_t)(sq(dx) + sq(dy)));
    if (d3 == 0) d3 = 1;                                  /* distance to the spot */
    int perfect = 0;
    if (g.gmode & (1 << gmhl)) perfect = 1;
    else if ((uint16_t)d3 <= 0xC8) {
        if (ReadGoaliePulled(o) & CCR_N) perfect = 1;
        else if (g.gmode2 & HI(0)) perfect = 1;
        else if (randomd0((uint16_t)((0x10 + o->shotacc) & 0xFF)) > 0xE) perfect = 1;
    }
    if (!perfect) {
        uint8_t acc = (uint8_t)(o->shotacc >> 1);
        uint16_t a = (uint16_t)(g.passspeed >> 4);
        a = (uint16_t)((a & 0xFF00) | ((uint8_t)(a - acc) & 0xFF));
        a = (uint16_t)((a & 0xFF00) | ((uint8_t)(a + 0x10) & 0xFF));
        a = (uint16_t)((uint16_t)(a * (uint16_t)d3) >> 6);
        if ((uint16_t)d3 <= 0xFA) a = (uint16_t)(a >> 1);
        if ((int16_t)a >= 0x88) a = 0x88;
        dx = (int16_t)(dx + (int16_t)randomd0s(a));
        if (a > 0x3C) a = 0x3C;
        uint16_t ay = a;
        if (dy < 0) ay = (uint16_t)(ay >> 1);
        dy = (int16_t)(dy + (int16_t)randomd0s(ay));
        dz = (int16_t)(dz + (int16_t)randomd0((uint16_t)(a >> 1)));
    }
    int32_t v = (int32_t)(int16_t)g.passspeed * 0x44;
    v = (int32_t)(int16_t)v * dx;
    g.puckvx = (uint16_t)m68k_divs(v, d3);
    v = (int32_t)(int16_t)g.passspeed * 0x44;
    v = (int32_t)(int16_t)v * dy;
    int32_t qy = m68k_divs(v, d3);
    g.puckvy = (uint16_t)qy;
    {
        uint16_t lim = PFLAG(o, pfgoal) ? 0 : 0x8000;
        if ((int16_t)((uint16_t)qy ^ lim) < 0) {          /* past the fastest allowed */
            g.puckvy = 0x3810;
            if (!PFLAG(o, pfgoal)) g.puckvy = 0xC7F0;
        }
    }
    if (dz != 0) {
        uint32_t h = (uint32_t)(uint16_t)dz * g.passspeed;
        h = (uint32_t)(uint16_t)h * 0x44u;
        h = (uint16_t)m68k_divu(h, (uint16_t)d3);
        uint32_t dd = (uint32_t)(uint16_t)d3 * 0xB33u;
        dd = (uint16_t)m68k_divu(dd, g.passspeed);
        uint16_t vz = (uint16_t)(dd + h);
        if (vz > 0x1800) vz = 0x1800;
        g.puckvz[0] = vz;
        int was = g.sflags5 & 0x10;
        g.sflags5 &= (uint8_t)~0x10;
        if (was && !(o->pflags3 & (1 << pf3onetimer))) puckvzadj(o);
    }
    sfx(snd);
}

/* logic94.asm:shotdiradj. Where a computer player (or a one-timer) shoots: at the post the
 * goalie leaves more open, straight when he covers the middle, 8 with no goalie. */
void shotdiradj(struct SortObj *o)
{
    if (!(o->pflags3 & (1 << pf3onetimer)) && PFLAG(o, pfjoycon)) return;
    uint16_t d0 = 8;
    const struct SortObj *gk = opponents(o);
    int found = 0;
    for (int i = 0; i < 6; i++, gk++)
        if (gk->position == 0) { found = 1; break; }
    if (found) {
        int16_t dx = (int16_t)((HIB(gk->Xvel) >> 1) + gk->Xpos - puckx());
        int16_t dy = (int16_t)((HIB(gk->Yvel) >> 1) + gk->Ypos - pucky());
        int16_t d2 = (int16_t)Sroot((uint32_t)(sq(dx) + sq(dy) + 1));
        int16_t d3 = 0x12;
        int16_t d4 = PFLAG(o, pfgoal) ? 0x108 : -0x108;
        int16_t d5 = CalcAngleOffset(dx, dy, d2, d3, d4);
        int16_t off = (int16_t)(CalcAngleOffset(dx, dy, d2, (int16_t)-d3, d4) + d5);
        d0 = 0;
        if (off <= 0x2C && off >= -0x2C) {
            if (PFLAG(o, pfgoal)) off = (int16_t)-off;
            d0 = off >= 0 ? 2 : 6;
        }
    }
    g.passdir = d0;
    if ((g.BA_PS_flags & HI(2)) || (g.gmode2 & HI(0))) PSandSOpassdir(o);
}

/* logic94.asm:CalcAngleOffset. The area of the triangle puck - goalie - post over the
 * distance from the goalie to the puck: which side of the goalie the post is seen on. */
int16_t CalcAngleOffset(int16_t d0, int16_t d1, int16_t d2, int16_t d3, int16_t d4)
{
    d3 = (int16_t)(d3 - puckx());
    d4 = (int16_t)(d4 - pucky());
    int32_t a = (int32_t)d0 * d4;
    int32_t b = (int32_t)d1 * d3;
    return (int16_t)m68k_divs(a - b, d2);
}

/* logic94.asm:setpads. Put the SCnum of a3 in the d4 nibble of PadControlBits; d4 = -2 puck
 * carrier, 0 / 2 pads, 4 replay target. */
void setpads(int16_t pad, struct SortObj *o)
{
    unsigned sh = ((unsigned)(2 + pad) * 2) & 63;
    uint16_t mask = (uint16_t)((0xFFF0u << sh) | (0xFFF0u >> (16 - sh)));
    if (sh == 0) mask = 0xFFF0;
    g.PadControlBits &= mask;
    g.PadControlBits |= (uint16_t)((uint16_t)o->SCnum << sh);
}

/* logic94.asm:check4bench. Check if player a3 should go to the bench: with a line change
 * pending (newpnum) and the puck not on him, switch him to assbench; when the new player is
 * himself, just take his new position. Both drop the caller's return address: returns 1 when
 * the calling assignment is over. */
int check4bench(struct SortObj *o)
{
    if (g.BA_PS_flags & HI(2)) return 0;
    if (PFLAG(o, pfjoycon)) return 0;
    if (o->pflags2 & 0x10) return 0;
    if ((int8_t)o->newpos < 0 && (int8_t)o->newpnum < 0) return 0;
    if (o->newpnum == o->pnum) {
        o->pflags2 &= (uint8_t)~4;
        o->pflags &= (uint8_t)~(1 << pfnc);
        o->newpnum = 0xFF;
        o->newpos = 0xFF;
        Setplass(o);
        return 1;
    }
    if (o->asslist[o->assnum] == 0xB) return 0;           /* already assbench */
    if (o->SCnum == puckc()) return 0;
    o->pflags2 |= 4;                                      /* pf2unav */
    o->temp1 = 0;
    assreplace(0xB, o);                                   /* assbench */
    return 1;
}

/* logic94.asm:assbench. Player a3 should go to the bench: skate to the gate, glide through
 * it (SPAwallleft), then the new player comes on (setplayer). */
void assbench(struct Team *tm, struct SortObj *o)
{
    if (PFLAG(o, pfalock)) return;
    if (o->temp1 == 0x64) {                               /* through the gate */
        o->frame = 0;
        struct Team *a0 = own_team(o);
        a0->pdst[(int8_t)o->pnum] = 0xFFFE;               /* he is on the bench */
        uint8_t d3 = o->newpnum;
        take_new_position(o);
        setplayer(d3, o);
        return;
    }
    if (check4bench(o)) return;
    if (o->pflags2 & 0x10) { assexit(o); return; }
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        o->temp2 = 8;
        o->temp4 = PFLAG(o, pfteam) ? 0x50 : -0x50;
        o->temp3 = -0x88 - 8;                             /* the gate */
        o->temp1 = 0;
    }
    if (o->newpnum == o->pnum) {                          /* the change was cancelled */
        o->pflags2 &= (uint8_t)~4;
        take_new_position(o);
        return;
    }
    o->temp1 = (int16_t)(o->temp1 - elapsed);
    if (o->temp1 >= 0) goto nodec;
    o->temp1 = (int16_t)(o->temp1 + 8);
    int16_t d0 = (int16_t)(o->Ypos - o->temp4);
    if (d0 > 0x28 || d0 < -0x28) goto nodec;
    d0 = (int16_t)(o->Xpos - o->temp3);
    if (d0 > 0x20) goto nodec;
    SetSPA(o, o->position != 0 ? SPAglide : SPAgready);
    o->pflags |= 1 << pfnc;
    if (o->facedir != 4) o->facedir = (int16_t)((o->facedir + 1) & 7);
    o->Yvel = 0;
    o->Xvel = -0x800;
    if (d0 > 0x10) return;
    o->Xvel = 0;
    if (o->facedir != 4) return;
    o->Xvel = -0x800;
    o->facedir = 2;
    SetSPA(o, SPAwallleft);
    o->pflags |= 1 << pfalock;
    o->temp1 = 0x64;
    return;
nodec:
    if (PFLAG(o, pfnc)) return;
    skateto(o->temp3, o->temp4, EvadePC, o);
}

/* logic94.asm:asseben. Player a3 should exit the bench area: hop the boards at his slot. */
void asseben(struct Team *tm, struct SortObj *o)
{
    if (PFLAG(o, pfalock)) return;
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        o->Xvel = 0;
        o->Yvel = 0;
        int16_t d0 = (int16_t)(o->SCnum - 6);
        if (d0 >= 0) d0 = (int16_t)(d0 + 1);
        o->Ypos = (int16_t)(d0 * 0xE);
        o->Xpos = -0x88;
        o->facedir = 2;
        o->pflags |= 1 << pfalock;
        SetSPA(o, SPAwallright);
        return;
    }
    o->facedir = 4;
    o->pflags &= (uint8_t)~(1 << pfnc);
    o->pflags2 &= (uint8_t)~0x20;
    o->pflags2 &= (uint8_t)~4;
    o->SPA = 0;
    o->Xvel = 0x1000;
    assexit(o);
}

/* logic94.asm:asspenalty. Player a3 should go to the penalty box: skate to the box on his
 * team's side (stacked by how many are in it), turn to the boards and hop over. */
void asspenalty(struct Team *tm, struct SortObj *o)
{
    if (PFLAG(o, pfalock)) return;
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        o->pflags2 |= 4;                                  /* unavailable */
        if (PFLAG(o, pfjoycon))                           /* take the joystick off him */
            changeplayer(o->SCnum == (int16_t)g.c1playernum ? 0 : 2, o);
        setpads(-2, o);
        o->temp2 = 8;
        int16_t d0 = 0xB;
        uint16_t d1 = (uint8_t)(g.PBnum >> 8);            /* players in the box: 00HV */
        if (!PFLAG(o, pfteam)) { d1 >>= 4; d0 = (int16_t)-d0; }
        d1 &= 0xF;
        if (d1 > 2) d1 = 2;
        d1 += 3;
        o->temp4 = (int16_t)((int16_t)d1 * d0);
        o->temp3 = 0x88;
        o->temp1 = 0;
        o->pflags2 |= 0x20;                               /* no player collisions */
        o->Wallcos = 0;
        o->Wallsin = 0;
    }
    int16_t d0 = (int16_t)(o->Ypos - o->temp4);
    if (d0 > 0xC || d0 < -0xC) goto st;
    d0 = (int16_t)(o->Xpos - o->temp3);
    if (d0 < -0x18) goto st;
    o->temp1 = (int16_t)(o->temp1 - elapsed);
    if (o->temp1 >= 0) return;
    o->temp1 = (int16_t)(o->temp1 + 8);
    o->pflags |= 1 << pfnc;
    SetSPA(o, SPAglide);
    {
        int16_t d2 = o->handed[0] ? 2 : 6;
        if (d2 != o->facedir) o->facedir = (int16_t)((o->facedir + 1) & 7);
        o->Yvel = 0;
        o->Xvel = 0x1000;
        if (d0 < -8) return;
        o->Xvel = 0;
        if (d2 != o->facedir) return;
    }
    o->pflags |= 1 << pfalock;
    o->facedir = 2;
    SetSPA(o, SPAwallright);
    o->pflags2 &= (uint8_t)~0x10;
    assreplace(0xD, o);                                   /* assdopen */
    return;
st:
    skateto(o->temp3, o->temp4, NULL, o);
}

/* logic94.asm:assdopen. Add player a3 to the penalty box. */
void assdopen(struct Team *tm, struct SortObj *o)
{
    if (PFLAG(o, pfalock)) return;
    uint8_t d0 = PFLAG(o, pfteam) ? 1 : 0x10;
    g.PBnum = (uint16_t)(((uint8_t)((g.PBnum >> 8) + d0) << 8) | (g.PBnum & 0xFF));
    ST_W(o->position);
    o->frame = 0;
}

/* logic94.asm:assepen. Player a3 should exit the penalty area: hop back onto the ice. */
void assepen(struct Team *tm, struct SortObj *o)
{
    if (PFLAG(o, pfalock)) return;
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        o->pflags |= 1 << pfnc;
        o->pflags2 |= 4;
        uint8_t d0 = 0x10;
        int16_t d1 = -0x3C;
        if (PFLAG(o, pfteam)) { d0 = 1; d1 = 0x3C; }
        g.PBnum = (uint16_t)(((uint8_t)((g.PBnum >> 8) - d0) << 8) | (g.PBnum & 0xFF));
        o->Ypos = d1;
        o->Xvel = 0;
        o->Yvel = 0;
        o->Xpos = 0x86;
        o->facedir = 2;
        o->pflags |= 1 << pfalock;
        SetSPA(o, SPAwallleft);
        SprSort();
        return;
    }
    o->facedir = 4;
    o->newpnum = 0xFF;
    o->newpos = 0xFF;
    o->pflags &= (uint8_t)~(1 << pfjoycon);
    o->pflags &= (uint8_t)~(1 << pfnc);
    o->pflags2 &= (uint8_t)~0x20;
    o->pflags2 &= (uint8_t)~4;
    o->Xvel = -0x1000;
    assexit(o);
}

/* logic94.asm:assgoaliebreakwait. In a shootout or penalty shot park the idle goalie off
 * the ice, else exit. */
void assgoaliebreakwait(struct Team *tm, struct SortObj *o)
{
    if (PFLAG(o, pfalock)) return;
    if (!(g.BA_PS_flags & HI(2)) && !(g.gmode2 & HI(0))) { assexit(o); return; }
    o->Xvel = 0;
    o->Yvel = 0;
    const struct SortObj *gk = sortobj((int16_t)g.BA_Goalie_SCnum);
    o->Ypos = PFLAG(gk, pfgoal) ? 0x191 : -0x191;
}

/* logic94.asm:assfaceoff. Players do nothing until the faceoff is over. */
void assfaceoff(struct Team *tm, struct SortObj *o)
{
    if (PFLAG(o, pfalock)) return;
    if (!(g.sflags2 & HI(sf2faceoff))) assexit(o);
}

/* logic94.asm:assfaceoffpl. The two players taking the faceoff: his stick position (from
 * the animation frame and his hand) goes to fofdata2 for the drop; a computer player starts
 * swinging, sometimes early. */
void assfaceoffpl(struct Team *tm, struct SortObj *o)
{
    if (PFLAG(o, pfalock)) return;
    if (!(g.sflags2 & HI(sf2faceoff))) {
        o->nopuck[0] = 0x14;
        assexit(o);
        return;
    }
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        o->temp1 = 0;
    }
    struct FoMan *fof = &g.fomen[PFLAG(o, pfteam) ? 1 : 0];
    uint16_t d0 = (uint16_t)((o->SPAnum >> 2) + 1);
    if (o->handed[0] == 0) d0 += 3;
    fof->frame = d0;
    if (PFLAG(o, pfjoycon)) return;
    if (o->pflags2 & (1 << pf2aip)) return;
    o->pflags2 |= 1 << pf2aip;
    uint16_t d1 = SPAfaceoff;
    if ((uint16_t)sortobj(puckscnum)->temp1 > 0x10 && randomd0(8) != 0)
        d1 = SPAfaceoffr;                                 /* jumps the drop */
    o->pflags2 |= 2;
    SetSPA(o, d1);
}

/* logic94.asm:assfight, assfwatch. rts only: there is no fighting. */
void assfight(struct Team *tm, struct SortObj *o) { }
void assfwatch(struct Team *tm, struct SortObj *o) { }

/* logic94.asm:donothing. The player should do nothing: coast. */
void donothing(struct SortObj *o)
{
    if (PFLAG(o, pfalock)) return;
    if (PFLAG(o, pfjoycon)) return;
    doplayeracc(8, o);
}

/* logic94.asm:assstanley. The player skates with the Stanley Cup overhead to the middle
 * of the screen. */
void assstanley(struct Team *tm, struct SortObj *o)
{
    if (PFLAG(o, pfalock)) return;
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        o->temp3 = 0x5A;
        if ((int16_t)g.Hpos < 0) o->temp3 = (int16_t)-o->temp3;
        o->temp4 = (int16_t)g.Vpos;
        SetSPA(o, SPAstanley);
    }
    uint16_t d0 = vtoa((int16_t)(o->temp3 - o->Xpos), (int16_t)(o->temp4 - o->Ypos));
    if ((int16_t)d0 > 7) return;
    o->facedir = (int16_t)d0;
    playeracc((int16_t)d0, o);
}

/* logic94.asm:assscore. Player a3 celebrates a goal: skate toward the middle, then
 * celebrate (the scorer pumps his arm) at a random moment. */
void assscore(struct Team *tm, struct SortObj *o)
{
    if (PFLAG(o, pfalock)) return;
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        o->temp1 = (int16_t)randomd0(0x78);
        o->temp2 = 8;
        o->temp3 = 0x5A;
        if ((int16_t)g.Hpos < 0) o->temp3 = (int16_t)-o->temp3;
        o->temp4 = (int16_t)g.Vpos;
    }
    int16_t d0 = o->temp3, d1 = o->temp4;
    if (g.gmode2 & HI(0)) {                               /* shootout */
        if (g.shootoutdelay != 0) {
            g.shootoutdelay--;
            if (g.shootoutdelay == 0) g.sflags2 |= HI(sf2drec);
        }
        if (o->Xpos > 0x88 || o->Xpos <= -0x88) d1 = 0;
    }
    o->temp1 = (int16_t)(o->temp1 - elapsed);
    if (o->temp1 >= 0) {
        if (!PFLAG(o, pfjoycon)) skateto(d0, d1, NULL, o);
        return;
    }
    o->pflags |= 1 << pfalock;
    SetSPA(o, g.shotplayer == (uint16_t)o->SCnum ? SPApump : SPAcelebrate);
    o->temp1 = (int16_t)randomd0(0x78);
}

/* logic94.asm:assdefo. Player a3 is a defensive player on offense: hold the point just
 * inside the blue line on his side, following the puck across. */
void assdefo(struct Team *tm, struct SortObj *o)
{
    if (!assignment_prologue(o)) return;
    if (TICK_HIB(o->temp1) < 0) {
        SET_HIB(o->temp1, o->aioff);
        if ((ReadGoaliePulled(o) & CCR_N) || ((g.sflags7 & 0x40) && HIB(o->temp1) != 0))
            SET_HIB(o->temp1, HIB(o->temp1) - 1);
        if (tm->flags & 0x10) { assreplace(2, o); return; }         /* assdefd */
        int16_t d1 = pucky();
        if (!PFLAG(o, pfgoal)) d1 = (int16_t)-d1;
        if (d1 < 0x5D) { assreplace(2, o); return; }      /* puck out of the attack zone */
        d1 = puckc();
        if (d1 >= 0 && carrier_is_opponent(d1, o)) { assreplace(2, o); return; }
    }
    int16_t d0 = o->position == 2 ? 0x50 : -0x50;
    int16_t d1 = 0x62;
    if (!PFLAG(o, pfgoal)) { d0 = (int16_t)-d0; d1 = (int16_t)-d1; }
    if ((int16_t)(puckx() ^ d0) >= 0) d0 = puckx();      /* the puck is on his side */
    else d0 = (int16_t)(d0 + (puckx() >> 1));
    skateto(d0, d1, EvadePC, o);
}

/* logic94.asm:assdefd. Player a3 is a defensive player on defense: stay between the
 * attackers and his net, on the puck's side. */
void assdefd(struct Team *tm, struct SortObj *o)
{
    if (!assignment_prologue(o)) return;
    if (aidef_timer(o)) {
        int16_t d1 = pucky();
        int16_t d3 = (int16_t)(puckvy() >> 6);
        if (!PFLAG(o, pfgoal)) { d1 = (int16_t)-d1; d3 = (int16_t)-d3; }
        if (d3 < 0) d3 = 0;
        d3 = (int16_t)(d3 + d1);
        if (d3 >= 0x58 && !(tm->flags & 0x10)) {
            d1 = puckc();
            if (d1 >= 0 && !chkpk(o) && !carrier_is_opponent(d1, o)) {
                assreplace(1, o);                         /* assdefo */
                return;
            }
        }
        o->temp3 = o->position == 2 ? 0x41 : -0x41;
        const struct SortObj *a0 = opponents(o);
        int16_t d0 = -1000;
        if (PFLAG(o, pfgoal)) {
            d0 = 1000;
            for (int i = 0; i < 6; i++, a0++) {           /* the deepest attacker */
                if (a0->pflags2 & 4) continue;
                if (a0->position < 0) continue;
                int16_t dy = a0->Yvel;
                if (dy >= 0) dy = 0;
                dy = (int16_t)((dy >> 4) + a0->Ypos);
                if (dy > d0) continue;
                d0 = dy;
            }
            d0 = (int16_t)(d0 - 0x32);
            if (!(d0 > -0xBE)) d0 = -0xDC;
            o->temp4 = d0;
        } else {
            for (int i = 0; i < 6; i++, a0++) {
                if (a0->pflags2 & 4) continue;
                if (a0->position < 0) continue;
                int16_t dy = a0->Yvel;
                if (dy < 0) dy = 0;
                dy = (int16_t)((dy >> 4) + a0->Ypos);
                if (dy < d0) continue;
                d0 = dy;
            }
            d0 = (int16_t)(d0 + 0x32);
            if (!(d0 < 0xBE)) d0 = 0xDC;
            o->temp4 = d0;
            o->temp3 = (int16_t)-o->temp3;
        }
        if ((int16_t)(puckx() ^ o->temp3) < 0) o->temp3 = 0;   /* the puck is on the other side */
    }
    skateto(o->temp3, o->temp4, EvadePC, o);
    check4check(o);
}

/* logic94.asm:asswingd. Player a3 is a winger on defense: cover the far side, level with
 * the puck in the defensive zone. */
void asswingd(struct Team *tm, struct SortObj *o)
{
    if (!assignment_prologue(o)) return;
    if (aidef_timer(o)) {
        int16_t d1 = puckc();
        if (d1 >= 0 && !carrier_is_opponent(d1, o)) { assreplace(4, o); return; }   /* asswingo */
    }
    int16_t d0 = o->position == 5 ? -0x64 : 0x64;
    int16_t d1 = 0x9E;
    int16_t d2;
    if (PFLAG(o, pfgoal)) {
        d0 = (int16_t)-d0;
        d1 = (int16_t)-d1;
        d2 = -0x4E;
        if (tm->flags & 0x10) d1 = d2;
        else if (d2 > pucky()) {                          /* puck in the defensive zone */
            d2 = -0x108;
            if (!(d2 < pucky())) d1 = d2;                 /* behind the goal line */
        } else
            d1 = pucky();
    } else {
        d2 = 0x4E;
        if (tm->flags & 0x10) d1 = d2;
        else if (d2 < pucky()) {
            d2 = 0x108;
            if (!(d2 > pucky())) d1 = d2;
        } else
            d1 = pucky();
    }
    if ((int16_t)(puckx() ^ d0) < 0) skateto(d0, d1, NULL, o);
    else skateto(puckx(), d1, NULL, o);
}

/* logic94.asm:asswingo. Player a3 is a winger on offense: skate to a random spot in the zone
 * the puck is in (or will be), evading the puck carrier. a2 = his team. */
void asswingo(struct Team *tm, struct SortObj *o)
{
    if (PFLAG(o, pfalock)) return;
    if (g.gmode & (1 << gmclock)) { donothing(o); return; }
    if (check4bench(o)) return;
    if (PFLAG(o, pfjoycon)) return;
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        ST_W(o->temp5);                                   /* old zone number */
        o->temp1 = 0;
        o->temp2 = 8;
    }
    if (aidef_timer(o)) {
        int16_t d1 = puckc();
        if (d1 >= 0 && (int16_t)((d1 - 6) ^ (o->SCnum - 6)) < 0) {   /* the other team has the puck */
            assreplace(3, o);                             /* asswingd */
            return;
        }
        int16_t d0 = pucky();
        int16_t d3 = (int16_t)((puckvy() >> 6) + d0);
        if (!PFLAG(o, pfgoal)) { d0 = (int16_t)-d0; d3 = (int16_t)-d3; }
        int16_t zone = 0;                                 /* byte offset into the zone table */
        if (d3 >= -0x58) {
            zone = 8;
            if (d0 >= 0x58 && !(tm->flags & 0x10)) {
                zone = 0x10;
                if (d3 >= 0x108) zone = 0x18;
            }
        }
        if (zone != o->temp5 || (vdp_hv16() & 0x7F) == 0) {   /* a new zone, or now and then while waiting */
            o->temp5 = zone;
            zone_target(wingo_zones, (unsigned)zone / 8, o);
        }
    }
    int16_t d0 = o->temp3;
    if (o->position != 5) d0 = (int16_t)-d0;              /* left wing mirrors */
    int16_t d1 = o->temp4;
    if (!PFLAG(o, pfgoal)) { d0 = (int16_t)-d0; d1 = (int16_t)-d1; }
    skateto(d0, d1, EvadePC, o);
}

/* logic94.asm:asscenterd. Player a3 is the center on defense: stay between the puck and his
 * own high slot. */
void asscenterd(struct Team *tm, struct SortObj *o)
{
    if (PFLAG(o, pfalock)) return;
    if (g.gmode & (1 << gmclock)) { donothing(o); return; }
    if (check4bench(o)) return;
    if (PFLAG(o, pfjoycon)) return;
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        o->temp1 = 0;
        o->temp2 = 8;
    }
    if (aidef_timer(o)) {
        int16_t d1 = puckc();
        if (d1 >= 0 && (int16_t)((d1 - 6) ^ (o->SCnum - 6)) >= 0) {  /* his team has the puck */
            assreplace(6, o);                             /* asscentero */
            return;
        }
    }
    int16_t d0 = (int16_t)(puckx() >> 1);
    int16_t d2 = pucky();
    if (!PFLAG(o, pfgoal)) d2 = (int16_t)-d2;
    int16_t d1 = -128;                                    /* own high slot */
    if (d2 >= -0x58)                                      /* puck not in the defensive zone */
        d1 = (int16_t)((int16_t)(d1 + pucky()) >> 1);
    if (!PFLAG(o, pfgoal)) d1 = (int16_t)-d1;
    skateto(d0, d1, NULL, o);
}

/* logic94.asm:asscentero. Player a3 is the center on offense. */
void asscentero(struct Team *tm, struct SortObj *o)
{
    if (PFLAG(o, pfalock)) return;
    if (g.gmode & (1 << gmclock)) { donothing(o); return; }
    if (check4bench(o)) return;
    if (PFLAG(o, pfjoycon)) return;
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        ST_W(o->temp5);
        o->temp1 = 0;
        o->temp2 = 8;
    }
    if (aidef_timer(o)) {
        int16_t d1 = puckc();
        if (d1 >= 0 && (int16_t)((d1 - 6) ^ (o->SCnum - 6)) < 0) {
            assreplace(5, o);                             /* asscenterd */
            return;
        }
        int16_t d0 = pucky();
        if (!PFLAG(o, pfgoal)) d0 = (int16_t)-d0;
        int16_t zone = 0;
        if (d0 >= -0x58) {
            zone = 8;
            if (d0 >= 0x58 && !(tm->flags & 0x10)) {
                zone = 0x10;
                if (d0 >= 0x108) zone = 0x18;
            }
        }
        if (zone != o->temp5 || (vdp_hv16() & 0x7F) == 0) {
            o->temp5 = zone;
            zone_target(centero_zones, (unsigned)zone / 8, o);
        }
    }
    int16_t d0 = o->temp3, d1 = o->temp4;
    if (!PFLAG(o, pfgoal)) d1 = (int16_t)-d1;             /* (only Y, unlike the wings: the center's X is kept) */
    skateto(d0, d1, EvadePC, o);
}

/* logic94.asm:assgoaliectrl. Joypad controlled goalie: out of the crease area the pad takes
 * over (pf3gctrl) and checkanim runs; else time his hold on the puck and his timer. (The
 * beq.w *+4 at the end targets the rts: it never falls into assgoaliecpu.) */
void assgoaliectrl(struct Team *tm, struct SortObj *o)
{
    if (!PFLAG(o, pfjoycon)) { assexit(o); return; }
    if (!PFLAG(o, pfna)) {
        int16_t d0 = o->Xpos, d1 = o->Ypos;
        if (d1 <= 0xAA && d1 >= -0xAA && !(g.gmode & (1 << gmclock)))
            AddPenalty2(8, o);                            /* stoppage: goalie out of range */
        d0 = (int16_t)(d0 - (int16_t)g.Hpos);
        d1 = (int16_t)(d1 - (int16_t)g.Vpos);
        o->pflags3 &= (uint8_t)~(1 << pf3gctrl);
        if (d0 >= 0x74 + view_more() || d0 <= -0x74 - view_more() || d1 >= 0x64 || d1 <= -0x64)   /* (off screen) */
            o->pflags3 |= 1 << pf3gctrl;
        if (o->pflags3 & (1 << pf3gctrl)) { checkanim(tm, o); return; }
    }
    if (PFLAG(o, pfalock)) return;
    if (check4bench(o)) return;
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        o->temp1 = 0;
        o->temp2 = 8;
        ST_W(o->temp4);
    }
    if (o->pflags2 & 2) return;                           /* animation in progress */
    if (g.gmode & (1 << gmclock)) return;
    if (o->temp5 >= 0) {
        if (o->SCnum == puckc()) {
            o->temp5 = (int16_t)(o->temp5 - elapsed);
            if (o->temp5 < 0) {                           /* held the puck too long */
                if (g.BA_PS_flags & HI(2)) g.BA_PS_flags |= HI(4);
                else AddPenalty2(8, o);
            }
        } else
            ST_W(o->temp5);
    }
    if (TICK_HIB(o->temp1) >= 0) return;
    goalie_timer_reload(o);
}

/* logic94.asm:assgoaliecpu. a3 = goalie. */
void assgoaliecpu(struct Team *tm, struct SortObj *o)
{
    if (PFLAG(o, pfjoycon)) { assgoaliectrl(tm, o); return; }
    checkanim(tm, o);
}

/* logic94.asm:checkanim. The goalie's own play: the stick slam after a goal, skating back to
 * the crease, passing the puck when he has it, diving on a loose puck, and reading the shot
 * (puckcross) to pick a save or move across the crease. Ends in playeracc or stopna. */
void checkanim(struct Team *tm, struct SortObj *o)
{
    g.TmpPuckX = (uint16_t)puckx();
    if ((g.sflags4 & HI(0)) && !(o->pflags2 & 2)) {
        if (g.sflags4 & HI(1)) {
            if (!(g.sflags4 & HI(3)) && o->frame == SPFslamtop && o->frame == SPFslambot) {   /* never both: dead */
                g.sflags4 |= HI(3);
                sfx(SYM_SFXcheck);
            }
        } else if ((int16_t)(pucky() ^ o->Ypos) >= 0 && o->Xpos <= 0x14 && o->Xpos > -0x14) {
            g.sflags4 |= HI(1);
            g.sflags4 &= (uint16_t)~HI(3);
            SetSPA(o, PFLAG(o, pfgoal) ? SPAgslambot : SPAgslamtop);   /* stick slam after a goal */
            o->pflags2 |= 2;
        }
    }
    if (PFLAG(o, pfalock)) return;
    if (check4bench(o)) return;
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        o->temp1 = 0;
        o->temp2 = 8;
        ST_W(o->temp4);
    }
    int in_crease = o->Xpos <= 0x34 && o->Xpos >= -0x34 && o->Ypos <= 0x10E && o->Ypos >= -0x10E
                    && (o->Ypos > 0xD2 || o->Ypos < -0xD2);
    if (!in_crease) {                                     /* skate back to the middle */
        int16_t d0 = o->Xpos >= 0 ? 4 : -4;
        int16_t d1 = PFLAG(o, pfgoal) ? -0xE4 : 0xE4;
        skateto(d0, d1, NULL, o);
        return;
    }
    if (o->pflags2 & 2) return;
    if (g.sflags4 & HI(0)) {
        if (o->frame == SPFslamtop + 1) o->facedir = 0;
        else if (o->frame == SPFslambot + 1) o->facedir = 4;
    }
    SetSPA(o, SPAgready);
    if (!(g.BA_PS_flags & HI(2)) && (g.gmode & (1 << gmclock)))
        return;
    if (o->temp5 >= 0) {
        if (o->SCnum == puckc()) {
            o->temp5 = (int16_t)(o->temp5 - elapsed);
            if (o->temp5 < 0) {
                if (g.BA_PS_flags & HI(2)) g.BA_PS_flags |= HI(4);
                else AddPenalty2(8, o);                   /* PenGhold: whistle for the faceoff */
            }
        } else
            ST_W(o->temp5);
    }
    int16_t d0, d1, d2, d3;
    const struct PuckCross *pc;
    if (TICK_HIB(o->temp1) >= 0)
        goto nodec;
    goalie_timer_reload(o);
    if (PFLAG(o, pfjoycon) && !(o->pflags3 & (1 << pf3gctrl))) {
        assinsert(0x1D, o);                               /* assgoaliectrl */
        return;
    }
    d0 = pucky(); d1 = o->Ypos;
    if ((int16_t)(d0 ^ d1) < 0) {                         /* puck in the other half */
        d0 = 0;
        d2 = PFLAG(o, pfgoal) ? -0xF4 : 0xF4;
        goto de5;
    }
    o->temp4 = (int16_t)(o->temp4 - 1);
    if (o->temp4 < 0) o->temp4 = -1;
    if (o->SCnum == puckc()) {
        if (o->temp5 < 0) o->temp5 = 0x5A;
        ST_W(o->temp4);
        if (o->temp5 <= 0x5A && (vdp_hv16() & 3) == 0) {
            struct SortObj *a0 = sortobj(PFLAG(o, pfteam) ? 0 : 6);   /* the other team */
            for (int i = 0; i < 6; i++, a0++) {
                if (a0->pflags2 & 4) continue;
                d1 = (int16_t)(pucky() - a0->Ypos);
                if (d1 > 0x1C || d1 < -0x1C) continue;
                d1 = (int16_t)(puckx() - a0->Xpos);
                if (d1 > 0x19) continue;
                if (d1 > -0x19) goto de1;                 /* an opponent is on him */
            }
            g.threat = 1;
            if (chk4pass(o)) return;
        }
        goto de1;
    }
    /* nobody has the puck: dive on it when it is loose and slow beside him */
    if (o->temp4 != 0) goto de1;
    if (puckc() >= 0) goto de1;
    d0 = (int16_t)(puckx() - o->Xpos);
    if (d0 > 0x14 || d0 < -0x14) goto de1;
    d1 = pucky();
    if (d1 > 0x108 || d1 < -0x108) goto de1;
    d1 = (int16_t)(d1 - o->Ypos);
    if (d1 > 0x1E || d1 < -0x1E) goto de1;
    {
        int16_t vx = puckvx(), vy = puckvy();
        if (vx < 0) vx = (int16_t)-vx;
        if (vy < 0) vy = (int16_t)-vy;
        if ((int16_t)(vx + vy) > 0x1000) goto de1;
    }
    o->facedir = (int16_t)vtoa(d0, d1);                   /* face the puck */
    o->nopuck[0] = 8;
    SetSPA(o, SPAgdive);
    o->pflags2 |= 2;
    g.crowdlevel += 0x96;
    return;
de1:
    pc = &g.puckcross[0];                                 /* x / frames to the goal line, top then bottom */
    d3 = 0x104;
    if (PFLAG(o, pfgoal)) { pc = &g.puckcross[1]; d3 = (int16_t)-d3; }
    if (o->Ypos > 0x104 || o->Ypos <= -0x104) {          /* behind the goal line: back to the post */
        d2 = 0;
        d0 = 0;
        if (o->Xpos > 0x2C || o->Xpos < -0x2C) goto de5;
        d0 = o->Xpos >= 0 ? 0x88 : -0x88;
        goto de5;
    }
    if (puckc() >= 0 && o->SCnum != puckc()) {
        const struct SortObj *car = sortobj(puckc());
        d0 = (int16_t)(puckx() - car->Xpos);
        d0 = (int16_t)((d0 >> 1) + car->Xpos);
        g.TmpPuckX = (uint16_t)d0;
    }
    d0 = (int16_t)g.TmpPuckX;
    d1 = pucky();
    ClampYPosition(&d0, &d1, d3, o);
    d0 = (int16_t)vtoa(d0, d1);
    AdjustFacingDirection(d0, o);
    d0 = (int16_t)(((g.gameclock & 7) << 4) + 0xA0);     /* frames ahead to look */
    if (pucky() > 0xDB || pucky() <= -0xDB)
        d0 = (int16_t)(d0 - 0x40);
    d1 = d0;
    d0 = (int16_t)((int16_t)(((int32_t)d0 * puckvx()) >> 16) + (int16_t)g.TmpPuckX);   /* where the puck will be */
    d1 = (int16_t)((int16_t)(((int32_t)d1 * puckvy()) >> 16) + pucky());
    ClampYPosition(&d0, &d1, d3, o);
    {
        int32_t dsq = sq(d0) + sq(d1);
        if ((uint32_t)dsq > 0x384) {                      /* far: scale the move down */
            d2 = (int16_t)(1 + (int16_t)Sroot((uint32_t)dsq));
            int16_t d4 = 0x12;
            if (g.sflags & HI(sfssdir)) d4 = (int16_t)(d4 + 8);
            int32_t p1 = (int32_t)d1 * d4;
            d4 = (int16_t)(d4 + 8);
            int32_t p0 = (int32_t)d0 * d4;
            d0 = (int16_t)m68k_divs(p0, d2);
            d1 = (int16_t)m68k_divs(p1, d2);
        }
    }
    d1 = (int16_t)(d1 + d3);
    d2 = d1;
    if (!(g.gmode2 & HI(0)) && !(g.BA_PS_flags & HI(2)) && (g.sflags & HI(sfssdir))
        && (pucky() > 0xD0 || pucky() < -0xD0)
        && o->Xpos <= 0x14 && o->Xpos >= -0x14 && !(g.gameclock & 2))
        goto save;
    if (pc->frames > 0x22) goto de5;                      /* not reaching the line soon (or never) */
    if (pc->x > 0x18 || pc->x < -0x18) {                               /* wide of the net */
        if (puckc() >= 0 || (g.iflags & 4)) goto de5;
        {
            struct Team *a1 = team_struct(0), *a2 = team_struct(1);
            if (PFLAG(o, pfteam)) { struct Team *t = a1; a1 = a2; a2 = t; }
            if ((int32_t)a1->neardist < 0x1324) goto de5;
            if ((int32_t)a2->neardist < 0x9C4) goto de5;
        }
        if (pucky() <= 0xE0 && pucky() > -0xE0) goto de5;
        {
            int n = puckvy() < 0;
            if (PFLAG(o, pfgoal)) n = !n;
            if (n) goto de5;                              /* moving away from the net */
            int16_t vx = puckvx(), vy = puckvy();
            if (vx < 0) vx = (int16_t)-vx;
            if (vy < 0) vy = (int16_t)-vy;
            if (vy < vx) goto de5;
        }
        assinsert(0xF, o);                                /* go get it behind the net */
        return;
    }
    if (pc->frames > 0xC) goto twenty;
save:
    if (pucky() <= 0x108 && pucky() >= -0x108 && !(o->pflags2 & 2)) {
        o->pflags2 |= 2;
        goaliesave(pc, d3, o);
    }
twenty:
    d0 = pc->x;
    if (pucky() > 0xFC || pucky() <= -0xFC)
        d0 = (int16_t)g.TmpPuckX >= 0 ? 0x18 : -0x18;
de5:
    d1 = d2;
    d0 = (int16_t)(-(int16_t)HIB(o->Xvel) + d0 - o->Xpos);
    d1 = (int16_t)(-(int16_t)HIB(o->Yvel) + d1 - o->Ypos);
    if (d0 <= 4 && d0 >= -4 && d1 <= 4 && d1 >= -4) { d0 = 0; d1 = 0; }
    d0 = (int16_t)vtoa(d0, d1);
    SET_LOB(o->temp2, d0);
nodec:
    d2 = LOB(o->temp2);
    if (d2 <= 7) playeracc(d2, o);
    else stopna(o);
}

/* logic94.asm:ClampYPosition. d1 = y clamped to +-$103, minus goal line d3. d0 = 0 if a3 has
 * the puck. */
void ClampYPosition(int16_t *d0, int16_t *d1, int16_t goalline, const struct SortObj *o)
{
    if (puckc() == o->SCnum) *d0 = 0;
    if (*d1 >= 0x103) *d1 = 0x103;
    if (*d1 <= -0x103) *d1 = -0x103;
    *d1 = (int16_t)(*d1 - goalline);
}

/* logic94.asm:AdjustFacingDirection. Turn facedir one step toward direction d0. A computer
 * goalie facing 1 or 6 cannot turn through the directions away from his net. */
void AdjustFacingDirection(int16_t dir, struct SortObj *o)
{
    int16_t d1 = o->facedir;
    int16_t d0 = (int16_t)(dir - d1);
    if (d0 == 0) return;
    d0 = (int16_t)(((-d0) & 4) >> 1);
    d0 = (int16_t)(d0 - 1);                               /* +1 / -1 */
    if (!PFLAG(o, pfjoycon) && ((0x42 >> (d1 & 7)) & 1)) {
        d1 = (int16_t)(d1 + d0);
        unsigned mask = PFLAG(o, pfgoal) ? 0x38 : 0x83;
        if ((mask >> (d1 & 7)) & 1)
            d1 = (int16_t)(d1 - d0 - d0);                 /* turn the other way */
        o->facedir = (int16_t)(d1 & 7);
        return;
    }
    o->facedir = (int16_t)((d1 + d0) & 7);
}

/* logic94.asm:goaliesave. Set the save animation for goalie a3 from where the puck will cross
 * (a0 = puckcross entry, d3 = his goal line): the direction of the shot relative to his facing
 * picks the side, the height and distance pick the type. Slows him down. */
void goaliesave(const struct PuckCross *pcross, int16_t goalline, struct SortObj *gk)
{
    int16_t d0 = (int16_t)(pcross->x - gk->Xpos);
    int16_t d1 = (int16_t)(goalline - gk->Ypos);
    d0 = (int16_t)((vtoa(d0, d1) - gk->facedir) & 7);
    int16_t d3 = d0;
    d0 = (int16_t)(d0 >> 2);
    if (gk->attribute & HI(3)) d0 ^= 1;
    if (puckz() > 8 || puckvz() > 0x800) {                /* a high shot */
        int16_t dx = (int16_t)(gk->Xpos - pcross->x);
        if (dx <= 0x10 && dx >= -0x10) d0 = (int16_t)(d0 + 6);
    } else {
        d0 = (int16_t)(d0 + 4);
        if (pcross->frames <= 8) {                        /* arriving now */
            int16_t dx = (int16_t)(gk->Xpos - pcross->x);
            if (dx > 0x10 || dx <= -0x10) d0 = (int16_t)(d0 + 4);
        } else if (gk->facedir != 2 && gk->facedir != 6 && puckc() >= 0) {
            d0 = (int16_t)(d0 - 2);
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
            if (gk->handed[0] & 1) d0 ^= 1;               /* left handed goalie */
        }
    }
    d1 = (int16_t)saveanim[d0];
    if (d1 == SPAgglovel && (d3 & 3) != 0 && (int8_t)gk->aggress >= 0xB)
        d1 = SPAgglovel2;                                 /* reach out glove save */
    SetSPA(gk, (uint16_t)d1);
    g.crowdlevel += 0x96;
    g.CwdExciteLvl += 0xA;
    gk->Xvel = (int16_t)(gk->Xvel >> 2);
    gk->Yvel = (int16_t)(gk->Yvel >> 2);
}

/* logic94.asm:assgoalietopuck. The goalie skates to a loose puck coming his way when no
 * skater of either team is close to it. */
void assgoalietopuck(struct Team *tm, struct SortObj *o)
{
    if (PFLAG(o, pfjoycon)) { assexit(o); return; }
    if (g.gmode & (1 << gmclock)) { assexit(o); return; }
    if (check4bench(o)) return;
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        o->temp1 = 0;
    }
    if (TICK_HIB(o->temp1) < 0) {
        goalie_timer_reload(o);
        if (puckc() >= 0) { assexit(o); return; }
        int n = puckvy() < 0;
        if (PFLAG(o, pfgoal)) n = !n;
        if (n) { assexit(o); return; }
        struct Team *a1 = team_struct(0), *a2 = team_struct(1);
        if (PFLAG(o, pfteam)) { struct Team *t = a1; a1 = a2; a2 = t; }
        if ((int32_t)a1->neardist < 0xE10) { assexit(o); return; }
        if ((int32_t)a2->neardist < 0x640) { assexit(o); return; }
    }
    skatetopuck(o);
}

/* logic94.asm:breakaway. Puck carrier a3 on a breakaway: beep on a new assignment and check
 * the skating direction toward the net. Returns the N flag: set when he is not skating at it. */
int breakaway(struct SortObj *o)
{
    g.sflags5 |= 4;
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        sfx(SYM_SFXbeep1);
        breakaway_crowd(o);
    }
    if (o->Yvel == 0) return 1;
    return (int16_t)(o->Ypos ^ o->Yvel) < 0;
}

/* logic94.asm:BreakawayOffsidesFlagSet. On a new assignment clear the offsides and breakaway
 * bits of a3. Behind the goal line or not yet in the attack zone set pf3oside; in the zone with
 * no defender between him and the net set pf3break. Returns d0 = 1 for a breakaway. */
int BreakawayOffsidesFlagSet(struct SortObj *o)
{
    if (PFLAG(o, pfna))
        o->pflags3 &= (uint8_t)~((1 << pf3oside) | (1 << pf3break));
    int16_t d1 = o->Ypos;
    if (d1 < 0) d1 = (int16_t)-d1;
    if (d1 >= 0x108) return 0;                            /* behind the goal line */
    d1 = o->Ypos;
    int inzone;
    if (PFLAG(o, pfgoal)) inzone = 0x58 < d1;
    else inzone = !(-0x58 < d1);
    if (!inzone) {
        o->pflags3 |= 1 << pf3oside;
        return 0;
    }
    int was = o->pflags3 & (1 << pf3oside);
    o->pflags3 &= (uint8_t)~(1 << pf3oside);
    if (!was) return 0;                                   /* on the blue line */
    const struct SortObj *a0 = sortobj(11);
    for (int i = 0; i < 12; i++, a0--) {                  /* any skater of either team nearer the net? */
        if (d1 < 0) { if (d1 <= a0->Ypos) continue; }
        else { if (d1 >= a0->Ypos) continue; }
        if (a0->position == 0) continue;                  /* goalies do not count */
        return 0;
    }
    o->pflags3 |= 1 << pf3break;
    return 1;
}

/* logic94.asm:chkpuckc. Puck carrier a3: check for a breakaway (not in a shootout or penalty
 * shot), else assign asspuckc. */
void chkpuckc(struct Team *tm, struct SortObj *o)
{
    if (!(g.gmode2 & HI(0)) && !(g.BA_PS_flags & HI(2)) && !breakaway(o)) {
        asspuckc(tm, o);                                  /* still skating at the net in y */
        return;
    }
    o->pflags3 &= (uint8_t)~(1 << pf3break);
    assreplace(0x10, o);                                  /* asspuckc */
    asspuckc(tm, o);
}

/* logic94.asm:asspuckc. Player a3 is the puck handler: carry it up the ice to a spot from
 * .postab2 (the shootout path instead), looking for a shot or a pass now and then. */
void asspuckc(struct Team *tm, struct SortObj *o)
{
    if (g.sflags5 & 4) g.sflags5 &= (uint8_t)~4;
    else o->pflags3 &= (uint8_t)~(1 << pf3break);
    if (puckc() != o->SCnum) { assexit(o); return; }
    if ((g.BA_PS_flags & HI(2)) && (int16_t)g.msgtimer > 1) return;
    if (!(o->pflags3 & (1 << pf3break)) && !(g.BA_PS_flags & HI(2)) && BreakawayOffsidesFlagSet(o))
        assreplace(0x21, o);                              /* chkpuckc */
    if (PFLAG(o, pfalock)) return;
    if (!(g.BA_PS_flags & HI(2)) && (g.gmode & (1 << gmclock))) { donothing(o); return; }
    if (PFLAG(o, pfjoycon)) { assexit(o); return; }
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        if (o->pflags3 & (1 << pf3break)) sfx(SYM_SFXbeep1);
        if (o->pflags3 & (1 << pf3break)) breakaway_crowd(o);
        o->temp1 = 0;
        o->temp2 = 8;
        o->temp3 = (int16_t)(vdp_hv16() & 3);
    }
    if (TICK_HIB(o->temp1) < 0) {
        SET_HIB(o->temp1, o->aioff);
        if ((ReadGoaliePulled(o) & CCR_N) || ((g.sflags7 & 0x40) && HIB(o->temp1) != 0))
            SET_HIB(o->temp1, HIB(o->temp1) - 1);
        if ((g.BA_PS_flags & HI(2)) || (g.gmode2 & HI(0))) {
            if (!ShootoutShootCheck(o)) goto nodec;
            CompShoot(o);                                 /* ends the assignment */
            return;
        }
        checkob(o);
        if (chk4lc(tm, o)) return;
        if ((g.gmode2 & HI(0)) || (g.BA_PS_flags & HI(2))) {
            if (!ShootoutShootCheck(o)) goto nodec;
            CompShoot(o);
            return;
        }
        int shot = 1;
        if (!(o->pflags3 & (1 << pf3break))) {
            uint16_t d0 = PFLAG(o, pfteam) ? 2 : 1;
            if (d0 != g.cont1team && d0 != g.cont2team && (g.vcount & 2))
                shot = 0;
        }
        if (shot && chk4shot(o)) return;
        if (chk4pass(o)) return;
    }
nodec:;
    int16_t d0 = (int16_t)(6 + o->temp3);
    if (g.sflags2 & HI(sf2offsig)) d0 = o->position;
    int16_t d1 = (int16_t)puckc_postab[d0 * 2 + 1];
    d0 = (int16_t)puckc_postab[d0 * 2];
    int path = (g.BA_PS_flags & HI(2)) || (g.gmode2 & HI(0));
    if (path) {
        SkatePath(&d0, &d1, o);
        if ((g.sopathx >> 8) == 0x80) return;
    }
    if (!PFLAG(o, pfgoal)) { d0 = (int16_t)-d0; d1 = (int16_t)-d1; }
    skateto(d0, d1, path ? NULL : asspuckc_chkdir, o);
}

/* logic94.asm:chk4lc. See if the computer should call a line change: with the puck in the
 * neutral zone, now and then, when the line's energy is low. Returns 1 when it did (CompShoot
 * ends the puck carrier's assignment). */
int chk4lc(struct Team *tm, struct SortObj *o)
{
    if (g.sflags7 & 0x10) return 0;
    int16_t d0 = pucky();
    if (!PFLAG(o, pfgoal)) d0 = (int16_t)-d0;
    if (d0 < 0) return 0;
    if (d0 > 0x58) return 0;
    if (vdp_hv16() & 3) return 0;
    if (o->pflags2 & 8) return 0;
    struct Team *a2 = team_struct(0), *a1 = team_struct(1);
    if (PFLAG(o, pfteam)) { struct Team *t = a2; a2 = a1; a1 = t; }
    if (AvgCline(a2) > 0xC00) return 0;
    CompLine(a2, a1);
    setpersonel(a2);
    printscores1();
    CompShoot(o);
    return 1;
}

/* logic94.asm:chkpk. Z set if killing a penalty. */
int chkpk(struct SortObj *o)
{
    if (g.sflags2 & HI(sf2pwrplay)) return chkpk2(o);
    return 0;
}

/* logic94.asm:chkpk2. Z set when sf2pwrtm (team on the power play) differs from pfteam. */
int chkpk2(struct SortObj *o)
{
    return ((g.sflags2 & HI(sf2pwrtm)) != 0) != (PFLAG(o, pfteam) != 0);
}

/* logic94.asm:chk4shot. Computer puck carrier a3 looks for a shot. Returns 1 when he shoots
 * (CompShoot ends the assignment). */
int chk4shot(struct SortObj *o)
{
    if (chkpk(o) && g.threat != 0 && (vdp_hv16() & 3) == 0) {   /* on the power play, pressured */
        int16_t d0 = 0x58;
        if (PFLAG(o, pfgoal)) {
            d0 = -0x58;
            if (d0 < o->Ypos) { CompShoot(o); return 1; } /* clear the puck */
        } else if (d0 > o->Ypos) { CompShoot(o); return 1; }
        g.sflags5 |= 8;
        dopass(o);
        return 0;
    }
    if (o->pflags3 & (1 << pf3break)) {
        int16_t d0 = (int16_t)(randomd0(0x1E) + 0x82);
        int shoot = d0 < pucky();
        if (!shoot) { d0 = (int16_t)-d0; shoot = d0 > pucky(); }
        if (shoot) {
            d0 = (int16_t)(randomd0s(2) + o->facedir);
            o->facedir = (int16_t)(d0 & 7);
            CompShoot(o);
            return 1;
        }
    }
    uint16_t d4 = (uint16_t)((0x20 - (o->spodds >> 1)) & 0xFF);
    d4 = (uint16_t)(d4 << 4);
    int16_t d1 = 0x108;
    if (!PFLAG(o, pfgoal)) d1 = (int16_t)-d1;
    d1 = (int16_t)(d1 - pucky());
    int16_t d0 = (int16_t)-puckx();
    if ((uint32_t)(sq(d0) + sq(d1)) <= 0x2710) {          /* close to the net: odds per open lane */
        d4 = (uint16_t)(d4 >> 4);
        int16_t d5 = (int16_t)vtoa(d0, d1);
        const struct SortObj *a1 = opponents(o);
        for (int d3 = 5; d3 >= 0; d3--, a1++) {
            if (a1->position != 0) {
                if (vtoa((int16_t)(a1->Xpos - puckx()), (int16_t)(a1->Ypos - pucky())) == (uint16_t)d5)
                    d4 = (uint16_t)(d4 << 1);             /* a defender in the lane */
            } else if (a1->pflags2 & 2) {
                d3 = 0;                                   /* the goalie is down: shoot */
                d4 = 1;
            }
        }
    }
    d0 = (int16_t)randomd0(d4);
    if (o->spodds & 1) { if (d0 > 7) return 0; }
    else if (d0 > 8) return 0;
    d0 = pucky();
    if (!PFLAG(o, pfgoal)) d0 = (int16_t)-d0;
    if (d0 < 0) return 0;
    d1 = (int16_t)(0x108 - d0);
    if (d1 < 0) return 0;
    if (g.sflags2 & HI(sf2offsig)) return 0;
    CompShoot(o);
    return 1;
}

/* logic94.asm:CompShoot. Computer shot: drop the return address (the caller's assignment
 * ends) and aim from the puck's distance to the goal line. */
void CompShoot(struct SortObj *o)
{
    int16_t d0 = pucky();
    if (!PFLAG(o, pfgoal)) d0 = (int16_t)-d0;
    uint16_t d1 = (uint16_t)(0x108 - d0) >> 3;
    if ((int16_t)d1 >= 0x14) d1 = 0x14;
    o->temp2 = (int16_t)d1;
    assreplace(0x12, o);                                  /* assshoot */
}

/* logic94.asm:chk4pass. Computer puck carrier a3 looks for a good pass to a random teammate.
 * Returns 1 when a skater passed (the assignment ends). */
int chk4pass(struct SortObj *o)
{
    if (g.threat == 0) {
        if (randomd0((uint16_t)((0x10 + o->spodds) & 0xFF)) > 0xC) return 0;
    }
    int16_t d0 = (int16_t)randomd0(6);
    if (o->SCnum >= 6) d0 = (int16_t)(d0 + 6);
    if (o->position != 0 && puckx() <= 0x28 && puckx() >= -0x28) {
        if (pucky() > 0xCC) return 0;                     /* in the slot: no pass */
        if (pucky() < -0xCC) return 0;
    }
    if (d0 == o->SCnum) return 0;
    const struct SortObj *a0 = sortobj(d0);
    if (a0->position <= 0) return 0;
    if (a0->pflags2 & 4) return 0;
    if (PFLAG(a0, pfalock)) return 0;
    d0 = a0->Ypos;
    int16_t d1 = o->Ypos;
    if (!PFLAG(o, pfgoal)) { d0 = (int16_t)-d0; d1 = (int16_t)-d1; }
    if (g.gmode & (1 << gmoffs)) {
        if ((int16_t)((int16_t)(d0 - 0x58) ^ (int16_t)(d1 - 0x58)) < 0) return 0;   /* across the blue line */
    }
    if (!(d0 > 0x58)) {
        d0 = (int16_t)(d0 - d1);
        if (d0 < -0xF) return 0;                          /* too far behind him */
    }
    int16_t dx = (int16_t)(a0->Xpos - puckx()), dy = (int16_t)(a0->Ypos - pucky());
    g.passdir = vtoa(dx, dy);
    int32_t d2 = sq(dx) + sq(dy);
    const struct SortObj *a1 = opponents(o);
    for (int d3 = 5; d3 >= 0; d3--, a1++) {              /* an opponent nearer in that direction? */
        int16_t ox = (int16_t)(a1->Xpos - puckx()), oy = (int16_t)(a1->Ypos - pucky());
        if ((uint32_t)(sq(ox) + sq(oy)) > (uint32_t)d2) continue;
        if (vtoa(ox, oy) == g.passdir) return 0;
    }
    dopass(o);
    if (o->position == 0) return 0;
    assexit(o);
    return 1;
}

/* logic94.asm:EvadePC. Player a3 should avoid the puck carrier if he's on their team: with the
 * carrier close, skate away from him; with offsides on, along the blue line around the puck. */
uint16_t EvadePC(uint16_t dir, struct SortObj *o)
{
    if (puckc() < 0) return dir;
    int16_t d2 = (int16_t)(int8_t)((uint8_t)HIB(o->Xvel) - (uint8_t)HIB(puckvx()));
    d2 = (int16_t)(d2 + o->Xpos - puckx());
    if (d2 > 0x28 || d2 < -0x28) return dir;
    int16_t d1 = (int16_t)(int8_t)((uint8_t)HIB(o->Yvel) - (uint8_t)HIB(puckvy()));
    d1 = (int16_t)(d1 + o->Ypos - pucky());
    if (d1 > 0x28 || d1 < -0x28) return dir;
    uint16_t d0 = vtoa((int16_t)(o->Xpos - puckx()), (int16_t)(o->Ypos - pucky()));
    if (!(g.gmode & (1 << gmoffs))) return d0;
    d1 = o->Ypos;
    if (!PFLAG(o, pfgoal)) d1 = (int16_t)-d1;
    d1 = (int16_t)(d1 - 0x58);
    if (d1 > 0xA) return d0;
    if (d1 < -50) return d0;
    if (o->Xpos > puckx()) return 2;
    return 6;
}

/* logic94.asm:checkob. Bring up the little warning ref when a player on a3's team is over
 * the blue line ahead of the puck (offsides on). */
void checkob(struct SortObj *o)
{
    if (!(g.gmode & (1 << gmoffs))) return;
    if (g.BA_PS_flags & HI(2)) return;
    const struct SortObj *a0 = teammates(o);
    int16_t d1 = 0x5C;
    int over = 0;
    if (!PFLAG(o, pfgoal)) {
        d1 = (int16_t)-d1;
        if (!(d1 > pucky()))
            for (int i = 0; i < 6; i++, a0++)
                if (a0->position >= 0 && d1 > a0->Ypos) { over = 1; break; }
    } else if (!(d1 < pucky()))
        for (int i = 0; i < 6; i++, a0++)
            if (a0->position >= 0 && d1 < a0->Ypos) { over = 1; break; }
    uint16_t d0;
    if (over) {
        d0 = 6;
        if (g.sflags2 & HI(sf2offsig)) return;
        g.sflags2 |= HI(sf2offsig);
    } else {
        d0 = 0x40;                                        /* clears the ref window */
        if (!(g.sflags2 & HI(sf2offsig))) return;
        g.sflags2 &= (uint16_t)~HI(sf2offsig);
    }
    if ((int16_t)g.RefCnt >= 0) return;
    PushRef(d0);
}

/* logic94.asm:assbreakaway. Assignment for a breakaway: the carrier keeps it while he skates
 * at the net, everyone else (and the carrier once he turns) goes to assnearest. */
void assbreakaway(struct Team *tm, struct SortObj *o)
{
    if (o->SCnum == puckc() && !breakaway(o)) {
        assnearest(tm, o);
        return;
    }
    o->pflags3 &= (uint8_t)~(1 << pf3break);
    assreplace(0x11, o);                                  /* assnearest */
    assnearest(tm, o);
}

/* logic94.asm:assnearest. A special assignment used for the player who is nearest the puck
 * but doesn't have it: find his team's nearest skater (tmneardist), hand the assignment to him
 * if it is someone else, else chase the puck (or the carrier) and look for a check. */
void assnearest(struct Team *tm, struct SortObj *o)
{
    if (g.sflags5 & 4) g.sflags5 &= (uint8_t)~4;
    else o->pflags3 &= (uint8_t)~(1 << pf3break);
    if (o->SCnum == puckc() && !(o->pflags3 & (1 << pf3break)) && !(g.BA_PS_flags & HI(2))
        && BreakawayOffsidesFlagSet(o))
        assreplace(0x22, o);                              /* assbreakaway */
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        if (o->pflags3 & (1 << pf3break)) sfx(SYM_SFXbeep1);
        if (o->pflags3 & (1 << pf3break)) breakaway_crowd(o);
        o->temp3 = 0;
        o->temp2 = 8;
        o->temp1 = 0;
    }
    if (o->SCnum == puckc()) {                            /* he picked it up */
        if (o->position == 0) { assgoaliecpu(tm, o); return; }
        if (!PFLAG(o, pfjoycon)) assinsert(0x10, o);      /* asspuckc */
        return;
    }
    int16_t d0, d1;
    int32_t d2;
    if (TICK_HIB(o->temp1) < 0) {
        SET_HIB(o->temp1, o->aioff);
        if ((ReadGoaliePulled(o) & CCR_N) || ((g.sflags7 & 0x40) && HIB(o->temp1) != 0))
            SET_HIB(o->temp1, HIB(o->temp1) - 1);
        if (g.sflags6 & HI(5)) {                          /* the carrier is in the slot: react twice as fast */
            int away_carrier = puckc() > 5;
            if (away_carrier != (PFLAG(o, pfteam) != 0)) {
                d1 = (int16_t)(HIB(o->temp1) >> 1);
                SET_HIB(o->temp1, d1);
                if ((uint8_t)d1 == 0) SET_HIB(o->temp1, 1);
            }
        }
    tm->neardist = 0;
    struct SortObj *a1 = NULL;
    d2 = (int32_t)assign_d2;                              /* (the caller's d2, as the 68k leaves it below) */
    d1 = puckc();
    if (d1 >= 0 && !((sortobj(d1)->pflags ^ o->pflags) & (1 << pfteam)))
        a1 = sortobj(d1);                                 /* our carrier is the nearest */
    else {
        d2 = -1;
        struct SortObj *a0 = sortobj_at(tm->sort);
        for (int i = 0; i < 6; i++, a0++) {
            if (a0->position <= 0) continue;
            if (a0->nopuck[0] != 0) continue;
            if (a0->pflags2 & 4) continue;
            if (a0->asslist[a0->assnum] == 0x13) continue;   /* going to receive a pass */
            int16_t hx, hy;
            GetHot(a0, &hx, &hy);
            d0 = (int16_t)(hx + a0->Xpos - puckx());
            d1 = (int16_t)(hy + a0->Ypos - pucky());
            d0 = (int16_t)(d0 - (puckvx() >> 6));
            d1 = (int16_t)(d1 - (puckvy() >> 6));
            int32_t dsq = sq(d0) + sq(d1);
            if ((uint32_t)dsq > (uint32_t)d2) continue;
            d2 = dsq;
            a1 = a0;
        }
        if (d2 < 0) goto de1;                             /* nobody can take it */
        tm->neardist = (uint32_t)d2;
    }
    if (a1 != o && !(g.gmode & (1 << gmclock)) && !(a1->pflags2 & 4) && !(a1->pflags3 & (1 << pf3onetimer))) {
        a1->pflags &= (uint8_t)~(1 << pfdoff);
        assinsert(0x11, a1);                              /* he is nearest now */
        assexit(o);
        return;
    }
de1:
    if (PFLAG(o, pfalock)) return;
    {
        int16_t sh = 2;
        int far = (uint16_t)d2 > 0x190;
        if (!far) sh = 0;
        if (far || (uint16_t)o->position > 2) {           /* defensemen stay back when close */
            if (g.sflags2 & HI(sf2pwrplay)) {
                sh = (int16_t)(sh - 2);
                if (chkpk2(o)) sh = (int16_t)(sh + 4);
            }
            uint16_t odds = (uint16_t)((0x28 - o->checking) & 0xFF);
            unsigned cnt = (unsigned)sh & 63;
            odds = cnt >= 16 ? 0 : (uint16_t)(odds << cnt);
            if (randomd0(odds) <= 2)
                o->temp3 = 0xF0;                          /* chase the carrier for a while */
        }
    }
    }                                                     /* end of the timer block (bpl .nodec) */
    if (PFLAG(o, pfalock)) return;
    if (!(g.BA_PS_flags & HI(2)) || (g.BA_PS_flags & HI(5))) {
        if (g.gmode & (1 << gmclock)) { donothing(o); return; }
    }
    if (PFLAG(o, pfjoycon)) return;
    if (!(tm->flags & 4)) {
        if (puckc() < 0) goto topuck;
        o->temp3 = (int16_t)(o->temp3 - elapsed);
        if (o->temp3 >= 0) goto topuck;
        o->temp3 = 0;
    }
    skatetopuckinit(&d0, &d1);
    {
        int16_t fx = d0, fy = d1;
        d0 = (int16_t)-d0;
        d1 = (int16_t)(-d1 + 0xF4);                       /* between the puck and his own net */
        if (PFLAG(o, pfgoal)) d1 = (int16_t)(d1 - 0x1E8);
        d0 = (int16_t)((d0 >> 1) + fx);
        d1 = (int16_t)((d1 >> 1) + fy);
    }
    if (tm->flags & 4) {
        if (PFLAG(o, pfgoal)) { if (d1 >= 0x53) d1 = 0x44; }
        else if (d1 <= -0x53) d1 = -0x44;
    }
    if (g.sflags6 & HI(5)) {                              /* the carrier is in the slot: a burst of speed */
        g.TempLegSpd = o->legspd;
        o->legspd = (uint8_t)(o->legspd + 6);
        if ((g.sflags8 & 2) || (g.sflags7 & 0x40)) o->legspd = (uint8_t)(o->legspd + 2);
        if ((int8_t)o->legspd > 0x1E) o->legspd = 0x1E;
        skateto(d0, d1, NULL, o);
        o->legspd = g.TempLegSpd;
    } else
        skateto(d0, d1, NULL, o);
    if (g.sflags6 & HI(5)) {
        d0 = pucky();
        int16_t d2w = o->Ypos;
        if (d0 < 0) { d0 = (int16_t)-d0; d2w = (int16_t)-d2w; }
        d2w = (int16_t)(d2w - d0);
        if (d2w < 0xA) return;                            /* not between the net and the carrier */
        d0 = (int16_t)(o->Xpos - puckx());
        if (d0 < 0) d0 = (int16_t)-d0;
        if (d0 >= 0xF) return;                            /* nor in line with him */
        d0 = o->checking;
        uint8_t saved = o->checking;
        int16_t dbl = (int16_t)(int8_t)(d0 + d0);
        if ((int8_t)dbl >= 0x1E) dbl = 0x1E;
        o->checking = (uint8_t)dbl;                       /* twice as eager to check */
        check4check(o);
        o->checking = saved;
        return;
    }
    check4check(o);
    return;
topuck:
    skatetopuck(o);
    check4check(o);
}

/* logic94.asm:check4check. Look for a good opportunity to check an opponent: now and then
 * (less with penalties on), an opponent within $1E in the direction he faces. */
void check4check(struct SortObj *o)
{
    if (o->position == 0) return;
    uint16_t d0 = (uint16_t)((0x28 - o->checking) & 0xFF);
    if (g.OptPen != 0) d0 = (uint16_t)(d0 << 1);
    if (randomd0(d0) > 6) return;
    struct SortObj *a0 = opponents(o);
    for (int i = 0; i < 6; i++, a0++) {
        if (a0->position == 0) continue;
        if (PFLAG(a0, pfalock)) continue;
        if (a0->pflags2 & (1 << pf2fight)) continue;
        int16_t dx = (int16_t)(a0->Xpos - o->Xpos);
        if (dx > 0x1E || dx < -0x1E) continue;
        int16_t dy = (int16_t)(a0->Ypos - o->Ypos);
        if (dy > 0x1E || dy < -0x1E) continue;
        if (vtoa(dx, dy) != (uint16_t)o->facedir) continue;
        if (g.sflags6 & HI(5)) { Acheck(a0, o); return; }
        if (vdp_hv16() & 3) { burst(o); return; }
        Acheck(a0, o);
        return;
    }
}

/* logic94.asm:asspassrec. Assignment for catching a pass: coast until the puck is loose,
 * then either wait for it or (not a human team, puck on the attack half) go for a one-timer. */
void asspassrec(struct Team *tm, struct SortObj *o)
{
    if (PFLAG(o, pfalock)) return;
    if (g.gmode & (1 << gmclock)) { donothing(o); return; }
    if (PFLAG(o, pfjoycon)) { assexit(o); return; }
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        o->pflags |= 1 << pfdoff;                         /* no deceleration */
        o->temp2 = 8;
        o->temp4 = -2;
    }
    if (puckc() >= 0) goto exit;
    o->temp4 = (int16_t)(o->temp4 + 1);
    if (o->temp4 <= 0) {
        if ((int16_t)g.onetimerplayer >= 0) goto ex2;
        if (o->position == 0) goto ex2;
        o->temp4 = 0;
        uint16_t d0 = PFLAG(o, pfteam) ? 2 : 1;
        if (d0 == g.cont1team || d0 == g.cont2team) goto ex2;
        if (PuckOnAttackHalf(o)) goto ex2;
        int16_t y = o->Ypos;
        if (!PFLAG(o, pfgoal)) y = (int16_t)-y;
        if (y < 0x58 || y > 0x108) {                      /* outside the attack zone: only sometimes */
            if (randomd0(8) != 0) goto ex2;
        }
        g.inputjoy = 0xFFFF;
        assreplace(0x23, o);                              /* assonetimer */
        return;
    }
ex2:
    if (TICK_HIB(o->temp1) >= 0) return;
exit:
    o->pflags &= (uint8_t)~(1 << pfdoff);
    assexit(o);
}

/* logic94.asm:SkateToTempTarget. Unreferenced: skate to temp3 / temp4. */
void SkateToTempTarget(struct SortObj *o)
{
    skateto(o->temp3, o->temp4, NULL, o);
}

/* logic94.asm:assshoot. Assignment for computer shooting: wind up (SetShotMode) and release
 * (ShotMode with the C button) when temp2 runs out. */
void assshoot(struct Team *tm, struct SortObj *o)
{
    if (PFLAG(o, pfalock)) return;
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        SetShotMode(o);
        return;
    }
    if (!(g.sflags & HI(sfssdir))) { assexit(o); return; }
    uint16_t d2 = 0;
    o->temp2 = (int16_t)(o->temp2 - elapsed);
    if (o->temp2 < 0) d2 |= 0x20;                         /* cbut */
    ShotMode((uint16_t)o->assnum, d2, o);                /* d0 is the assignment index at the call */
}

/* logic94.asm:puckshootout. Puck logic for the shootout: on a new assignment clear the pads
 * and select the skater, then run the attempt. */
void puckshootout(struct Team *tm, struct SortObj *o)
{
    if (PFLAG(o, pfna)) {
        o->pflags &= (uint8_t)~(1 << pfna);
        g.sflags7 |= 0x80;
        memset(g.padcont, 0, sizeof g.padcont);         /* (clr.l x 3) */
        g.gmode2 &= (uint16_t)~HI(7);
        if (g.gmode2 & HI(0))
            NextShooter(o);
        else {
            StartShootoutPath(o);
            struct Team *t = team_struct(g.BA_Team != 0);
            t->psattempts++;
        }
        g.BA_PS_flags &= (uint16_t)~(HI(2) | HI(4) | HI(5) | HI(6));
        g.sflags6 |= HI(1);
        if (g.gmode & 8) { Stop4Pen(SYM_puckshootout, o); return; }            /* a0 = this routine */
        o->pflags &= (uint8_t)~(1 << pfna);
        int play = 1;
        if (g.gmode2 & HI(0)) {
            if (g.shootoutteam != 0 || g.homeshootnum != 0) play = 0;
        }
        if (play) {
            g.HmTeam = g.HomeTeam;
            g.SongIndex = 2;
            ChooseSong();
            song((int16_t)g.SongNum);
        }
        g.gmode |= 1 << gmclock;
        g.gmode2 |= HI(1);
        g.puckvx = 0;
        g.puckvy = 0;
        g.shootoutclock = 0x19;
        ReturnGoalies();
        ST_W(o->temp1);
        ST_W(o->temp2);
    }
    g.BA_PS_flags |= HI(2);
    if (SelectPenaltyShotSkater()) {
        g.BA_PS_flags &= (uint16_t)~HI(2);
        assreplace(0x1B, o);                              /* puckfaceoff */
        return;
    }
    int16_t d0 = (int16_t)g.BA_Goalie_SCnum;
    const struct SortObj *gk = sortobj(d0);
    if (gk->position != 0 || (gk->pflags2 & 4)) {         /* find an available goalie on that side */
        for (int d2 = 5; d2 >= 0; d2--) {
            d0 = (int16_t)(d0 - 1);
            if (d0 < 0) d0 = 5;
            else if (d0 == 5) d0 = 0xB;
            gk = sortobj(d0);
            if (gk->position != 0) continue;
            if (gk->pflags2 & 4) continue;
            g.BA_Goalie_SCnum = (uint16_t)d0;
            goto found;
        }
        g.BA_PS_flags &= (uint16_t)~HI(2);
        assreplace(0x1B, o);
        return;
    }
found:
    assreplace(0x1F, o);                                  /* puckpenshot */
}

/* logic94.asm:SelectPenaltyShotSkater. Pick the skater for the penalty shot from team BA_Team:
 * the best rated (sum of the attribute nibbles) player on the ice or bench (any player in a
 * shootout). Returns the N flag: set when nobody is eligible. Registers are restored. */
int SelectPenaltyShotSkater(void)
{
    struct Team *a0 = team_struct(g.BA_Team != 0);
    const struct RosterTeam *T = rosters_team(a0->team);
    int16_t d6 = -1, d5 = -1;
    for (uint16_t d1 = 0; d1 < T->players; ) {
        const uint8_t *r = T->player[d1].rating;
        uint16_t d7 = (uint16_t)(d1 * 2);
        uint8_t d0 = r[LR_SHOT_ACCURACY];
        if ((int8_t)d0 > 1) {
            if (g.gmode2 & HI(0)) goto setskater;
            uint16_t st = a0->pdst[d7 / 2];
            if (st == 0xFFFE || st == 0xFFFF) {           /* bench or ice */
                int16_t d3 = (int16_t)(r[LR_AGILITY] + r[LR_OFF_AWARENESS] + r[LR_SPEED] + r[LR_SHOT_POWER] + r[LR_DEF_AWARENESS]
                                       + r[LR_SHOT_ACCURACY] + r[LR_STICK_HANDLING] + r[LR_ENDURANCE] + r[LR_PASS_ACCURACY]);
                if (d1 == g.BA_Skater_Offset) d3 = 0x7FFF;
                if (!(d3 < d6)) { d6 = d3; d5 = (int16_t)d1; }
            }
        }
        d1++;
        if (!(d1 < 0x1A)) break;
    }
    if (d5 < 0) return 1;
    g.BA_Skater_Offset = (uint16_t)d5;
setskater:
    g.BA_Sktr_SCnum = g.BA_Team != 0 ? 6 : 0;
    return 0;
}

/* logic94.asm:puckpenshot. Puck start for a penalty shot / shootout attempt: black out, put
 * the skater on the puck at center ice and the goalie in his net, everyone else off the ice. */
void puckpenshot(struct Team *tm, struct SortObj *o)
{
    if (!PFLAG(o, pfna)) {                                /* resume play */
        g.gmode2 |= HI(2);
        assreplace(0x18, o);                              /* pucknorm */
        o->Xvel = 0;
        o->Yvel = 0;
        return;
    }
    o->pflags &= (uint8_t)~(1 << pfna);
    forceblack();
    while (g.disflags & HI(dfok)) hal_wait_vblank();      /* wait for dfok clear */
    if (g.crowdlevel > 0x258) {
        g.crowdlevel = 0x258;
        g.CwdExciteLvl += 0x14;
    }
    DecompressGraphicsPlain(ART(RefsMap) + 8, g.ExtraChars);
    g.disflags |= HI(dfclock);
    g.sflags &= (uint16_t)~HI(sfpz);
    g.sflags3 &= (uint8_t)~1;
    g.glovecords = 0;
    g.iflags = 0;
    ST_W(g.RefCnt);
    g.sflags2 &= (uint16_t)~HI(sf2refref);
    ST_W(g.passplayer);
    ClrHor();
    g.Vpos = 0;
    g.Hpos = 0;
    g.puckx[0] = 0;
    g.pucky = 0;
    g.puckz[0] = 0;
    g.puckvx = 0;
    g.puckvy = 0;
    g.puckvz[0] = 0;
    puck_loose();
    reset_ice_objects();
    setpersonel(team_struct(0));
    SetupPenaltyShot(team_struct(0));
    setpersonel(team_struct(1));
    SetupPenaltyShot(team_struct(1));
    resetplstuff();
    struct SortObj *pl = sortobj(11);
    for (int d2 = 11; d2 >= 0; d2--, pl--) {
        if (d2 == (int16_t)g.BA_Sktr_SCnum) {
            if (pl->position < 0 && (pl->pflags2 & 4)) {
                pl->pflags2 &= (uint8_t)~4;
                setplayer(pl->pnum, pl);
            }
            pl->Xpos = puckx();
            int16_t y = pucky();
            if (!PFLAG(pl, pfgoal)) { y = (int16_t)(y + 0x10); pl->facedir = 4; }
            else { y = (int16_t)(y - 0x10); pl->facedir = 0; }
            pl->Ypos = y;
            pl->Xvel = 0; pl->Yvel = 0; pl->Zvel = 0;
            assinsert(0x11, pl);                          /* assnearest */
        } else if (d2 == (int16_t)g.BA_Goalie_SCnum) {
            if (g.gmode2 & HI(0)) {                       /* shootout: the goalie of the shooting order */
                pl->pnum = g.awayshooters[0xB];           /* (shootoutteam-1: the away list's last word, low byte) */
                if (g.shootoutteam != 0) pl->pnum = g.homeshooters[0xB];   /* (homeshootnum-1) */
                setplayer(pl->pnum, pl);
            } else if (pl->position < 0 && (pl->pflags2 & 4)) {
                pl->pflags2 &= (uint8_t)~4;
                setplayer(pl->pnum, pl);
            }
            pl->pflags2 &= (uint8_t)~4;
            int16_t d1 = PFLAG(pl, pfgoal) ? 0xE5 : -0xE5;
            pl->Xpos = 0;
            pl->Ypos = d1;
            pl->Xvel = 0; pl->Yvel = 0; pl->Zpos = 0;
            pl->facedir = (int16_t)vtoa(puckx(), (int16_t)(pucky() - d1));
            pl->pflags2 &= (uint8_t)~4;
            pl->pflags &= (uint8_t)~(1 << pfalock);
            SetSPA(pl, SPAglide);
        } else {
            pl->Xpos = -0xF0;
            pl->Ypos = 0;
            pl->frame = 0;
            assinsert(0x20, pl);
        }
    }
    SprSort();
    ResetAndSelectPlayers(o);
    struct SortObj *gk = sortobj((int16_t)g.BA_Goalie_SCnum);
    gk->Xpos = 0;
    gk->Ypos = PFLAG(gk, pfgoal) ? -0xFA : 0xFA;
    uint16_t d1 = PFLAG(gk, pfteam) ? 2 : 1;
    if (d1 == g.cont1team) setc1player(g.BA_Goalie_SCnum);
    else if (d1 == g.cont2team) setc2player(g.BA_Goalie_SCnum);
    assreplace(0xE, gk);
    g.BA_PS_flags |= HI(7);
    g.sflags2 |= HI(sf2drec);
    g.msgtimer = 0x190;
    if (g.cont1team == 0 && g.cont2team == 0) g.msgtimer = 0x64;
    g.palcount = 0x18;
}

/* logic94.asm:UpdatePenaltyShotEnd. End the penalty shot when the goalie has the puck, the
 * puck is loose or the timer runs out. */
void UpdatePenaltyShotEnd(struct SortObj *o)
{
    if (puck_carrier() == g.BA_Goalie_SCnum) goto ended;
    if (!(g.BA_PS_flags & HI(5))) goto timer;
    if (!(g.gmode2 & HI(5))) {
        if (puckc() >= 0) goto countdown;
        g.gmode2 |= HI(5);
    }
    if (puckc() >= 0) goto ended;                         /* picked up again */
countdown:
    if ((int16_t)g.passmodetimer < 0) goto ended;
    g.passmodetimer--;
    goto timer;
ended:
    g.BA_PS_flags |= HI(4);
timer:
    if (g.shootoutclock == 0) g.BA_PS_flags |= HI(4);
    if (!(g.BA_PS_flags & HI(4))) return;
    if (g.BA_PS_flags & HI(6)) return;
    g.BA_PS_flags |= HI(6);
    AddPenalty2(0xA, o);
    EndPenaltyShotPlay();
}

/* logic94.asm:EndPenaltyShotPlay. Stop play after a penalty shot or shootout attempt: freeze
 * the window, stop the clock, clear the penalty shot flags. */
void EndPenaltyShotPlay(void)
{
    freezewindow();
    if (!(g.gmode2 & HI(0))) {
        g.replaydelay = 0xA;
        g.sflags2 |= HI(sf2drec);
    }
    g.gmode |= 1 << gmclock;
    g.gmode2 &= (uint16_t)~HI(2);
    g.BA_PS_flags &= (uint16_t)~(HI(2) | HI(3) | HI(5) | HI(4) | HI(6));
    sortobj((int16_t)g.BA_Sktr_SCnum)->newpnum = g.savednewpnum;
    CountShootoutGoals();
}

/* logic94.asm:puckfaceoff. This is where the action starts: on a new assignment settle the
 * period end, delayed penalties, pulled goalies and the song, then start the line changes and
 * wait for them (WaitForFaceoffLineChanges) before puckfaceoff2. */
void puckfaceoff(struct Team *tm, struct SortObj *o)
{
    g.sflags8 &= (uint8_t)~0x10;
    if (g.gmode2 & HI(0)) {
        g.puckvx = 0;
        g.puckvy = 0;
        assreplace(0x1E, o);                              /* puckshootout */
        return;
    }
    if (!PFLAG(o, pfna)) { WaitForFaceoffLineChanges(o); return; }
    o->pflags &= (uint8_t)~(1 << pfna);
    if (g.sflags8 & 1) {
        g.sflags8 &= (uint8_t)~1;
        g.fox = 0;
        g.foy = 0;
    }
    g.sflags7 &= (uint8_t)~0x80;
    memset(g.padcont, 0, sizeof g.padcont);         /* (clr.l x 3) */
    g.sflags7 &= (uint8_t)~0x40;
    g.sflags7 &= (uint8_t)~4;
    CheckScoreLeader();
    if ((int16_t)g.faceoffanim[0] >= 0)
        SetFaceoffAnim(g.faceoffanim[0]);
    else {
        uint16_t d0 = RandomFaceoffAnim();
        if ((int16_t)d0 >= 0) SetFaceoffAnim(d0);
    }
    g.sflags6 &= (uint16_t)~HI(1);
    g.gmode2 &= (uint16_t)~HI(1);
    if (g.gameclock == 0) { PeriodOver(); return; }
    if (g.gmode & 0x40) { PeriodOver(); return; }
    if (g.gsp == 3 && g.teams[0].score != g.teams[1].score) { clockcont_0(); return; }   /* overtime goal */
    if (g.gmode & (1 << gmpendel)) { Stop4Pen(SYM_puckfaceoff, o); return; }            /* a0 = this routine */
    {
        uint16_t half = (uint16_t)((int16_t)g.PerTimeTotal >> 1);
        if (!(half <= g.gameclock) && !((int16_t)g.gameclock < 0x3C) && (g.sflags3 & 0x80)) {
            g.sflags3 &= (uint8_t)~0x80;                 /* once in the second half of a period */
            if ((g.gmode & (1 << gmclock)) && !(g.sflags8 & 0x40)) {
                g.HmTeam = g.HomeTeam;
                g.SongIndex = 2;
                ChooseSong();
                g.sflags8 |= 0x10;
            }
        }
    }
    ReturnGoalies();
    ST_W(o->temp1);
    ST_W(o->temp2);
    if (g.OptLine != 0) { WaitForFaceoffLineChanges(o); return; }
    team_struct(0)->flags &= (uint8_t)~2;
    team_struct(1)->flags &= (uint8_t)~2;
    for (int i = 0; i < 12; i++) sortobj(i)->pflags2 &= (uint8_t)~8;
    if ((g.cont1team | g.cont2team) != 0 && (g.sflags & HI(sfhor))) {
        forceblack();
        g.sflags |= HI(sfslock);
        ClrHor();
        g.palcount = 0x18;
        if (g.OptLine == 0 && (g.sflags7 & 0x10)) g.palcount = 0;
    }
    if ((int16_t)g.c1playernum >= 0) StartFaceoffLineChange(g.c1playernum, o);
    if ((int16_t)g.c2playernum >= 0) StartFaceoffLineChange(g.c2playernum, o);
    SetFaceoffComputerLine(2, team_struct(1), team_struct(0));
    WaitForFaceoffLineChanges(o);
}

/* logic94.asm:SetFaceoffComputerLine. Faceoff: start a line change for team d0 when the
 * computer controls it (line changes on). */
void SetFaceoffComputerLine(uint16_t team, struct Team *a2, struct Team *a1)
{
    if (g.OptLine != 0) return;
    if (!(g.sflags7 & 0x10)) {
        if (team == g.cont1team) return;
        if (team == g.cont2team) return;
    }
    CompLine(a2, a1);
    setpersonel(a2);
    printscores1();
    g.crowdnoisedelay = 0x2710;
}

/* logic94.asm:StartFaceoffLineChange. Start the line change box for team d0's sort object
 * before a faceoff; the puck's temp1/temp3 (home) or temp2/temp4 (away) time it. */
void StartFaceoffLineChange(uint16_t obj, struct SortObj *puck)
{
    struct SortObj *pl = sortobj((int16_t)obj);
    pl->pflags2 &= (uint8_t)~8;
    SetLCmode(pl);
    if (!(pl->pflags2 & 8)) return;
    if (PFLAG(pl, pfteam)) {
        puck->temp2 = 0x168;
        puck->temp4 = pl->SCnum;
    } else {
        puck->temp1 = 0x258;
        puck->temp3 = pl->SCnum;
    }
}

/* logic94.asm:WaitForFaceoffLineChanges. Run both teams' faceoff line change timers (temp1 /
 * temp2); continue when both have run out. */
void WaitForFaceoffLineChanges(struct SortObj *puck)
{
    UpdateFaceoffLineChangeTimer(0x40, puck);
    UpdateFaceoffLineChangeTimer(0x42, puck);
    if (puck->temp1 >= 0) return;
    if (puck->temp2 >= 0) return;
    SetFaceoffComputerLine(1, team_struct(0), team_struct(1));
    g.facelcwait = 0xFFFF;
    assreplace(0x1C, puck);                               /* puckfaceoff2 */
}

/* logic94.asm:UpdateFaceoffLineChangeTimer. Count down one team's faceoff line change timer
 * at offset d0 ($40 = temp1, $42 = temp2) of a3; the object number sits 4 bytes on. */
void UpdateFaceoffLineChangeTimer(uint16_t off, struct SortObj *puck)
{
    int16_t timer = off == 0x40 ? puck->temp1 : puck->temp2;
    int16_t obj = off == 0x40 ? puck->temp3 : puck->temp4;
    if (timer < 0) return;
    struct SortObj *pl = sortobj(obj);
    struct Team *a2 = team_struct(PFLAG(pl, pfteam));
    if (!(pl->pflags2 & 8)) {
        ST_W(timer);
        if (off == 0x40) puck->temp1 = timer; else puck->temp2 = timer;
        SetLCmode2(a2);
        return;
    }
    timer = (int16_t)(timer - elapsed);
    if (off == 0x40) puck->temp1 = timer; else puck->temp2 = timer;
    if (timer >= 0) return;
    if (pl->pflags2 & 8) {
        lcfound(0, a2, pl);
        SetLCmode2(a2);
    }
}

/* logic94.asm:ChkGoalies. Computer pulls the goalie on a delayed penalty (or late in the
 * game when behind). */
void ChkGoalies(void)
{
    if (g.gmode & (1 << gmclock)) return;
    chkgoalie(1, team_struct(0), team_struct(1));
    chkgoalie(2, team_struct(1), team_struct(0));
}

/* logic94.asm:ReturnGoalies. If the computer pulled his goalie, look to see if he should
 * return him (he stays out for a faceoff in the attack zone). */
void ReturnGoalies(void)
{
    returngoalie(1, team_struct(0), team_struct(1));
    returngoalie(2, team_struct(1), team_struct(0));
}

/* logic94.asm:CPgoalie. See if the computer should pull his goalie: third period, two goals
 * behind, under a minute left, play (d1 = y) in the attack half. */
void CPgoalie(int16_t y, struct Team *a1, struct Team *a2)
{
    if (g.gsp != 2) return;
    int16_t d0 = (int16_t)(a1->score - a2->score);
    if (d0 < 0) return;
    if (d0 != 2) return;
    if ((int16_t)g.gameclock > 0x3C) return;
    const struct SortObj *a0 = sortobj_at(a2->sort);
    if (!PFLAG(a0, pfgoal)) y = (int16_t)-y;
    if (y < 0) return;
    ST_W(a2->goalie);
    setpersonel(a2);
}

/* logic94.asm:CompLine. Find a good line for the computer team a2 to switch to (a1 = the
 * other team): the power play / penalty kill line pair, else a rested line matched to the
 * opponent's. */
void CompLine(struct Team *a2, struct Team *a1)
{
    int16_t d1 = (int16_t)(a2->ap - a1->ap);
    if (d1 != 0) {
        uint16_t d0 = 3;
        if (d1 < 0) d0 = 5;
        uint16_t e1 = getlinee(d0, a2);
        uint16_t e2 = getlinee((uint16_t)(d0 + 1), a2);
        if ((int16_t)e1 < (int16_t)e2) d0++;
        a2->line = d0;
        return;
    }
    if (a2 == team_struct(0)) {
        const uint16_t *tab = homelines;
        if (g.gsp == 2) {
            int16_t d2 = (int16_t)a2->score;
            if (d2 != (int16_t)a1->score) {
                tab += 7;
                if (!(d2 > (int16_t)a1->score)) tab += 7;
            }
        }
        uint16_t idx = a1->line;
        uint16_t d0 = tab[idx];
        if (getlinee(d0, a2) > 0xC00) {
            a2->line = tab[idx];
            return;
        }
    }
    const uint16_t *tab = awaylines;
    if (g.gsp == 2) {
        int16_t d2 = (int16_t)a2->score;
        if (d2 != (int16_t)a1->score) {
            tab += 3;
            if (!(d2 > (int16_t)a1->score)) tab += 3;
        }
    }
    int16_t d1c = 2;
    for (;;) {
        uint16_t d0 = *tab++;
        if (getlinee(d0, a2) > 0xC00) break;
        if (--d1c == -1) break;
    }
    a2->line = *--tab;
}

/* logic94.asm:puckfaceoff2. Face off control logic and general setup for action: on a new
 * assignment black out, reset the rink and every object around the faceoff spot, draw the
 * faceoff window and pick the drop time; then count the drop time down (updatefaceoff) and
 * drop the puck (Endfaceoff). */
void puckfaceoff2(struct Team *tm, struct SortObj *o)
{
    if (!PFLAG(o, pfna)) {
        o->temp1 = (int16_t)(o->temp1 - 1);
        if (o->temp1 >= 0) { updatefaceoff(o); return; }
        EndArenaAnim();
        Endfaceoff(o);
        return;
    }
    o->pflags &= (uint8_t)~(1 << pfna);
    g.BA_PS_flags &= (uint16_t)~(HI(2) | HI(5));
    g.sflags4 &= (uint16_t)~(HI(0) | HI(1));
    g.DelayedPen &= (uint8_t)~7;
    forceblack();
    while (g.disflags & HI(dfok)) hal_wait_vblank();      /* wait for dfok clear */
    uint16_t sr = cpu_get_sr();
    g.holdreset = 0x3C;
    if (g.fox == 0 && g.foy == 0) g.faceoffanim[0] = 0xFFFF;   /* center ice: no arena animation */
    else cpu_set_sr(0x2700);                              /* (masked until the end of the setup) */
    if (g.crowdlevel > 0x258) g.crowdlevel = 0x258;
    g.disflags |= HI(dfclock);                                  /* dfclock */
    g.sflags &= (uint16_t)~HI(sfpz);
    g.sflags3 &= (uint8_t)~(1 << sf3llcs);                /* no line changes */
    g.glovecords = 0;
    g.iflags = 0;
    ST_W(g.RefCnt);
    g.puckcross[0].frames |= 0xFF00;                      /* (st: the high byte) */
    g.puckcross[1].frames |= 0xFF00;
    g.sflags2 &= (uint16_t)~HI(sf2refref);
    g.sflags2 |= HI(sf2faceoff);
    g.sflags2 |= HI(sf2drec);
    ST_W(g.passplayer);
    ST_W(g.onetimerplayer);
    g.disflags |= HI(4);
    ClrHor();
    g.crowdnoisedelay = 0x2710;
    g.Vpos = 0;
    g.Hpos = 0;
    g.puckx[0] = g.fox;
    g.pucky = g.foy;
    ST_W(g.puckz[0]);                                     /* no visible puck */
    g.puckvx = 0;
    g.puckvy = 0;
    g.puckvz[0] = 0;
    puck_loose();
    reset_ice_objects();
    setpersonel(team_struct(0));
    forcepldata(team_struct(0));
    setpersonel(team_struct(1));
    forcepldata(team_struct(1));
    resetplstuff();
    struct SortObj *pl = sortobj(0);
    for (int d2 = 11; d2 >= 0; d2--, pl++) {
        pl->Xpos = -0xF0;
        pl->Ypos = 0;
        pl->frame = 0;
        int16_t d1 = pl->position;
        if (d1 < 0) continue;
        if (d1 != 0) {
            uint16_t d0 = 0x16;                           /* assfaceoff */
            if (d1 == 4) {                                /* the center takes the draw */
                struct Team *tc = own_team(pl);
                tc->lasttouch = 0;
                tc->lasttouch = (uint16_t)((tc->lasttouch & 0xFF00) | (uint8_t)(pl->pnum));
                g.sflags8 &= (uint8_t)~0x80;
                ST_W(tc->assist1);
                ST_W(tc->assist2);
                tc->flags &= (uint8_t)~8;
                d0 = 0x17;                                /* assfaceoffpl */
            }
            assinsert(d0, pl);
        }
        int16_t d4 = (int16_t)team_struct(PFLAG(pl, pfteam))->ap;            /* skaters on the ice 4-6 */
        d4 = (int16_t)((6 - d4) << 3);
        d4 = (int16_t)(fo_apl[d4 + d1] << 2);
        int16_t d0 = (int16_t)fo_ptab[d4 / 2];
        d1 = (int16_t)fo_ptab[d4 / 2 + 1];
        if (!PFLAG(pl, pfgoal)) { d0 = (int16_t)-d0; d1 = (int16_t)-d1; }
        if (pl->position != 0) {
            if (d4 <= 8) {
                if ((int16_t)(g.fox ^ d0) < 0)
                    d1 = (int16_t)(d1 - ((int16_t)g.foy >> 3));
                d0 = (int16_t)(d0 - ((int16_t)g.fox >> 2));
            }
            d0 = (int16_t)(d0 + (int16_t)g.fox);
            d1 = (int16_t)(d1 + (int16_t)g.foy);
        }
        pl->Xpos = d0;
        pl->Ypos = d1;
        pl->Xvel = 0;
        pl->Yvel = 0;
        pl->facedir = (int16_t)vtoa((int16_t)(puckx() - d0), (int16_t)(pucky() - d1));
        pl->pflags2 &= (uint8_t)~4;
        pl->pflags &= (uint8_t)~(1 << pfalock);
        SetSPA(pl, SPAglide);
    }
    SprSort();
    ResetAndSelectPlayers(o);
    uint16_t d4 = DecompressGraphicsPlain(ART(FaceOffMap) + 8, g.ExtraChars);
    g.faceoffvrcset = d4;
    DecompressGraphicsPlain(ART(FaceOffSprites) + 8, d4);
    g.arenaanim = 0xFFFF;
    if ((g.sflags7 & 1) && (int16_t)g.faceoffanim[0] >= 0)
        StartArenaAnim(g.faceoffanim[0]);
    printz(str_puckfaceoff2_1, sizeof str_puckfaceoff2_1);
    int16_t d0 = 0x36;                                    /* x of the faceoff window */
    if ((int16_t)g.fox < 0) d0 = 0xBE;
    g.fodropx = (uint16_t)d0;
    g.printx = (uint16_t)((int16_t)(d0 - 0x2E) >> 3);
    d0 = 0x5C;
    g.fodropy = (uint16_t)d0;
    g.printy = (uint16_t)((int16_t)(d0 - 0x44) >> 3);
    {
        const uint8_t *map = rom_ptr(ART(FaceOffMap));
        uint32_t pal = ART(FaceOffMap) + rd32(map), mp = ART(FaceOffMap) + rd32(map + 4);
        g.sflags6 |= HI(0);
        dobitmap(pal, mp, ART(FaceOffMap) + 8, 0, 0, 0xC, 0xA, g.ExtraChars, 0);
        g.sflags6 &= (uint16_t)~HI(0);
    }
    if (g.OptLine == 0) {                                 /* the two lines on the ice */
        printsmallz(str_puckfaceoff2_2, sizeof str_puckfaceoff2_2);
        Framer(0xC, 3);
        uint16_t l0 = team_struct(0)->line, l1 = team_struct(1)->line;
        if (!(g.gmode & (1 << gmdir))) { uint16_t t = l0; l0 = l1; l1 = t; }
        printsmallz(str_puckfaceoff2_3, sizeof str_puckfaceoff2_3);
        Fprintsmall(SYM_linelist, l0);
        g.printx += 4;
        Fprintsmall(SYM_linelist, l1);
    }
    d0 = (int16_t)(randomd0(0x78) + 0xB4);                /* time to the drop */
    {
        uint16_t a = g.arenaanim;
        if ((a == 0 || a == 1 || a == 5 || a == 4 || a == 2) && !(d0 > 0x10E))
            d0 = 0x10E;                                   /* long enough for the arena animation */
    }
    o->temp1 = d0;
    g.palcount = 0x18;
    struct FoMan *fof = g.fomen;                          /* the two faceoff men and the ref: frame, attribute */
    fof[0].frame = 1;
    fof[0].attribute = 0x8000;
    fof[1].frame = 4;
    fof[1].attribute = 0xA800;
    fof[2].frame = 7;                                     /* frame of the ref */
    fof[2].attribute = 0x8000;
    if (!(g.gmode & (1 << gmdir))) {
        fof[0].attribute ^= 0x800;
        fof[1].attribute ^= 0x800;
    }
    g.fodir1 = 0xFFFF;
    g.fodir2 = 0xFFFF;
    LeadSong();
    int play = 0;
    if (g.sflags8 & 0x40) { g.sflags8 &= (uint8_t)~0x40; play = 1; }
    else if (g.sflags8 & 0x10) play = 1;
    if (play) {
        g.sflags8 &= (uint8_t)~0x10;
        song((int16_t)g.SongNum);
    }
    g.palcount = 0x18;
    cpu_set_sr(sr);
}

/* logic94.asm:ResetAndSelectPlayers. c1playernum / c2playernum = -1, then changeplayer for
 * each human team (pads 3 and 4 borrow the pad 1 and 2 variables). */
void ResetAndSelectPlayers(struct SortObj *o)
{
    g.c1playernum = 0xFFFF;
    g.c2playernum = 0xFFFF;
    if (g.cont1team != 0) changeplayer(0, o);
    if (g.cont2team != 0) changeplayer(2, o);
    if (g.cont3team != 0) {
        uint16_t t = g.cont1team, p = g.c1playernum;
        g.cont1team = g.cont3team;
        g.c1playernum = 0xFFFF;
        changeplayer(0, o);
        g.c3playernum = g.c1playernum;
        g.cont3team = g.cont1team;
        g.c1playernum = p;
        g.cont1team = t;
    }
    if (g.cont4team != 0) {
        uint16_t t = g.cont2team, p = g.c2playernum;
        g.cont2team = g.cont4team;
        g.c2playernum = 0xFFFF;
        changeplayer(2, o);
        g.c4playernum = g.c2playernum;
        g.cont4team = g.cont2team;
        g.c2playernum = p;
        g.cont2team = t;
    }
}

/* logic94.asm:updatefaceoff. a3 = puck. Drop animation frame from temp1; at 0 erases the
 * faceoff box. */
void updatefaceoff(struct SortObj *puck)
{
    int16_t d0 = puck->temp1;
    if (d0 != 0) {
        d0 = (int16_t)((uint16_t)(d0 + 6) >> 3);
        if (d0 > 2) return;
        g.fomen[2].frame = (uint16_t)(0xA - d0);
        return;
    }
    g.disflags &= (uint16_t)~HI(4);
    printz(str_updatefaceoff_1, sizeof str_updatefaceoff_1);
    g.printx = (uint16_t)((int16_t)(g.fodropx - 0x2E) >> 3);
    g.printy = (uint16_t)((int16_t)(g.fodropy - 0x44) >> 3);
    eraser(0xC, 0xD, 0x7FF);
}

/* logic94.asm:Endfaceoff. Drop the puck: pick the faceoff winner from the two men's button
 * timing (fofdata2), send the puck off in his direction (or a random one) with a hop. */
void Endfaceoff(struct SortObj *puck)
{
    sfx(SYM_SFXpuckice + 3);
    g.sflags2 &= (uint16_t)~HI(sf2faceoff);
    g.holdreset = 0x3C;
    DecompressGraphicsPlain(ART(RefsMap) + 8, g.ExtraChars);
    g.sflags2 &= (uint16_t)~HI(sf2drec);
    g.gmode &= (uint8_t)~(1 << gmclock);
    puck->pflags2 &= (uint8_t)~(1 << pf2fight);
    g.crowdnoisedelay = 0;
    g.sflags3 |= 0x10;
    int16_t d3 = (int16_t)g.fodir1;
    int16_t d4 = 0x800;
    const struct FoMan *a1 = &g.fomen[0];
    uint8_t d2 = 0x10;
    d2 = (uint8_t)(d2 - fo_ftab[(int16_t)a1[0].frame - 1]);
    d2 = (uint8_t)(d2 + fo_ftab[(int16_t)a1[1].frame - 1]);
    uint16_t d0 = randomd0(0x21);
    if (!(d2 <= (uint8_t)d0))
        a1 = &g.fomen[1];                                 /* the second man won */
    if (a1->attribute & 0x800) {
        d3 = (int16_t)g.fodir2;
        d4 = (int16_t)-d4;
    }
    d0 = (uint16_t)d3;
    int random = 1;
    if (!(d0 & 8)) {                                      /* he pushed a direction */
        d0 &= 7;
        if ((vdp_hv16() & 3) != 0) random = 0;
    }
    if (random) {
        d0 = (uint16_t)((randomd0(5) - 2) & 7);
        if (d4 >= 0) d0 ^= 4;
    }
    d0 = (uint16_t)(d0 << 2);
    puck->Xvel = (int16_t)(dirtab[d0 / 2] << 5);
    puck->Yvel = (int16_t)((int16_t)(dirtab[d0 / 2 + 1] << 5) + d4);
    puck->Zvel = (int16_t)randomd0(0x800);
    g.puckz[0] = 0;
    puck->pflags &= (uint8_t)~(1 << pfnc);
    assreplace(0x18, puck);                               /* pucknorm */
}

/* logic94.asm:pucknorm. Assignment for the puck most of the time: age the crossing table,
 * bungie the puck to its carrier's hot spot, icing and offsides, the delay of game whistle
 * when it sits still, then the collisions. */
void pucknorm(struct Team *tm, struct SortObj *puck)
{
    if (g.BA_PS_flags & HI(2)) UpdatePenaltyShotEnd(puck);
    if (PFLAG(puck, pfna)) {
        puck->pflags &= (uint8_t)~(1 << pfna);
        puck->temp1 = 0;
        puck->temp2 = 0x78;
    }
    g.puckcross[0].frames -= (uint16_t)elapsed;
    g.puckcross[1].frames -= (uint16_t)elapsed;
    puck->temp1 = (int16_t)(puck->temp1 - elapsed);
    if (puck->temp1 < 0) {
        puck->temp1 = (int16_t)(puck->temp1 + 5);
        findpc();
    }
    int16_t d0 = puckc();
    if (d0 >= 0) {
        struct SortObj *a2 = sortobj(d0);
        a2touchpuck(a2, puck);
        int16_t hx, hy;
        GetHot(a2, &hx, &hy);
        d0 = (int16_t)(hx + a2->Xpos - puck->Xpos);
        d0 = (int16_t)(d0 >> 2);
        puck->Xpos = (int16_t)(puck->Xpos + d0);
        int16_t d1 = (int16_t)(hy + a2->Ypos - puck->Ypos);
        d1 = (int16_t)(d1 >> 2);
        puck->Ypos = (int16_t)(puck->Ypos + d1);
        puck->Xvel = a2->Xvel;
        puck->Yvel = a2->Yvel;
    }
    d0 = PuckIChk(d0);
    ChkOffsides(d0, puck);
    if (!(g.gmode & (1 << gmclock))) {
        if (puckc() < 0 && puck->Xpos == puck->OldXpos && puck->Ypos == puck->OldYpos) {
            puck->temp2 = (int16_t)(puck->temp2 - 1);
            if (puck->temp2 < 0) AddPenalty2(6, puck);    /* the puck sat still too long */
        } else
            puck->temp2 = 0x78;
    }
    if (HIB(puck->Zvel) == 0 && puck->Zpos == 0)
        puckunflip(puck);
    checkpuckcoll(puck);
}

/* logic94.asm:ChkOffsides. Check for offsides: a3 = puck crossing a blue line with a skater
 * of the attacking team already over it flags the whole team (tmflags bit 4). d0 passes
 * through ClearOffsidesIfAllPlayers. */
void ChkOffsides(int16_t d0, struct SortObj *puck)
{
    if (!(g.gmode & (1 << gmoffs))) return;
    if (g.BA_PS_flags & HI(2)) return;
    struct Team *a1 = team_struct(0), *a2 = team_struct(1);
    ClearOffsidesIfAllPlayers(a2, &d0);
    { struct Team *t = a1; a1 = a2; a2 = t; }
    ClearOffsidesIfAllPlayers(a2, &d0);
    d0 = 0x54;
    if (d0 > puck->Ypos) {                                /* the bottom blue line */
        d0 = (int16_t)-d0;
        if (d0 < puck->Ypos) return;
        if (d0 >= puck->OldYpos) return;
        d0 = (int16_t)(d0 - 0xA);
        if (!(g.gmode & (1 << gmdir))) { struct Team *t = a1; a1 = a2; a2 = t; }
        const struct SortObj *a0 = sortobj_at(a2->sort);
        for (int i = 0; i < 6; i++, a0++) {
            if (a0->position < 0) continue;
            if (d0 <= a0->Ypos) continue;
            a2->flags |= 0x10;
            return;
        }
        return;
    }
    if (d0 <= puck->OldYpos) return;                      /* did not just cross it */
    d0 = (int16_t)(d0 + 0xA);
    if (g.gmode & (1 << gmdir)) { struct Team *t = a1; a1 = a2; a2 = t; }
    const struct SortObj *a0 = sortobj_at(a2->sort);
    for (int i = 0; i < 6; i++, a0++) {
        if (a0->position < 0) continue;
        if (d0 >= a0->Ypos) continue;
        a2->flags |= 0x10;
        return;
    }
}

/* logic94.asm:ClearOffsidesIfAllPlayers. a2 = team struct: clear the team offsides flag once
 * no skater is past the line. d0 is left over from the caller for objects with no position. */
void ClearOffsidesIfAllPlayers(struct Team *tm, int16_t *d0)
{
    if (!(tm->flags & 0x10)) return;
    const struct SortObj *a0 = sortobj_at(tm->sort);
    for (int d1 = 5; ; a0++) {
        if (a0->position >= 0) {
            *d0 = a0->Ypos;
            if (!PFLAG(a0, pfgoal)) *d0 = (int16_t)-*d0;
        }
        if (*d0 > 0x58) return;
        if (--d1 == -1) break;
    }
    tm->flags &= (uint8_t)~0x10;
}

/* logic94.asm:a2offsides. Player a2 touched the puck: offsides penalty if his team is flagged
 * and the puck is past the blue line. */
void a2offsides(struct SortObj *pl, struct SortObj *puck)
{
    if (!(g.gmode & (1 << gmoffs))) return;
    int16_t d0 = pucky();
    if (!PFLAG(pl, pfgoal)) d0 = (int16_t)-d0;
    if (d0 < 0x68) return;
    struct Team *a0 = own_team(pl);
    if (!(a0->flags & 0x10)) return;
    if (g.gmode & (1 << gmhl)) return;
    AddPenalty(0x10, pl);
}

/* logic94.asm:a2touchpuck. Player a2 touches the puck: remember him for the scoring, look for
 * offsides and icing. */
void a2touchpuck(struct SortObj *pl, struct SortObj *puck)
{
    g.ltx = (uint16_t)pl->Xpos;
    g.lty = (uint16_t)pl->Ypos;
    g.ltplayer = (uint16_t)pl->SCnum;
    struct Team *a0 = own_team(pl);
    uint16_t d0 = pl->pnum;
    if (pl->pflags3 & (1 << pf3onetimer)) g.sflags8 |= 0x80;
    if (d0 != a0->lasttouch) {
        int was = a0->flags & 8;
        a0->flags &= (uint8_t)~8;
        if (!was) {
            if (!rules[RULE_ASSIST_FIX] || d0 != a0->assist1)            /* (rules: a give-and-go keeps the second assist) */
                a0->assist2 = a0->assist1;
            a0->assist1 = a0->lasttouch;
        }
        a0->lasttouch = d0;
        if (d0 == a0->assist2) ST_W(a0->assist2);               /* no assist to himself */
    }
    g.sflags2 &= (uint16_t)~HI(sf2shot);
    a2offsides(pl, puck);
    if ((g.iflags & 4) && (g.iflags & 1) && pl->position != 0) {
        int icing = (g.iflags & 2) ? !PFLAG(pl, pfgoal) : PFLAG(pl, pfgoal) != 0;
        if (icing) {
            AddPenalty(0xC, sortobj(g.icingPlayer));   /* PenIcing on the player who shot it */
            return;
        }
    }
    g.iflags = 0;
    g.icingPlayer = (uint8_t)pl->SCnum;
    int16_t y = pucky();
    if (PFLAG(pl, pfgoal)) {
        g.iflags |= 2;
        y = (int16_t)-y;
    }
    if (y < 0) return;                                    /* past center: icing possible */
    int16_t d = (int16_t)(team_struct(0)->ap - team_struct(1)->ap);
    if (PFLAG(pl, pfteam)) d = (int16_t)-d;
    if (d < 0) return;                                    /* short handed: no icing */
    g.iflags |= 4;
}

/* logic94.asm:PuckIChk. Check for icing of the puck: a loose puck crossing the far goal line
 * outside the crease sets ifcgl. d0 is the goal line on that path, else unchanged. */
int16_t PuckIChk(int16_t d0)
{
    if (!(g.iflags & 4)) return d0;
    if (g.iflags & 1) return d0;
    if (puckc() >= 0) return d0;
    d0 = 0x108;
    if (!(g.iflags & 2)) {
        d0 = (int16_t)-d0;
        if (!(d0 > pucky())) return d0;
    } else if (d0 > pucky())
        return d0;
    if (puckx() > 0x2C || puckx() < -0x2C) {
        g.iflags |= 1;
        return d0;
    }
    g.iflags &= (uint8_t)~4;                              /* through the crease: no icing */
    return d0;
}

/* logic94.asm:puckunflip. Stop the spinning puck. */
void puckunflip(struct SortObj *puck)
{
    if (puck->SPAnum >= 8 && puck->SPAnum < 0x18)
        puck->facedir ^= 2;
    puck->facedir |= 4;
    puck->SPAnum = 0;
    ST_W(puck->SPAcnt);
}

/* logic94.asm:puckflip. Start the puck spinning; d0 bit 0 picks the direction. */
void puckflip(int16_t d0, struct SortObj *puck)
{
    puck->facedir = (int16_t)((puck->facedir ^ (d0 & 1)) & 3);
    ST_W(puck->SPAcnt);
    SetSPA(puck, SPApflip);
}

/* logic94.asm:puckshadow. Assignment for the puck shadow: follow the puck, one below; when
 * its frame is not the puck it is the goal siren instead. */
void puckshadow(struct Team *tm, struct SortObj *shadow)
{
    const struct SortObj *puck = shadow - 1;
    if (shadow->frame != SPFpuck) {
        if (shadow->frame == 0) return;
        shadow->Xpos = 0;
        shadow->Ypos = 0x12C;
        shadow->Zpos = 0xE;
        if (puck->Ypos < 0) {
            shadow->attribute = 0x8000;
            shadow->Ypos = (int16_t)-shadow->Ypos;
            shadow->Zpos = (int16_t)(shadow->Zpos - 1);
        }
        return;
    }
    shadow->Xpos = puck->Xpos;
    shadow->Ypos = puck->Ypos;
    shadow->Zpos = 0;
    if (g.sflags & HI(sfhor)) shadow->Xpos = (int16_t)(shadow->Xpos + 1);
    else shadow->Ypos = (int16_t)(shadow->Ypos + 1);
}

/* logic94.asm:findpc. Find where and when the puck will cross each goal line (if at all),
 * into puckcross (x, frames) x 2. */
void findpc(void)
{
    findpc_calc(&g.puckcross[0], 0x88, 0x108);
    findpc_calc(&g.puckcross[1], 0x88, -0x108);
}

/* logic94.asm:skateto. Skate player a3 toward d0/d1, every 12 frames: the direction to the
 * point (9 = there), through the aux routine a0 (collision avoidance), then the acceleration.
 * Standing at the point turn to face the puck. */
void skateto(int16_t x, int16_t y, skate_aux aux, struct SortObj *o)
{
    if (TICK_HIB(o->temp2) < 0) {
        SET_HIB(o->temp2, HIB(o->temp2) + 0xC);
        avdgoal(&x, &y, o);
        int16_t d0 = (int16_t)(-(o->Xvel >> 8) + x - o->Xpos);
        int16_t d1 = (int16_t)(-(o->Yvel >> 8) + y - o->Ypos);
        if (d0 <= 0xC && d0 >= -0xC && d1 <= 0xC && d1 >= -0xC)
            d0 = 9;
        else
            d0 = (int16_t)vtoa(d0, d1);
        if (aux) d0 = (int16_t)aux((uint16_t)d0, o);
        SET_LOB(o->temp2, d0);
        if (d0 > 7 && (o->Xvel | o->Yvel) == 0) {         /* standing there: face the puck */
            d0 = puckx();
            d1 = pucky();
            if (g.puck_pflags2[0] & (1 << pf2fight)) { d0 = (int16_t)g.xc1; d1 = (int16_t)g.yc1; }
            d0 = (int16_t)(d0 - o->Xpos);
            d1 = (int16_t)(d1 - o->Ypos);
            d0 = (int16_t)(vtoa(d0, d1) - o->facedir);
            if (d0 != 0) {
                d0 = (int16_t)((((-d0) & 4) >> 1) - 1);
                o->facedir = (int16_t)((o->facedir + d0) & 7);
            }
        }
    }
    doplayeracc((uint8_t)o->temp2, o);
}

/* logic94.asm:avdgoal. Don't try to skate through the goal: if the line from a3 to d0/d1
 * crosses a net, move the point around it (deltax / deltay). */
void avdgoal(int16_t *x, int16_t *y, struct SortObj *o)
{
    g.deltax = 0;
    g.deltay = 0;
    if (o->position == 0) return;
    int16_t d0 = *x, d1 = *y;
    if ((int16_t)(o->Xpos ^ d0) < 0) {                    /* crossing x = 0: where in y? */
        int32_t d2 = (int32_t)o->Xpos * (int16_t)(d1 - o->Ypos);
        int16_t d3 = (int16_t)(o->Xpos - d0);
        int16_t cy = (int16_t)((int16_t)m68k_divs(d2, d3) + o->Ypos);
        if (cy <= 0x121) {
            if (cy >= 0xDB) {
                int16_t t = 0x144;
                if (!(cy > 0xFE) && (cy < 0xFE || !(o->Ypos > 0xFE))) t = 0xB8;
                g.deltay = (uint16_t)(t - cy);
            } else if (cy <= -0xDB && cy >= -0x121) {
                int16_t t = -0xB8;
                if (!(cy > -0xFE) && (cy < -0xFE || !(o->Ypos > -0xFE))) t = -0x144;
                g.deltay = (uint16_t)(t - cy);
            }
        }
    }
    avdgoal_ch1(d0, (int16_t)(o->Ypos - 0xFE), (int16_t)(d1 - 0xFE), o);
    avdgoal_ch1(d0, (int16_t)(o->Ypos + 0xFE), (int16_t)(d1 + 0xFE), o);
    *x = (int16_t)(d0 + (int16_t)g.deltax);
    *y = (int16_t)(d1 + (int16_t)g.deltay);
}

/* logic94.asm:skatetopuckinit. Find where the puck is going to be. */
void skatetopuckinit(int16_t *x, int16_t *y)
{
    *x = (int16_t)((HIB(puckvx()) >> 1) + puckx());
    *y = (int16_t)((HIB(puckvy()) >> 1) + pucky());
}

/* logic94.asm:skatetopuck. Player a3 should skate to the puck, every 10 frames; a skater
 * facing a puck coming straight at him from a little way off sweep checks. */
void skatetopuck(struct SortObj *o)
{
    int16_t x, y;
    skatetopuckinit(&x, &y);
    if (TICK_HIB(o->temp2) < 0) {
        SET_HIB(o->temp2, HIB(o->temp2) + 0xA);
        avdgoal(&x, &y, o);
        int16_t hx, hy;
        GetHot(o, &hx, &hy);
        int16_t d0 = (int16_t)(int8_t)((uint8_t)-(int8_t)hx - (uint8_t)HIB(o->Xvel));
        d0 = (int16_t)(d0 + x - o->Xpos);
        int16_t d1 = (int16_t)(int8_t)((uint8_t)-(int8_t)hy - (uint8_t)HIB(o->Yvel));
        d1 = (int16_t)(d1 + y - o->Ypos);
        d0 = (int16_t)vtoa(d0, d1);
        SET_LOB(o->temp2, d0);
        if (o->position != 0) {
            d0 = (int16_t)(vtoa(puckvx(), puckvy()) ^ 4);
            if (d0 != o->facedir) {
                d0 = (int16_t)(puckx() - o->Xpos);
                d1 = (int16_t)(pucky() - o->Ypos);
                if (vtoa(d0, d1) == (uint16_t)o->facedir) {
                    uint32_t dsq = (uint32_t)(sq(d0) + sq(d1));
                    if (dsq > 0x384 && dsq <= 0x5A4) { Sweepcheck(o); return; }
                }
            }
        }
    }
    doplayeracc((uint8_t)o->temp2, o);
}

/* logic94.asm:assexit. Exit the current assignment on player a3. */
void assexit(struct SortObj *o)
{
    o->assnum = (int16_t)((o->assnum + 1) & 7);
    o->pflags |= 1 << pfna;
}

/* logic94.asm:assinsert. Insert a new assignment on player a3 (the current one resumes
 * after it). */
void assinsert(uint16_t ass, struct SortObj *o)
{
    o->assnum = (int16_t)((o->assnum - 1) & 7);
    assreplace(ass, o);
}

/* logic94.asm:assreplace. Replace the current assignment on player a3. */
void assreplace(uint16_t ass, struct SortObj *o)
{
    o->asslist[o->assnum] = (uint8_t)ass;
    o->pflags |= 1 << pfna;
}

/* logic94.asm:vtoa. d0/d1 = x/y vector -> d0 = facing direction 0-7 (0 = up, clockwise),
 * 8 = no direction. Classifies the vector by the signs and by whether |x| is within half /
 * double of |y|, then looks the octant up. */
uint16_t vtoa(int16_t x, int16_t y)
{
    static const uint8_t dt[16] = { 1, 7, 3, 5, 0, 0, 4, 4, 2, 6, 2, 6, 1, 7, 3, 5 };
    if ((uint16_t)(x | y) == 0)
        return 8;
    unsigned q = 0;
    uint16_t ax = (uint16_t)x, ay = (uint16_t)y;
    if (x < 0) { ax = (uint16_t)-x; q |= 1; }
    if (y < 0) { ay = (uint16_t)-y; q |= 2; }
    if (!(ax > (uint16_t)(ay << 1))) q |= 4;        /* |x| <= 2|y| */
    if (!(ay > (uint16_t)(ax << 1))) q |= 8;        /* |y| <= 2|x| */
    return dt[q];
}

/* logic94.asm:GetHot. Long on the stack = object; returns d0/d1 = the hot spot (where the
 * puck sits) of the object's current frame, from HotList, flipped with the sprite and swapped
 * for the horizontal rink. */
void GetHot(const struct SortObj *o, int16_t *hx, int16_t *hy)
{
    int16_t x = 0, y = 0;
    if ((int16_t)o->frame > 0) {
        const uint8_t *hot = rom_ptr(SYM_HotList) + o->frame * 2;
        x = (int8_t)hot[0];
        y = (int8_t)hot[1];
        if (o->attribute & HI(3)) x = (int16_t)-x;          /* x flip */
        if (!(o->attribute & HI(4))) y = (int16_t)-y;       /* no y flip */
        if (g.sflags & HI(sfhor)) { int16_t t = x; x = y; y = (int16_t)-t; }
    }
    *hx = x; *hy = y;
}

/* logic94.asm:SetSPA. a3 = object, d1 = animation. Starts the animation from its first
 * frame unless it is already playing. */
void SetSPA(struct SortObj *o, uint16_t spa)
{
    if (o->SPA == spa)
        return;
    o->SPAnum = 0;
    o->SPA = spa;
    o->SPAcnt = (uint16_t)(0xFF00 | (o->SPAcnt & 0xFF));
}

/* logic94.asm:doplayeracc. Player a3 gets acceleration in direction d0 (8 = none, 9 = stop):
 * decide whether to skate backwards (defending in his own zone, or already going that way),
 * turn toward the direction with the turning animation when moving, then playeracc. */
void doplayeracc(uint16_t dir, struct SortObj *o)
{
    if (o->position == 0) { goalieacc(dir, o); return; }
    uint16_t d1 = PFLAG(o, pfrev) ? SPAglideback : SPAglide;
    uint16_t d0 = dir & 0xF;
    if (d0 > 7) {
        if (d0 == 9 && (o->Xvel | o->Yvel) != 0) { dostop(o); return; }
        if (!(o->pflags2 & (1 << pf2aip))) SetSPA(o, d1);
        return;
    }
    {
        int16_t d2 = (int16_t)d0;
        int rev = 0, decided = 0;
        if ((int16_t)o->SCnum != puckc()) {
            int16_t px = (int16_t)(puckx() - o->Xpos);
            int16_t d3 = o->Ypos;
            int16_t py = (int16_t)(HIB(puckvy()) + pucky() - d3);
            if (!PFLAG(o, pfgoal)) { px = (int16_t)-px; py = (int16_t)-py; d3 = (int16_t)-d3; d2 ^= 4; }
            if (!PFLAG(o, pfrev)) {
                if (d3 < 0 && d2 == 4) {                  /* in his own zone, skating toward his net */
                    uint16_t v = (uint16_t)((vtoa(px, py) + 1) & 7);
                    if (v <= 2) rev = 1;                  /* with the puck ahead of him */
                }
            } else {
                d2 = (int16_t)((d2 - 3) & 7);
                if ((uint16_t)d2 <= 2) {
                    uint16_t v = (uint16_t)((vtoa(px, py) + 2) & 7);
                    if (v <= 4) rev = 1;
                }
            }
            if (rev) {
                if (!PFLAG(o, pfrev)) {
                    int set = 1;
                    if ((o->Xvel | o->Yvel) != 0) {
                        uint16_t v = (uint16_t)((vtoa(o->Xvel, o->Yvel) - d0 + 1) & 7);
                        if (v > 2) set = 0;               /* only when already moving that way */
                    }
                    if (set) o->pflags |= 1 << pfrev;
                }
                decided = 1;
            }
        }
        if (!decided) o->pflags &= (uint8_t)~(1 << pfrev);
    }
    int16_t d2 = o->facedir;
    d0 = (uint16_t)((d0 - d2) & 7);
    if (turn_ftab[d0] == 0) { noturn0((uint16_t)(d0 * 2), d2, o); return; }
    int32_t spd = sq(o->Xvel) + sq(o->Yvel);
    int16_t d3 = (int16_t)(spd >> 16);
    int16_t d4 = (int16_t)(0x300 - d3);
    if (d4 < 0x180) d4 = 0x180;
    int32_t turn = (int32_t)d4 * (int16_t)turn_ftab[d0];
    if (PFLAG(o, pfrev)) turn = -turn;
    uint32_t v = (((uint32_t)(uint16_t)o->facedir << 16) | o->_pad56) + (uint32_t)turn;
    o->facedir = (int16_t)((v >> 16) & 7);
    o->_pad56 = (uint16_t)v;
    d2 = o->facedir;
    if ((uint16_t)d3 > 0x14) {                            /* fast enough for the turning animation */
        d1 = 0;
        if ((int16_t)turn_ftab[d0] < 0) d1 ^= SPAturnl - SPAturnr;
        if (o->attribute & HI(3)) d1 ^= SPAturnl - SPAturnr;
        d1 = (uint16_t)(d1 + SPAturnr);
        o->pflags2 |= 1 << pf2aip;
    }
    SetSPA(o, d1);
    if ((uint16_t)d3 > 2) noturn(d2, o);
}

/* logic94.asm:goalieacc. Goalie gets acceleration in direction d0: a human goalie in his
 * crease turns to face the puck (AdjustFacingDirection) and stops when idle; otherwise turn
 * one step toward d0 and skate. */
void goalieacc(uint16_t dir, struct SortObj *o)
{
    uint16_t d0 = dir;
    if (PFLAG(o, pfjoycon) && puckc() != o->SCnum && o->Xpos <= 0x30 && o->Xpos >= -0x30
        && (o->Ypos > -0xC0 ? (o->Ypos >= 0xC0 && o->Ypos <= 0x106) : o->Ypos >= -0xFA - 0xC)) {
        int16_t px = (int16_t)(puckx() - o->Xpos), py = (int16_t)(pucky() - o->Ypos);
        uint16_t face;
        if (px <= 0x10 && px >= -0x10 && py <= 0x10 && py >= -0x10) face = (uint16_t)o->facedir;
        else face = vtoa(px, py);
        if (!(g.sflags4 & HI(0)))
            AdjustFacingDirection((int16_t)face, o);
        else if (d0 != 8)
            AdjustFacingDirection((int16_t)d0, o);
        if (o->pflags2 & 2) return;
        SetSPA(o, SPAgready);
        if (d0 == 8 && o->position == 0 && PFLAG(o, pfjoycon))
            stopna2(o);                                   /* in the crease or not (.topstay), he slows down */
        playeracc((int16_t)d0, o);
        return;
    }
    uint16_t d1 = SPAgready;
    if (PFLAG(o, pfjoycon)) {
        if (PFLAG(o, pfnc)) goto cgl;
        if (d0 != 8) goto turn;
        if (o->position != 0) goto cgl;
        {
            int16_t y = o->Ypos;
            if (PFLAG(o, pfgoal)) y = (int16_t)-y;         /* his own end: the other way from the net he shoots at */
            if (y >= 0xD8 && o->Xpos <= 0x20 && o->Xpos >= -0x20)
                stopna(o);                                /* idle in the crease */
        }
        goto cgl;
    }
    d0 &= 0xF;
    if (d0 <= 7) goto turn;
    if (d0 != 9) goto cgl;
    if ((o->Xvel | o->Yvel) == 0) goto cgl;
    stopna2(o);
    return;
cgl:
    if (!(o->pflags2 & (1 << pf2aip))) SetSPA(o, d1);
    return;
turn:
    {
        int16_t d = (int16_t)(d0 - o->facedir);
        if (d != 0) {
            d = (int16_t)((((-d) & 4) >> 1) - 1);
            o->facedir = (int16_t)((o->facedir + d) & 7);
        }
    }
    SetSPA(o, SPAgskate);
    playeracc(o->facedir, o);
}

/* logic94.asm:noturn0. doplayeracc when the wanted direction needs no turn (.ftab entry 0).
 * d0 = direction minus facedir, times 2; d2 = facedir. Facing the other way: stop or glide
 * round; facing it: the skating animation. */
void noturn0(uint16_t d0, int16_t d2, struct SortObj *o)
{
    int16_t d4 = 2;
    if (PFLAG(o, pfrev)) { d4 = (int16_t)(d4 + 4); d0 ^= 8; }
    if (d0 != 0) {
        uint16_t v = vtoa(o->Xvel, o->Yvel);
        if (!(v & 8)) {
            v = (uint16_t)((v - o->facedir + d4) & 7);
            if (v < 4) { dostop(o); return; }
        }
        o->facedir = (int16_t)(o->facedir + 1);
        if (o->attribute & HI(3)) o->facedir = (int16_t)(o->facedir - 2);
        o->facedir &= 7;
        SetSPA(o, PFLAG(o, pfrev) ? SPAglideback : SPAglide);
        return;
    }
    uint16_t d1 = SPAskateback;
    if (!PFLAG(o, pfrev)) {
        d1 = SPAskate;
        if (o->pflags2 & 0x40) d1 = SPAstumble;           /* hurt in a fight */
        if (puckc() == o->SCnum) d1 = SPAskatewp;
    }
    if (!(o->pflags2 & (1 << pf2aip))) SetSPA(o, d1);
    noturn(d2, o);
}

/* logic94.asm:noturn. Reverse the acceleration direction when skating backwards, then
 * playeracc. */
void noturn(int16_t d2, struct SortObj *o)
{
    if (PFLAG(o, pfrev)) d2 ^= 4;
    playeracc(d2, o);
}

/* logic94.asm:playeracc. Accelerate player a3 in direction d2: the direction's unit vector
 * scaled by his agility and weight (goalies double), not into a wall, over the elapsed frames;
 * capped at his top speed, which his energy, speed rating and a fight injury set; then now and
 * then burn energy (line changes on). */
void playeracc(int16_t dir, struct SortObj *o)
{
    /* dirtab is read from the ROM: a human goalie's pad value can carry extra bits (d0 = $21), and
     * the 68k then reads the code that follows the table */
    const uint8_t *dt = rom_ptr(SYM_dirtab + ((uint16_t)dir << 2));
    int16_t d1 = (int16_t)rd16(dt + 2);
    int16_t d0 = (int16_t)rd16(dt);
    if (o->Wallsin != 0 && (int16_t)(d0 ^ o->Wallsin) < 0) d0 = 0;   /* don't push the wall */
    if (o->Wallcos != 0 && (int16_t)(d1 ^ o->Wallcos) >= 0) d1 = 0;
    int16_t d2 = (int16_t)(o->weight >> 2);
    d2 = (int16_t)(0x40 - d2);
    d2 = (int16_t)((d2 & 0xFF00) | ((d2 + o->legstr) & 0xFF));
    if ((g.sflags8 & 2) || (g.sflags7 & 0x40))
        d2 = (int16_t)((d2 & 0xFF00) | ((d2 + 2) & 0xFF));   /* crowd meter boost */
    if (o->position == 0) {
        d2 = (int16_t)((d2 & 0xFF00) | ((d2 + o->legstr) & 0xFF));   /* goalie gets double agility */
        d2 = (int16_t)(d2 + 0x10);
    }
    d2 = (int16_t)(d2 >> 1);
    d2 = (int16_t)rule_scale(RULE_SKATING, d2);           /* (rules: skating) */
    int32_t ax = ((int32_t)d0 * d2) >> 5;
    int32_t ay = ((int32_t)d1 * d2) >> 5;
    ax = (int32_t)(int16_t)ax * elapsed;
    ay = (int32_t)(int16_t)ay * elapsed;
    d0 = (int16_t)ax;
    d1 = (int16_t)ay;
    if (o->position == 0 && PFLAG(o, pfjoycon) && puckc() == o->SCnum) {
        d0 = (int16_t)(d0 >> 1);                          /* a human goalie carrying the puck */
        d1 = (int16_t)(d1 >> 1);
    }
    d0 = (int16_t)(d0 + o->Xvel);
    d1 = (int16_t)(d1 + o->Yvel);
    uint32_t d3 = (uint32_t)(sq(d0) + sq(d1));
    int16_t e = getpde(o);
    if (g.sflags7 & 0x10) e = 0x1000;
    uint16_t spd = o->legspd;
    if ((g.sflags8 & 2) || (g.sflags7 & 0x40)) {
        spd = (uint16_t)((spd + 2) & 0xFF);
        if ((int8_t)spd > 0x1E) spd = 0x1E;
    }
    g.TempRawSpd = spd;
    uint32_t prod = (uint32_t)(spd >> 1) * (uint16_t)e;
    prod <<= 4;
    uint16_t d2w = (uint16_t)(prod >> 16);
    d2w = (uint16_t)((d2w << 2) & 0x3F);
    g.TempEnergySpd = d2w;
    uint32_t cap = MaxSpeed[d2w / 4];
    g.TempMaxSpd = cap;
    if ((int16_t)g.TempEnergySpd < 0x3C && (g.TempRawSpd & 1)) {   /* odd rating: halfway to the next */
        int32_t nxt = (int32_t)MaxSpeed[(g.TempEnergySpd + 4) / 4];
        cap = (uint32_t)((((int32_t)nxt - (int32_t)g.TempMaxSpd) >> 1) + (int32_t)g.TempMaxSpd);
    }
    if (rules[RULE_SKATING] != 100) cap = (uint32_t)((uint64_t)cap * (uint32_t)(rules[RULE_SKATING] * rules[RULE_SKATING]) / 10000u);   /* (rules: a speed squared) */
    if (o->pflags2 & 0x40) cap >>= 3;                     /* injured in a fight */
    if (o->position == 0 && puckc() == o->SCnum) cap = (uint32_t)((int32_t)cap >> 1);
    if (!(d3 > cap)) {
        o->Xvel = d0;
        o->Yvel = d1;
    }
    if (g.OptLine != 0) return;
    if (vdp_hv16() & 0x7F) return;
    struct Team *tm = own_team(o);
    int16_t pd1 = (int16_t)((int8_t)o->pnum * 2);
    e = (int16_t)(getpde(o) - rule_scale(RULE_TIRING, 0x21));   /* (rules: tiring) */
    if (e < 0xC00) { setpde(tm, pd1, e); return; }
    e = (int16_t)(e + ((uint16_t)(int16_t)(int8_t)o->endurance >> 1));
    setpde(tm, pd1, e);
    if ((o->endurance & 1) && (g.vcount & 1))
        setpde(tm, pd1, (int16_t)(e + 1));
}

/* logic94.asm:dostop. Player a3 stops: the stopping animation when moving fast enough. */
void dostop(struct SortObj *o)
{
    if (o->Xvel > 0x1000 || o->Xvel < -0x1000 || o->Yvel > 0x1000 || o->Yvel < -0x1000) {
        uint16_t d1 = SPAglide;
        if (!PFLAG(o, pfrev)) {
            o->pflags2 |= 1 << pf2aip;
            d1 = SPAstop;
        }
        SetSPA(o, d1);
    }
    if (o->position != 0) stopna(o);
    else stopna2(o);
}

/* logic94.asm:stopna. Stop with no animation: slow the velocity toward 0 by $96. */
void stopna(struct SortObj *o)
{
    if (o->Xvel < 0) {
        o->Xvel = (int16_t)(o->Xvel + 0x96);
        if (o->Xvel >= 0) o->Xvel = 0;
    } else {
        o->Xvel = (int16_t)(o->Xvel - 0x96);
        if (o->Xvel < 0) o->Xvel = 0;
    }
    if (o->Yvel < 0) {
        o->Yvel = (int16_t)(o->Yvel + 0x96);
        if (o->Yvel >= 0) o->Yvel = 0;
    } else {
        o->Yvel = (int16_t)(o->Yvel - 0x96);
        if (o->Yvel < 0) o->Yvel = 0;
    }
}

/* the a0 callback of skateto in asspuckc: count the opponents converging on the puck (threat)
 * and turn away from one that is right in front */
uint16_t asspuckc_chkdir(uint16_t dir, struct SortObj *o)
{
    int16_t d0 = (int8_t)dir;
    int16_t d2 = (int16_t)(HIB(o->Xvel) + puckx());
    int16_t d3 = (int16_t)(HIB(o->Yvel) + pucky());
    g.threat = 0;
    const struct SortObj *a0 = opponents(o);
    for (int i = 0; i < 6; i++, a0++) {
        int16_t d1 = (int16_t)(HIB(a0->Xvel) + a0->Xpos - d2);
        if (d1 > 0x14 || d1 < -0x14) continue;
        d1 = (int16_t)(HIB(a0->Yvel) + a0->Ypos - d3);
        if (d1 > 0x14 || d1 < -0x14) continue;
        g.threat++;
        d0 = (int16_t)vtoa((int16_t)(o->Xpos - a0->Xpos), (int16_t)(o->Ypos - a0->Ypos));
        d1 = (int16_t)(o->facedir ^ 4);
        if (d1 != d0) continue;
        d1 = (int16_t)(vdp_hv16() & 1);
        d0 = (int16_t)((d0 + d1) & 7);
    }
    return (uint16_t)d0;
}
