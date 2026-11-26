/* Fighting, from NHL Hockey 93 (NHLPA93Genesis logic93.asm / hockey93.asm: checkfight, SetInst, fightinput,
 * assfight, chkhit, assfwatch and the fight animations in frames93.asm). The 94 cartridge keeps the fight
 * sprites (SPFgloves, SPFfight, SPFfinjury), the penalties (PenFighting $26, PenFighting2 $28, the
 * instigator $2A), the pf2fight flag and the ready made handles, and has dropped the rest: this puts the 93
 * code back, behind RULE_FIGHT (0 the cartridge's game, with no fights).
 *
 * The animations are not in the 94 ROM's SPAlist, so they are built here (fight_spa_table) and a SPA of
 * FIGHT_SPA or above is looked up in them (spa_table, used by updateanim).
 *
 * Not put back: the gloves on the ice (93 drew them with its glovestruct object, which 94 gave to the 4 Way
 * Play pad arrow), and the fight banner of the fighters' names (print only). */
#include "game.h"
#include "hal.h"
#include "rom.h"
#include "rules.h"
#include "symbols_gen.h"
#include "anim_gen.h"
#include <string.h>

/* ---- the fight animations (frames93.asm) ---- */
#define FIGHT_SPA 0x4000u                                 /* the first of them; one every $80 */
#define SPA_STRIDE 0x80
#define PFLAG(o, bit) (((o)->pflags >> (bit)) & 1)
enum { FA_FIGHT, FA_FGRAB, FA_FHELD, FA_FHIGH, FA_FLOW, FA_FHITH, FA_FHITL, FA_FFALL, FA_FINJURY, FA_COUNT };
#define SPAfight_ (FIGHT_SPA + FA_FIGHT * SPA_STRIDE)
#define SPAfgrab_ (FIGHT_SPA + FA_FGRAB * SPA_STRIDE)
#define SPAfheld_ (FIGHT_SPA + FA_FHELD * SPA_STRIDE)
#define SPAfhigh_ (FIGHT_SPA + FA_FHIGH * SPA_STRIDE)
#define SPAflow_ (FIGHT_SPA + FA_FLOW * SPA_STRIDE)
#define SPAfhith_ (FIGHT_SPA + FA_FHITH * SPA_STRIDE)
#define SPAfhitl_ (FIGHT_SPA + FA_FHITL * SPA_STRIDE)
#define SPAffall_ (FIGHT_SPA + FA_FFALL * SPA_STRIDE)
#define SPAfinjury_ (FIGHT_SPA + FA_FINJURY * SPA_STRIDE)

#define F SPFfight
static const int16_t fa_fight[2][10] = { { F, 8, F + 1, 8, F + 2, 8, F + 3, -6 }, { F, 8, F + 1, 8, F + 2, 8, F + 8, -6 } };
#define A (F + 3)
static const int16_t fa_fgrab[2][10] = { { A + 1, 28, A, -4 }, { A + 6, 28, A + 5, -4 } };
static const int16_t fa_fheld[2][10] = { { A + 1, 30, A, -4 }, { A + 6, 30, A + 5, -4 } };
static const int16_t fa_fhigh[2][10] = { { A + 2, 6, A + 3, 16, A + 2, 4, A, -16 }, { A + 7, 6, A + 8, 16, A + 7, 4, A + 5, -16 } };
static const int16_t fa_flow[2][10] = { { A + 2, 6, A + 4, 16, A + 2, 4, A, -16 }, { A + 7, 6, A + 9, 16, A + 7, 4, A + 5, -16 } };
static const int16_t fa_fhith[2][10] = { { A + 11, 16, A, -8 }, { A + 11, 16, A + 5, -8 } };
static const int16_t fa_fhitl[2][10] = { { A + 10, 16, A, -8 }, { A + 10, 16, A + 5, -8 } };
#define FA (F + 14)
#define FB SPFfinjury
static const int16_t fa_ffall[1][10] = { { FA, 8, FA + 1, 8, FA + 2, 200, FA + 2, -2 } };
static const int16_t fa_finjury[1][40] = { { FA, 8, FA + 1, 8, FA + 2, 20, FB, 8, FB + 1, 8, FB + 2, 8, FB + 3, 10, FB + 4, 10, FB + 5, 10,
                                             FB + 3, 10, FB + 4, 10, FB + 5, 10, FB + 3, 10, FB + 4, 10, FB + 5, 10, FB + 3, 10, FB + 4, 10, FB + 5, -400 } };

static uint8_t fight_spa_tab[FA_COUNT][SPA_STRIDE];       /* each: 8 direction offsets, the attribute word, the frame / time lists */

static void put(uint8_t *t, int at, int16_t v) { t[at] = (uint8_t)((uint16_t)v >> 8); t[at + 1] = (uint8_t)v; }

/* an animation of 1 or 2 lists: directions 0-2 take the first, 3-7 the second (a single list is all of them) */
static void build(int n, const int16_t *l0, const int16_t *l1, int len0, int len1)
{
    uint8_t *t = fight_spa_tab[n];
    int at = 0x12, o0 = at;
    memset(t, 0, SPA_STRIDE);
    for (int i = 0; i < len0; i++, at += 2) put(t, at, l0[i]);
    int o1 = o0;
    if (l1) {
        o1 = at;
        for (int i = 0; i < len1; i++, at += 2) put(t, at, l1[i]);
    }
    for (int d = 0; d < 8; d++) put(t, d * 2, (int16_t)((d < 3 || !l1) ? o0 : o1));
    put(t, 0x10, 0);                                      /* attributes: no loop */
}

static int len_of(const int16_t *l) { int n = 0; do n += 2; while (l[n - 1] >= 0); return n; }

static void fight_spa_init(void)
{
    static int done;
    if (done) return;
    done = 1;
#define TWO(id, a) build(id, a[0], a[1], len_of(a[0]), len_of(a[1]))
    TWO(FA_FIGHT, fa_fight); TWO(FA_FGRAB, fa_fgrab); TWO(FA_FHELD, fa_fheld); TWO(FA_FHIGH, fa_fhigh);
    TWO(FA_FLOW, fa_flow); TWO(FA_FHITH, fa_fhith); TWO(FA_FHITL, fa_fhitl);
    build(FA_FFALL, fa_ffall[0], NULL, len_of(fa_ffall[0]), 0);
    build(FA_FINJURY, fa_finjury[0], NULL, len_of(fa_finjury[0]), 0);
}

/* the animation table of a SPA (SPAlist's entry, or one of the fight animations here) */
const uint8_t *spa_table(uint16_t spa)
{
    if (spa >= FIGHT_SPA && spa < FIGHT_SPA + FA_COUNT * SPA_STRIDE) {
        fight_spa_init();
        return fight_spa_tab[(spa - FIGHT_SPA) / SPA_STRIDE];
    }
    return rom_ptr(SYM_SPAlist + spa);
}

/* The picture of a fight frame. The 94 cartridge kept the fight frames' numbers (SPFgloves 353 to SPFfinjury+5 376) but not their
 * art: each is one 8 x 8 sprite of tile 3749 (nothing to see), so a fighter was invisible. NHL 95 PC has no fights either (it gave
 * those numbers to its face off, anthem and goalie wall pictures). The fight's frames stay what the game counts (chkhit reads
 * them); only the picture is taken from the cartridge's own frames nearest each, in the fighter's facing (facedir 0-2 the right
 * hand list of the 93 tables, 3-7 the left): holding (SPFHold) for the gloves off, the stance, the grab and being held; hooking
 * (SPFhook) for the punches; stumbling (SPFstumble) for being hit; falling backward (SPFfallback) for the fall; injured on the ice
 * (SPFinjury1) for the injury. Any other frame is itself. */
#define SPFHold_ (SPFduck + 8)
uint16_t fight_frame_shown(uint16_t fr, int facedir)
{
    if (fr < SPFfight || fr >= SPFPen) return fr;
    int right = (facedir & 7) <= 2;
    int k = fr - SPFfight;                                /* 0-2 gloves off, 3 stance, 4 grab, 5-7 punch (wind up, high, low) ... */
    if (k >= 3 && k <= 7) right = 1;                      /* (A to A+4: the right hand list's own) */
    else if (k >= 8 && k <= 12) { right = 0; k -= 5; }    /* (A+5 to A+9: the left hand list's) */
    int d = right ? 2 : 6;
    if (k <= 4) return (uint16_t)(SPFHold_ + d);
    if (k <= 7) return (uint16_t)(SPFhook + d * 2 + (k == 5 ? 0 : 1));
    if (k == 13) return (uint16_t)(SPFstumble + d * 3 + 1);                    /* (A+10: hit low) */
    if (k <= 16) return (uint16_t)(k == 14 ? SPFstumble + d * 3 + 2 : SPFfallback + d * 4 + (k - 14));   /* (A+11 = FA: hit high or the fall's start; FA+1, FA+2 down) */
    return (uint16_t)(SPFinjury1 + (right ? 0 : 6) + (fr - SPFfinjury));       /* (FB: lying injured) */
}

/* ---- ui94.asm / logic: the fight's own state ---- */
#define pf2pen 4
#define pf2unav 2
#define pf2npc 5
#define pf2inj 6
#define PenFighting 0x26
#define PenFighting2 0x28
#define PenInstigator 0x2A
#define SFXhithigh 9
#define SFXhitlow 10
#define SFXcrowdcheer 0xB
#define SFXcrowdboo 0xC

/* the fighter a player is up against */
#define BUT_A 0x40
#define BUT_B 0x10
#define BUT_C 0x20
static struct SortObj *opponent(struct SortObj *o) { return sortobj((uint16_t)o->impactp); }

static void clear_message_line(void)
{
    static const uint8_t s[6] = { 0, 6, 191, 0, 1, 0 };   /* logic93.asm:ClearMessageLine: printz CC_Font1Pri,0,1,0 */
    printz(s, sizeof s);
    eraser(0x28, 5, 0x7FF);
}

/* logic93.asm:chkhit. Fighter a3 against a0: when his frame changed, a punch frame in reach hits. */
static void chkhit(struct SortObj *a3, struct SortObj *a0)
{
    if (a3->frame == a3->oldframe) return;
    uint16_t f = a3->frame;
    if (f == SPFfight + 2) {                              /* the gloves are dropped */
        a3->pflags2 &= (uint8_t)~(1 << pf2npc);
        int16_t d0 = (int16_t)g.xc1;
        if (d0 >= 90) d0 = 90;
        if (d0 <= -90) d0 = -90;
        d0 >>= 2;
        if (d0 == 0) d0 = 1;
        int16_t y = (int16_t)((int16_t)g.yc1 >> 2);
        g.glovecords = (uint16_t)(((uint8_t)d0 << 8) | (uint8_t)y);
        return;
    }
    int16_t dx = (int16_t)(a3->Xpos - a0->Xpos), dy = (int16_t)(a3->Ypos - a0->Ypos);
    if (dx > 22 || dx < -22 || dy > 8 || dy < -8) return;
    struct SortObj *victim = a0, *puncher = a3;
    int hit = 0;                                          /* 1 high, 2 low, 3 grab */
    if (f == SPFfight + 6 || f == SPFfight + 6 + 5) hit = 1;
    else if (f == SPFfight + 7 || f == SPFfight + 7 + 5) hit = 2;
    else if (f == SPFfight + 4 || f == SPFfight + 4 + 5) hit = 3;
    if (!hit) return;
    if (hit == 3) {                                       /* grabbed: held */
        victim->pflags2 |= 1 << pf2aip;
        SetSPA(victim, SPAfheld_);
        return;
    }
    /* the crowd reacts, the victim is pushed back by the puncher's strength */
    g.Crowdlevel = (uint16_t)(g.Crowdlevel + 0x50);
    g.CwdExciteLvl = (uint16_t)(g.CwdExciteLvl + 0xA);
    uint32_t push = (uint32_t)(5 + (uint16_t)puncher->temp3) * 0x1F4u;
    int16_t pv = (int16_t)push;
    if (victim->facedir == 2) pv = (int16_t)-pv;
    victim->Xvel = (int16_t)(victim->Xvel + pv);
    victim->pflags2 |= 1 << pf2aip;
    victim->temp3 = (int16_t)(victim->temp3 - 1);
    if (victim->temp3 >= 0) {
        SetSPA(victim, hit == 1 ? SPAfhith_ : SPAfhitl_);
        sfx(hit == 1 ? SFXhithigh : SFXhitlow);
        return;
    }
    /* the victim lost the fight: the puncher's penalty becomes the fighting one, maybe an injury */
    for (uint8_t *r = g.PenBuf; r < g.PenBuf + sizeof g.PenBuf; r += 2) {
        if (r[0] == PenFighting && (r[1] & 0xF) == (uint8_t)puncher->SCnum) { r[0] = PenFighting2; break; }
    }
    g.PenCntDwn = 0x3C;
    puncher->temp3 = -1;
    g.Crowdlevel = (uint16_t)(g.Crowdlevel + 0x258);
    g.CwdExciteLvl = (uint16_t)(g.CwdExciteLvl + 0x1E);
    uint16_t d0 = randomd0(0x3C);
    if ((int8_t)d0 > (int8_t)puncher->checking) {         /* no injury: he falls, the crowd cheers a visitor's fall and boos a home player's */
        SetSPA(victim, SPAffall_);
        song((int16_t)((victim->pflags & (1 << pfteam)) ? SFXcrowdcheer : SFXcrowdboo));
        return;
    }
    g.PenCntDwn = 0xB4;
    victim->pflags2 |= 1 << pf2inj;
    SetSPA(victim, SPAfinjury_);
    clear_message_line();
    setInjuryType(victim, victim);
    struct Team *tm = team_struct((victim->pflags & (1 << pfteam)) != 0);
    tm->tmpdst[victim->pnum] = 0x112C;                    /* (over setInjuryType's result) */
    g.InjCntDown = 3;
}

/* logic93.asm:assfight. A fighter's assignment: strength from energy and fighting, a computer fighter
 * throws punches, both are pulled to the fight's x and y. */
void assfight(struct Team *tm, struct SortObj *o)
{
    (void)tm;
    if (!rules[RULE_FIGHT]) return;
    if (PFLAG(o, pfalock)) return;
    if ((int16_t)g.PenCntDwn < 0) { donothing(o); return; }
    int first = PFLAG(o, pfna);
    o->pflags &= (uint8_t)~(1 << pfna);
    if (first) {
        uint16_t d0 = (uint16_t)((uint16_t)((uint16_t)getpde(o) * (uint16_t)(int16_t)(int8_t)o->fighting) >> 12);   /* (mulu.w: lsr.w works on the low word) */
        if ((int16_t)d0 <= 5) d0 = 5;
        o->temp3 = (int16_t)d0;
        o->temp2 = 0;
        o->temp4 = (int16_t)0xFFFF;
        if (o->facedir == 2) o->temp4 = 0x5A;             /* one of them shows the banner (not put back) */
    }
    o->temp4 = (int16_t)((uint16_t)o->temp4 - (uint16_t)elapsed);
    if (o->temp3 < 0) return;
    struct SortObj *a0 = opponent(o);
    g.xc1 = (uint16_t)(((int16_t)o->Xpos + (int16_t)a0->Xpos) >> 1);
    if (!PFLAG(o, pfjoycon)) {                            /* a computer fighter punches when the opponent is in range */
        int was = (o->pflags2 >> pf2aip) & 1;
        o->pflags2 |= 1 << pf2aip;
        if (!was) {
            int16_t d0 = (int16_t)(o->Xpos - a0->Xpos);
            d0 = (int16_t)(d0 + (o->Xvel >> 8));
            d0 = (int16_t)(d0 - (a0->Xvel >> 8));
            if (d0 <= 20 && d0 >= -20) {
                uint16_t r = randomd0(8);
                if (r > 2) r &= 1;
                static const uint16_t al[3] = { SPAfhigh_, SPAflow_, SPAfgrab_ };
                SetSPA(o, al[r]);
            }
        }
    }
    chkhit(o, a0);
    if (o->SPA == SPAfheld_ || (o->SPA != SPAfight_ && !PFLAG(o, pfjoycon))) {      /* (held: always; in the ready stance or human: not) */
        int16_t d0 = (int16_t)(g.xc1 + 0xA);
        if (o->facedir == 2) d0 = (int16_t)(d0 - 0x14);
        d0 = (int16_t)(d0 - o->Xpos);
        d0 = (int16_t)((uint16_t)d0 << 5);
        o->Xvel = (int16_t)(o->Xvel + d0);
    }
    int16_t d1 = (int16_t)((uint16_t)(int16_t)(g.yc1 - o->Ypos) << 8);
    if (d1 >= 0x1000) d1 = 0x1000;
    if (d1 <= -0x1000) d1 = (int16_t)0xF000;
    o->Yvel = d1;
}

/* logic93.asm:assfwatch. A player watching the fight skates to a spot 80 px from it, a new one every second. */
void assfwatch(struct Team *tm, struct SortObj *o)
{
    (void)tm;
    if (!rules[RULE_FIGHT]) return;
    if (PFLAG(o, pfalock)) return;
    if (PFLAG(o, pfjoycon)) return;
    int first = PFLAG(o, pfna);
    o->pflags &= (uint8_t)~(1 << pfna);
    if (first) o->temp1 = 0;
    o->temp1 = (int16_t)(o->temp1 - elapsed);
    if (o->temp1 < 0) {
        o->temp1 = (int16_t)(o->temp1 + 0x3C);
        int16_t dx = (int16_t)(o->Xpos - (int16_t)g.xc1), dy = (int16_t)(o->Ypos - (int16_t)g.yc1);
        uint16_t d2 = Sroot((uint32_t)((int32_t)dx * dx + (int32_t)dy * dy));
        if (d2 == 0) d2 = 1;
        int32_t x = (int32_t)dx * 0x50 / (int16_t)d2 + (int16_t)g.xc1;
        if (x >= 0x7E) x = 0x7E;
        if (x <= -0x7E) x = -0x7E;
        o->temp3 = (int16_t)x;
        o->temp4 = (int16_t)((int32_t)dy * 0x50 / (int16_t)d2 + (int16_t)g.yc1);
    }
    skateto(o->temp3, o->temp4, NULL, o);
    check4check(o);
}

/* logic93.asm:fightinput. A human fighter: the pad skates, C a high punch, B a low one, A a grab. d0 = the
 * direction bits, d1 = the new buttons. */
void fightinput(uint16_t d0, uint16_t d1, struct SortObj *o)
{
    if (o->temp3 < 0) return;
    if ((int16_t)g.PenCntDwn < 0) return;
    if (o->SPA == SPAfight_) return;
    uint16_t d2 = d0;
    if (!(d2 & 8)) playeracc((int16_t)(d2 & 7), o);
    int16_t t = (int16_t)(g.xc1 + 0xC);
    if (o->facedir == 2) t = (int16_t)(t - 0x18);
    t = (int16_t)(t - o->Xpos);
    if ((int16_t)(o->Xvel ^ t) < 0) {
        int16_t m = t < 0 ? (int16_t)-t : t;
        o->Xvel = (int16_t)(o->Xvel - (int16_t)(((int32_t)m * o->Xvel) >> 4));
    }
    if (o->pflags2 & (1 << pf2aip)) return;
    o->pflags2 |= 1 << pf2aip;
    if (d1 & BUT_C) { SetSPA(o, SPAfhigh_); return; }
    if (d1 & BUT_B) { SetSPA(o, SPAflow_); return; }
    if (d1 & BUT_A) { SetSPA(o, SPAfgrab_); return; }
    o->pflags2 &= (uint8_t)~(1 << pf2aip);
}

/* hockey93.asm:SetInst. a3 (rating needed d0) against a2: both are set to fight when a3's fighting is at
 * least d0, a2's is 2 or more and a2's is at least $10 - 2 * the checks it has had. Then, when a3 was not
 * already fighting and the game has penalties, 1 time in 4 a skater of a3's team that is free is penalised
 * as the instigator. */
static void SetInst(uint16_t need, struct SortObj *a2, struct SortObj *a3)
{
    if (a3->fighting < need) return;
    if ((int8_t)a2->fighting < 2) return;
    struct Team *tm = team_struct((a2->pflags & (1 << pfteam)) != 0);
    int d1 = 0x10 - 2 * (int8_t)tm->pchecks[a2->pnum];
    if ((int8_t)d1 > (int8_t)a2->fighting) return;
    a2->pflags2 |= 1 << pf2fight;
    if (a3->pflags2 & (1 << pf2fight)) return;             /* a3 was already fighting */
    a3->pflags2 |= 1 << pf2fight;
    if (g.OptPen == 0) return;
    if ((vdp_hv8() & 3) != 0) return;
    struct SortObj *p = sortobj((a3->pflags & (1 << pfteam)) ? 6 : 0);
    for (int i = 0; i < 6; i++, p++) {
        if (p == a3) continue;
        if (p->position <= 0) continue;
        if (p->pflags2 & (1 << pf2pen)) continue;
        if (p->pflags2 & (1 << pf2unav)) continue;
        AddPenalty2(PenInstigator, p);
        return;
    }
}

static void start_fight(struct SortObj *a2, struct SortObj *a3)   /* .sf: a3 against a2 */
{
    a3->impactp = a2->SCnum;
    AddPenalty2(PenFighting, a3);
    setd0player((uint16_t)a3->SCnum);
    assinsert(afight, a3);
    a3->attribute &= (uint16_t)~HI(3);
    int16_t d1 = (int16_t)0xFC00;
    if (a3->facedir != 2) { a3->attribute |= HI(3); d1 = (int16_t)-d1; }
    a3->Xvel = d1;
    a3->pflags2 |= (1 << pf2unav) | (1 << pf2aip) | (1 << pf2npc);
    a3->pflags &= (uint8_t)~(1 << pfalock);
    SetSPA(a3, SPAfight_);
}

/* hockey93.asm:checkfight. Two opposing players collided (a3 moving, a2 the other): when both are hit hard
 * enough (impact 10 and up), free to move, no penalty on them, no goalie, the clock running and the puck not on a
 * face off, SetInst decides whether they fight. */
/* NHL 92's SetInst (hockey.asm): a player in a shoulder check who starts a fight is the instigator: the first
 * available skater on his team (not him) gets the instigator's penalty */
static void inst92(struct SortObj *a3)
{
    if (a3->SPA != SPAshoulderchkl && a3->SPA != SPAshoulderchkr) return;
    struct SortObj *p = sortobj((a3->pflags & (1 << pfteam)) ? 6 : 0);
    for (int i = 0; i < 6; i++, p++) {
        if (p == a3) continue;
        if (p->position <= 0) continue;
        if (p->pflags2 & (1 << pf2pen)) continue;
        if (p->pflags2 & (1 << pf2unav)) continue;
        AddPenalty2(PenInstigator, p);
        return;
    }
}

int (*fight_cheat_hook)(void);

void checkfight(struct SortObj *a2, struct SortObj *a3)
{
    if (!rules[RULE_FIGHT]) return;
    int mode = rules[RULE_FIGHT];                          /* 1 NHL 93's, 2 NHL 92's, 3 the two together */
    if (PFLAG(a3, pfalock) || PFLAG(a2, pfalock)) return;
    if ((a3->pflags2 & (1 << pf2pen)) || (a2->pflags2 & (1 << pf2pen))) return;
    if (a3->position == 0 || a2->position == 0) return;
    if (g.gmode & (1 << gmclock)) return;
    if (g.gmode & (1 << gmhl)) return;
    struct SortObj *puck = sortobj(puckscnum);
    if (puck->pflags2 & (1 << pf2fight)) return;
    if (puck->asslist[puck->assnum >> 0] == pfaceoff) return;
    if (fight_cheat_hook && fight_cheat_hook()) {          /* (debug: FIGHT was typed: this check starts one, whatever the hit) */
        a3->pflags2 |= 1 << pf2fight;
        a2->pflags2 |= 1 << pf2fight;
        if (mode != 1 && g.OptPen) { inst92(a3); inst92(a2); }
    } else if (mode != 2 && (a3->impact < 10 || a2->impact < 10)) return;
    else if (mode == 1) {
        int16_t need = (int16_t)(0x28 - g.ChkCnt);
        if (need < 0) need = 0;
        need = (int16_t)((need >> 2) + 0xA);
        SetInst((uint16_t)need, a2, a3);
        SetInst((uint16_t)need, a3, a2);
        if (!(a3->pflags2 & (1 << pf2fight))) return;
    } else {                                              /* 92's roll: the more aggressive the two and the harder the hit, the likelier */
        if (mode == 3 && ((int8_t)a3->fighting < 2 || (int8_t)a2->fighting < 2)) return;   /* (hybrid: only between players who can fight) */
        uint16_t range = (uint16_t)((0x28 - (int8_t)a3->aggress - (int8_t)a2->aggress) << 7);
        uint16_t roll = (uint16_t)(randomd0(range) + 64);
        if (roll > (uint16_t)(a3->impact + a2->impact)) return;
        a3->pflags2 |= 1 << pf2fight;
        a2->pflags2 |= 1 << pf2fight;
        if (g.OptPen) { inst92(a3); inst92(a2); }
    }
    g.ChkCnt = 0;
    for (int i = 0; i < 26; i++) { team_struct(0)->pchecks[i] = 0; team_struct(1)->pchecks[i] = 0; }
    g.Crowdlevel = (uint16_t)(g.Crowdlevel + 0x3E8);
    g.CwdExciteLvl = (uint16_t)(g.CwdExciteLvl + 0x23);
    g.sflags &= (uint16_t)~(HI(sfspdir) | HI(sfssdir));
    set_puck_carrier((uint16_t)a3->SCnum);
    g.yc1 = (uint16_t)(((int16_t)a3->Ypos + (int16_t)a2->Ypos) >> 1);
    g.sflags |= HI(sfslock);
    int16_t d1 = 2;
    if (a3->Xpos >= a2->Xpos) d1 ^= 7;
    a3->facedir = d1;
    a2->facedir = (int16_t)(d1 ^ 7);
    start_fight(a2, a3);
    start_fight(a3, a2);
    for (int i = 0; i < 12; i++) {
        struct SortObj *p = sortobj((uint16_t)i);
        if (p->pflags2 & (1 << pf2fight)) continue;
        if (p->position == 0) continue;
        if (p->pflags2 & (1 << pf2unav)) continue;
        assinsert(afwatch, p);
    }
    puck->pflags2 |= 1 << pf2fight;
    assinsert(pnothing, puck);
    ST_W(g.collflag);
}
