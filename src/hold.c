/* The pause for the announcer (RULE_HOLD). NHL 95 PC stops play after a goal or a penalty for as long as its
 * announcer takes ("Los Angeles goal number 99, assisted by number 4 and number 7"); the cartridge goes
 * straight back to the face off. With the rule on, the face off's drop waits until that long has passed since
 * the stoppage began (the clips' lengths in 95 PC's XBRUCE2.VIV, fixed here: the game must not depend on
 * whether a machine has the file). What the game already spends on the replay and the face off set up counts
 * towards it. */
#include "game.h"
#include "rules.h"

static uint16_t until;                                /* the vblank (g.Vcount) the wait ends at */
static int pending;

void hold_for(int frames)
{
    if (!rules[RULE_HOLD]) return;
    if (frames > 900) frames = 900;
    if (pending && (int16_t)(until - g.Vcount) > 0) until = (uint16_t)(until + frames);
    else until = (uint16_t)(g.Vcount + frames);
    pending = 1;
}

int hold_wait(void)
{
    if (!pending) return 0;
    if (rules[RULE_HOLD] && (int16_t)(until - g.Vcount) > 0) return 1;
    pending = 0;
    return 0;
}

void hold_reset(void) { pending = 0; }
