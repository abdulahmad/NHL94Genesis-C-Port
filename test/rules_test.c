/* The game rules (src/rules.h) on the game's own routines, with RAM set up by hand and a hardware
 * layer that does nothing (hal_null.c): Classic plays as the original, and each rule changes only
 * what it says. make rulestest (the ROM: NHL94_ROM, or the repository's). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "rom.h"
#include "rules.h"
#include "symbols_gen.h"
#include "anim_gen.h"
#include "native.h"
#include "../mods/anims.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void classic_rules(void) { memcpy(rules, rule_classic, sizeof rules); }

/* the weights in a check: the original's byte wraps when a light pad player (base $F0) checks a
 * heavy skater; in full, the heavier the checked player the better his odds, always */
static void weights(void)
{
    struct SortObj a2, a3;
    memset(&a2, 0, sizeof a2);
    memset(&a3, 0, sizeof a3);
    a3.weight = 0;
    a2.weight = 120;
    CHECK(CheckWeightOdds(0xF0, &a2, &a3) == (uint8_t)(0xF0 + 120));   /* 104: wrapped */
    rules[RULE_WEIGHT_FIX] = 1;
    CHECK(CheckWeightOdds(0xF0, &a2, &a3) == 0xF0 + 120);
    int up = 1, wraps = 0;
    for (int w = 1; w < 16; w++) {
        struct SortObj lo = a2, hi = a2;
        lo.weight = (uint8_t)(8 * (w - 1));
        hi.weight = (uint8_t)(8 * w);
        rules[RULE_WEIGHT_FIX] = 1;
        up &= CheckWeightOdds(0xF0, &hi, &a3) > CheckWeightOdds(0xF0, &lo, &a3);
        rules[RULE_WEIGHT_FIX] = 0;
        wraps |= CheckWeightOdds(0xF0, &hi, &a3) < CheckWeightOdds(0xF0, &lo, &a3);
    }
    CHECK(up && wraps);
    a3.weight = 60; a2.weight = 60;                       /* (a computer's check, base $78: no wrap, the same) */
    rules[RULE_WEIGHT_FIX] = 0;
    uint16_t c = CheckWeightOdds(0x78, &a2, &a3);
    rules[RULE_WEIGHT_FIX] = 1;
    CHECK(CheckWeightOdds(0x78, &a2, &a3) == c && c == 0x78);
    classic_rules();
}

/* the Rosters page's overall ratings (native/ratings.c, reckoned without the game's RAM) as the
 * cards show them (CalcAttrib with the overall weights, no streak, x 100 / its divisor,
 * AttribAdjust): every player of the cartridge */
static void overall_ratings(void)
{
    int players = 0, same = 0;
    for (int t = 0; t < (int)SYM_NumTeams; t++) {
        memset(&g, 0, sizeof g);
        struct Team *tm = team_struct(0);
        memset(tm->tmHotCold, 0, sizeof tm->tmHotCold);               /* (no streaks) */
        tm->tmteam = (uint16_t)t;                           /* (as InitTeamStructure makes it) */
        tm->tmdata = rosters_team((uint16_t)t)->data;
        uint16_t count = GetPlayerCount(tm), goalies = GetGoalieCount(tm);
        for (uint16_t p = 0; p < count; p++) {
            const struct RosterPlayer *P = &rosters_team((uint16_t)t)->player[p];   /* his number, then his 14 ratings */
            const uint8_t *nib = P->rating;
            int goalie = p < goalies, number = (P->number >> 4) * 10 + (P->number & 15);
            uint16_t div, v = CalcAttrib(p, tm, rd32(rom_ptr(goalie ? SYM_GAttribOverallMask : SYM_PAttribOverallMask)), &div);
            int want = AttribAdjust((uint16_t)m68k_divu((uint32_t)v * 100u, div));
            players++;
            same += rating_overall(rom_cart, nib, number, goalie, NULL) == want;
        }
    }
    printf("  overall ratings: %d of %d players as their cards'\n", same, players);
    CHECK(players > 600 && same == players);
}

/* the Rosters page's other ratings (native/ratings.c's rating_column) as the roster screen shows
 * them (GetNameandAttrib: CalcAttrib with the column's mask, no streak; AttribRating's x 100 / its
 * divisor, AttribAdjust; AttribWeight's pounds): every player of the cartridge, every column of
 * PAttribColumns (skaters) or GAttribColumns (goalies) those show, the overall ratings aside */
static void column_ratings(void)
{
    int checked = 0, same = 0, columns = 0;
    for (int t = 0; t < (int)SYM_NumTeams; t++) {
        memset(&g, 0, sizeof g);
        struct Team *tm = team_struct(0);                 /* (no streaks) */
        tm->tmteam = (uint16_t)t;
        tm->tmdata = rosters_team((uint16_t)t)->data;
        uint16_t count = GetPlayerCount(tm), goalies = GetGoalieCount(tm);
        for (uint16_t p = 0; p < count; p++) {
            const struct RosterPlayer *P = &rosters_team((uint16_t)t)->player[p];
            int goalie = p < goalies, number = (P->number >> 4) * 10 + (P->number & 15), cols = 0;
            uint32_t overall = rd32(rom_ptr(goalie ? SYM_GAttribOverallMask : SYM_PAttribOverallMask));
            for (uint32_t a = goalie ? SYM_GAttribColumns : SYM_PAttribColumns; (int16_t)rd16(rom_ptr(a)) >= 0; a += rd16(rom_ptr(a)) + 4u) {
                uint32_t sel = rd32(rom_ptr(a + rd16(rom_ptr(a))));
                uint32_t handler = SYM_attribjmp + (uint32_t)(int16_t)rd16(rom_ptr(SYM_attribjmp + (int16_t)(uint16_t)sel));
                if ((handler != SYM_AttribRating && handler != SYM_AttribWeight) || sel == overall || (uint16_t)sel <= 2) continue;
                uint16_t div, v = CalcAttrib(p, tm, sel, &div);
                int weight = handler == SYM_AttribWeight;
                int want = weight ? 140 + 8 * v : AttribAdjust((uint16_t)m68k_divu((uint32_t)v * 100u, div));
                int got = weight ? 140 + 8 * P->rating[LR_WEIGHT] : rating_column(rom_cart, P->rating, number, sel >> 16);
                checked++;
                cols++;
                same += got == want;
            }
            if (cols > columns) columns = cols;
        }
    }
    printf("  roster screen ratings: %d of %d as the screen shows them (up to %d columns a player)\n", same, checked, columns);
    CHECK(checked > 6000 && same == checked);
}

/* the Sega CD's changes (RULE_CD_*): its uniforms' colours, and ClearShotData's length */
static void sega_cd(void)
{
    uint16_t hfd[16], otw[16];
    memcpy(hfd, rosters_team(9)->colours[0], sizeof hfd);
    memcpy(otw, rosters_team(15)->colours[1], sizeof otw);
    CHECK(hfd[9] == 0x042 && otw[4] == 0x002 && otw[12] == 0x444);   /* (the cartridge's) */
    cd_colours(9, 0, hfd);
    cd_colours(15, 1, otw);
    CHECK(hfd[9] == 0x264 && otw[4] == 0x200 && otw[5] == 0x200 && otw[8] == 0x006 && otw[10] == 0 && otw[11] == 0 && otw[12] == 0);
    uint16_t same[16];
    memcpy(same, rosters_team(9)->colours[1], sizeof same);   /* (HFD away: the same on the disc) */
    cd_colours(9, 1, same);
    CHECK(!memcmp(same, rosters_team(9)->colours[1], sizeof same));
    for (int cd = 0; cd < 2; cd++) {
        classic_rules();
        rules[RULE_CD_CLEARSHOT] = (uint8_t)cd;
        ram_fill(RAM(outputbuffer), 0xAA, 0xE4 * 2);
        ClearShotData();
        CHECK(r16(RAM(outputbuffer), 0x71 * 2) == 0);     /* (the last word the disc clears) */
        CHECK(r16(RAM(outputbuffer), 0x72 * 2) == (cd ? 0xAAAA : 0));
    }
    classic_rules();
}

/* RULE_REFEREE (mods/referee.c): with the rule off nothing; on, he comes on beside the play, a skater who runs into him
 * bounces off (the mover keeps his place: collflag) and pushes him, the puck bounces off him, a pad player's hard check
 * knocks him down and the third such check is a roughing penalty; his state goes through the state's bytes, and bytes
 * that are not a referee's are refused (a state is hostile until checked) */
static void referee(void)
{
    struct referee_view v;
    classic_rules();
    referee_state_load(NULL);
    ram_fill(RAM(PenBuf), 0, sizeof g.PenBuf);
    g.demoflag = 1; g.sflags = 0; g.gmode = 0; g.gmode2 = 0; g.OptPen = 1; g.PalMode = 0; g.Vpos = 0x40;
    struct SortObj *pk = sortobj(puckscnum);
    memset(pk, 0, sizeof *pk);
    pk->SCnum = puckscnum;
    pk->asslist[0] = pnorm;                                /* (not a face off) */
    pk->newpos = 0xFF;                                     /* (no carrier) */
    pk->Xpos = 0x20; pk->Ypos = 0x40;
    for (int i = 0; i < 12; i++) { struct SortObj *o = sortobj(i); memset(o, 0, sizeof *o); o->SCnum = (int16_t)i; o->position = -1; }
    elapsed = 1;
    referee_update();
    CHECK(!referee_get(&v));                               /* (the rule off: no referee) */
    rules[RULE_REFEREE] = 1;
    referee_update();
    CHECK(referee_get(&v) && v.x >> 16 == 0x7C && v.y >> 16 == 0x40 && !v.at_dot && v.fall < 0);
    for (int k = 0; k < 30; k++) referee_update();          /* (past the drop's wait: he skates) */
    referee_get(&v);
    int16_t rx = (int16_t)(v.x >> 16), ry = (int16_t)(v.y >> 16);
    /* a skater (not checking) runs into him */
    struct SortObj *o = sortobj(3);
    o->position = 3; o->Xpos = (int16_t)(rx - 12); o->Ypos = ry; o->Xvel = 0x2000; o->weight = 10;
    g.collflag = 0;
    referee_cx(o->Xpos, o->Ypos, o);
    referee_get(&v);
    CHECK((int16_t)g.collflag < 0 && v.vx > 0 && o->Xvel < 0x2000);
    CHECK(v.fall < 0);                                     /* (a bump, not a check: he stays up) */
    /* the puck at his feet */
    for (int k = 0; k < 10; k++) referee_update();          /* (his nopuck wait over) */
    referee_get(&v);
    rx = (int16_t)(v.x >> 16); ry = (int16_t)(v.y >> 16);
    pk->Xpos = (int16_t)(rx + 4); pk->Ypos = (int16_t)(ry + 4); pk->Zpos = 0; pk->Xvel = -0x900; pk->Yvel = 0;
    referee_puck(pk);
    CHECK((int8_t)(pk->Xvel >> 8) == 4 && (int8_t)(pk->Yvel >> 8) == 4);   /* (off him, along the gap) */
    /* a pad player's hard checks */
    int pen = 0;
    for (int hit = 0; hit < 3; hit++) {
        referee_get(&v);
        rx = (int16_t)(v.x >> 16); ry = (int16_t)(v.y >> 16);
        o->Xpos = (int16_t)(rx - 16); o->Ypos = ry; o->Xvel = 0x7C00; o->Yvel = 0;
        o->SPA = SPAburst; o->pflags = 1 << pfjoycon; o->pflags2 = 0; o->pflags3 = 0; o->impact = 0;
        g.collflag = 0;
        referee_cx(o->Xpos, o->Ypos, o);
        referee_get(&v);
        CHECK(v.fall >= 0);
        for (int i = 0; i < 32; i += 2) if (g.PenBuf[i] == 0x1A && g.PenBuf[i + 1] == 3) pen = hit + 1;
    }
    CHECK(pen == 3);                                       /* (the third, not before) */
    /* the state */
    uint8_t a[REFEREE_STATE_SIZE], b[REFEREE_STATE_SIZE];
    CHECK(referee_state_save(a) == REFEREE_STATE_SIZE);
    CHECK(referee_state_load(a) == 0);
    referee_state_save(b);
    CHECK(!memcmp(a, b, sizeof a));
    for (int k = 14; k < REFEREE_STATE_SIZE; k++) {        /* (each flag byte out of range is refused) */
        memcpy(b, a, sizeof b);
        b[k] = 0xFF;
        CHECK(k == 14 || referee_state_load(b) < 0);       /* (byte 14, nopuck, takes any value) */
    }
    CHECK(referee_state_load(NULL) == 0 && !referee_get(&v));
    classic_rules();
}

/* RULE_HOLD (mods/hold.c): the walk to the spots and the referee's puck are the game's alone (where the puck lay, the dot, the
 * walk's time); the pause goes through the state's bytes and bytes that are not a pause's are refused. RULE_ANTHEM: the
 * anthem's wait is the rule's, fixed per recording */
static void hold(void)
{
    classic_rules();
    hold_state_load(NULL);
    CHECK(hold_anthem_frames(0, 3) == 0 && hold_anthem_frames(1, 0) == 735 && hold_anthem_frames(1, 3) == 666 &&
          hold_anthem_frames(2, 0) == 783 && hold_anthem_frames(2, 7) == 603);   /* (the songs' last notes: mods/hold.c anthem_len) */
    rules[RULE_HOLD] = 1;
    g.gameclock = 100; g.PerTimeTotal = 300; g.Vcount = 1000;
    for (int i = 0; i < 12; i++) { struct SortObj *o = sortobj(i); memset(o, 0, sizeof *o); o->SCnum = (int16_t)i; o->position = i % 6 ? 1 : 0; o->pnum = (uint8_t)i; }
    g.puckx[0] = (uint16_t)-40; g.pucky = 60;
    hold_skate_begin();
    for (int i = 0; i < 12; i++) { sortobj(i)->Xpos = 100; sortobj(i)->Ypos = 100; }   /* (the face off's set up: 141 to go) */
    g.fox = 100; g.foy = 100;
    hold_skate_arm();
    int x, y, x2, y2;
    CHECK(hold_skating() && !hold_skate_carry(&x, &y));    /* (the referee is on his way to the puck) */
    for (int k = 0; k < 400 && hold_skate_ref() < 160; k++) g.Vcount++;   /* (past the pick up: the walk's length is the skaters' own) */
    CHECK(hold_skate_carry(&x, &y) && x > -40 * 256 && x < 115 * 256 && y > 60 * 256 && y < 100 * 256);
    uint8_t a[HOLD_STATE_SIZE], b[HOLD_STATE_SIZE];
    CHECK(hold_state_save(a) == HOLD_STATE_SIZE);
    hold_state_load(NULL);
    CHECK(!hold_skating() && !hold_skate_carry(&x2, &y2));
    CHECK(hold_state_load(a) == 0 && hold_skating() && hold_skate_carry(&x2, &y2) && x2 == x && y2 == y);   /* (the same walk) */
    hold_state_save(b);
    CHECK(!memcmp(a, b, sizeof a));
    static const int flags[] = { 2, 3, 4, 7, 82, 83, 84, 89, 96, 97, 324, 325 };
    for (size_t k = 0; k < sizeof flags / sizeof flags[0]; k++) {
        memcpy(b, a, sizeof b);
        b[flags[k]] = flags[k] == 84 ? 3 : 2;                  /* (byte 84, the goal speech's wait: 2 is the Sega CD goal song's, mods/hold.c) */
        if (hold_state_load(b) >= 0) printf("  flag byte %d taken\n", flags[k]);
        CHECK(hold_state_load(b) < 0);
    }
    memcpy(b, a, sizeof b); b[84] = 2; CHECK(hold_state_load(b) == 0);   /* (the speech waiting for the disc's goal song) */
    memcpy(b, a, sizeof b); b[334] = 4; CHECK(hold_state_load(b) < 0);                /* (the face off's flags: held back by the anthem 1, the window after the skate 2) */
    memcpy(b, a, sizeof b); b[334] = 2; CHECK(hold_state_load(b) == 0);
    memcpy(b, a, sizeof b); b[98] = 0x91; b[99] = 1; CHECK(hold_state_load(b) < 0);   /* (a walk of more than 400 frames, EZ_CAP) */
    memcpy(b, a, sizeof b); b[322] = 1; CHECK(hold_state_load(b) < 0);                /* (the puck picked up at 1 of 256) */
    memcpy(b, a, sizeof b); b[335] = 13; CHECK(hold_state_load(b) < 0);               /* (13 held back face off men) */
    memcpy(b, a, sizeof b); b[335] = 1; b[336] = 12; b[348] = afaceoff; CHECK(hold_state_load(b) < 0);   /* (not a skater's object) */
    memcpy(b, a, sizeof b); b[335] = 1; b[336] = 3; b[348] = 0x30; CHECK(hold_state_load(b) < 0);       /* (not a face off's assignment) */
    memcpy(b, a, sizeof b); b[335] = 1; b[336] = 3; b[348] = afaceoff; CHECK(hold_state_load(b) == 0);
    CHECK(hold_state_load(a) == 0 && hold_skate_carry(&x2, &y2) && x2 == x);        /* (a refused state changed nothing it had) */
    hold_state_load(NULL);
    classic_rules();
}

/* RULE_SIREN: the announcer's wait after the goal (RULE_ANNOUNCE 2) is the PC siren's fixed 420 frames from the siren, the game's
 * alone (no host, no mixer); its start goes through the state's bytes, and bytes that are not a siren's are refused */
static void siren(void)
{
    classic_rules();
    hold_state_load(NULL);
    for (int i = 0; i < 8; i++) g.fm_slots[i].stream = 0xFFFFFFFF;   /* (no song, no cartridge siren in a slot) */
    g.songdelay = 0;
    g.demoflag = 1;
    rules[RULE_ANNOUNCE] = 2;
    rules[RULE_SIREN] = 1;
    g.Vcount = 2000;
    hold_siren();
    g.Vcount = 2100;
    hold_goal_after_song(222, team_struct(0), 3, -1, -1);
    uint8_t a[HOLD_SIREN_SIZE], b[HOLD_SIREN_SIZE];
    CHECK(hold_siren_save(a) == HOLD_SIREN_SIZE && a[0] == 1);
    int t = 2100;
    for (; t < 3000 && hold_wait(); t++) { g.Vcount = (uint16_t)t; hold_tick(); if (t == 2300) { hold_siren_load(NULL); CHECK(hold_siren_load(a) == 0); } }
    CHECK(t == 2000 + 420 + 222 + 1);                      /* (the speech from the siren's end, then its 222 frames) */
    hold_siren_save(b);
    CHECK(!memcmp(a, b, sizeof a));
    memcpy(b, a, sizeof b); b[0] = 2; CHECK(hold_siren_load(b) < 0);
    rules[RULE_SIREN] = 0;                                  /* (the cartridge's siren: none in a slot, so the speech at once) */
    g.Vcount = 4000;
    hold_siren();
    hold_goal_after_song(222, team_struct(0), 3, -1, -1);
    for (t = 4000; t < 5000 && hold_wait(); t++) { g.Vcount = (uint16_t)t; hold_tick(); }
    CHECK(t == 4000 + 222 + 1);
    hold_state_load(NULL);
    hold_siren_load(NULL);
    classic_rules();
}

/* RULE_PENALTIES (mods/penalties.c): check drawn Roughing and Cross Check are 4 minutes; under NHL rules a power play goal
 * ends only a double minor's first 2 minutes and never a major, and otherwise lets the player out as the cartridge does */
static void penalties(void)
{
    classic_rules();
    CHECK(pen_minutes(0x1A, 2) == 2 && pen_minutes(0x1C, 2) == 2 && pen_minutes(0x26, 5) == 5);
    rules[RULE_PENALTIES] = 1;
    CHECK(pen_minutes(0x1A, 2) == 4 && pen_minutes(0x1C, 2) == 4);
    CHECK(pen_minutes(0x16, 2) == 2 && pen_minutes(0x20, 2) == 2 && pen_minutes(0x26, 5) == 5 && pen_minutes(0x3A, 0xFF) == 0xFF);
    for (int mode = 0; mode <= 2; mode++) {
        rules[RULE_PENALTIES] = (uint8_t)mode;
        for (int kind = 0; kind < 3; kind++) {           /* 0 a minor, 1 a double minor, 2 a one sided major */
            memset(&g, 0, sizeof g);
            struct Team *a1 = team_struct(1), *a2 = team_struct(0);   /* (the away team short handed, the home team scores) */
            a1->tmap = 4; a2->tmap = 6;
            static const uint8_t pens[3] = { 0x24, 0x1A, 0x26 };     /* Holding, Roughing, Fighting */
            g.PenSum[0] = 0; g.PenSum[1] = 0; g.PenSum[2] = (uint8_t)(pens[kind] | 0x80); g.PenSum[3] = 3;   /* (player 3) */
            g.PenSum[4] = 0; g.PenSum[5] = 0; g.PenSum[6] = 0xA0; g.PenSum[7] = 5;                          /* (player 5: Charging) */
            g.PenSumLength = 8;
            a1->tmpdst[3] = (uint16_t)(kind == 0 ? 100 : kind == 1 ? 200 : 250);
            a1->tmpdst[5] = 110;
            a1->tmboxlist[0] = 6; a1->tmboxlist[1] = 10; a1->tmboxlist[2] = 0xFF;
            PenGoalStuff(a1, a2);
            int nhl = mode == 2;
            if (kind == 0 || !nhl) CHECK(a1->tmpdst[3] == 0 && a1->tmboxlist[0] == 10 && a1->tmap == 5);   /* (the cartridge's: he is out) */
            else if (kind == 1) CHECK(a1->tmpdst[3] == 120 && a1->tmboxlist[0] == 6 && a1->tmboxlist[1] == 10 && a1->tmap == 4);
            else CHECK(a1->tmpdst[3] == 250 && a1->tmpdst[5] == 0 && a1->tmboxlist[0] == 6 && a1->tmboxlist[1] == 0xFF && a1->tmap == 5);
            CHECK(a2->tmPwrGoals == 1);
        }
    }
    classic_rules();
}

/* NHL 95's moves and the PC goalie (mods/moves.c, mods/goalie.c, mods/anims.c) */
static void moves(void)
{
    classic_rules();
    moves_state_load(NULL);
    /* the animations: every list ends, every frame is the cartridge's or a mod frame with a cartridge frame and a PC picture */
    int bad = 0, modf = 0;
    for (int n = 0; n < MA_COUNT; n++) {
        const uint8_t *t = spa_table(MA_SPA(n));
        for (int d = 0; d < 8; d++) {
            const uint8_t *l = t + (int16_t)rd16(t + d * 2);
            int k = 0;
            for (; k < 12; k++) {
                uint16_t fr = rd16(l + 4 * k);
                if (fr >= MODF_BASE) {
                    modf++;
                    if (mod_frame(fr) >= MODF_BASE || mod_frame(fr) < 1 || mod_frame_pc(fr) < 0 || mod_frame_pc(fr) >= 1150) bad++;
                    if (n >= MA_GFALLB && (mod_frame(fr) < SPFGoalie || mod_frame(fr) >= SPFSiren)) bad++;   /* (puckgoalie's save odds) */
                } else if (fr < 1 || fr >= 0x2D0) bad++;
                if ((int16_t)rd16(l + 4 * k + 2) < 0) break;
            }
            if (k == 12) bad++;
        }
    }
    CHECK(!bad && modf > 0);
    CHECK(mod_frame_pc(MODF_BASE + 140) >= 0 && mod_frame_pc(MODF_BASE + 141) < 0 && mod_frame(SPFGoalie) == SPFGoalie);
    CHECK(spa_table(SPAshotf) == rom_ptr(SYM_SPAlist + SPAshotf));
    struct SortObj *o = sortobj(1), *c = sortobj(8);
    for (int i = 0; i < 12; i++) { struct SortObj *q = sortobj(i); memset(q, 0, sizeof *q); q->SCnum = (int16_t)i; q->position = 3; q->Xpos = 0x60; q->Ypos = 0x200; }
    /* 95 PC's fake: B or A early in the wind up */
    o->SPA = SPAshotf; o->SPAnum = 8; g.sflags = HI(sfssdir);
    CHECK(!moves_fake_pc(0x40, o));                        /* (the rule off) */
    rules[RULE_FAKESHOT] = 1;
    CHECK(!moves_fake_pc(0x20, o) && moves_fake_pc(0x40, o) && o->SPA == MA_SPA(MA_FAKEF_PC) && o->SPAnum == 8 && !(g.sflags & HI(sfssdir)));
    o->SPA = SPAshotb; o->SPAnum = 0x14; g.sflags = HI(sfssdir);
    CHECK(!moves_fake_pc(0x10, o));                        /* (too late in the wind up) */
    /* 95G's fake: A let go in the attacking zone, the wind up of the fake, no shot at its end */
    rules[RULE_FAKESHOT] = 2;
    o->pflags = 1 << pfgoal; o->SPA = 0; g.sflags = 0; g.BA_PS_flags = 0; g.puckx[0] = 0; g.pucky = 0x40;
    CHECK(!moves_fake_g(o));                               /* (outside the zone: a pass) */
    g.pucky = 0x60;
    CHECK(moves_fake_g(o) && (g.sflags & HI(sfssdir)) && (o->SPA == MA_SPA(MA_FAKEF_G) || o->SPA == MA_SPA(MA_FAKEB_G)));
    CHECK(moves_fake_end(o) && !(g.sflags & HI(sfssdir)) && o->SPA == SPAglide && !moves_fake_pending());
    /* 95G's drop pass: A with the pad back, a quarter speed on the ice; over once someone else has the puck */
    rules[RULE_DROPPASS] = 2;
    struct SortObj *pk = sortobj(puckscnum);
    pk->newpos = 0; pk->newpnum = 1;                       /* (player 1 has the puck) */
    moves_frame();
    CHECK(!moves_drop_g(1, o) && moves_drop_g(2, o) && moves_dropping() && (g.sflags & HI(sfspdir)));
    CHECK(moves_pass_spa(SPApassf) == MA_SPA(MA_DROP_G) && moves_pass_spa(SPAgswing) == SPAgswing);
    int16_t t = 5;
    CHECK(moves_drop_time(&t) && t == 20);
    g.puckvx = 0x1000; g.puckvy = (uint16_t)-0x800; g.puckvz[0] = 0x300;
    moves_drop_loose();
    CHECK(g.puckvx == 0x400 && (int16_t)g.puckvy == -0x200 && g.puckvz[0] == 0);
    uint8_t a[MOVES_STATE_SIZE], b[MOVES_STATE_SIZE];
    CHECK(moves_state_save(a) == MOVES_STATE_SIZE && moves_state_load(a) == 0 && moves_state_save(b) && !memcmp(a, b, sizeof a));
    pk->newpos = 0xFF; pk->newpnum = 0xFF;
    moves_frame();
    CHECK(!moves_dropping());
    memcpy(b, a, sizeof b); b[0] = 8; CHECK(moves_state_load(b) < 0);
    /* 95 PC's block: a pad defender between the shooter and his goal, facing the puck, kneels to a low puck */
    rules[RULE_SHOTBLOCK] = 1;
    memset(g.puckcross, 0, sizeof g.puckcross);
    g.puckcross[1].frames = 20;
    o->pflags = (1 << pfgoal) | (1 << pfjoycon); o->Xpos = 0; o->Ypos = -0xC0; o->SPA = 0;
    c->pflags = 1 << pfteam; c->Xpos = 0x40; c->Ypos = -0x90; c->SPA = SPAshotf;
    sortobj(13)->Xpos = 0; sortobj(13)->Ypos = -0x10C;
    pk->newpos = 0; pk->newpnum = 8;
    g.puckx[0] = 0; g.pucky = (uint16_t)-0xA0; g.puckz[0] = 0; g.puckvz[0] = 0;
    o->facedir = (int16_t)vtoa(0, 0x20);
    CHECK(moves_block_hold(o) && o->SPA == MA_SPA(MA_BLOCKK) && (o->pflags & (1 << pfalock)) && moves_blocking(o));
    o->SPA = 0; o->pflags = 1 << pfgoal;
    g.puckz[0] = 0x10;
    rules[RULE_SHOTBLOCK] = 0;
    CHECK(!moves_block_hold(o) && !moves_blocking(o));
    /* 95G's block: a computer defender in the line from his goal to the carrier's puck, facing it, while the carrier winds up; once */
    rules[RULE_SHOTBLOCK] = 2;
    g.sflags = HI(sfssdir);
    o->pflags = 1 << pfgoal; o->Xpos = 0xD; o->Ypos = -0xA0; o->SPA = 0;
    g.puckx[0] = 0x10; g.pucky = (uint16_t)-0x80;
    o->facedir = (int16_t)vtoa(0x10, 0x8B);
    CHECK(moves_block_cpu(o) && o->SPA == MA_SPA(MA_BLOCK_G) && (o->pflags & (1 << pfalock)));
    o->SPA = 0; o->pflags = 1 << pfgoal;
    CHECK(!moves_block_cpu(o));                            /* (one block a shot) */
    o->pflags = (1 << pfgoal) | (1 << pfjoycon);
    CHECK(!moves_block_cpu(o) && moves_block_hold(o) && o->SPA == MA_SPA(MA_BLOCK_G));   /* (a pad player: A while he winds up) */
    g.sflags = 0; o->SPA = 0;
    /* the PC goalie */
    struct PuckCross pc = { 0x20, 4 };
    struct SortObj *gk = sortobj(0);
    gk->Xpos = 0; gk->position = 0; g.puckvx = 0; g.puckvy = 0x3000; g.puckz[0] = 2; g.puckvz[0] = 0x100;
    CHECK(goalie_save_spa(SPAgstickr, 3, &pc, gk) == SPAgstickr && goalie_wall_spa(gk, SPAwallright) == SPAwallright);
    rules[RULE_GOALIE] = 1;
    CHECK(goalie_save_spa(SPAgstickr, 3, &pc, gk) == MA_SPA(MA_GFALLS) && goalie_save_spa(SPAgstickl, 5, &pc, gk) == MA_SPA(MA_GFALLG));
    CHECK(goalie_save_spa(SPAgglover, 1, &pc, gk) == SPAgglover);   /* (in front of him: the cartridge's) */
    g.puckvy = 0x800;
    CHECK(goalie_save_spa(SPAgstickr, 4, &pc, gk) == MA_SPA(MA_GFALLB));   /* (straight at him, slow) */
    CHECK(goalie_wall_spa(gk, SPAwallright) == MA_SPA(MA_GWALLR) && goalie_wall_spa(o, SPAwallleft) == SPAwallleft);
    rules[RULE_GOALIE] = 2;
    pc.x = 1; g.puckvz[0] = 0x400;
    CHECK(goalie_save_spa(SPAgglover, 1, &pc, gk) == MA_SPA(MA_GKNEE));
    g.puckvz[0] = 0x800;
    CHECK(goalie_save_spa(SPAgglover, 1, &pc, gk) == MA_SPA(MA_GFALLB));
    moves_state_load(NULL);
    classic_rules();
}

int main(void)
{
    const char *rom = getenv("NHL94_ROM");
    if (!rom_load(rom ? rom : "NHL 94 (UE) [!].gen")) { fprintf(stderr, "cannot load ROM (set NHL94_ROM)\n"); return 2; }
    rosters_retail();                                      /* (the roster set's tables, the cartridge's) */
    weights();
    overall_ratings();
    column_ratings();
    sega_cd();
    referee();
    hold();
    siren();
    penalties();
    moves();
    printf("rulestest: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
