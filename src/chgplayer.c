/* chgplayer94.asm: chgplayer and the far thunks. The 92/93 changeplayer body is in logic.c. */
#include "game.h"
#include "rom.h"

static inline int16_t puckx(void) { return (int16_t)g.puckx[0]; }

/* chgplayer94.asm:rtsChgplayer. chgplayer's early exit. */
void rtsChgplayer(void) {}

/* chgplayer94.asm:swpchk. jmp Sweepcheck. */
void swpchk(struct SortObj *o) { Sweepcheck(o); }

/* chgplayer94.asm:j_setc1player. jmp setc1player. */
void j_setc1player(uint16_t player) { setc1player(player); }

/* chgplayer94.asm:j_setc2player. jmp setc2player. */
void j_setc2player(uint16_t player) { setc2player(player); }

/* chgplayer94.asm:chgplayer. Pad d4 takes the free skater of its team nearest to where the puck
 * is going (not a goalie, not unavailable, locked or on the other pad, and in a penalty shot
 * or shootout only the shooter or the goalie). When that is its own player, a3 sweep checks.
 * If nobody qualifies the 68k compares a register the caller left (d6); the C does nothing. */
void chgplayer(uint16_t pad, struct SortObj *o)
{
    if (g.sflags5 & 0x40) { rtsChgplayer(); return; }    /* never set */
    int16_t tx = (int16_t)(((int16_t)g.puckvx >> 8) + puckx());
    int16_t ty = (int16_t)(((int16_t)g.puckvy >> 8) + (int16_t)g.pucky);
    struct SortObj *pl = sortobj((pad ? g.cont2team : g.cont1team) == 1 ? 0 : 6);   /* (pad: 0 or 2, the word of pad 1 or 2) */
    uint16_t mine = pad ? g.c2playernum : g.c1playernum, other = pad ? g.c1playernum : g.c2playernum;
    uint32_t best = 0xFFFFFFFF;
    int found = 0;
    uint16_t pick = 0;
    for (int i = 0; i < 6; i++, pl++) {
        if (pl->position <= 0) continue;                 /* not the goalie */
        if (pl->pflags2 & 4) continue;                   /* unavailable */
        if (pl->SCnum != (int16_t)mine && (pl->pflags & (1 << pfjoycon))) continue;
        if ((g.BA_PS_flags & HI(2)) && pl->SCnum != (int16_t)g.BA_Sktr_SCnum && pl->SCnum != (int16_t)g.BA_Goalie_SCnum) continue;
        if (pl->pflags & (1 << pfalock)) continue;
        int16_t dx = (int16_t)(tx - pl->Xpos), dy = (int16_t)(ty - pl->Ypos);
        uint32_t dist = (uint32_t)((int32_t)dx * dx) + (uint32_t)((int32_t)dy * dy);
        if (dist > best) continue;
        if (pl->SCnum == (int16_t)other) continue;
        best = dist;
        pick = (uint16_t)pl->SCnum;
        found = 1;
    }
    if (!found) return;
    if (pick == mine) swpchk(o);
    else if (pad == 0) j_setc1player(pick);
    else j_setc2player(pick);
}

