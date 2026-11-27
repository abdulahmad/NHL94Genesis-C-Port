/* RULE_PENALTIES: NHL 96 Genesis's double minors, and the NHL's way of serving them and a major.
 *
 * 96G (NHL96Genesis penalty96.asm DoubleMinorChk, logic96.asm DoubleMinorPenalties, called from collide96.asm's
 * checking code after PenShotChk): a check that knocks a skater down and draws Roughing or Cross Check, and is not a
 * penalty shot, is called as its double minor, 4 minutes (96G's PenDatap34 / PenDatap36, ttmm $0404). The 94
 * cartridge draws the same two penalties at the same place (collide94.asm: $1A or $1C by the H counter, then
 * PenShotChk), and nothing else of its game draws them, so here the two keep their numbers and only their minutes
 * change: pen_minutes is a pure function of the penalty and the rule, so the call, the penalty box, the stats,
 * the power play clock, a replay and a state all see the same 4 minutes, and the referee's text and animation are
 * the cartridge's own Roughing and Cross Check. (RULE_REFEREE's knock down of the referee is Roughing too: it is
 * a double minor under this rule as well.)
 *
 * The rule's values:
 *   1 NHL 96 Genesis: as 96G. A power play goal lets the player out as the cartridge does (PenGoalStuff is the
 *     same in 94G and 96G), so a goal ends a double minor whole.
 *   2 NHL rules: the same double minors, served as the NHL serves them: a power play goal ends only the running
 *     2 minutes of a double minor (more than 2:00 left: the clock goes to 2:00 and he stays in the box), and never
 *     a major (5 minutes: the goal lets out the next player in the box instead). The player's penalty is the last
 *     one the penalty summary (PenSum) logged for him, so nothing is kept here. In the cartridge the only major is
 *     Fighting's, which both fighters take, so it is matched as coincidental and no goal ends it either way; a
 *     major against one team alone is what this changes.
 * 0 is the cartridge's: every hook here gives its own answer (native/vanilla/mods.c has the same). */
#include "game.h"
#include "rom.h"
#include "rules.h"
#include "symbols_gen.h"

#define PenRoughing 0x1A                                  /* (tables94.asm Penaltylist) */
#define PenCrossChk 0x1C

/* the minutes of penalty pen, whose Penaltylist entry has mins (penmins) */
uint8_t pen_minutes(uint16_t pen, uint8_t mins)
{
    if (!RULE(RULE_PENALTIES)) return mins;
    if ((pen == PenRoughing || pen == PenCrossChk) && mins == 2) return 4;   /* (DoubleMinorPenalties: $1A -> $34, $1C -> $36, 4 minutes) */
    return mins;
}

/* the minutes of the penalty PenSum last logged for player pnum of the team (away 0 or 1); 0 when none */
static uint8_t logged_minutes(int away, uint8_t pnum)
{
    int last = -1, len = g.PenSumLength;
    int top = len >= 0xEC ? 0xEC : len - 4;               /* (InProgress: once full, the newest is written over at $EC) */
    for (int k = 0; k <= top && k + 4 <= (int)sizeof g.PenSum; k += 4) {
        const uint8_t *e = g.PenSum + k;
        if (((e[2] & 0x80) != 0) == (away != 0) && e[3] == pnum) last = e[2] & 0x7F;
    }
    if (last < 0) return 0;
    const uint8_t *t = rom_ptr(SYM_Penaltylist);
    return pen_minutes((uint16_t)last, t[rd16(t + last) + 1]);
}

/* a power play goal on team tm, whose player box (his roster offset * 2) is in the box with st his tmpdst word
 * (bit 14 coincidental, bit 13 new, bits 0-10 the seconds left): -1 let him out (the cartridge's), -2 not him (look
 * at the next one), else he stays and the goal is counted: his tmpdst word from now (the rest of his penalty) */
int32_t pen_goal_keep(const struct Team *tm, uint8_t box, uint16_t st)
{
    if (RULE(RULE_PENALTIES) != 2) return -1;
    uint8_t mins = logged_minutes(tm == team_struct(1), (uint8_t)(box >> 1));
    if (mins >= 5) return -2;                             /* a major: a goal does not end it */
    if (mins == 4 && (st & 0x7FF) > 2 * 60) return (st & ~0x7FF) | 2 * 60;   /* a double minor's first 2 minutes: they end */
    return -1;
}
