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
#include "view.h"
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
    referee_state_load(NULL, 0);
    ram_fill(RAM(PenBuf), 0, sizeof g.PenBuf);
    g.demoflag = 1; g.sflags = 0; g.gmode = 0; g.gmode2 = 0; g.OptPen = 1; g.PalMode = 0; g.Vpos = 0x40; g.gameclock = 100;
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
    CHECK(referee_get(&v) && v.x >> 16 == 0x7A && v.y >> 16 == 0x40 && !v.at_dot && v.fall < 0);   /* (assreffollow's 90h: 7Ah here) */
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
    CHECK(referee_state_load(a, sizeof a) == 0);
    referee_state_save(b);
    CHECK(!memcmp(a, b, sizeof a));
    for (int k = 14; k <= 24; k++) {                       /* (each flag byte out of range is refused) */
        memcpy(b, a, sizeof b);
        b[k] = k == 24 ? 0x7F : 0xFF;                      /* (24: skateto's countdown, -12 to 12) */
        CHECK(k == 14 || referee_state_load(b, sizeof b) < 0);   /* (byte 14, nopuck, takes any value) */
    }
    CHECK(referee_state_load(a, REFEREE_STATE_SIZE_9) == 0);   /* (a state from before his skating: the first 22 bytes, his skating afresh) */
    referee_state_save(b);
    CHECK(!memcmp(a, b, 14) && b[15] == a[15]);
    CHECK(referee_state_load(a, 23) < 0);
    CHECK(referee_state_load(a, REFEREE_STATE_SIZE_10) == 0);   /* (version 10: before his fight watch) */
    memcpy(b, a, sizeof b); b[29] = 3; CHECK(referee_state_load(b, sizeof b) < 0);
    memcpy(b, a, sizeof b); b[30] = 0x3D; CHECK(referee_state_load(b, sizeof b) < 0);
    CHECK(referee_state_load(NULL, 0) == 0 && !referee_get(&v));
    classic_rules();
}

/* RULE_REFEREE: in play he keeps his line (no EvadePlayers: a skater coming at him does not turn him aside); a fight is the fighters'
 * (he does not collide with them, and gets out of it to watch from 50h away as the others do, NHLPA 93's assfwatch: assfwatchref); a dead puck (the
 * clock stopped) does not bounce off him (bounced every 8 frames, it went along at his feet) */
static void referee_avoid(void)
{
    struct referee_view v;
    classic_rules();
    referee_state_load(NULL, 0);
    g.demoflag = 1; g.sflags = 0; g.gmode = 0; g.gmode2 = 0; g.PalMode = 0; g.Vpos = 0x40; g.gameclock = 100;
    struct SortObj *pk = sortobj(puckscnum);
    memset(pk, 0, sizeof *pk);
    pk->SCnum = puckscnum; pk->asslist[0] = pnorm; pk->newpos = 0xFF; pk->Xpos = 0x20; pk->Ypos = 0x40;
    for (int i = 0; i < 12; i++) { struct SortObj *o = sortobj(i); memset(o, 0, sizeof *o); o->SCnum = (int16_t)i; o->position = -1; }
    elapsed = 1;
    rules[RULE_REFEREE] = 1;
    for (int k = 0; k < 40; k++) referee_update();          /* (on beside the play, past the drop's wait) */
    referee_get(&v);
    int16_t rx = (int16_t)(v.x >> 16), ry = (int16_t)(v.y >> 16);
    CHECK(rx == 0x7A && ry == 0x40);
    /* a dead puck at his feet stays as it was */
    g.gmode = 1 << gmclock;
    pk->Xpos = (int16_t)(rx + 4); pk->Ypos = (int16_t)(ry + 4); pk->Zpos = 0; pk->Xvel = -0x900; pk->Yvel = 0;
    referee_puck(pk);
    CHECK(pk->Xvel == -0x900 && pk->Yvel == 0);
    g.gmode = 0;
    pk->Xpos = 0x20; pk->Ypos = 0x40; pk->Xvel = 0;
    /* a skater coming at him: he does not dodge (Abdul: only out of a fight) */
    struct SortObj *o = sortobj(3);
    o->position = 3; o->Xpos = (int16_t)(rx - 0x30); o->Ypos = ry; o->Xvel = 0x1800; o->weight = 10;
    for (int k = 0; k < 40; k++) referee_update();
    referee_get(&v);
    CHECK((v.y >> 16) == ry && (v.x >> 16) == rx);
    o->position = -1;
    /* a fight beside him at the boards */
    struct SortObj *f1 = sortobj(2), *f2 = sortobj(8);
    f1->position = 2; f1->Xpos = 0x66; f1->Ypos = 0x40; f1->pflags2 = 1 << pf2fight;
    f2->position = 2; f2->Xpos = 0x7A; f2->Ypos = 0x40; f2->pflags2 = 1 << pf2fight; f2->Xvel = 0x1000;
    g.collflag = 0;
    referee_get(&v);
    f2->Xpos = (int16_t)((v.x >> 16) - 12); f2->Ypos = (int16_t)(v.y >> 16);
    referee_cx(f2->Xpos, f2->Ypos, f2);
    CHECK(g.collflag == 0 && f2->Xvel == 0x1000);          /* (a fighter does not hit him) */
    f2->Xpos = 0x7A; f2->Ypos = 0x40; f2->Xvel = 0;
    pk->pflags2 = 1 << pf2fight; pk->newpos = 0; pk->newpnum = 2;
    g.xc1 = 0x70; g.yc1 = 0x40;
    for (int k = 0; k < 240; k++) referee_update();
    referee_get(&v);
    int dx = (int)(v.x >> 16) - 0x70, dy = (int)(v.y >> 16) - 0x40;
    CHECK(dx * dx + dy * dy >= 0x40 * 0x40 && (v.x >> 16) <= 0x7E);   /* (watching from about 50h, not between them) */
    /* on the fight's centre in mid ice: out toward the nearer side boards, 50h; back to assreffollow when it is over */
    g.xc1 = (uint16_t)-0x20; g.yc1 = 0x10;
    {
        uint8_t st[REFEREE_STATE_SIZE];
        referee_state_save(st);
        st[0] = (uint8_t)-0x20; st[1] = 0xFF; st[4] = 0x10; st[5] = 0; st[6] = 0; st[7] = 0; st[8] = 0; st[9] = 0; st[10] = 0; st[11] = 0;   /* (him on the centre, still) */
        st[2] = 0; st[3] = 0;
        st[29] = 0;                                        /* (not watching yet: a spot at once) */
        CHECK(referee_state_load(st, sizeof st) == 0);
    }
    for (int k = 0; k < 120; k++) referee_update();
    referee_get(&v);
    dx = (int)(v.x >> 16) + 0x20; dy = (int)(v.y >> 16) - 0x10;
    CHECK(dx < -0x40 && dy * dy < 0x10 * 0x10);
    uint8_t st2[REFEREE_STATE_SIZE];
    referee_state_save(st2);
    CHECK(st2[29] == 2 && (v.vx | v.vy) == 0);  /* (clear of it: stopped, watching; kept in the state) */
    pk->pflags2 = 0;
    referee_update();
    referee_state_save(st2);
    CHECK(st2[29] == 0);
    f1->position = f2->position = -1; f1->pflags2 = f2->pflags2 = 0;
    pk->pflags2 = 0; pk->newpos = 0xFF;
    referee_state_load(NULL, 0);
    classic_rules();
}

/* RULE_PENCALL (mods/referee.c): NHL 95 PC's penalty call. With a penalty with minutes waiting for its screen (gmpen, PenCntDwn), the
 * referee in play skates to the timekeeper's side (A0h, 0: 8 inside the boards here), turns a step every 8 frames to face us (4) and
 * signals the call (hooking: REF_SIG_HOOK); chkprogress waits for it (pencall_ready -1), then cuts (1). Off, or without the referee
 * in play: the cartridge's way (0). The state keeps the call */
static void referee_pencall(void)
{
    struct referee_view v;
    classic_rules();
    referee_state_load(NULL, 0);
    g.demoflag = 1; g.sflags = 0; g.gmode = 0; g.gmode2 = 0; g.PalMode = 0; g.Vpos = 0x40; g.gameclock = 100;
    struct SortObj *pk = sortobj(puckscnum);
    memset(pk, 0, sizeof *pk);
    pk->SCnum = puckscnum; pk->asslist[0] = pnorm; pk->newpos = 0xFF; pk->Xpos = -0x20; pk->Ypos = 0x40;
    for (int i = 0; i < 12; i++) { struct SortObj *o = sortobj(i); memset(o, 0, sizeof *o); o->SCnum = (int16_t)i; o->position = -1; }
    elapsed = 1;
    rules[RULE_REFEREE] = 1;
    for (int k = 0; k < 40; k++) referee_update();          /* (on beside the play) */
    memset(g.PenBuf, 0, sizeof g.PenBuf);
    g.PenBuf[0] = 0x1E; g.PenBuf[1] = 2;                    /* (hooking, player 2) */
    g.gmode = (1 << gmclock) | (1 << gmpen); g.PenCntDwn = 100;
    CHECK(pencall_ready() == 0);                            /* (the rule off: the cartridge's way) */
    rules[RULE_PENCALL] = 1;
    CHECK(pencall_ready() == -1);
    int k, sig = 0, at = 0;
    for (k = 0; k < 500 && pencall_ready() < 0; k++) {
        referee_update();
        referee_get(&v);
        if (v.anim == REF_SIGNAL) { sig++; if (v.sig != REF_SIG_HOOK) at = -1; }
        if (k == 100) {                                     /* (the state keeps the call) */
            uint8_t a[REFEREE_STATE_SIZE], b[REFEREE_STATE_SIZE];
            CHECK(referee_state_save(a) == REFEREE_STATE_SIZE && a[33] >= 1 && a[33] <= 4);
            CHECK(referee_state_load(a, sizeof a) == 0);
            referee_state_save(b);
            CHECK(!memcmp(a, b, sizeof a));
            memcpy(b, a, sizeof b); b[33] = 5; CHECK(referee_state_load(b, sizeof b) < 0);
            memcpy(b, a, sizeof b); b[34] = REF_SIGS; CHECK(referee_state_load(b, sizeof b) < 0);
            CHECK(referee_state_load(a, REFEREE_STATE_SIZE_11) == 0);   /* (version 11: no call under way) */
            CHECK(referee_state_load(a, sizeof a) == 0);
        }
    }
    referee_get(&v);
    CHECK(pencall_ready() == 1 && referee_call_made());
    CHECK((v.x >> 16) >= 0x78 - 0x14 && (v.x >> 16) <= 0x80 && (v.y >> 16) >= -0xC && (v.y >> 16) <= 0xC && v.dir == 4);
    CHECK(sig >= 60 && at == 0);                            /* (the hooking signal, its 60 frames) */
    rules[RULE_REFEREE] = 0;
    CHECK(pencall_ready() == 0);                            /* (no referee in play: the cartridge's way) */
    g.gmode = 0; memset(g.PenBuf, 0, sizeof g.PenBuf);
    referee_state_load(NULL, 0);
    classic_rules();
}

/* RULE_REFEREE: 95 PC's skating (mods/referee.c). The boards: skating into them, along a side or into a corner, he is turned back
 * as a skater is (checkwallcoll / wallcoll: the side boards at 80h, the ends at 126h, the corners' circles of 40h), never more than a
 * frame's way past them. The turn: his facing moves a step of 45 degrees at a time, the PC's doplayeracc rate (at least 5 frames a
 * step, 11 at his top speed: no flip from one way to another). The face off: while it is to be set up (RULE_HOLD, the walk not yet
 * known) he stays where he is, not put at the dot (gone from the view, then the walk from the dot). The state keeps his skating */
static void referee_skating(void)
{
    struct SortObj o;
    classic_rules();
    referee_state_load(NULL, 0);
    g.demoflag = 1; g.sflags = 0; g.gmode = 0; g.gmode2 = 0; g.PalMode = 0; g.Vpos = 0x40; g.gameclock = 100;
    elapsed = 1;
    memset(&o, 0, sizeof o);
    referee_skate_start(&o, 0x78, 0x40);
    o.Xvel = 0x1E00;                                       /* (2 a frame into the side boards) */
    int mx = 0;
    for (int k = 0; k < 60; k++) { referee_glide(&o); if (o.Xpos > mx) mx = o.Xpos; }
    CHECK(mx <= 0x80 + 2 && o.Xvel <= 0 && o.Xpos < 0x80);
    memset(&o, 0, sizeof o);
    referee_skate_start(&o, 0x50, -0xF0);
    o.Xvel = 0x1400; o.Yvel = -0x1400;                     /* (into a corner) */
    int out = 0;
    for (int k = 0; k < 90; k++) {
        referee_glide(&o);
        int qx = o.Xpos - 0x40, qy = -o.Ypos - 0xE6;
        if (qx > 0 && qy > 0 && qx * qx + qy * qy > 0x42 * 0x42) out++;
    }
    CHECK(!out);
    /* in play: the puck goes from one end to the other, he follows facing his way a step at a time */
    struct SortObj *pk = sortobj(puckscnum);
    memset(pk, 0, sizeof *pk);
    pk->SCnum = puckscnum; pk->asslist[0] = pnorm; pk->newpos = 0xFF; pk->Xpos = 0x20; pk->Ypos = 0x40;
    for (int i = 0; i < 12; i++) { struct SortObj *q = sortobj(i); memset(q, 0, sizeof *q); q->SCnum = (int16_t)i; q->position = -1; }
    rules[RULE_REFEREE] = 1;
    struct referee_view v;
    int pd = -1, pt = -100, bad = 0, near = 0, turns = 0;
    for (int k = 0; k < 600; k++) {
        if (k == 40) { pk->Xpos = -0x60; pk->Ypos = -0xE0; g.Vpos = (uint16_t)-0xC0; }
        if (k == 300) { pk->Xpos = 0x30; pk->Ypos = 0xD0; g.Vpos = 0xC0; }
        referee_update();
        referee_get(&v);
        if (pd >= 0 && v.dir != pd) {
            turns++;
            if (((v.dir - pd) & 7) != 1 && ((pd - v.dir) & 7) != 1) bad++;
            if (k - pt < 3) near++;
            pt = k;
        }
        pd = v.dir;
    }
    CHECK(turns > 2 && !bad && near * 4 < turns);
    CHECK((v.y >> 16) > 0x80);                              /* (he went up the ice with the play, to its corner: assreffollow) */
    /* the state */
    uint8_t a[REFEREE_STATE_SIZE], b[REFEREE_STATE_SIZE];
    referee_state_save(a);
    for (int k = 0; k < 7; k++) referee_update();
    referee_state_save(b);
    CHECK(referee_state_load(a, sizeof a) == 0);
    for (int k = 0; k < 7; k++) referee_update();
    uint8_t c[REFEREE_STATE_SIZE];
    referee_state_save(c);
    CHECK(!memcmp(b, c, sizeof b));                        /* (from a state he skates on as he did) */
    /* the face off is to be set up (RULE_HOLD): he stays */
    referee_get(&v);
    int16_t rx = (int16_t)(v.x >> 16), ry = (int16_t)(v.y >> 16);
    rules[RULE_HOLD] = 1;
    hold_state_load(NULL);
    pk->asslist[0] = pfaceoff2; pk->pflags = 1 << pfna; g.fox = (uint16_t)-0x60; g.foy = (uint16_t)-0xA0;
    referee_update();
    referee_get(&v);
    CHECK((int16_t)(v.x >> 16) == rx && (int16_t)(v.y >> 16) == ry && v.at_dot);
    pk->pflags = 0;                                        /* (set up, no walk: at the dot) */
    referee_update();
    referee_get(&v);
    CHECK((int16_t)(v.x >> 16) == -0x60 - 15 && (int16_t)(v.y >> 16) == -0xA0);
    rules[RULE_HOLD] = 0;
    pk->asslist[0] = pnorm;
    referee_state_load(NULL, 0);
    classic_rules();
}

/* a puck out of play at the whistle (RULE_HOLD): NHL 95 PC's assrefwhistle takes him to the timekeeper's side, turns him to facing 2 a
 * step every 8 ticks and has him point (SPArefpointgoa, 8 and 24 ticks), then assrefdrop to the dot. Before, he only glided there */
static void refpoint(void)
{
    classic_rules();
    hold_state_load(NULL);
    rules[RULE_HOLD] = 1;
    g.gameclock = 100; g.PerTimeTotal = 300; g.Vcount = 1000;
    for (int i = 0; i < 12; i++) { struct SortObj *o = sortobj(i); memset(o, 0, sizeof *o); o->SCnum = (int16_t)i; o->position = i % 6 ? 1 : 0; o->pnum = (uint8_t)i; }
    g.puckx[0] = 0xA0; g.pucky = 0x40;                     /* (over the glass) */
    hold_skate_begin();
    for (int i = 0; i < 12; i++) { sortobj(i)->Xpos = 100; sortobj(i)->Ypos = 100; }
    g.fox = 100; g.foy = 100;
    hold_skate_arm();
    struct hold_refpose q;
    int first = -1, n = 0, ok = 1, pick = 0;
    for (int k = 0; k < 900 && hold_skating(); k++, g.Vcount++) {
        hold_skate_ref_pose(0, 0, 0, 0, 0, &q);
        if (q.anim == REF_PICKUP) pick = 1;
        if (q.anim != REF_POINT) continue;
        if (first < 0) first = k;
        n++;
        if (q.dir != 2 || q.vx || q.vy || q.y != 0) ok = 0;
    }
    CHECK(first > 0 && n == 34 && ok && !pick);
    rules[RULE_HOLD] = 0;
    hold_state_load(NULL);
    classic_rules();
}

/* the referee's way between plays keeps out of the nets (NHL 95 PC's avdgoal and SkateToSpot aims, mods/referee.c referee_way): from
 * centre ice, the follow spots and the corners to a puck behind a net and back to every face off spot, no point of it within the net's
 * frame (the cartridge's oval about 0, +-10Ch). Before, the straight way from centre to behind the net went through it */
static void refnet(void)
{
    struct SortObj *n12 = sortobj(12), *n13 = sortobj(13);
    memset(n12, 0, sizeof *n12); memset(n13, 0, sizeof *n13);
    n12->Ypos = 0x10C; n13->Ypos = -0x10C;
    for (int i = 0; i < 12; i++) { struct SortObj *o = sortobj(i); o->position = 1; o->frame = 0; }
    CHECK(referee_in_net(0, 0x10C, NULL, NULL) && referee_in_net(30, 0x10C, NULL, NULL) && !referee_in_net(34, 0x10C, NULL, NULL) &&
          !referee_in_net(0, 0xF8, NULL, NULL) && !referee_in_net(0, 0x118, NULL, NULL) && referee_in_net(0, -0x112, NULL, NULL));
    static const int16_t from[][2] = { { 0, 0 }, { 0x7A, 0 }, { -0x7A, 0x60 }, { 0x66, 0x108 }, { -0x66, 0x108 }, { 0x3B, 0x10D }, { 0, 0xC0 },
                                       { 0x10, 0xD0 }, { -0x40, 0x120 }, { 0x60, 0x11E }, { 0, 0x120 }, { 0x30, -0x100 } };
    static const int16_t to[][2] = { { 0, 0x120 }, { 0x08, 0x11E }, { -0x18, 0x122 }, { 0x30, 0x120 }, { 0, 0xF8 }, { -0x50, 0xC0 }, { 0x50, 0xC0 },
                                     { -0x50, 0x58 }, { 0, 0 }, { 0x50, -0xC0 }, { 0x12, 0x10C + 14 }, { 0, -0x120 } };
    int bad = 0;
    for (int sign = 1; sign >= -1; sign -= 2)
        for (size_t a = 0; a < sizeof from / sizeof from[0]; a++)
            for (size_t b = 0; b < sizeof to / sizeof to[0]; b++) {
                int ax = from[a][0], ay = from[a][1] * sign, bx = to[b][0], by = to[b][1] * sign;
                if (referee_in_net(ax, ay, NULL, NULL) || referee_in_net(bx, by, NULL, NULL)) continue;
                int wx[5], wy[5], n = referee_way(ax, ay, bx, by, wx, wy, 5), x = ax, y = ay, hit = 0;
                for (int i = 0; i < n; x = wx[i], y = wy[i], i++)
                    for (int k = 0; k <= 64; k++)
                        if (referee_in_net(x + (wx[i] - x) * k / 64, y + (wy[i] - y) * k / 64, NULL, NULL)) hit = 1;
                if (hit || wx[n - 1] != bx || wy[n - 1] != by) {
                    bad++;
                    printf("  net: %d,%d -> %d,%d:", ax, ay, bx, by);
                    for (int i = 0; i < n; i++) printf(" %d,%d", wx[i], wy[i]);
                    printf("\n");
                }
            }
    CHECK(bad == 0);
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
    /* the referee's walk (NHL 95 PC's assrefgoalpa, assrefdrop and SkateToSpot): from rest he speeds up, skates at no more than the PC
     * referee's top speed, brakes onto the puck, picks it up (SPArefpickup), carries it to the dot and turns there; the walk waits for him */
    {
        struct hold_refpose p, q;
        int acc, top, brake, t0 = (int16_t)g.Vcount, fast = 0, early = -1, pick = -1, last = -1, ok = 1;
        referee_rates(0, &acc, &top, &brake);
        CHECK(acc > 200 && acc < 400 && top > 7000 && top < 8500 && brake > 150 && brake < 250);
        CHECK(hold_skate_ref_pose(0, 0, 0, 0, 0, &p));
        for (int k = 0; k < 600 && hold_skating(); k++, g.Vcount++) {
            hold_skate_ref_pose(0, 0, 0, 0, 0, &q);
            int64_t v2 = (int64_t)q.vx * q.vx + (int64_t)q.vy * q.vy;
            if (v2 > (int64_t)top * top + top) ok = 0;
            if (v2 > (int64_t)top * top / 2) fast = 1;
            if (k == 3) early = (int)(v2 < (int64_t)top * top / 16);
            if (q.anim == REF_PICKUP && pick < 0) { pick = k; if (q.x != -40 * 256 || q.y != 60 * 256 || v2) ok = 0; }
            if (q.anim == REF_STOP && v2 == 0) ok = 0;
            last = k;
        }
        CHECK(ok && fast && early == 1 && pick > 0);
        CHECK(q.x == 115 * 256 && q.y == 100 * 256 && q.dir == 6);   /* (15 off the dot, facing it: assrefatdot) */
        g.Vcount = (uint16_t)t0;
        (void)last;
    }
    for (int k = 0; k < 600 && !hold_skate_carry(&x, &y); k++) g.Vcount++;   /* (the pick up: his own time, not a share of the skaters') */
    for (int k = 0; k < 40; k++) g.Vcount++;
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
    memcpy(b, a, sizeof b); b[98] = 0x59; b[99] = 2; CHECK(hold_state_load(b) < 0);   /* (a walk of more than 600 frames) */
    memcpy(b, a, sizeof b); b[322] = 1; CHECK(hold_state_load(b) < 0);                /* (the puck picked up at 1 of 256) */
    memcpy(b, a, sizeof b); b[335] = 13; CHECK(hold_state_load(b) < 0);               /* (13 held back face off men) */
    memcpy(b, a, sizeof b); b[335] = 1; b[336] = 12; b[348] = afaceoff; CHECK(hold_state_load(b) < 0);   /* (not a skater's object) */
    memcpy(b, a, sizeof b); b[335] = 1; b[336] = 3; b[348] = 0x30; CHECK(hold_state_load(b) < 0);       /* (not a face off's assignment) */
    memcpy(b, a, sizeof b); b[335] = 1; b[336] = 3; b[348] = afaceoff; CHECK(hold_state_load(b) == 0);
    CHECK(hold_state_load(a) == 0 && hold_skate_carry(&x2, &y2) && x2 == x);        /* (a refused state changed nothing it had) */
    struct SortObj *pk = sortobj(puckscnum), *sh = sortobj(puckscnum + 1);
    sh->frame = SPFpuck; pk->Zpos = 0;
    hold_tick();                                           /* (in his hand: neither the puck nor its shadow on the ice) */
    CHECK(hold_skate_carry(&x2, &y2) && pk->Zpos < 0 && sh->frame == 0);
    for (int k = 0; k < 500 && hold_skating(); k++) { g.Vcount++; hold_tick(); }
    CHECK(!hold_skating() && sh->frame == 0 && pk->Xpos == 100 && pk->Ypos == 100 && (int16_t)g.puckz[0] < 0);   /* (at the dot, still in his hand: no shadow, as 95 PC) */
    pk->Xpos = 50; g.puckz[0] = 0; hold_tick();
    CHECK(sh->frame == 0);                                 /* (a puck on the ice away from the dot, as one over the glass: its frame 0 is the game's own) */
    pk->Xpos = 100; hold_tick();
    CHECK(sh->frame == SPFpuck);                           /* (dropped at the dot: the cartridge's shadow again) */
    /* a puck out of play at the whistle (95 PC's assrefgoalpa: not fetched): he skates to the timekeeper's side for one, none on the ice */
    for (int i = 0; i < 12; i++) { sortobj(i)->Xpos = 0; sortobj(i)->Ypos = 0; }
    g.puckx[0] = (uint16_t)-0xB5; g.pucky = (uint16_t)-0x102;
    hold_skate_begin();
    for (int i = 0; i < 12; i++) { sortobj(i)->Xpos = 100; sortobj(i)->Ypos = 100; }
    hold_skate_arm();
    int pick;
    hold_skate_puck(&x, &y, &pick);
    CHECK(hold_skating() && x == 0x78 && y == 0);
    g.Vcount++; hold_tick();
    CHECK(pk->Zpos < 0 && sh->frame == 0);
    {
        struct hold_refpose p;
        for (int k = 0; k < 20; k++) { x2 = x; hold_skate_ref_pose(1, -0x70, -0x100, 0, 0, &p); x = p.x; y = p.y; g.Vcount++; }
        CHECK(x > x2 && x > -0x70 * 256 && y > -0x100 * 256);   /* (on his way to it, not out of the rink) */
    }
    /* the walk's part of a state: kept, and bytes that are not one refused */
    {
        uint8_t w[HOLD_WALK_SIZE], w2[HOLD_WALK_SIZE];
        struct hold_refpose p, p2;
        CHECK(hold_walk_save(w) == HOLD_WALK_SIZE && hold_walk_check(w) == 0 && w[0] == 1);
        hold_skate_ref_pose(0, 0, 0, 0, 0, &p);
        hold_walk_load(NULL);
        hold_walk_load(w);
        hold_skate_ref_pose(0, 0, 0, 0, 0, &p2);
        hold_walk_save(w2);
        CHECK(!memcmp(w, w2, sizeof w) && p.x == p2.x && p.y == p2.y && p.anim == p2.anim);
        memcpy(w2, w, sizeof w); w2[0] = 2; CHECK(hold_walk_check(w2) < 0);
        memcpy(w2, w, sizeof w); w2[8] = 8; CHECK(hold_walk_check(w2) < 0);
        memcpy(w2, w, sizeof w); w2[6] = 0xFF; w2[7] = 0x7F; CHECK(hold_walk_check(w2) < 0);
        CHECK(hold_walk_check(NULL) == 0);
    }
    /* a puck against the boards: he reaches it from inside */
    g.puckx[0] = 0x85; g.pucky = 0x20;
    hold_skate_begin();
    hold_skate_arm();
    hold_skate_puck(&x, &y, &pick);
    CHECK(x == 0x78 && y == 0x20);
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

/* RULE_FIGHT_RATINGS (mods/fight.c fight_rating): the cartridge's value unless both it and RULE_FIGHT are on and the host has ratings;
 * never for a goalie; the host's value, made even and kept in 0-14 */
static int fake_rating(const char *name, int cart) { return !strcmp(name, "Bob Probert") ? 14 : !strncmp(name, "Wayne", 5) ? 15 : cart; }
static void fight_ratings(void)
{
    int det = -1, s = -1, la = -1, w = -1;
    for (int t = 0; t < rosters.teams; t++) {
        const struct RosterTeam *T = rosters_team((uint16_t)t);
        for (int k = 0; k < T->players; k++) {
            const uint8_t *n = T->player[k].name.s + 2;
            if (!memcmp(n, "Bob Probert", 11)) { det = t; s = k; }
            if (!memcmp(n, "Wayne Gretzky", 13) && la < 0) { la = t; w = k; }
        }
    }
    CHECK(det >= 0 && la >= 0);
    if (det < 0 || la < 0) return;
    uint8_t cart = rosters_team((uint16_t)det)->player[s].rating[LR_HANDED_FIGHTING] & 0xE;
    fight_rating_hook = fake_rating;
    CHECK(fight_rating((uint16_t)det, (uint16_t)s, cart) == cart);          /* (both off) */
    rules[RULE_FIGHT_RATINGS] = 1;
    CHECK(fight_rating((uint16_t)det, (uint16_t)s, cart) == cart);          /* (Fighting off: nothing changes) */
    rules[RULE_FIGHT] = 1;
    CHECK(fight_rating((uint16_t)det, (uint16_t)s, cart) == 14);
    CHECK(fight_rating((uint16_t)la, (uint16_t)w, 2) == 2);                 /* (15: out of range, the cartridge's) */
    CHECK(fight_rating((uint16_t)det, 0, 6) == 6);                          /* (a goalie) */
    rules[RULE_FIGHT_RATINGS] = 0;
    CHECK(fight_rating((uint16_t)det, (uint16_t)s, cart) == cart);
    fight_rating_hook = NULL;
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

/* RULE_TICKER_HL (mods/ticker.c): the cartridge's InitScores leaves the other games at periods 0-2, so the pregame's UpdateScores
 * never asks for a highlight; with the rule a game can be at period 3 then, and a close one goes to overtime with gsfhl */
static void ticker(void)
{
    for (int rule = 0; rule <= 1; rule++) {
        int most = 0, hl = 0;
        for (uint32_t seed = 1; seed <= 200; seed++) {
            classic_rules();
            rules[RULE_TICKER_HL] = (uint8_t)rule;
            memset(&g, 0, sizeof g);
            g.seed = seed * 0x9E3779B9u;
            g.gamelevel = 0;                                 /* (the first round: 8 games) */
            g.gamenum = 0;
            for (int k = 0; k < 8; k++) { g.matchups[k].gst1 = (uint16_t)(2 * k); g.matchups[k].gst2 = (uint16_t)(2 * k + 1); }
            InitScores();
            for (int k = 1; k < 8; k++) if ((int)g.matchups[k].gsper > most) most = g.matchups[k].gsper;
            UpdateScores();                                  /* (the pregame's, PeriodOver_sp) */
            for (int k = 1; k < 8; k++) if (g.matchups[k].gsflags & MU_HIGHLIGHT) { hl++; CHECK(g.matchups[k].gsper == 3); }
            CHECK(!(g.matchups[0].gsflags & MU_HIGHLIGHT) && g.matchups[0].gsper == 0);   /* (not the game being played) */
        }
        CHECK(most == (rule ? 3 : 2));
        CHECK(rule ? hl > 0 : hl == 0);
    }
    classic_rules();
    CHECK(ticker_init_periods() == 3);
}

/* NHL 95's moves and the goalie's saves (mods/moves.c, mods/goalie.c, mods/anims.c) */
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
    CHECK(mod_frame_pc(MODF_BASE + 156) >= 0 && mod_frame_pc(MODF_BASE + 157) < 0 && mod_frame(SPFGoalie) == SPFGoalie);
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
    /* the goalie's saves */
    struct PuckCross pc = { 0x20, 4 };
    struct SortObj *gk = sortobj(0);
    gk->Xpos = 0; gk->Ypos = 0xC0; gk->Xvel = gk->Yvel = 0; gk->position = 0; gk->pflags = 0; gk->attribute = 0; gk->handed[0] = 0;
    gk->SPA = 0; set_puck_carrier((uint16_t)-1);
    g.puckx[0] = 0; g.pucky = 0x90; g.puckvx = 0; g.puckvy = 0x800; g.puckz[0] = 2; g.puckvz[0] = 0x100;
    int16_t sa = (int16_t)vtoa(0, (int16_t)(0x90 + 2 - 0xC0));
    CHECK(!goalie_save(&pc, 0x108, gk) && gk->SPA == 0 && goalie_wall_spa(gk, SPAwallright) == SPAwallright);
    rules[RULE_GOALIE] = 1;                                /* (95 PC) */
    gk->facedir = (int16_t)((sa + 4) & 7);
    CHECK(goalie_save(&pc, 0x108, gk) && gk->SPA == MA_SPA(MA_GFALLB));   /* (straight at him, slow: falls back) */
    gk->facedir = (int16_t)((sa - 2) & 7);
    CHECK(goalie_save(&pc, 0x108, gk) && gk->SPA == MA_SPA(MA_GFALLS));   /* (low, wide: to the stick side) */
    gk->attribute = HI(3);
    CHECK(goalie_save(&pc, 0x108, gk) && gk->SPA == MA_SPA(MA_GFALLG));   /* (the other way round: the glove side) */
    gk->attribute = 0; g.puckz[0] = 0x20;
    CHECK(goalie_save(&pc, 0x108, gk) && gk->SPA == SPAgglover);           /* (high: no shoulder saves) */
    CHECK(goalie_wall_spa(gk, SPAwallright) == MA_SPA(MA_GWALLR) && goalie_wall_spa(o, SPAwallleft) == SPAwallleft);
    rules[RULE_GOALIE] = 4;                                /* (96 PC: a loose puck straight at him, the toe and kick saves) */
    gk->facedir = sa; g.puckvy = 0x2000; g.puckz[0] = 2;
    CHECK(goalie_save(&pc, 0x108, gk) && gk->SPA == MA_SPA(MA_GKNEE));
    g.puckz[0] = 0xC;
    CHECK(goalie_save(&pc, 0x108, gk) && gk->SPA == SPAgstickr);
    rules[RULE_GOALIE] = 2;                                /* (96 Genesis: at most 2 across, the block and the low catch) */
    pc.x = 1; g.puckz[0] = 2; g.puckvz[0] = 0x400;
    CHECK(goalie_save(&pc, 0x108, gk) && gk->SPA == MA_SPA(MA_GKNEE));
    g.puckvz[0] = 0x800;
    CHECK(goalie_save(&pc, 0x108, gk) && gk->SPA == MA_SPA(MA_GFALLB));
    CHECK(goalie_wall_spa(gk, SPAwallright) == MA_SPA(MA_GWALLR));
    rules[RULE_GOALIE] = 3;                                /* (95 Genesis: a pad stack facing up or down during a shot dives) */
    pc.x = 0x40; pc.frames = 20; gk->Xpos = 0; gk->facedir = 0; g.puckz[0] = 2; g.puckvz[0] = 0x100;
    set_puck_carrier(1); g.sflags = HI(sfssdir);
    CHECK(goalie_save(&pc, 0x108, gk) && (gk->SPA == MA_SPA(MA_GFALLB) || gk->SPA == MA_SPA(MA_GFALLG)) && gk->Xvel == 0);
    g.sflags = 0; g.puckz[0] = 4; gk->Xpos = 0;
    CHECK(goalie_save(&pc, 0x108, gk) && (gk->SPA == SPAghighr || gk->SPA == SPAgglovel2));   /* (a raised puck: the high saves) */
    CHECK(goalie_wall_spa(gk, SPAwallright) == SPAwallright);   /* (95 Genesis has no goalie hop) */
    /* the hop over the boards: NHL 95 PC's first table for directions 0-3, the reverse for 4-7 */
    {
        const uint8_t *wt = spa_table(MA_SPA(MA_GWALLR));
        CHECK(rd16(wt + rd16(wt + 3 * 2)) == rd16(wt + rd16(wt)) && rd16(wt + rd16(wt + 4 * 2)) != rd16(wt + rd16(wt)));
    }
    /* the PC goalie's poke check and knee hold (values 1 and 4) */
    rules[RULE_GOALIE] = 1;
    gk->pflags = 0; gk->pflags2 = 0; gk->Xpos = 0; gk->Ypos = 0xC0; gk->SPA = 0;
    g.puckx[0] = 0x10; g.pucky = 0xB0;
    set_puck_carrier(7);                                   /* (an opponent has it, 16 or so from him) */
    goalie_state_load(NULL);
    int pokes = 0;
    for (int k = 0; k < 400 && !pokes; k++) pokes = goalie_poke(gk);
    CHECK(pokes && gk->SPA == MA_SPA(MA_GPOKE) && (gk->pflags2 & 2));
    gk->pflags2 = 0;
    CHECK(!goalie_poke(gk));                               /* (240 ticks before the next) */
    {
        uint8_t ga[GOALIE_STATE_SIZE], gb[GOALIE_STATE_SIZE];
        CHECK(goalie_state_save(ga) == GOALIE_STATE_SIZE && (ga[0] | ga[1]) != 0);
        goalie_state_load(ga); goalie_state_save(gb);
        CHECK(!memcmp(ga, gb, sizeof ga));
    }
    int16_t was = elapsed; elapsed = 0xF0;
    goalie_tick(gk);
    elapsed = was;
    set_puck_carrier(1);                                   /* (his own team's: no poke) */
    for (int k = 0; k < 400; k++) if (goalie_poke(gk)) { pokes = 0; break; }
    CHECK(pokes);
    struct SortObj *att = sortobj(8);
    gk->SCnum = 0; set_puck_carrier(0);                    /* (he has it; an attacker 10 from the puck) */
    att->position = 2; att->pflags2 = 0; att->Xpos = 0x10; att->Ypos = 0xA6;
    CHECK(goalie_kneehold(gk) && gk->SPA == MA_SPA(MA_GKNEEHOLD));
    gk->temp5 = 1; g.gmode &= (uint16_t)~(1 << gmclock); g.BA_PS_flags = HI(2);
    was = elapsed; elapsed = 2;
    goalie_busy(gk);
    elapsed = was;
    CHECK(gk->temp5 < 0 && (g.BA_PS_flags & HI(4)));      /* (his hold count runs on while he kneels) */
    g.BA_PS_flags = 0;
    att->Xpos = 0x60;
    CHECK(!goalie_kneehold(gk));
    rules[RULE_GOALIE] = 3;
    att->Xpos = 0x10;
    CHECK(!goalie_kneehold(gk) && !goalie_poke(gk));       /* (95 Genesis has neither) */
    goalie_state_load(NULL); att->Xpos = 0; att->Ypos = 0; att->position = 0;
    set_puck_carrier((uint16_t)-1); gk->SPA = 0; gk->Xpos = 0;
    moves_state_load(NULL);
    classic_rules();
}

/* the host's side of the rules (native/rules.c, linked in): the menus' defaults, the Widescreen setting and the state's part. Its
 * host calls stand in here: no netplay session unless net_on, the cartridge's roster set */
static int net_on;
int native_net_on(void) { return net_on; }
int native_net_session(uint8_t *v, int *sent, int *host) { (void)v; (void)sent; (void)host; return 0; }
const char *native_net_identity(const char *setup) { (void)setup; return ""; }
void clock_set_original(int on) { (void)on; }
int rosters_list(const char *const **names, const char *const **labels) { static const char *const n[1] = { "cartridge" }; *names = n; *labels = n; return 1; }
const char *rosters_list_dir(int i, char *out, size_t size) { (void)i; (void)out; (void)size; return NULL; }
void rosters_next(const char *dir) { (void)dir; }
int rosters_session_entry(void) { return 0; }
uint64_t rosters_hash(void) { return 0; }
const char *rosters_dir(void) { return ""; }
int rosters_use(const char *dir, uint64_t hash) { (void)dir; (void)hash; return 0; }
const char *rosters_error(void) { return ""; }

/* every rule's default is the original's: a new install (settings.ini and the menus at their defaults) plays the cartridge's game,
 * the rink announcer's timing ("talks over the face off") among them; Recommended and the other presets pick their own */
static void defaults(void)
{
    for (int i = 0; i < RULE_COUNT; i++) {
        if (rule_options[i].def != rule_classic[i]) printf("  %s defaults to %d\n", rule_options[i].key, rule_options[i].def);
        CHECK(rule_options[i].def == rule_classic[i]);
    }
    CHECK(rule_options[RULE_ANNOUNCE].def == 0);
    int v[RULE_COUNT];
    for (int i = 0; i < RULE_COUNT; i++) v[i] = rule_options[i].def;
    rules_next(v, NULL);
    CHECK(rules_classic());
    rules_power_on();
    CHECK(!memcmp(rules, rule_classic, sizeof rules));
}

/* the wide view (src/view.h): 16:9 makes the picture wide (RULE_VIEW 1) and nothing of the play. The game's camera keeps the
 * cartridge's reach ($3C) and a pad's goalie is off screen where the cartridge's is; the picture's camera (view_cam) stays within
 * the wide picture. Wide play (2), an option of its own, plays by the 320, and 16:9 leaves it so. A state from before version 14,
 * whose 1 was played by the wide view, loads as wide play */
static void wide_view(void)
{
    int v[RULE_COUNT] = { 0 };
    net_on = 0;
    rules_next(v, NULL);
    rules_widescreen(1);
    rules_power_on();
    CHECK(rules[RULE_VIEW] == 1 && view_wide() && !view_play_wide());
    uint8_t sent[RULE_COUNT];
    rules_next_send(sent);
    CHECK(sent[RULE_VIEW] == 1);
    v[RULE_VIEW] = 2;
    rules_next(v, NULL);
    rules_power_on();
    CHECK(rules[RULE_VIEW] == 2 && view_play_wide());
    rules_next_send(sent);
    CHECK(sent[RULE_VIEW] == 2);
    v[RULE_VIEW] = 0;
    rules_next(v, NULL);
    net_on = 1;                                            /* (a session: the host's settings say it) */
    rules_power_on();
    CHECK(rules[RULE_VIEW] == 0);
    net_on = 0;
    rules_widescreen(0);
    rules_power_on();
    CHECK(rules[RULE_VIEW] == 0);
    CHECK(rules_arg("view=wide play") == NULL && rules_arg("view=wide") == NULL && rules_arg("view=3") != NULL && rules_arg("view=0") == NULL);

    /* the game's camera: the target far to the right, the view followed until it stops */
    static const int reach[3] = { 0x3C, 0x3C, VIEW_HPOS }, pic[3] = { 0x3C, VIEW_HPOS, VIEW_HPOS };
    for (int view = 0; view < 3; view++) {
        classic_rules();
        rules[RULE_VIEW] = (uint8_t)view;
        g.sflags |= HI(sfslock);
        g.xc1 = 0x100; g.yc1 = 0; g.Hpos = 0; g.Vpos = 0;
        for (int k = 0; k < 200; k++) checkwindow();
        CHECK((int16_t)g.Hpos == reach[view] && view_cam() == pic[view]);
        g.xc1 = (uint16_t)-0x100;
        for (int k = 0; k < 400; k++) checkwindow();
        CHECK((int16_t)g.Hpos == -reach[view] && view_cam() == -pic[view]);
        g.sflags &= (uint16_t)~HI(sfslock);
    }

    /* a pad's goalie $80 right of the view's middle: off screen on the cartridge's 256 (the computer takes him over), on it in
     * wide play's 320 */
    for (int view = 0; view < 3; view++) {
        classic_rules();
        rules[RULE_VIEW] = (uint8_t)view;
        struct SortObj *gk = sortobj(0);
        memset(gk, 0, sizeof *gk);
        gk->pflags = 1 << pfjoycon | 1 << pfalock;          /* (pfalock: no further than the test) */
        gk->Xpos = 0x80; gk->Ypos = 0x100;                    /* (not in front of his net: no stoppage) */
        g.Hpos = 0; g.Vpos = 0x100;
        assgoaliectrl(&g.teams[0], gk);
        CHECK(!(gk->pflags3 & (1 << pf3gctrl)) == (view == 2));
    }
    rules[RULE_VIEW] = 1;
    CHECK(view_play_more() == 0 && view_more() == VIEW_SHIFT);
    rules[RULE_VIEW] = 2;
    CHECK(view_play_more() == VIEW_SHIFT && view_hpos() == VIEW_HPOS);
    rules[RULE_VIEW] = 0;
    CHECK(view_play_more() == 0 && view_more() == 0 && view_hpos() == 0x3C);

    /* the state's part: version 15 keeps 1 and 2; version 14 is it without the goalie's part; version 13's 1 (the wide view played by)
     * loads as 2 */
    classic_rules();
    hold_state_load(NULL);
    referee_state_load(NULL, 0);
    moves_state_load(NULL);
    static uint8_t st[4096];
    for (int view = 0; view < 3; view++) {
        classic_rules();
        rules[RULE_VIEW] = (uint8_t)view;
        size_t n = rules_state_save(NULL, NULL);
        CHECK(n <= sizeof st && rules_state_save(NULL, st) == n && st[0] == 15);
        classic_rules();
        CHECK(rules_state_load(NULL, st, n) == 0 && rules[RULE_VIEW] == view);
        st[0] = 14;
        classic_rules();
        CHECK(rules_state_load(NULL, st, n - GOALIE_STATE_SIZE) == 0 && rules[RULE_VIEW] == view && rules_state_load(NULL, st, n) < 0);
        st[0] = 13;
        classic_rules();
        CHECK(rules_state_load(NULL, st, n - GOALIE_STATE_SIZE) == 0 && rules[RULE_VIEW] == (view ? 2 : 0));
    }
    st[0] = 16;
    CHECK(rules_state_load(NULL, st, rules_state_save(NULL, NULL)) < 0);
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
    referee_avoid();
    referee_pencall();
    referee_skating();
    refnet();
    refpoint();
    hold();
    siren();
    fight_ratings();
    penalties();
    moves();
    ticker();
    defaults();
    wide_view();
    printf("rulestest: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
