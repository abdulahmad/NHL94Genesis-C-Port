/* scoreboard94.asm: the power play graphic, the rink's horizontal and vertical views, the
 * scoreboard, the intermission between periods, the score ticker and the highlights of the other
 * games (Intermission, DoHiLights, StartHL, StartHL2). Cut from penalty94.asm as the asm is. */
#include <string.h>
#include "game.h"
#include "anim_gen.h"
#include "hal.h"
#include "rom.h"
#include "symbols_gen.h"
#include "view.h"
#include "scoreboard_tables_gen.h"
#include "scoreboard_strings_gen.h"

#define PFLAG(o, b) ((o)->pflags & (1 << (b)))

/* the end of a power play: the EASN logo comes back */
static void clrpwrplay(void)
{
    int was = g.sflags2 & HI(sf2pwrplay);
    g.sflags2 &= (uint16_t)~HI(sf2pwrplay);
    if (!was) return;
    EASNLogo();
}

/* scoreboard94.asm:updatepwrplay. Show the graphic and time remaining for a power play (nothing
 * when gmode2 bit 1 is set). Prints the time and the team on the power play. */
void updatepwrplay(void)
{
    if (g.gmode2 & HI(1)) return;
    struct Team *a2 = team_struct(0), *a3 = team_struct(1);
    int16_t d0 = (int16_t)(a2->tmap - a3->tmap);
    if (d0 == 0) { clrpwrplay(); return; }                /* the same number on the ice */
    if (d0 < 0) {
        if (!(g.sflags2 & HI(sf2pwrtm))) {
            clrpwrplay();
            g.sflags2 |= HI(sf2pwrtm);
        }
    } else {
        struct Team *t = a2; a2 = a3; a3 = t;
        if (g.sflags2 & HI(sf2pwrtm)) {
            clrpwrplay();
            g.sflags2 &= (uint16_t)~HI(sf2pwrtm);
        }
    }
    int was = g.sflags2 & HI(sf2pwrplay);
    g.sflags2 |= HI(sf2pwrplay);
    if (!was) a3->tmPwrPlays++;
    printz(str_updatepwrplay_1, sizeof str_updatepwrplay_1);
    print(PushTime(GetLowestPen(a2, a3)));
    uint32_t mes = ram_addr(g.mesarea);
    wr16(g.mesarea, 2);
    appendz(str_updatepwrplay_2, sizeof str_updatepwrplay_2, mes);
    appstring_p(roster_text(&rosters_team(a3->tmteam)->abbr), mes);      /* the team's abbreviation */
    print(mes);
}

/* scoreboard94.asm:ClrHor. Revert the graphics to the vertical ice rink: reload the rink tiles
 * and the home team's graphics, redraw the scoreboard. */
void ClrHor(void)
{
    g.sflags &= (uint16_t)~HI(sfhor);
    view_rows_scroll();
    g.Oldrow = 0x3E8;
    uint16_t d4 = DecompressGraphicsPlain(ART(Rinktiles), g.rinkvrcset);
    LoadHomeTeamGfx(d4);
    SprSort();
    if (g.sflags & HI(sfpz)) return;
    printz(str_ClrHor_1, sizeof str_ClrHor_1);
    eraser(0x20, 0x1C, 0x7FF);
    printscores1();
}

/* scoreboard94.asm:SetHor. Switch the graphics to the horizontal ice rink. */
void SetHor(void)
{
    g.sflags |= HI(sfhor);
    while (g.disflags & HI(dfok)) hal_wait_vblank();      /* wait for dfok clear */
    g.Hscroll = (uint16_t)view_shift();                   /* (the wide view: the 256-wide rink in the middle) */
    g.Vscroll = 0;
    SprSort();
    if (view_shift()) DoFill(0x800, g.maps[1].vram, 0x7FF);     /* (and nothing beside it) */
    view_rows_scroll();
    printz(str_SetHor_1, sizeof str_SetHor_1);
    uint32_t a1 = ART(HorRinkMap) + rd32(rom_ptr(ART(HorRinkMap)) + 4);
    dobitmap(0, a1, ART(HorRinkTiles), 0, 0, 0x20, 0x1C, g.rinkvrcset, 0);   /* a0 (the palette) is not set */
    USBoard();
    if (g.sflags & HI(sfpz)) return;
    DoFill(0x800, g.maps[0].vram, 0x7FF);
}

/* scoreboard94.asm:printscores1. Draw the scoreboard. Vertical rink: the period box and the
 * score box (team names and scores). Horizontal rink: the period and both team logos with big
 * scores. */
void printscores1(void)
{
    if (g.sflags & HI(sfhor)) {
        printz(str_printscores1_6, sizeof str_printscores1_6);
        uint16_t d0 = g.gsp;
        if (g.gmode2 & HI(0)) d0 = 4;
        uint32_t a1 = AdvanceStringPtr(SYM_PerLabels2, d0);   /* the period name, centered */
        g.printx -= rd16(rom_ptr(a1)) >> 1;
        print(a1);
        printz(str_printscores1_7, sizeof str_printscores1_7);
        PrintTeamLogoAndScore(team_struct(0), 0);
        printz(str_printscores1_8, sizeof str_printscores1_8);
        PrintTeamLogoAndScore(team_struct(1), 0x2C);
        return;
    }
    int was = g.sflags5 & 0x80;
    g.sflags5 &= (uint8_t)~0x80;
    if (was) g.crowdnoisedelay = 0x78;
    printz(str_printscores1_1, sizeof str_printscores1_1);
    Framer(9, 5);
    g.disflags |= HI(dfclock);                                  /* dfclock */
    printz(str_printscores1_2, sizeof str_printscores1_2);
    uint16_t d0 = g.gsp;
    uint32_t list = SYM_PerLabels;
    if (g.gmode2 & HI(1)) { d0 = 4; list = SYM_PerLabels2; }
    Fprintsmall(list, d0);
    EASNLogo();
    if (g.sflags3 & (1 << sf3llcs)) return;               /* the lower line change box is up */
    printz(str_printscores1_3, sizeof str_printscores1_3);
    Framer(8, 5);
    printz(str_printscores1_4, sizeof str_printscores1_4);
    printscores1_ps(team_struct(0));
    printz(str_printscores1_5, sizeof str_printscores1_5);
    printscores1_ps(team_struct(1));
}

/* scoreboard94.asm:printscores1_ps. The abbreviation of team a2 at printx / printy, then its
 * score as 2 digits at x $1C. */
void printscores1_ps(struct Team *tm)
{
    print_p(roster_text(&rosters_team(tm->tmteam)->abbr));
    g.printx = 0x1C;
    print(PushNumberWidth(tm->tmscore, 2));
}

/* scoreboard94.asm:PrintTeamLogoAndScore. The team logo map (d0 = offset) 5 columns left of
 * printx, then the score of a2 in the big font, centered 2 rows lower. */
void PrintTeamLogoAndScore(struct Team *tm, uint16_t logo)
{
    uint16_t saved = g.printx;
    g.printx -= 5;
    PutTeamBlock(logo);
    g.printx = saved;
    g.printy += 2;
    uint16_t d0 = tm->tmscore;
    if ((int16_t)d0 >= 0xA) g.printx -= 1;
    uint32_t a1 = PushNumber(d0);
    d0 = (uint16_t)((uint16_t)(m16(a1) - 2) >> 1);
    g.printx -= d0;
    g.printx += 1;
    printbig(a1);
}

/* scoreboard94.asm:EASNLogo. The EASN logo map at x 1, y $19 on the vertical rink if no power
 * play. */
void EASNLogo(void)
{
    if (g.sflags2 & HI(sf2pwrplay)) return;
    printz(str_EASNLogo_1, sizeof str_EASNLogo_1);
    uint32_t a1 = ART(EASNmap) + rd32(rom_ptr(ART(EASNmap)) + 4);
    const uint8_t *m = rom_ptr(a1);
    dobitmap(0, a1, SYM_NoTiles, 0, 0, rd16(m), rd16(m + 2), g.EASNcset, 0);
}

/* the penalty box list of team a2: the players without a coincidental penalty first, then
 * those with one */
static void dispen(struct Team *tm)
{
    for (int pass = 0; pass < 2; pass++) {
        const uint8_t *a0 = tm->tmboxlist;
        for (;;) {
            uint8_t d0 = *a0++;
            if ((int8_t)d0 < 0) break;
            int coin = (tm->tmpdst[d0 / 2] & 0x4000) != 0;
            if (coin != pass) continue;
            pplpen(tm, d0);
        }
    }
}

/* scoreboard94.asm:USBoard. Update the scoreboard, including the players in the penalty box and
 * their time remaining. */
void USBoard(void)
{
    g.disflags |= HI(dfclock);
    printscores1();
    printz(str_USBoard_1, sizeof str_USBoard_1);
    dispen(team_struct(0));
    printz(str_USBoard_2, sizeof str_USBoard_2);
    dispen(team_struct(1));
}

/* scoreboard94.asm:pplpen. One penalty box line: the player's number and time remaining. a2 =
 * team, d0 = roster offset (player * 2). Only rows up to y $A are printed. */
void pplpen(struct Team *tm, uint16_t d0)
{
    if (g.printy > 0xA) return;
    uint16_t d2 = (uint16_t)(tm->tmpdst[d0 / 2] & 0x7FF);
    uint16_t num = rosters_team(tm->tmteam)->player[d0 >> 1].number;  /* his number (BCD) */
    uint16_t saved = g.printx;
    ConvertByteToDigits(num, text_buffer());
    wr16(g.mesarea, 4);                                   /* string length: 2 digits */
    print(ram_addr(g.mesarea));
    print(PushTime(d2));
    g.printx = saved;
    g.printy += 1;
}

/* scoreboard94.asm:linebar. The energy bar of line d0 for team a2 at printx / printy (16
 * steps). */
void linebar(uint16_t line, struct Team *tm)
{
    uint16_t e = getlinee(line, tm);
    uint16_t saved = g.printa;
    g.printa = 0x8000;
    uint16_t d0 = (uint16_t)m68k_divu((uint32_t)(int32_t)(int16_t)e, 0x100);
    if (d0 > 0xF) d0 = 0xF;
    uint16_t d1 = (uint16_t)(0xF - d0);                   /* bar frame, 15 = empty */
    uint32_t a1 = ART(EnergyBarMap) + rd32(rom_ptr(ART(EnergyBarMap)) + 4);
    dobitmap(0, a1, SYM_NoTiles, 0, d1, rd16(rom_ptr(a1)), 1, g.energybarchars, 0);
    g.printa = saved;
}

/* scoreboard94.asm:getlinee. d0 = line number, a2 = team struct. Returns the average energy of
 * that line's skaters. */
uint16_t getlinee(uint16_t line, struct Team *tm)
{
    const uint8_t *a1 = tm->tmlines[line];                    /* the line set */
    const uint8_t *prio = rom_ptr(SYM_priolist);
    uint32_t d0 = 0;
    uint16_t d1 = 0;
    for (int d4 = (int)tm->tmap - 1; d4 >= 0; d4--) {
        uint8_t d5 = prio[d4];
        if (d5 == 0) continue;                            /* the goalie's energy does not count */
        d1++;
        d0 += tm->tmpde[a1[d5] - 1];
    }
    return (uint16_t)m68k_divu(d0, d1);
}

/* scoreboard94.asm:AvgCline. The average energy of the current line on team a2. */
uint16_t AvgCline(struct Team *tm)
{
    uint32_t d0 = 0;
    uint16_t d1 = 0;
    const struct SortObj *a0 = sortobj_at(tm->tmsort);
    for (int i = 0; i < 6; i++, a0++) {
        if (a0->position <= 0) continue;
        d0 += tm->tmpde[a0->pnum];
        d1++;
    }
    if (d1 == 0) return (uint16_t)d0;
    return (uint16_t)m68k_divu(d0, d1);
}

/* scoreboard94.asm:ChkShotStat. Determine if a shot was taken and add it to the stats: crowd,
 * team (power play and period shots), shooter and goalie shots against. A second path counts
 * the shot for the player in the last ScoreSum entry. */
void ChkShotStat(void)
{
    if (g.gmode & (1 << gmclock)) return;
    if (g.gmode & (1 << gmhl)) return;
    int16_t d0;
    int was = g.sflags2 & HI(sf2shot);
    g.sflags2 &= (uint16_t)~HI(sf2shot);
    if (!was) {
        if (!(g.sflags4 & HI(0))) return;
        int was8 = g.sflags8 & 8;
        g.sflags8 &= (uint8_t)~8;
        if (!was8) { g.sflags8 |= 8; return; }            /* every second call */
        uint8_t *a4 = g.ScoreSum + -6 + g.ScoreSumbytes;                                  /* the last goal entry */
        struct Team *tm = team_struct((a4[2] & 0x80) != 0);
        uint8_t pn = a4[3];
        const struct SortObj *p = sortobj_at(tm->tmsort);
        int i;
        for (i = 0; i < 6; i++, p++)
            if (p->pnum == pn) break;
        if (i == 6) return;
        d0 = p->SCnum;
    } else {
        g.Crowdlevel += 0x64;
        g.CwdExciteLvl += 0xA;
        d0 = (int16_t)g.shotplayer;
    }
    struct SortObj *a3 = sortobj(d0);
    struct Team *a1;
    struct Team *a2 = loadTeamStruct(a3, &a1);
    a2->tmshots++;                                          /* tmshots */
    if (g.sflags2 & HI(sf2pwrplay)) {
        int pp_away = (g.sflags2 & HI(sf2pwrtm)) != 0;
        if ((PFLAG(a3, pfteam) != 0) == pp_away)
            a2->tmPPShots++;                                        /* power play shots */
    }
    a2->tmPerShots[g.gsp]++;                                                        /* shots by period */
    a2->tmpshots[a3->pnum]++;
    int16_t gk = (int16_t)a1->tmgoalie;
    if (gk < 0) return;                                   /* empty net */
    a1->tmpshots[gk]++;
}

/* scoreboard94.asm:loadTeamStruct. a2 = team struct of player a3, a1 = the other team's. */
struct Team *loadTeamStruct(const struct SortObj *o, struct Team **other)
{
    int away = (o->pflags & (1 << pfteam)) != 0;
    if (other) *other = team_struct(!away);
    return team_struct(away);
}

/* refill one team (a2) and pick the other's (a3) starting line from the players on the ice */
static void intermission_team(struct Team *a2, struct Team *a3)
{
    reenergizeteam(a2);
    a3->tmline = 0;
    if (g.OptLine != 0) return;
    int16_t d0 = (int16_t)(a3->tmap - a2->tmap);
    if (d0 == 0) return;
    a3->tmline = 3;                                         /* Pw1 */
    if (d0 >= 0) return;
    a3->tmline = 5;                                         /* PK1 */
}

/* scoreboard94.asm:SetupTeamForIntermission. Reset the bench, then for each team refill the
 * energy and pick the starting line. */
void SetupTeamForIntermission(void)
{
    ResetBench();
    intermission_team(team_struct(0), team_struct(1));
    intermission_team(team_struct(1), team_struct(0));
}

/* scoreboard94.asm:RestoreTeamEnergy. a2 = team: max energy for every player. */
void RestoreTeamEnergy(struct Team *tm)
{
    for (int16_t d0 = 0x32; d0 >= 0; d0 -= 2)
        tm->tmpde[d0 / 2] = 0x1000;
}

/* scoreboard94.asm:reenergizeteam. Max energy for team a2 except the players at tmpdst -4;
 * players marked -3 go to the bench (-2). */
void reenergizeteam(struct Team *tm)
{
    for (int16_t d0 = 0x32; d0 >= 0; d0 -= 2) {
        if (tm->tmpdst[d0 / 2] == 0xFFFC) continue;
        tm->tmpde[d0 / 2] = 0x1000;
        if (tm->tmpdst[d0 / 2] == 0xFFFD) tm->tmpdst[d0 / 2] = 0xFFFE;
    }
}

/* scoreboard94.asm:Intermission. Between periods (and after the game, which first updates the
 * records): the zamboni crosses under the period's menu (the playoff and shootout lists in
 * their modes), the ticker shows the other games' scores, then the highlights; start cuts it
 * short. Waits while a pad is on a team. */
void Intermission(void)
{
    static const uint32_t sslist[10] = {
        SYM_StartGameText, SYM_IntermissionText, SYM_IntermissionText, SYM_IntermissionText, SYM_ExitGameText,
        SYM_StartGameTextPO, SYM_IntermissionText, SYM_IntermissionText, SYM_IntermissionText, SYM_ExitGameTextPO,
    };
    if (g.gsp == 4) {
        g.gameclock = 0;
        UpdateRecords();
    }
    SetupTeamForIntermission();
    for (int i = 0; i < 0x10; i++) sortobj(i)->Xpos = 0;
    DecompressGraphicsPlain(ART(ZamFrameList) + 8, g.ExtraChars);
    g.Zamx = (uint16_t)(0x140 - view_zam_more());
    uint16_t list = g.gsp;
    if (g.OptPlayMode != 0) list += 5;
    uint32_t items = sslist[list];
    if (g.gmode2 & HI(0)) items = SYM_ShootoutIntermissionMenu;
    g.sflags |= HI(sfpz);
    Pausemode_top(items, SYM_Pausemode_pall);
    uint16_t d1;
    g.TickerNum = GetShifter(NULL);
    int skip = 0;
    while ((int16_t)g.TickerNum >= 0) {
        struct Matchup *gs = NULL;
        int16_t d3 = NewTicker2(&gs);
        NewTicker3(d3, gs);
        if (waitxsr(0x96) & 0x80) { skip = 1; break; }
        ClearTickerArea();
        if (waitxsr(0x3C) & 0x80) { skip = 1; break; }
    }
    if (skip) g.sflags3 |= 1 << sf3sbut;
    DoHiLights();
    int cut = (g.sflags3 & (1 << sf3sbut)) != 0;
    g.sflags3 &= (uint8_t)~(1 << sf3sbut);
    if (!cut) {
        do {
            d1 = waitxsr(0x1E0);
            if (d1 & 0x80) break;
        } while (g.cont1team != 0 || g.cont2team != 0);
    }
    ST_W(g.Zamx);
}

/* scoreboard94.asm:InitScores. Initialize the other games' scores / period in the playoffs. */
void InitScores(void)
{
    if ((int16_t)g.gamelevel > 1) return;
    int16_t d1 = (int16_t)GetShifter(NULL);
    for (; d1 >= 0; d1--) {
        struct Matchup *a0 = &g.matchups[d1];
        if ((uint16_t)d1 == g.gamenum) continue;
        if (a0->gsflags & MU_SERIES_OVER) continue;
        a0->gsper = 0;
        for (int16_t d0 = (int16_t)randomd0(ticker_init_periods()); d0 > 0; d0--) SetScore(a0);   /* (3; RULE_TICKER_HL: mods/ticker.c) */
    }
}

/* scoreboard94.asm:UpdateScores. Update the ticker score values (PeriodOver). */
void UpdateScores(void)
{
    int16_t d1 = (int16_t)GetShifter(NULL);
    g.TickerNum = (uint16_t)d1;
    for (; d1 >= 0; d1--) {
        struct Matchup *a0 = &g.matchups[d1];
        if ((uint16_t)d1 == g.gamenum) continue;
        a0->gsflags &= (uint8_t)~MU_HIGHLIGHT;
        if (a0->gsflags & MU_SERIES_OVER) continue;
        SetScore(a0);
    }
}

/* the weights at nibblebuffer two to a long, as getscore adds them (add.l) */
static uint32_t nib_long(int k) { return (uint32_t)g.nibblebuffer[2 * k] << 16 | g.nibblebuffer[2 * k + 1]; }
static void set_nib_long(int k, uint32_t v) { g.nibblebuffer[2 * k] = (uint16_t)(v >> 16); g.nibblebuffer[2 * k + 1] = (uint16_t)v; }

/* the weighted goals of one team's period: d0 = scoring team, d1 = the other team. Adds the
 * scoring team's (bits 4-6) and the other team's (bits 0-2) sctab rows as weights */
static uint16_t getscore(uint16_t d0, uint16_t d1)
{
    unsigned row = (rosters_team(d0)->offense & 7u) << 3;            /* row * 8: the scorers' offense */
    set_nib_long(0, ((uint32_t)sctab[row / 2] << 16) | sctab[row / 2 + 1]);
    set_nib_long(1, ((uint32_t)sctab[row / 2 + 2] << 16) | sctab[row / 2 + 3]);
    row = (rosters_team(d1)->defense & 7u) << 3;                    /* the other team's defense */
    set_nib_long(0, nib_long(0) + (((uint32_t)sctab[row / 2] << 16) | sctab[row / 2 + 1]));
    set_nib_long(1, nib_long(1) + (((uint32_t)sctab[row / 2 + 2] << 16) | sctab[row / 2 + 3]));
    return WeightedRandomSelect(4);
}

/* scoreboard94.asm:SetScore. Add to the score of the ticker game a0. After period 3 a game
 * within one goal goes to overtime and asks for a highlight. */
void SetScore(struct Matchup *a0)
{
    if ((int16_t)a0->gsper >= 4) return;
    if (a0->gsper == 3) {
        a0->gsper = 5;                                      /* final */
        int16_t d0 = (int16_t)(a0->gss1 - a0->gss2);
        if (d0 > 1) return;
        if (d0 < -1) return;
        a0->gsper = 3;                                      /* close game: overtime */
        a0->gsflags |= MU_HIGHLIGHT;
        return;
    }
    a0->gsper++;
    uint16_t s = getscore(a0->gst1, a0->gst2);
    a0->gss1 += s;
    s = getscore(a0->gst2, a0->gst1);
    a0->gss2 += s;
}

/* scoreboard94.asm:NewTicker. Show the next ticker score once the clock is at 8:00 or less.
 * Returns d3 = the game, negative for none. */
int16_t NewTicker(struct Matchup **gs)
{
    if ((int16_t)g.gameclock > 0x1E0) return -1;
    return NewTicker2(gs);
}

/* scoreboard94.asm:NewTicker2. The next ticker game, skipping this game. */
int16_t NewTicker2(struct Matchup **gs)
{
    int16_t d3 = (int16_t)g.TickerNum;
    if (d3 < 0) return d3;
    g.TickerNum--;
    if ((uint16_t)d3 == g.gamenum) return NewTicker2(gs);
    return CheckGameTickerStatus(d3, gs);
}

/* scoreboard94.asm:CheckGameTickerStatus. d3 = game. Returns a0 = its gstruct, skipping to the
 * next ticker game if a highlight is coming up or the series is over. */
int16_t CheckGameTickerStatus(int16_t d3, struct Matchup **gs)
{
    struct Matchup *a0 = &g.matchups[d3];
    if (a0->gsflags & MU_HIGHLIGHT) return NewTicker2(gs);  /* a highlight is coming */
    if (a0->gsflags & MU_SERIES_OVER) return NewTicker2(gs);   /* the series is over */
    *gs = a0;
    return d3;
}

/* one team's name and score on the next ticker row */
static void ticker_team(uint16_t team, uint16_t score, uint16_t x)
{
    g.printx = x;
    g.printy += 1;
    print_p(roster_text(&rosters_team(team)->city));
    g.printx = (uint16_t)(x + 0x15);
    print(PushNumberWidth(score, 2));
}

/* scoreboard94.asm:NewTicker3. Display the ticker score for game d3 (a0 = its gstruct); nothing
 * for a negative d3. */
void NewTicker3(int16_t d3, const struct Matchup *gs)
{
    if (d3 < 0) return;
    uint16_t d0, d1;
    SetTickerAreaPosition(&d0, &d1);
    Framer(d0, d1);
    g.printx += 1;
    g.printy -= 4;
    uint16_t x = g.printx;
    GetShifter(NULL);
    g.printa = 0xA000;
    printz(str_NewTicker3_1, sizeof str_NewTicker3_1);
    g.printx = x;
    d0 = (uint16_t)(1 + g.gamelevel);
    if (g.OptPlayMode == 0) d0 = 0;                       /* not the playoffs: 'EA Hockey Night' */
    Fprintsmall(SYM_GameLabels, d0);
    uint32_t a1 = AdvanceStringPtr(SYM_PerLabels, (uint16_t)(gs->gsper - 1));
    d0 = rd16(rom_ptr(a1)) >> 1;                          /* the period name centered on x + $17 */
    g.printx = (uint16_t)(x + 0x17 - d0);
    print(a1);
    g.printa = 0x8000;
    ticker_team(gs->gst2, gs->gss2, x);
    ticker_team(gs->gst1, gs->gss1, x);
}

/* scoreboard94.asm:ClearTickerArea. Erase the ticker box. */
void ClearTickerArea(void)
{
    uint16_t d0, d1;
    SetTickerAreaPosition(&d0, &d1);
    eraser(d0, d1, 0x7FF);
}

/* scoreboard94.asm:SetTickerAreaPosition. printx / printy / printm for the ticker box (x 3, y
 * $17; map 3 in pause mode). Returns d0 = $1A wide, d1 = 5 high. */
void SetTickerAreaPosition(uint16_t *d0, uint16_t *d1)
{
    printz(str_SetTickerAreaPosition_1, sizeof str_SetTickerAreaPosition_1);
    if (!(g.sflags & HI(sfpz))) printz(str_SetTickerAreaPosition_2, sizeof str_SetTickerAreaPosition_2);
    *d0 = 0x1A;
    *d1 = 5;
}

/* scoreboard94.asm:Fprintsmall. Print string d0 of the String list a1 with printsmall. */
void Fprintsmall(uint32_t list, uint16_t n)
{
    printsmall(AdvanceStringPtr(list, n));
}

/* scoreboard94.asm:AdvanceStringPtr. a1 = string d0 of the String list a1. */
uint32_t AdvanceStringPtr(uint32_t list, uint16_t n)
{
    for (; n != 0; n--) list += rd16(rom_ptr(list));
    return list;
}

/* scoreboard94.asm:DoHiLights. The highlights: each game of the round (GetShifter, last first)
 * whose highlight flag is set (gsfhl, cleared here), unless the series is over (gsfso). */
void DoHiLights(void)
{
    int16_t n = (int16_t)GetShifter(NULL);
    do {
        struct Matchup *gs = &g.matchups[n];
        if (!(gs->gsflags & MU_SERIES_OVER)) {
            int hl = gs->gsflags & MU_HIGHLIGHT;
            gs->gsflags &= (uint8_t)~MU_HIGHLIGHT;
            if (hl) StartHL(gs, n);
        }
    } while (n-- != 0);
}

/* scoreboard94.asm:StartHL. Play the highlight of game a0 (d1 = its number): StartHL2. */
void StartHL(struct Matchup *gs, int16_t game) { StartHL2(gs, game); }

/* ---- the intermission and the highlights of the other games ---- */

#define GS_SAVE_WORDS 0x5BE                               /* StartHL2's .sv: gmode on, to M68K_RAM */

/* StartHL2's .sv / .lo: keep the game's RAM from gmode on (and ScoreSumbytes) at M68K_RAM while
 * a highlight game uses it */
static void hl_save(void)
{
    rp ram = ram_at(RAM_BASE);
    ram_copy(ram, RAM(gmode), 2 * GS_SAVE_WORDS);
    w16(ram, 2 * GS_SAVE_WORDS, g.ScoreSumbytes);
}
static void hl_restore(void)
{
    rp ram = ram_at(RAM_BASE);
    ram_copy(RAM(gmode), ram, 2 * GS_SAVE_WORDS);
    g.ScoreSumbytes = r16(ram, 2 * GS_SAVE_WORDS);
}

/* StartHL2's .ranres: settle highlight game a0 at random when tied, and mark it final */
static void hl_random_result(struct Matchup *gs)
{
    if (gs->gss1 == gs->gss2) {
        uint16_t d0 = vdp_hv16() & 1;
        gs->gss1 += d0;
        gs->gss2 += d0 ^ 1;
    }
    gs->gsper = 5;                                        /* final */
}

/* StartHL2's .setteam: refill the energy, personnel and player data of team a2 */
static void hl_set_team(struct Team *tm)
{
    reenergizeteam(tm);
    setpersonel(tm);
    forcepldata(tm);
    ST_W(tm->tmtouch1);
    g.sflags8 &= (uint8_t)~0x80;
    ST_W(tm->tmtouch2);
    ST_W(tm->tmtouch3);
}

/* scoreboard94.asm:StartHL2. Play the highlight of game a0 (d1 = its number): its ticker score,
 * "Highlight from game:", then the two teams on the rink with a random 60-180 seconds left, the
 * computer playing until 180 frames after the clock stops or C / start (a tie is settled at
 * random); the game's RAM is kept aside meanwhile. Still tied: another highlight. Start skips
 * it with a random result (sf3sbut). */
void StartHL2(struct Matchup *gs, int16_t game)
{
    for (;;) {
        struct Matchup *a0 = gs;
        if (g.sflags3 & (1 << sf3sbut)) { hl_random_result(a0); return; }
        hl_save();
        struct Matchup *t = a0;
        int16_t d3 = CheckGameTickerStatus(game, &t);
        a0 = t;
        NewTicker3(d3, a0);
        if (g.sflags3 & (1 << sf3sbut)) { hl_restore(); hl_random_result(a0); g.sflags3 |= 1 << sf3sbut; return; }
        g.printx = 4;
        g.printy -= 6;
        Framer(0x18, 3);
        g.printx += 1;
        g.printy -= 2;
        printz(str_StartHL2_1, sizeof str_StartHL2_1);
        if (pa_hook) pa_hook(PA_HIGHLIGHT, NULL, 0, a0->gst1, a0->gst2);   /* (the rink announcer: "we take you now to the highlight...") */
        if (waitxsr(0xB4) & 0x80) { hl_restore(); hl_random_result(a0); g.sflags3 |= 1 << sf3sbut; return; }
        ST_W(g.Zamx);
        g.CwdExciteLvl = 0;
        g.HomeTeam = a0->gst1;
        g.VisTeam = a0->gst2;
        struct Team *home = team_struct(0), *away = team_struct(1);
        home->tmgoalie = 0;
        away->tmgoalie = 0;
        clearTeamStats();
        restoreteams();
        ST_W(g.c1playernum);
        ST_W(g.c2playernum);
        g.cont1team = 0;
        g.cont2team = 0;
        g.gameclock = (uint16_t)(randomd0(0x78) + 0x3C);
        g.gsp = (uint16_t)(a0->gsper - 1);
        home->tmscore = a0->gss1;
        away->tmscore = a0->gss2;
        g.gmode = 1 << gmhl;
        if (g.gsp & 1) g.gmode |= 1 << gmdir;
        g.sflags &= 0x00FF;                               /* clr.b: the high byte */
        g.sflags2 = (uint16_t)(HI(sf2drec) | (g.sflags2 & 0xFF));
        g.sflags3 = 0;
        g.disflags &= (uint16_t)~HI(4);
        g.disflags |= HI(dfclock);
        g.glovecords = 0;
        g.iflags = 0;
        ST_W(g.RefCnt);
        g.puckcross[0].frames |= 0xFF00;                  /* (st puckcross2: the high byte) */
        g.puckcross[1].frames |= 0xFF00;
        p_turnoff();
        setupice_highlight();
        ClrHor();
        vdp_ctrl16(SYM_VDPR_WINX);
        vdp_ctrl16(SYM_VDPR_WINY);
        DecompressGraphicsPlain(ART(RefsMap) + 8, g.ExtraChars);   /* the ref cam */
        g.Vpos = 0;
        g.Hpos = 0;
        ResetBench();
        hl_set_team(home);
        hl_set_team(away);
        resetplstuff();
        for (int i = 0; i < 12; i++) {
            struct SortObj *o = sortobj(i);
            int16_t p = o->position;
            if (!PFLAG(o, pfgoal)) p += 6;
            o->Xpos = (int16_t)hl_postab[2 * p];
            o->Ypos = (int16_t)hl_postab[2 * p + 1];
            o->Xvel = 0;
            o->Yvel = 0;
        }
        SprSort();
        g.palcount = 0x18;
        g.OldVcount = g.Vcount;
        int16_t after = 0xB4;                             /* frames after the clock stops */
        for (;;) {
            DoGameFrame();
            if ((g.gmode & (1 << gmclock)) && --after < 0) break;
            uint16_t d1 = orjoy();
            if ((d1 & 0x20) || (d1 & 0x80)) {
                if (home->tmscore == away->tmscore) {                       /* tied: a random winner */
                    uint16_t d0 = vdp_hv16() & 1;
                    home->tmscore += d0;
                    away->tmscore += d0 ^ 1;
                }
                break;
            }
        }
        KillCrowd();                                      /* the scores go to a0 as pushed: CheckGameTickerStatus's */
        p_turnoff();
        a0->gss1 = home->tmscore;
        a0->gss2 = away->tmscore;
        forceblack();
        for (int i = 0; i < 0x10; i++) sortobj(i)->Xpos = 0;
        hl_restore();
        setplayercolors();
        SetHor();
        setvideo();
        Pausemode_pall();
        DrawMenuScreen();
        g.palcount = 0x18;
        song(SYM_SngEOG);
        gs->gsper = 4;                                    /* overtime */
        if (gs->gss1 == gs->gss2) continue;           /* still tied: another highlight */
        gs->gsper = 5;                                      /* final */
        t = gs;
        d3 = CheckGameTickerStatus(game, &t);
        NewTicker3(d3, t);
        if (waitxsr(0xB4) & 0x80) { g.sflags3 |= 1 << sf3sbut; return; }
        ClearTickerArea();
        if (waitxsr(0x3C) & 0x80) g.sflags3 |= 1 << sf3sbut;
        return;
    }
}
