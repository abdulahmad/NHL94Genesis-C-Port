/* penalty94.asm: penalty logic. Calls penalties and the other stoppages of play, awards penalty
 * shots on breakaways, sends players to the penalty box, runs power plays, shows the referee's
 * messages. The scoreboard, rink views, intermission, ticker and highlights are scoreboard.c
 * (scoreboard94.asm). */
#include <string.h>
#include "game.h"
#include "anim_gen.h"
#include "hal.h"
#include "rom.h"
#include "symbols_gen.h"
#include "view.h"
#include "penalty_strings_gen.h"

#define PFLAG(o, b) ((o)->pflags & (1 << (b)))
static inline int16_t puckx(void) { return (int16_t)g.puckx[0]; }
static inline int16_t pucky(void) { return (int16_t)g.pucky; }
static inline int16_t puckc(void) { return (int16_t)puck_carrier(); }
/* the Penaltylist entry of penalty number d0 (a word offset into the table): byte 0 = delay before
 * the stop, byte 1 = minutes (penmins), +2 a String (penname) (the message), then the referee animation */
static inline const uint8_t *penalty_entry(uint16_t pen) { const uint8_t *t = rom_ptr(SYM_Penaltylist); return t + rd16(t + pen); }
static inline uint8_t *penbuf(void) { return g.PenBuf; }

/* penalty94.asm:AddPenalty. Add penalty d0 for player a3. Ignored while the clock is stopped,
 * when penalties are off, for a player with pflags2 bit 2 set, or offsides when offsides are
 * off. */
void AddPenalty(uint16_t pen, struct SortObj *o)
{
    if (g.gmode & (1 << gmclock)) return;
    if (pen != 0xC) {                                     /* icing always */
        if (g.OptPen == 0) return;
        if (o->pflags2 & 4) return;
        if (!(g.gmode & (1 << gmoffs)) && pen == 0x10) return;
    }
    if (pen > 0x10 && !rule_chance(rules[RULE_PENALTIES])) return;   /* (rules: the penalties called) */
    AddPenalty2(pen, o);
}

/* penalty94.asm:AddPenalty2. Forced penalties like the faceoff and game over. d0 = penalty
 * number, a3 = player or a player on the penalized team. From $E (goal) up: crowd and song. A
 * penalty with minutes sets DelayedPen bit 1 (home) or bit 2 (away). */
void AddPenalty2(uint16_t pen, struct SortObj *o)
{
    if (g.sflags & HI(sfhor)) return;
    if ((int16_t)pen >= 0xE) {
        g.Crowdlevel += 0xC8;
        int16_t snd = 0xC;
        if (PFLAG(o, pfteam)) { g.CwdExciteLvl += 0x14; snd = 0xB; }
        song(snd);
    }
    uint8_t *a1 = penbuf();
    int i;
    for (i = 0; i < 32; i++, a1 += 2)                     /* a free slot */
        if (rd16(a1) == 0) break;
    if (i == 32) return;                                  /* buffer full */
    a1[1] = (uint8_t)o->SCnum;
    a1[0] = (uint8_t)pen;
    const uint8_t *e = penalty_entry(pen);
    if ((int8_t)e[penmins] <= 0) return;                        /* no minutes */
    g.DelayedPen |= PFLAG(o, pfteam) ? 4 : 2;
    int was = o->pflags2 & 0x10;
    o->pflags2 |= 0x10;
    if (was) wr16(a1, 0);                                 /* he already has one pending: drop this */
}

/* penalty94.asm:PenaltyManager. Called every game loop pass (d7 = elapsed frames). */
void PenaltyManager(void)
{
    updatepentime();
    checkfornewpen();
    chkprogress();
    UpdatePA();
}

/* penalty94.asm:chkprogress. Control the progress of the ref and the game through the
 * penalty events: the penalty shot message timer, then when the stop delay runs out and a
 * penalty with minutes is pending switch to the horizontal rink for the player to enter the
 * box (InProgress takes it from there). */
void chkprogress(void)
{
    if (g.BA_PS_flags & HI(2)) {
        if (g.BA_PS_flags & HI(7)) {
            PenaltyShotBox();
            g.BA_PS_flags &= (uint16_t)~HI(7);
            return;
        }
        if ((int16_t)g.msgtimer < 0) goto cont;
        g.msgtimer--;
        if ((int16_t)g.msgtimer >= 0) return;
        g.sflags2 &= (uint16_t)~HI(sf2drec);
        g.gmode2 &= (uint16_t)~HI(7);
        printz(str_chkprogress_1, sizeof str_chkprogress_1);
        eraser(0x1B, (g.gmode2 & HI(0)) ? 0xC : 8, 0x7FF);   /* the message area */
        return;
    }
cont:
    if (!(g.gmode & (1 << gmpen))) return;
    if ((int16_t)g.PenCntDwn < 0) { InProgress(); return; }
    g.PenCntDwn = (uint16_t)(g.PenCntDwn - elapsed);
    if ((int16_t)g.PenCntDwn >= 0) return;
    g.gmode &= (uint8_t)~(1 << gmpendel);
    uint8_t *a0 = penbuf();
    for (;; a0 += 2) {                                    /* any penalty with minutes? */
        if (rd16(a0) == 0) return;
        if ((int8_t)penalty_entry(a0[0])[penmins] > 0) break;
    }
    g.sflags2 |= HI(sf2drec);
    uint16_t vc = g.Vcount;
    forceblack();
    g.Vcount = vc;
    SetHor();
    DecompressGraphicsPlain(ART(RefMap2) + 8, g.ExtraChars);   /* the horizontal ref tiles */
    struct Matchup *gs = NULL;
    int16_t d3 = NewTicker(&gs);
    NewTicker3(d3, gs);
    puck_loose();
    struct SortObj *puck = sortobj(puckscnum);
    assinsert(pnothing, puck);
    puck->temp1 = 0x1C20;
    ST_W(g.RefStep);
    g.RefCnt = 0;
    UpdatePA();
    g.RefCnt = 0x32;
    PushRef(0);
    g.palcount = 0x18;
}

/* penalty94.asm:PenShotChk. a3 = checker, a2 = player hit, d0 = penalty. If a2 is on a
 * breakaway, no penalty shot is pending and PenShotPenalties has an entry for d0,
 * getBAplayerInfo sets one up and the result is that penalty (also pspenalty). */
uint16_t PenShotChk(uint16_t pen, struct SortObj *hit, struct SortObj *o)
{
    if (!(hit->pflags3 & (1 << pf3break)) || (g.BA_PS_flags & HI(3))) return pen;
    int16_t d1 = (int16_t)rd16(rom_ptr(SYM_PenShotPenalties) + pen);
    if (d1 < 0) return pen;
    if (getBAplayerInfo((uint16_t)hit->SCnum, o) & CCR_N) {
        g.BA_PS_flags &= (uint16_t)~HI(3);
        return pen;
    }
    g.pspenalty = (uint16_t)d1;
    return (uint16_t)d1;
}

/* penalty94.asm:getBAplayerInfo. d0 = SCnum of the player on the breakaway, a3 = the checker.
 * With penalties on and the other team's goalie in, set BA_PS_flags bit 3 and save the skater,
 * team, goalie and the three roster offsets. Returns the ccr: N clear when set up. */
uint8_t getBAplayerInfo(uint16_t skater, const struct SortObj *o)
{
    if (g.OptPen == 0) return CCR_N;
    g.BA_PS_flags |= HI(3);
    int16_t gk = getGoalieSCnum((int16_t)skater > 5 ? 5 : 0xB);   /* the other team's goalie */
    if (gk < 0) {
        g.BA_PS_flags &= (uint16_t)~HI(3);
        return CCR_N;
    }
    g.BA_Sktr_SCnum = skater;
    const struct SortObj *sk = sortobj((int16_t)skater);
    g.BA_Team = PFLAG(sk, pfteam) ? 1 : 0;
    g.BA_Skater_Offset = sk->pnum;
    g.BA_Goalie_SCnum = (uint16_t)gk;
    g.BA_Goalie_Offset = sortobj(gk)->pnum;
    g.BA_Checker_Offset = o->pnum;
    return (uint8_t)((int8_t)o->pnum < 0 ? CCR_N : 0);
}

/* penalty94.asm:InProgress. Ref in progress: update graphics, stats and the penalty
 * information. Takes the last PenBuf entry; the first pass logs a penalty with minutes and
 * sends the player to the box, later passes wait until he is off the ice and drop the entry.
 * When PenBuf is empty, recount the players and go to the faceoff or the penalty shot. */
void InProgress(void)
{
    if ((int16_t)g.RefCnt >= 0) return;                   /* animation in progress */
    if ((int16_t)g.RefStep >= 0) return;
    for (;;) {
        uint8_t *a0 = penbuf();
        if (rd16(a0) == 0) break;
        while (rd16(a0) != 0) a0 += 2;
        a0 -= 2;                                          /* the last entry */
        int called = a0[1] & 0x80;
        a0[1] = (uint8_t)(a0[1] & 0x7F);
        if (!called) {
            if (sortobj(a0[1])->position >= 0) return;       /* still on the ice */
            wr16(a0, 0);
            return;
        }
        uint16_t pen = a0[0];
        const uint8_t *e = penalty_entry(pen);
        g.sflags5 &= (uint8_t)~0x20;
        uint16_t d2 = e[penmins];
        if (d2 == 0 || (int8_t)d2 < 0) {                  /* no player involved */
            wr16(a0, 0);
            if (g.sflags & HI(sfhor)) continue;
            SetPA(pen);
            return;
        }
        if ((uint8_t)d2 == 5) g.sflags5 |= 0x20;          /* a major: matched as coincidental */
        uint16_t t = GetPeriodTimeRemaining();            /* log time, penalty and player */
        uint8_t *a4 = g.PenSum + g.PenSumLength;
        if (g.PenSumLength != 0xEC) g.PenSumLength += 4;
        wr16(a4, t);
        a4[2] = (uint8_t)pen;
        struct SortObj *pl = sortobj(a0[1]);
        uint16_t d0 = 0;
        struct Team *a2 = team_struct(0), *a1 = team_struct(1);
        if (PFLAG(pl, pfteam)) {
            a4[2] = (uint8_t)(a4[2] | 0x80);
            d0 = 0x8000;
            struct Team *tt = a1; a1 = a2; a2 = tt;
        }
        a2->tmPenalties++;
        a2->tmPenmin += d2;
        d0 = (uint16_t)((d0 & 0xFF00) | pl->pnum);
        a4[3] = pl->pnum;
        g.TempPlOffset = d0;
        int16_t p = (int8_t)pl->pnum;
        a2->ppim[p] += d2;                                        /* his penalty minutes */
        p = (int16_t)(p * 2);
        d2 = (uint16_t)((uint16_t)(int8_t)d2 * 0x3C);
        d2 |= 0x2000;
        uint16_t old = a2->tmpdst[p / 2];
        if ((int16_t)old >= 0 && (old & 0x1000)) d2 |= 0x1000;   /* keep bit 12 of an old time */
        a2->tmpdst[p / 2] = d2;
        d2 &= 0xEFFF;
        for (int16_t d1 = 0x32; d1 >= 0; d1 -= 2) {       /* the same time on the other team: coincidental */
            if ((a1->tmpdst[d1 / 2] & 0xEFFF) != d2) continue;
            if (!(g.sflags5 & 0x20)) continue;
            a1->tmpdst[d1 / 2] |= 0x4000;
            a2->tmpdst[p / 2] |= 0x4000;
            break;
        }
        uint8_t *box = a2->tmboxlist;                        /* add him to the penalty box list */
        for (int i = 0; i <= 0x18; i++)
            if ((int8_t)*box++ < 0) break;
        box[-1] = (uint8_t)p;
        *box = 0xFF;
        assreplace(apenalty, pl);
        SetPA(pen);
        USBoard();
        return;
    }
    InProgress_ap(team_struct(0));
    InProgress_ap(team_struct(1));
    g.gmode &= (uint8_t)~(1 << gmpen);
    g.BA_PS_flags &= (uint16_t)~HI(6);
    assreplace((g.BA_PS_flags & HI(3)) ? pshootout : pfaceoff, sortobj(puckscnum));
}

/* penalty94.asm:InProgress_ap. a2 = team. Clears tmpdst bit 5, counts the players kept off the
 * ice by penalties (coincidental and bit 4 times do not count, at most 2) and sets tmap. */
void InProgress_ap(struct Team *tm)
{
    uint16_t d1 = 6;
    for (int16_t d0 = 0x32; d0 >= 0; d0 -= 2) {
        uint16_t st = tm->tmpdst[d0 / 2];
        if ((int16_t)st <= 0) continue;
        st &= (uint16_t)~0x2000;
        tm->tmpdst[d0 / 2] = st;
        if (st & 0x4000) continue;
        if (st & 0x1000) continue;
        if (d1 == 4) continue;                            /* never below 4 players */
        d1--;
    }
    tm->tmap = d1;
}

/* penalty94.asm:checkfornewpen. Look for a new penalty (entered through AddPenalty). Stops
 * play at once for penalties without minutes or when the guilty team does not have the puck,
 * otherwise starts a delayed penalty call. */
void checkfornewpen(void)
{
    if (g.sflags & HI(sfhor)) return;
    uint8_t *a0 = penbuf();
    for (;;) {
        if (rd16(a0) == 0) return;
        a0 += 2;
        if (!(g.gmode & (1 << gmpen))) {
            const uint8_t *e = penalty_entry(a0[-2]);
            int delayed = 0;
            if (e[penmins] != 0) {
                int16_t d0 = puckc();
                if (d0 < 0) delayed = 1;
                else {
                    d0 = (int16_t)(d0 - 6);
                    int16_t d1 = (int16_t)((int8_t)a0[-1] - 6);
                    if ((int16_t)(d0 ^ d1) < 0) delayed = 1;   /* the other team has the puck */
                }
            }
            if (delayed) {
                int was = g.gmode & (1 << gmpendel);
                g.gmode |= 1 << gmpendel;
                if (!was) SetPA(0x2C);                    /* delayed penalty */
                continue;
            }
            Stop4Pen(ram_addr(a0), NULL);
        }
        int was = a0[-1] & 0x80;
        a0[-1] = (uint8_t)(a0[-1] | 0x80);
        if (was) continue;
        const uint8_t *e = penalty_entry(a0[-2]);         /* the longest stop delay wins */
        uint16_t d1 = (uint16_t)(e[0] << 5);
        if ((int16_t)d1 <= (int16_t)g.PenCntDwn) continue;
        g.PenCntDwn = d1;
    }
}

/* penalty94.asm:Stop4Pen. a0 = PenBuf entry + 2 (the penalty number and player sit before it;
 * puckfaceoff and puckshootout branch here with a0 = their own code). Stop the clock, set the
 * faceoff spot from the penalty type, blow the whistle. */
void Stop4Pen(uint32_t a0, struct SortObj *o)
{
    int was = g.gmode & (1 << gmclock);
    g.gmode |= 1 << gmclock;
    uint16_t leak = 0;                                    /* d0 for stop_current_song (the caller's when the clock was stopped: not followed) */
    if (!was) {
        int16_t d0 = 0, d1 = 0;
        uint8_t pen = m8(a0 - 2);
        if (pen != 0xE) {                                 /* a goal: center ice */
            d0 = (int16_t)g.ltx;
            d1 = (int16_t)g.lty;
            if (pen != 6) {                               /* out of play: the last touch */
                d0 = puckx();
                d1 = pucky();
                if (pen == 0xC) {                         /* icing: the far end */
                    const struct SortObj *pl = sortobj((int8_t)m8(a0 - 1));
                    d1 = PFLAG(pl, pfgoal) ? -0x258 : 0x258;
                }
            }
        }
        int16_t x0 = d0;
        if (d0 >= 0x46) d0 = 0x46;
        if (d0 <= -0x46) d0 = -0x46;
        if (d1 >= 0xA6) { d1 = 0xCE; d0 = x0 < 0 ? -0x46 : 0x46; }
        if (d1 <= -0xA6) { d1 = -0xCE; d0 = x0 < 0 ? -0x46 : 0x46; }
        g.fox = (uint16_t)d0;
        g.foy = (uint16_t)d1;
        limitfo();                                        /* keeps d0 */
        leak = (uint16_t)d0;
    }
    g.PenCntDwn = 0;
    g.gmode |= 1 << gmpen;
    stop_current_song(leak);
    sfx(SYM_SFXwhistle);
    SetPA(0xA);                                           /* the whistle */
}

/* penalty94.asm:limitfo. Limit the faceoff to 5-20 feet from the walls, for every player in
 * PenBuf (the first entry is looked at even when empty). */
void limitfo(void)
{
    uint8_t *a0 = penbuf();
    do {
        const struct SortObj *pl = sortobj(a0[1] & 0x7F);
        int16_t d1 = 0x58;
        if (!PFLAG(pl, pfgoal)) {
            d1 = (int16_t)-d1;
            if (d1 < (int16_t)g.foy) goto next;
            g.foy = (uint16_t)-0x41;
        } else {
            if (d1 > (int16_t)g.foy) goto next;
            g.foy = 0x41;
        }
        g.fox = (uint16_t)((int16_t)g.fox < 0 ? -0x46 : 0x46);
next:
        a0 += 2;
    } while (rd16(a0) != 0);
}

/* penalty94.asm:UpdatePA. Animate the ref in the ref window. Runs ShowThreeStars for game
 * over, and the horizontal penalty message line is reprinted when penmsgtimer runs out. */
void UpdatePA(void)
{
    if ((int16_t)g.RefCnt < 0) return;
    g.RefCnt = (uint16_t)(g.RefCnt - elapsed);
    if ((int16_t)g.RefCnt < 0) SetPA2();
    if (g.RefPen == 4) ShowThreeStars();
    g.penmsgtimer = (uint16_t)(g.penmsgtimer - elapsed);
    if ((int16_t)g.penmsgtimer >= 0) return;
    g.penmsgtimer = 0x7FFF;
    ClearPenaltyMessage();
    printz(str_UpdatePA_1, sizeof str_UpdatePA_1);
    uint32_t a1 = GetPlayerName();
    g.printx -= m16(a1) >> 1;                             /* center it */
    print(a1);
}

/* penalty94.asm:SetPA. Start the ref animation d0 (the penalty number). Animations from $2E up
 * are ignored; a goal shows the goal box and the shootout its result. */
void SetPA(uint16_t pen)
{
    g.RefPen = pen;
    if ((int16_t)pen >= 0x2E) return;
    g.RefStep = 0;
    prefmes((int16_t)pen);
    if (pen == 0xE) ShowGoalBox();
    if ((g.gmode2 & HI(0)) && (g.gmode2 & HI(3))) ShootoutWonBy();
    g.penmsgtimer = 0x7FFF;
    if (g.sflags & HI(sfhor)) g.penmsgtimer = 0x3C;
    SetPA2();
}

/* penalty94.asm:SetPA2. The next frame / delay pair of the ref animation of RefPen (frame in
 * the high byte, delay * 8 frames in the low byte; negative = the last one). The delay is
 * doubled when the shootout is decided. */
void SetPA2(void)
{
    uint16_t d0 = 0x40;                                   /* clear the ref window */
    if ((int16_t)g.RefStep >= 0) {
        d0 = g.RefStep;
        g.RefStep += 2;
        const uint8_t *a0 = penalty_entry(g.RefPen) + penname;
        a0 += rd16(a0);
        d0 = rd16(a0 + d0);
        if ((int16_t)d0 < 0) {
            d0 = (uint16_t)-d0;
            ST_W(g.RefStep);
        }
        g.RefCnt = (uint16_t)((d0 & 0xFF) << 3);
        if ((g.gmode2 & HI(0)) && (g.gmode2 & HI(3))) g.RefCnt = (uint16_t)(g.RefCnt * 2);
        d0 >>= 8;
    }
    PushRef(d0);
}

/* penalty94.asm:PushRef. Tell vblank what to display: d0 = ref frame into RefRamMap, $40
 * clears the window. The horizontal ref uses RefMap2. */
void PushRef(uint16_t frame)
{
    uint8_t *a1 = g.RefRamMap;
    if (frame == 0x40) {
        if (!(g.sflags & HI(sfhor))) {                    /* horizontal mode only clears the line */
            for (int i = 0; i < 0x38; i++) wr16(a1 + i * 2, 0x7FF);
            g.sflags2 |= HI(sf2refref);
        }
        prefmes(-1);
        return;
    }
    const uint8_t *a0 = rom_ptr((g.sflags & HI(sfhor)) ? ART(RefMap2) : ART(RefsMap));
    a0 += rd32(a0 + 4);
    a0 += 4;
    a0 += (int16_t)(uint16_t)(frame * 0x70);
    uint16_t d2 = g.ExtraChars;
    if (!(g.sflags & HI(sfhor))) d2 |= 0x8000;            /* priority */
    for (int i = 0; i < 0x38; i++) wr16(a1 + i * 2, (uint16_t)(rd16(a0 + i * 2) + d2));
    g.sflags2 |= HI(sf2refref);
}

/* penalty94.asm:prefmes. Print the message for penalty d0 (negative clears it). Vertical
 * mode: framed under the ref. Horizontal mode: one centered line on row $B. */
void prefmes(int16_t pen)
{
    if (g.sflags & HI(sfhor)) {
        ClearPenaltyMessage();
        if (pen < 0) return;
        printz(str_prefmes_3, sizeof str_prefmes_3);
        const uint8_t *a1 = penalty_entry((uint16_t)pen) + penname;
        g.printx -= rd16(a1) >> 1;
        print((uint32_t)(a1 - rom_cart));
        return;
    }
    if (pen < 0) {
        printz(str_prefmes_1, sizeof str_prefmes_1);
        eraser(0xD, 3, 0x7FF);
        return;
    }
    printz(str_prefmes_2, sizeof str_prefmes_2);
    const uint8_t *a1 = penalty_entry((uint16_t)pen) + penname;
    uint16_t d0 = (uint16_t)(rd16(a1) - 2);
    if (d0 == 0) return;                                  /* empty string */
    g.printx -= d0 >> 1;
    if ((int16_t)g.printx < 0) g.printx = 0;
    d0 = rd16(a1);
    if (a1[d0 - 1] == 0) d0--;
    Framer(d0, 3);
    g.printy -= 2;
    g.printx += 1;
    print((uint32_t)(a1 - rom_cart));
}

/* penalty94.asm:ClearPenaltyMessage. Blank the horizontal mode penalty message line. */
void ClearPenaltyMessage(void)
{
    printz(str_ClearPenaltyMessage_1, sizeof str_ClearPenaltyMessage_1);
}

/* penalty94.asm:PenGoalStuff. After a goal: a1 = scored on team, a2 = scoring team. Clears
 * PenBuf only when the scored on team has 6 on the ice. A power play goal releases the first
 * scored on player in the box without a coincidental penalty. */
void PenGoalStuff(struct Team *a1, struct Team *a2)
{
    if (a1->tmap == 6) ClearPenaltyBuffer();
    if ((int16_t)a2->tmap <= (int16_t)a1->tmap) return;
    uint8_t *a0 = a1->tmboxlist;
    for (;;) {
        uint8_t d2 = *a0++;
        if ((int8_t)d2 < 0) return;
        if (a1->tmpdst[d2 / 2] & 0x4000) continue;          /* coincidental */
        a1->tmpdst[d2 / 2] = 0;
        RemovePlayerFromList(a0);
        g.DelayedPen |= 1;
        a1->tmap++;
        a2->tmPwrGoals++;
        team_struct(0)->tmflags |= 1;                                                /* tmflcc: line changes */
        team_struct(1)->tmflags |= 1;
        return;
    }
}

/* penalty94.asm:ClearPenaltyBuffer. */
void ClearPenaltyBuffer(void)
{
    uint8_t *a0 = penbuf();
    for (int i = 0; i < 32; i++) wr16(a0 + i * 2, 0);
}

/* penalty94.asm:updatepentime. Update the time remaining on all penalized players, once a
 * second (sflags3 bit 6 marks that tick). */
void updatepentime(void)
{
    g.sflags3 &= (uint8_t)~0x40;
    if (g.gmode & (1 << gmclock)) return;
    g.Penaltytimer = (uint16_t)(g.Penaltytimer - elapsed);
    if ((int16_t)g.Penaltytimer >= 0) return;
    g.Penaltytimer += 0x18;
    g.sflags3 |= 0x40;
    chkatop();
    updatePPTeamTime();
    updatepentime_ut(team_struct(0));
    updatepentime_ut(team_struct(1));
}

/* a player's box time is 5 seconds or less: beep, and release him at 0 */
static void penbox_check(struct Team *tm, uint16_t d0)
{
    if ((int16_t)tm->tmpdst[d0 / 2] > 5) return;
    int16_t snd = SYM_SFXbeep1;
    if (tm->tmpdst[d0 / 2] == 0) {
        releasepl(tm, d0);
        snd = SYM_SFXbeep2;
    }
    sfx(snd);
}

/* penalty94.asm:updatepentime_ut. a2 = team. Walk the penalty box list: the first two
 * players without a coincidental penalty count down one second and the rest wait; beep when
 * the first served time gets to 5 or less and release the player at 0. Coincidental penalties
 * count down on their own. */
void updatepentime_ut(struct Team *tm)
{
    uint8_t *a0 = tm->tmboxlist;
    uint8_t *a1 = g.mesarea;                              /* the serving players (the message area as scratch) */
    int16_t d1 = 2;
    for (;;) {
        uint8_t d0 = *a0++;
        if ((int8_t)d0 < 0) break;
        uint16_t st = tm->tmpdst[d0 / 2];
        if (st & 0x4000) {                                /* coincidental: on its own clock */
            st--;
            tm->tmpdst[d0 / 2] = st;
            if (!(st & 0x0800)) continue;
            tm->tmpdst[d0 / 2] = (st & 0x1000) ? 0 : 0x1000;
            RemovePlayerFromList(a0);
            return;
        }
        if (--d1 < 0) continue;                           /* third or later player waits */
        wr16(a1, d0);
        a1 += 2;
        st--;
        tm->tmpdst[d0 / 2] = st;
        if (st == 0) RemovePlayerFromList(a0);            /* time is up */
    }
    uint16_t first = rd16(g.mesarea), second = rd16(g.mesarea + 2);   /* (the message area as scratch) */
    if (d1 == 1) { penbox_check(tm, first); return; }
    if (d1 == 0) { penbox_check(tm, first); penbox_check(tm, second); return; }
    if (d1 == -1) penbox_check(tm, second);
}

/* penalty94.asm:RemovePlayerFromList. Remove the entry before a0 from a penalty box list by
 * shifting the rest down (the list ends with a negative byte). Returns the removed slot. */
uint8_t *RemovePlayerFromList(uint8_t *a0)
{
    int d2 = 0;
    for (;;) {
        a0[d2 - 1] = a0[d2];
        if ((int8_t)a0[d2] < 0) break;
        d2++;
    }
    return a0 - 1;
}

/* penalty94.asm:chkatop. Attack time of possession stat, once a second. */
void chkatop(void)
{
    uint16_t d1 = 0;
    int16_t d0 = pucky();
    if (!(d0 > 0x58)) {
        d1 = tmsize;
        d0 = (int16_t)-d0;
        if (d0 < 0x58) return;
    }
    if (g.gmode & (1 << gmdir)) d1 ^= tmsize;
    struct Team *tm = team_struct(d1 != 0);
    tm->tmATOP++;
}

/* penalty94.asm:releasepl. A player's penalty time is up so let him out: a2 = team, d0 =
 * player * 2. He takes the first sort object not on the ice, at the next position in
 * importance (priolist, after the goalie when there is one). */
void releasepl(struct Team *tm, uint16_t d0)
{
    struct SortObj *a3 = sortobj_at(tm->tmsort);
    while (a3->position >= 0) a3++;
    uint16_t d3 = d0 >> 1;
    uint16_t d1 = tm->tmap;
    tm->tmap = (uint16_t)(d1 + 1);
    team_struct(0)->tmflags |= 1;
    team_struct(1)->tmflags |= 1;
    const uint8_t *a0 = rom_ptr(SYM_priolist);
    if ((int16_t)tm->tmgoalie < 0) a0++;
    a3->position = a0[d1];
    Setplass(a3);
    setplayer(d3, a3);
    a3->pflags2 |= 4;
}

/* penalty94.asm:GetLowestPen. a2 = shorthanded team, a3 = team on the power play. Returns
 * the power play time left from the penalty box times. */
uint16_t GetLowestPen(struct Team *a2, struct Team *a3)
{
    uint16_t d0 = 0, d3 = 0;
    const uint8_t *a0 = a2->tmboxlist;
    for (;;) {
        uint8_t i = *a0++;
        if ((int8_t)i < 0) break;
        uint16_t d2 = a2->tmpdst[i / 2];
        if (d2 & 0x4000) continue;                        /* coincidental */
        d2 = (uint16_t)(d2 - d3);
        d0 = (uint16_t)(d0 + d2);
        d3 = d2;
    }
    if (a3->tmap == 6) return d0;                           /* 6 on the ice: done */
    d0 = (uint16_t)(d0 - d3);
    a0 = a3->tmboxlist;
    for (;;) {
        uint8_t i = *a0++;
        if ((int8_t)i < 0) return d0;
        if ((uint8_t)(a3->tmpdst[i / 2] >> 8) & 0x40) continue;
        if ((int16_t)d0 < (int16_t)a3->tmpdst[i / 2]) return d0;
        return (uint16_t)(d0 + d3);
    }
}
