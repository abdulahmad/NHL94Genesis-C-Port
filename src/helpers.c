/* helpers94.asm: leftover helpers packed before the title screen. */
#include "game.h"
#include "rom.h"
#include "symbols_gen.h"
#include "anim_gen.h"

/* helpers94.asm:SetSflags8Bit1. Sets sflags8 bit 1 (the bclr after the bra.w is never reached). */
void SetSflags8Bit1(void) { g.sflags8 |= 2; }

/* helpers94.asm:AttribAdjust. An attribute under $32 becomes d0 / 2 + $19. */
uint16_t AttribAdjust(uint16_t v)
{
    if ((int16_t)v >= 0x32) return v;
    return (uint16_t)(((int16_t)v >> 1) + 0x19);
}

/* helpers94.asm:ReadGoaliePulled. -> the flags of tst.w on the goalie word of a3's team (Z: the
 * goalie is pulled). */
uint8_t ReadGoaliePulled(struct SortObj *o)
{
    uint16_t v = team_struct(o->pflags & (1 << pfteam))->goalie;
    return (uint8_t)((v == 0 ? CCR_Z : 0) | (v & 0x8000 ? CCR_N : 0));
}

/* helpers94.asm:EndOneTimer. End player a3's one-timer: flags cleared, onetimerplayer = none,
 * glide, then assexit (goalie) or Setplass, and restart the animation. */
void EndOneTimer(struct SortObj *o)
{
    o->pflags3 &= (uint8_t)~(1 << pf3onetimer);
    o->pflags &= (uint8_t)~0x20;
    o->pflags2 &= (uint8_t)~2;
    g.onetimerflags = 0;
    ST_W(g.onetimerplayer);
    SetSPA(o, SPAglide);
    if (o->position < 0) assexit(o);
    else Setplass(o);
    o->SPAnum = 0;
    ST_W(o->SPAcnt);
}

