/* RULE_GOALIE: the goalie in NHL 95 PC's animations (mods/anims.c, the PC's pictures).
 *
 * 1 NHL 95 PC (goalie/assgoalie.c's save, frames.asm): a puck coming straight at him and slow falls him back (SPAgofallback); a low
 *   puck wide of him drops him to the side, his stick's or his glove's (SPAgofallstick, SPAgofallglove); any other save is the
 *   cartridge's. He goes over the boards in his own hop (SPAgwallright, SPAgwallleft), not the skaters'.
 * 2 also NHL 96 Genesis's (puck.asm goaliesave): a puck at most 2 from him across, not rising fast, is blocked falling back above
 *   $600, caught on a knee below (96G's SPAgblock and SPAgcatchlow, in the PC's SPAgofallback and SPAgoalieknee).
 * 0 is the cartridge's (native/vanilla/mods.c). The save is still the cartridge's choice of when and where (goaliesave); only its
 * animation changes, and the save odds are those of the cartridge's frame each picture stands for. */
#include "game.h"
#include "rules.h"
#include "anim_gen.h"
#include "anims.h"
#include <stdlib.h>

static int right_side(uint16_t spa)
{
    return spa == SPAgglover || spa == SPAgstackr || spa == SPAgstickr || spa == SPAghighr || spa == SPAgstick2r;
}

uint16_t goalie_save_spa(uint16_t spa, int16_t d3, const struct PuckCross *pc, const struct SortObj *gk)
{
    int r = RULE(RULE_GOALIE);
    if (!r) return spa;
    int16_t z = (int16_t)g.puckz[0], vz = (int16_t)g.puckvz[0];
    if (r == 2 && abs(gk->Xpos - pc->x) <= 2 && vz <= 0xC00)
        return MA_SPA(vz > 0x600 ? MA_GFALLB : MA_GKNEE);
    int16_t vx = (int16_t)g.puckvx, vy = (int16_t)g.puckvy;
    if ((d3 & 7) == 4 && abs(vx) + abs(vy) <= 0x1200) return MA_SPA(MA_GFALLB);
    int f = d3 & 7;
    if (f >= 4) f = 7 - f;
    if (f >= 2 && z < 0xA && vz < 0x800) return MA_SPA(right_side(spa) ? MA_GFALLS : MA_GFALLG);
    return spa;
}

uint16_t goalie_wall_spa(const struct SortObj *o, uint16_t spa)
{
    if (!RULE(RULE_GOALIE) || o->position != 0) return spa;
    return MA_SPA(spa == SPAwallright ? MA_GWALLR : MA_GWALLL);
}
