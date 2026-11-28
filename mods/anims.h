/* The animations of mods/moves.c and mods/goalie.c, built in mods/anims.c: each is SPA MOD_SPA + its number * MA_STRIDE (src/rules.h) */
#pragma once
#include "rules.h"

enum {
    MA_FAKEF_PC, MA_FAKEB_PC,             /* NHL 95 PC's fake shots, forehand and backhand (SPAfakeshotf, SPAfakeshotb) */
    MA_FAKEF_G, MA_FAKEB_G,               /* NHL 95 Genesis's (frames95.asm SPAfakeshotf, SPAfakeshotb) */
    MA_DROP_G, MA_DROPB_G,                /* 95G's drop pass, forehand and backhand (SPAdroppass, SPApassb2) */
    MA_BLOCK_G,                           /* 95G's shot block (SPAblock), in the PC's pictures of a block */
    MA_BLOCKL, MA_BLOCKR, MA_BLOCKF, MA_BLOCKK,   /* NHL 95 PC's slides left and right, the block in front, on a knee */
    MA_GWALLR, MA_GWALLL,                 /* the PC goalie over the boards (SPAgwallright, SPAgwallleft) */
    MA_GFALLB, MA_GFALLG, MA_GFALLS, MA_GKNEE,   /* the PC goalie's saves: falling back, to the glove, to the stick; the knee */
    MA_GPOKE, MA_GKNEEHOLD,               /* the PC goalie's poke check (SPAgoaliepoke), the knee he freezes the puck on (SPAgokneehold) */
    MA_COUNT
};
#define MA_STRIDE 0x200                       /* (the table bytes each: 95G's fake shots take 306) */
#define MA_SPA(n) ((uint16_t)(MOD_SPA + (n) * MA_STRIDE))
static inline int ma_of(uint16_t spa) { return spa >= MOD_SPA && spa < MOD_SPA + MA_COUNT * MA_STRIDE ? (int)((spa - MOD_SPA) / MA_STRIDE) : -1; }
