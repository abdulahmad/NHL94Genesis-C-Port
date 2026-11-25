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
#include "native.h"

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

int main(void)
{
    const char *rom = getenv("NHL94_ROM");
    if (!rom_load(rom ? rom : "NHL 94 (UE) [!].gen")) { fprintf(stderr, "cannot load ROM (set NHL94_ROM)\n"); return 2; }
    rosters_retail();                                      /* (the roster set's tables, the cartridge's) */
    weights();
    overall_ratings();
    column_ratings();
    sega_cd();
    printf("rulestest: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
