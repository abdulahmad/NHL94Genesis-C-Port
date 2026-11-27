/* RULE_TICKER_HL: the score ticker's highlights from the pregame.
 *
 * The cartridge's InitScores (scoreboard94.asm, the same in NHLPA 93 and NHL 92) plays randomd0(3) = 0-2 periods of each other
 * game before the pregame, and PeriodOver_sp's UpdateScores one more, so at the pregame a game is at most in its 3rd period:
 * SetScore asks for a highlight only when it runs on a game already at period 3, so the first highlight can come at the first
 * intermission. With the rule (1) InitScores plays 0-3 periods: a game at period 3 meets the pregame's UpdateScores, which sends
 * it to overtime when it is within a goal (gsfhl), and DoHiLights plays it at the Start Game screen. 0 is the cartridge's
 * (native/vanilla/mods.c). The draw is still the game's randomd0, on the game's own seed, so netplay and states stay in step. */
#include "game.h"
#include "rules.h"

uint16_t ticker_init_periods(void)
{
    return RULE(RULE_TICKER_HL) ? 4 : 3;
}
