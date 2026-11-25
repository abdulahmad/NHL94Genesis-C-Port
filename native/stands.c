/* Widescreen's sides of the pause and Start Game screens: the horizontal rink's backdrop
 * (IceRinkHor, plane B) carried on 32 pixels a side, as Abdul chose from the mock-ups (2026-11-25):
 *
 *   the roof          its edge column carried on; the stubbled beam (lines 56-63) tiled every 8
 *   the stands        whole fans only, built from the fan block beside the one set of stairs
 *                     (columns 16-47, lines 102-143): two rows of it, the far one 36 lines up,
 *                     mirrored about the stairs so the girl beside them sits the same on each side;
 *                     the half-hidden fan's legs, seat and the scoreboard frame's corner taken out,
 *                     the green-jacket fan of the same row put in the gap; the top row with no legs
 *                     above its heads; the crowd stops under the roof's beam (line 64)
 *   the glass         its edge column carried on
 *   the ice           plane B's own ice tiled every 16 (no plane A: the Start Game ticker band
 *                     stays in the middle)
 *   the sprites       drawn on top past the edges (the Zamboni comes in whole from the side)
 *
 * The right side is the left built from the mirrored backdrop. Made each frame from the VDP. */
#include <string.h>
#include "stands.h"
#include "wide.h"
#include "game.h"

#define Y0 102          /* the fan block's first line */
#define Y1 144          /* the glass's top: the block's end */
#define TOP 64          /* under the roof's beam: the crowd's first line */
#define BEAM 56         /* the beam's lines, 56 to 63 */
#define ICE 165         /* the ice's first line */
#define RH (Y1 - Y0)    /* 42 */

static int r_(uint32_t c) { return (c >> 16) & 0xFF; }
static int g_(uint32_t c) { return (c >> 8) & 0xFF; }
static int b_(uint32_t c) { return c & 0xFF; }
static int empty(uint32_t c) { return (c & 0xFFFFFF) == 0; }
static int greyish(uint32_t c)
{
    int r = r_(c), gg = g_(c), b = b_(c), mx = r > gg ? (r > b ? r : b) : (gg > b ? gg : b), mn = r < gg ? (r < b ? r : b) : (gg < b ? gg : b);
    return mx - mn < 25;
}

/* the pixels not reachable from the block's border through empty ones: the figures, holes filled */
static void fill_holes(uint32_t src[RH][32], uint8_t fig[RH][32])
{
    static uint8_t seen[RH][32];
    static int st[RH * 32 * 4 + 256][2];
    int n = 0;
    memset(seen, 0, sizeof seen);
    for (int y = 0; y < RH; y++) { st[n][0] = y; st[n++][1] = 0; st[n][0] = y; st[n++][1] = 31; }
    for (int x = 0; x < 32; x++) { st[n][0] = 0; st[n++][1] = x; st[n][0] = RH - 1; st[n++][1] = x; }
    while (n) {
        n--;
        int y = st[n][0], x = st[n][1];
        if (y < 0 || y >= RH || x < 0 || x >= 32 || seen[y][x] || !empty(src[y][x])) continue;
        seen[y][x] = 1;
        st[n][0] = y + 1; st[n++][1] = x;
        st[n][0] = y - 1; st[n++][1] = x;
        st[n][0] = y; st[n++][1] = x + 1;
        st[n][0] = y; st[n++][1] = x - 1;
    }
    for (int y = 0; y < RH; y++) for (int x = 0; x < 32; x++) fig[y][x] = !seen[y][x];
}

/* one side's stands (Y1 - TOP lines x 32), column 0 beside the stairs. bg: the backdrop's 256
 * columns of lines 0 to Y1, left as it is (the left side) or mirrored (the right) */
static void stack(uint32_t bg[Y1][256], int left, uint32_t out[Y1 - TOP][32])
{
    static uint32_t src0[RH][256], src[RH][32];
    static uint8_t fig[RH][32], topfig[RH][32];
    for (int y = 0; y < RH; y++) for (int x = 0; x < 256; x++) src0[y][x] = bg[Y0 + y][left ? x : 255 - x];
    for (int y = 0; y < RH; y++) for (int x = 0; x < 32; x++) src[y][x] = src0[y][16 + x];
    for (int y = 0; y < 18; y++) for (int x = 0; x < 8; x++) src[y][x] = 0;          /* the half-hidden fan's legs and seat */
    for (int x = 0; x < 32; x++) src[0][x] = 0;                                     /* the scoreboard frame's bottom edge */
    static uint8_t blue[RH][32];
    for (int y = 0; y < RH; y++) for (int x = 0; x < 32; x++) blue[y][x] = b_(src[y][x]) > r_(src[y][x]) + 40;
    for (int y = 16; y < 28; y++) for (int x = 0; x < 6; x++) if (blue[y][x]) src[y][x] = 0;   /* his blue trouser leg */
    for (int y = 0; y < 20; y++)                                                    /* the green-jacket fan of the row in the gap */
        for (int x = 0; x < 8; x++) {
            uint32_t gj = src0[y][71 + x];
            if (!empty(gj) && !greyish(gj) && empty(src[y][x])) src[y][x] = gj;
        }
    for (int y = 0; y < 6; y++) for (int x = 5; x < 11; x++) if (greyish(src[y][x])) src[y][x] = 0;   /* the frame's corner by the hat */
    fill_holes(src, fig);
    memcpy(topfig, fig, sizeof fig);                                                /* the top row: no legs above the heads */
    for (int y = 0; y < 8; y++) for (int x = 17; x < 22; x++) topfig[y][x] = 0;
    for (int y = 0; y < 10; y++) for (int x = 28; x < 32; x++) topfig[y][x] = 0;
    for (int y = 0; y < 4; y++) for (int x = 22; x < 28; x++) if (blue[y][x]) topfig[y][x] = 0;
    memset(out, 0, sizeof(uint32_t) * (Y1 - TOP) * 32);
    static const int shifts[2] = { -36, 0 };                                        /* the far row first, the near one over it */
    for (int i = 0; i < 2; i++)
        for (int rr = 0; rr < RH; rr++) {
            int y = Y0 + rr + shifts[i] - TOP;
            if (y < 0 || y >= Y1 - TOP) continue;
            for (int x = 0; x < 32; x++) if ((i ? fig : topfig)[rr][x]) out[y][x] = src[rr][x];
        }
}

int stands_frame(const mdrt_vdp *v, int M, int h, uint32_t *out, int opitch)
{
    if (M != 32 || !(g.sflags & HI(sfhor)) || h < ICE) return 0;
    static uint8_t idx[WIDE_MAX_W];
    static uint32_t bg[224][256], side[224][2][32];
    int w = 256, vm = mdrt_vdp_width(v) == 320 ? 0 : M;   /* (the wide view's horizontal rink: 256 in the middle of 320, its sides there) */
    for (int y = 0; y < h && y < 224; y++) {
        wide_line(v, y, vm, WIDE_B, idx);
        for (int x = 0; x < w; x++) bg[y][x] = wide_rgb(v, idx[M + x]);
    }
    static uint32_t ls[Y1 - TOP][32], rs[Y1 - TOP][32];
    stack(bg, 1, ls);
    stack(bg, 0, rs);
    for (int y = 0; y < h; y++) {
        const uint32_t *a = out + (long)y * opitch + M;                             /* the game's picture */
        for (int x = 0; x < M; x++) {
            int xl = x - M, xr = w + x;                                             /* screen columns */
            uint32_t L, R;
            if (y >= BEAM && y < TOP) { L = a[((xl % 8) + 8) % 8]; R = a[248 + (((xr - 248) % 8) + 8) % 8]; }
            else if (y < TOP || (y >= Y1 && y < ICE)) { L = a[0]; R = a[w - 1]; }
            else if (y >= ICE) { L = bg[y][176 + (((xl - 176) % 16) + 16) % 16]; R = bg[y][176 + (((xr - 176) % 16) + 16) % 16]; }
            else { L = ls[y - TOP][M - 1 - x]; R = rs[y - TOP][x]; }
            side[y][0][x] = L | 0xFF000000u;
            side[y][1][x] = R | 0xFF000000u;
        }
    }
    for (int y = 0; y < h; y++) {                                                   /* the sprites past the edges, on top */
        uint32_t *o = out + (long)y * opitch;
        wide_line(v, y, vm, WIDE_S, idx);
        for (int x = 0; x < M; x++) {
            o[x] = idx[x] ? wide_rgb(v, idx[x]) : side[y][0][x];
            o[w + M + x] = idx[w + M + x] ? wide_rgb(v, idx[w + M + x]) : side[y][1][x];
        }
    }
    return 1;
}
