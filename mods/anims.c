/* The animations NHL 95's moves and the PC goalie need (mods/moves.c, mods/goalie.c), in the 94 cartridge's SPAlist format (8 direction
 * offsets from the table, an attribute word, then frame / time pairs ending in a negative time; updateanim reads them through
 * spa_table). The fake shots and the drop passes are the cartridge's own swing frames (NHL 95 has the same ones). The blocks and the
 * goalie's saves and hop over the boards are pictures the 94 cartridge does not have: NHL 95 PC's (frames.asm SPAblockleft ..
 * SPAblockknee, SPAgwallright, SPAgofallback .. SPAgofallstick, SPAgoalieknee), drawn from the PC game's files when the host has them
 * (native/pcspr.c asks mod_frame_pc). Each such frame gets a number of its own from MODF_BASE, and stands for a cartridge frame of the
 * same kind (mod_frame): the slides for falling forward, the blocks for the duck, the goalie's for his stick, glove and stack saves,
 * the hop for the skaters' hop. That frame's art is what a host without the PC's files shows, its hot spot is where the stick is
 * (GetHot) and a goalie's save odds are its (puckgoalie). The numbers are given in one fixed order, so every machine has the same. */
#include "game.h"
#include "rom.h"
#include "rules.h"
#include "symbols_gen.h"
#include "anim_gen.h"
#include "anims.h"
#include <string.h>

#define MODF_MAX (0x400 - MODF_BASE)                      /* (a replay keeps a frame in 10 bits) */
static uint16_t mf_pc[MODF_MAX], mf_cart[MODF_MAX];
static int mf_n;
static uint8_t tab[MA_COUNT][MA_STRIDE];
static uint8_t *cur;
static int cur_at;

static void put(uint8_t *t, int at, int v) { t[at] = (uint8_t)((uint16_t)v >> 8); t[at + 1] = (uint8_t)v; }

/* the mod frame of PC picture pc, standing for cartridge frame cart (the first one given for a picture keeps it) */
static int mf(int pc, int cart)
{
    for (int i = 0; i < mf_n; i++) if (mf_pc[i] == pc) return MODF_BASE + i;
    if (mf_n >= MODF_MAX) return cart;                    /* (never: anims_init gives 141) */
    mf_pc[mf_n] = (uint16_t)pc;
    mf_cart[mf_n] = (uint16_t)cart;
    return MODF_BASE + mf_n++;
}

/* the cartridge's frame k of animation spa in direction d (its SPAlist) */
static int cart_frame(uint16_t spa, int d, int k)
{
    const uint8_t *t = rom_ptr(SYM_SPAlist + spa);
    const uint8_t *l = t + (int16_t)rd16(t + d * 2);
    return rd16(l + k * 4) & 0x7FF;
}

static void begin(int n) { cur = tab[n]; memset(cur, 0, MA_STRIDE); cur_at = 0x12; put(cur, 0x10, 0); }
static void dir(int d) { put(cur, d * 2, cur_at); }
static void add(int fr, int t) { if (cur_at + 4 <= MA_STRIDE) { put(cur, cur_at, fr); put(cur, cur_at + 2, t); cur_at += 4; } }

/* the PC's block pictures, by direction: in front (f0, f1) and on a knee (k0, k1) (frames.asm SPAblockfront, SPAblockknee) */
static const int16_t f0[8] = { 1104, 1107, 1110, 1113, 1116, 1119, 1122, 1125 }, f1[8] = { 1105, 1108, 1111, 1114, 1117, 1120, 1122, 1126 };
static const int16_t k0[8] = { 1105, 1108, 1111, 1114, 1117, 1120, 1122, 1125 }, k1[8] = { 1106, 1109, 1112, 1115, 1118, 1121, 1124, 1127 };
/* 95G's SPAblock gets up through its fall forward frame 4 of these directions, then frame 5 of its own (frames95.asm) */
static const int8_t up_dir[8] = { 7, 0, 1, 2, 3, 6, 7, 0 };
/* the PC goalie's fall to the glove and to the stick, by direction: the first of its 3 pictures, and the stance it ends in */
static const int16_t fg0[8] = { 1028, 1031, 1034, 1037, 1040, 1046, 1043, 1049 }, fg_end[8] = { 0, 3, 6, 9, 12, 18, 15, 21 };

static void anims_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    const int sw = SPFswing, G = SPFGoalie;
    /* NHL 95 PC's fake shots: the wind up back to the start, then standing with the puck (frames.asm SPAfakeshotf / b) */
    static const int8_t pcf[6] = { 3, 2, 1, 0, 1, 2 }, pcb[6] = { 3, 4, 5, 5, 5, 4 };
    begin(MA_FAKEF_PC);
    for (int d = 0; d < 8; d++) { dir(d); for (int k = 0; k < 6; k++) add(sw + 6 * d + pcf[k], 4); add(SPFskatewp + 5 * d, -4); }
    begin(MA_FAKEB_PC);
    for (int d = 0; d < 8; d++) { dir(d); for (int k = 0; k < 6; k++) add(sw + 6 * d + pcb[k], 4); add(SPFskatewp + 5 * d, -4); }
    /* NHL 95 Genesis's: the whole swing, faster, ending without the puck leaving (frames95.asm SPAfakeshotf / b) */
    static const int8_t gf[9] = { 2, 1, 0, 0, 1, 2, 3, 4, 5 }, gb[9] = { 3, 4, 5, 5, 5, 4, 3, 2, 1 };
    begin(MA_FAKEF_G);
    for (int d = 0; d < 8; d++) { dir(d); for (int k = 0; k < 9; k++) add(sw + 6 * d + gf[k], k < 8 ? 3 : -8); }
    begin(MA_FAKEB_G);
    for (int d = 0; d < 8; d++) { dir(d); for (int k = 0; k < 9; k++) add(sw + 6 * d + gb[k], k < 8 ? 3 : -8); }
    /* 95G's drop passes (SPAdroppass; SPApassb2 is 95's own backhand swing, the 94 cartridge's backhand is its forehand frames
     * back to front, as SPApassb) */
    begin(MA_DROP_G);
    for (int d = 0; d < 8; d++) { dir(d); add(sw + 6 * d + 2, 4); add(sw + 6 * d + 3, -20); }
    begin(MA_DROPB_G);
    for (int d = 0; d < 8; d++) { dir(d); add(sw + 6 * d + 3, 4); add(sw + 6 * d + 2, 4); add(sw + 6 * d + 2, -10); }
    /* the PC's slides: down on the ice, up again, a crouch, skating (frames.asm SPAblockleft / right) */
    begin(MA_BLOCKL);
    for (int d = 0; d < 8; d++) {
        int s = SPFfallfwd + 4 * d + 1;
        dir(d); add(mf(1072 + 2 * d, s), 8); add(mf(1073 + 2 * d, s + 1), 24);
        add(SPFfallfwd + 4 * ((d + 2) & 7) + 3, 8); add(SPFduck + ((d + 2) & 7), 8); add(SPFskate + 5 * ((d + 1) & 7), -4);
    }
    begin(MA_BLOCKR);
    for (int d = 0; d < 8; d++) {
        int s = SPFfallfwd + 4 * d + 1;
        dir(d); add(mf(1088 + 2 * d, s), 8); add(mf(1089 + 2 * d, s + 1), 24);
        add(SPFfallfwd + 4 * ((d + 6) & 7) + 3, 8); add(SPFduck + ((d + 6) & 7), 8); add(SPFskate + 5 * ((d + 7) & 7), -4);
    }
    begin(MA_BLOCKF);
    for (int d = 0; d < 8; d++) { dir(d); add(mf(f0[d], SPFduck + d), 8); add(mf(f1[d], SPFduck + d), 20); add(mf(f0[d], SPFduck + d), -8); }
    begin(MA_BLOCKK);
    for (int d = 0; d < 8; d++) { dir(d); add(mf(k0[d], SPFduck + d), 8); add(mf(k1[d], SPFduck + d), 24); add(mf(k0[d], SPFduck + d), -8); }
    /* 95G's block (SPAblock: down, held 40, then up through the fall forward), in the PC's block pictures */
    begin(MA_BLOCK_G);
    for (int d = 0; d < 8; d++) {
        dir(d); add(mf(f0[d], SPFduck + d), 4); add(mf(f1[d], SPFduck + d), 4); add(mf(k1[d], SPFduck + d), 40);
        add(SPFfallfwd + 4 * up_dir[d] + 3, 8); add(SPFduck + d, -8);
    }
    /* the PC goalie's hop over the boards: the skaters' hop in the cartridge (SPAwallright .1, frames 377-380) */
    begin(MA_GWALLR);
    for (int d = 0; d < 8; d++) { dir(d); for (int k = 0; k < 4; k++) { int j = d < 3 ? k : 3 - k; add(mf(372 + j, SPFPen + j), k < 3 ? 8 : -8); } }
    begin(MA_GWALLL);
    for (int d = 0; d < 8; d++) { dir(d); for (int k = 0; k < 4; k++) { int j = d < 3 ? 3 - k : k; add(mf(372 + j, SPFPen + j), k < 3 ? 8 : -8); } }
    /* the PC goalie's saves (frames.asm), each standing for the cartridge's save of that kind in its direction */
    begin(MA_GFALLB);
    for (int d = 0; d < 8; d++) { dir(d); add(mf(1012 + 2 * d, cart_frame(SPAgstackr, d, 0)), 8); add(mf(1013 + 2 * d, cart_frame(SPAgstackr, d, 1)), 20); add(G + 3 * d, -4); }
    begin(MA_GFALLG);
    for (int d = 0; d < 8; d++) {
        int s = cart_frame(SPAgstickl, d, 0);
        dir(d); add(mf(fg0[d], s), 4); add(mf(fg0[d] + 1, s), 4); add(mf(fg0[d] + 2, s), 20); add(G + fg_end[d], -4);
    }
    begin(MA_GFALLS);
    for (int d = 0; d < 8; d++) {
        static const int16_t fs[8][3] = { { 1052, 1053, 1054 }, { 1055, 1056, 1057 }, { 1058, 1059, 1060 }, { 1061, 1062, 1063 },
                                          { 1064, 1065, 1066 }, { 1067, 1068, 1069 }, { 1070, 1071, 974 }, { 975, 976, 977 } };
        int s = cart_frame(SPAgstickr, d, 0);
        dir(d); add(mf(fs[d][0], s), 4); add(mf(fs[d][1], s), 4); add(mf(fs[d][2], s), 20); add(G + 3 * d, -4);
    }
    begin(MA_GKNEE);
    for (int d = 0; d < 8; d++) {
        int s = cart_frame(SPAgstickr, d, 0);
        dir(d);
        if (d < 2) {
            int a = 994 + 3 * d;
            add(mf(a, s), 6); add(mf(a + 1, s), 6); add(mf(a + 2, s), 20); add(mf(a + 1, s), 6); add(mf(a, s), 6); add(G + 3 * d, -8);
        } else {
            int a = 1000 + 2 * (d - 2);
            add(G + 3 * d, 6); add(mf(a, s), 6); add(mf(a + 1, s), 20); add(mf(a, s), 6); add(G + 3 * d, 4); add(G + 3 * d, -4);
        }
    }
}

const uint8_t *mod_spa_table(uint16_t spa)
{
    anims_init();
    int n = ma_of(spa);
    return n >= 0 ? tab[n] : tab[0];
}

uint16_t mod_frame(uint16_t fr)
{
    if (fr < MODF_BASE || fr >= 0x400) return fr;
    anims_init();
    return fr - MODF_BASE < mf_n ? mf_cart[fr - MODF_BASE] : SPFskatewp;
}

int mod_frame_pc(uint16_t fr)
{
    if (fr < MODF_BASE || fr >= 0x400) return -1;
    anims_init();
    return fr - MODF_BASE < mf_n ? mf_pc[fr - MODF_BASE] : -1;
}
