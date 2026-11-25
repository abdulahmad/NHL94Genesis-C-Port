/* Colour player photos (the Game page's "Colour player photos"): a player's grey picture on his card
 * and on the scouting report shown in colour, from the same photo in the player's own NHL 95 PC or
 * NHL 94 PC files (native/cardcolor.c). */
#pragma once
#include <stdint.h>
#include "mdrt/vdp.h"

void cardcolor_start(void);                /* looks for the PC files (a thread of its own) and sets the game's hook */
void cardcolor_wait(void);                 /* (headless runs) until the search is over */
int cardcolor_searched(void);
int cardcolor_found(void);                 /* bit 0: 95 PC portraits, bit 1: 94 PC */
void cardcolor_set(int on);
/* 1: team's roster player's card photo will be in colour (so the Sega CD's photo stays out of its way) */
int cardcolor_covers(uint16_t team, uint16_t player);
/* the frame pic (w x h, ARGB, rows pitch apart) with the shown pictures in colour; nothing to the game or the VDP */
void cardcolor_present(const mdrt_vdp *v, uint32_t *pic, int w, int h, int pitch);
