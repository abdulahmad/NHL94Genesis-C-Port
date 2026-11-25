/* Game rules (rules.h): Classic until the platform sets them at power on. */
#include <string.h>
#include "game.h"
#include "rules.h"

const uint8_t rule_classic[RULE_COUNT] = RULES_CLASSIC;
uint8_t rules[RULE_COUNT] = RULES_CLASSIC;

void cd_colours(uint16_t art, int away, uint16_t c[16])
{
    /* (team art, side, index, the cartridge's colour, the disc's: HOCKEY.BIN's team palettes) */
    static const struct { uint8_t art, away, i; uint16_t cart, disc; } diff[] = {
        { 9, 0, 9, 0x042, 0x264 },                        /* HFD home: the stripes a lighter green */
        { 15, 1, 4, 0x002, 0x200 }, { 15, 1, 5, 0x004, 0x200 }, { 15, 1, 8, 0x028, 0x006 },   /* OTW away: black, */
        { 15, 1, 10, 0x002, 0x000 }, { 15, 1, 11, 0x222, 0x000 }, { 15, 1, 12, 0x444, 0x000 },   /* dark blue pants */
    };
    for (unsigned k = 0; k < sizeof diff / sizeof diff[0]; k++)
        if (diff[k].art == art && diff[k].away == away && c[diff[k].i] == diff[k].cart) c[diff[k].i] = diff[k].disc;
}
