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
#define BRIGHT 0xFFEEEEEEu                 /* the starbursts' centres (238, 238, 238) */
#define WHITE_ICE 0xFFDAE1E8u              /* (218, 225, 232): white ice with the faintest cool tone */
#define WHITE_SHEEN 0xFFEEF3F8u            /* (238, 243, 248): the lighter shade of the glare */
#define WHITE_BRIGHT 0xFFFFFFFFu           /* (255, 255, 255): the lighter still, as the blue ice has its two */

/* (a game: the graphics are ready (dfok) in the 32 column mode; the opening film has the mode without them) */
static int rink_up(void) { return g.demoflag && (g.disflags & HI(dfok)) && (g.disflags & HI(df32c)); }

uint32_t whiteice_rgb(uint32_t c)
{
    if (!on || !rink_up()) return c;
    if ((c | 0xFF000000u) == ICE) return WHITE_ICE;
    if ((c | 0xFF000000u) == SHEEN) return WHITE_SHEEN;
    return c;                                          /* (BRIGHT needs its neighbours: whiteice_present only) */
}

void whiteice_present(uint32_t *pic, int w, int h, int pitch)
{
    if (!on || !rink_up()) return;
    for (int y = 0; y < h; y++)                          /* (a bright pixel next to the sheen is a starburst's centre; elsewhere it is a box or text) */
        for (int x = 0; x < w; x++) {
            uint32_t c = pic[(long)y * pitch + x];
            if (c != BRIGHT) continue;
            int near_sheen = 0;
            for (int dy = -1; dy <= 1 && !near_sheen; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && xx < w && yy >= 0 && yy < h && pic[(long)yy * pitch + xx] == SHEEN) { near_sheen = 1; break; }
                }
            if (near_sheen) pic[(long)y * pitch + x] = WHITE_BRIGHT;
        }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t c = pic[(long)y * pitch + x];
            if (c == ICE) pic[(long)y * pitch + x] = WHITE_ICE;
            else if (c == SHEEN) pic[(long)y * pitch + x] = WHITE_SHEEN;
        }
}
