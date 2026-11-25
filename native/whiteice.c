/* White ice option. The cartridge draws the rink in one pale cyan ($EE8) with a lighter sheen ($EEC);
 * the PC games' ice is white. Done on the finished picture, so nothing in the game changes: frame
 * hashes, saved states, replays and online play are the same with it on or off. Only while a game's
 * rink is on the screen (the same colour in a menu is left alone). */
#include "whiteice.h"
#include "../src/game.h"

static volatile int on;

void whiteice_set(int v) { on = v != 0; }
int whiteice_on(void) { return on; }

#define ICE  0xFF88EEEEu                   /* the cartridge's ice (136, 238, 238) */
#define SHEEN 0xFFCCEEEEu                  /* the glass's sheen over it (204, 238, 238) */
#define WHITE_ICE 0xFFE2E8EEu              /* (226, 232, 238): white with the faintest cool tone */
#define WHITE_SHEEN 0xFFFFFFFFu

/* (a game: the graphics are ready (dfok) in the 32 column mode; the opening film has the mode without them) */
static int rink_up(void) { return g.demoflag && (g.disflags & HI(dfok)) && (g.disflags & HI(df32c)); }

uint32_t whiteice_rgb(uint32_t c)
{
    if (!on || !rink_up()) return c;
    if ((c | 0xFF000000u) == ICE) return WHITE_ICE;
    if ((c | 0xFF000000u) == SHEEN) return WHITE_SHEEN;
    return c;
}

void whiteice_present(uint32_t *pic, int w, int h, int pitch)
{
    if (!on || !rink_up()) return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t c = pic[(long)y * pitch + x];
            if (c == ICE) pic[(long)y * pitch + x] = WHITE_ICE;
            else if (c == SHEEN) pic[(long)y * pitch + x] = WHITE_SHEEN;
        }
}
