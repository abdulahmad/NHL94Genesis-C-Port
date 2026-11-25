/* Widescreen (wide.h): the game's frame with sides. The middle is always the game's own picture,
 * line for line; the sides are drawn here from the VDP as it stands at the frame's end:
 *
 *   the rink in the wide view (rules.view, 320 columns)   as it is: already wide
 *   the pause and Start Game screens (256, the rink seen   the stands carried on (stands.c)
 *     from the side)
 *   the title picture, the main menu, a 256 rink           black bars
 *   the blue SPORTS screens (320)                          the SPORTS backdrop carried on, tile for
 *                                                           tile (its map repeats every 20 columns a
 *                                                           row down, and 10 columns 4 rows down)
 *   the player cards (320)                                 the same backdrop, in the colours the
 *                                                           picture's own edges have on each line
 *   the Sega CD's screens (drawn by the port)             their edges carried on
 *   any other screen                                       the planes and sprites past the edges
 *
 * Sprites are drawn past the screen's edges too (the Zamboni comes in from the side). */
#include <string.h>
#include "wide.h"
#include "whiteice.h"
#include "game.h"
#include "view.h"
#include "rom.h"
#include "rosters.h"
#include "symbols_gen.h"
#include "stands.h"

static uint16_t vword(const mdrt_vdp *v, uint32_t a)
{
    a &= 0xFFFE;
    return (uint16_t)(v->s.vram[a] << 8 | v->s.vram[a + 1]);
}

static int tile_pixel(const mdrt_vdp *v, uint16_t e, int x, int y)
{
    if (e & 0x0800) x = 7 - x;
    if (e & 0x1000) y = 7 - y;
    uint8_t b = v->s.vram[(uint32_t)((e & 0x7FF) << 5) + (uint32_t)(y << 2) + (uint32_t)(x >> 1)];
    return (x & 1) ? (b & 0xF) : (b >> 4);
}

/* a layer's line, M pixels of side first: colour (palette << 4 | index, 0 clear), priority, and where
 * in its plane the pixel is (WIDE_AT) */
struct layer { uint8_t col[WIDE_MAX_W], pri[WIDE_MAX_W]; uint32_t at[WIDE_MAX_W]; };

static void plane_line(const mdrt_vdp *v, int line, uint32_t table, int hscroll, int plane, int width, int M, struct layer *out)
{
    static const int cells[4] = { 32, 64, 32, 128 };
    int w = cells[v->s.reg[16] & 3], h = cells[(v->s.reg[16] >> 4) & 3];
    int fine = hscroll & 15;
    for (int sx = -M; sx < width + M; sx++) {
        int vs = v->s.vsram[plane];
        if ((v->s.reg[11] & 4) && sx >= fine && sx < width) vs = v->s.vsram[(((sx - fine) >> 4) * 2 + plane) % 40];
        int py = (line + vs) & (h * 8 - 1);
        int px = (sx - hscroll) & (w * 8 - 1);
        uint16_t e = vword(v, table + (uint32_t)(((py >> 3) * w + (px >> 3)) << 1));
        int c = tile_pixel(v, e, px & 7, py & 7);
        out->col[sx + M] = (uint8_t)(c ? (((e >> 13) & 3) << 4) | c : 0);
        out->pri[sx + M] = (uint8_t)(e >> 15);
        out->at[sx + M] = WIDE_AT(plane ? WIDE_FROM_B : WIDE_FROM_A, px, py);
    }
}

/* the sprites on a line, as the VDP takes them, the line's dot budget M a side larger */
static void sprite_line(const mdrt_vdp *v, int line, int width, int M, struct layer *out)
{
    int h40 = width == 320;
    uint32_t base = h40 ? (uint32_t)(v->s.reg[5] & 0x7E) << 9 : (uint32_t)(v->s.reg[5] & 0x7F) << 9;
    int total = h40 ? 80 : 64, per_line = h40 ? 20 : 16, dots = (h40 ? 320 : 256) + 2 * M;
    memset(out->col, 0, sizeof out->col);
    int link = 0, seen = 0, shown = 0, masked = 0;
    while (seen++ < total) {
        const uint8_t *c = v->sat + link * 8;
        int y = ((c[0] << 8 | c[1]) & 0x3FF) - 128;
        int sh = (c[2] & 3) + 1, sw = ((c[2] >> 2) & 3) + 1;
        int next = c[3] & 0x7F;
        if (line >= y && line < y + sh * 8) {
            if (++shown > per_line) break;
            uint32_t s = base + (uint32_t)(link << 3);
            uint16_t e = vword(v, s + 4);
            int rawx = vword(v, s + 6) & 0x1FF, x = rawx - 128;
            if (rawx == 0 && shown > 1) masked = 1;
            int row = line - y;
            if (e & 0x1000) row = sh * 8 - 1 - row;
            for (int cx = 0; cx < sw * 8; cx++) {
                if (dots-- <= 0) break;
                int sx = x + cx;
                if (masked || sx < -M || sx >= width + M || out->col[sx + M]) continue;
                int tx = (e & 0x0800) ? sw * 8 - 1 - cx : cx;
                uint16_t t = (uint16_t)((e & 0x7FF) + (tx >> 3) * sh + (row >> 3));
                int p = tile_pixel(v, (uint16_t)(t & 0x7FF), tx & 7, row & 7);
                if (!p) continue;
                out->col[sx + M] = (uint8_t)((((e >> 13) & 3) << 4) | p);
                out->pri[sx + M] = (uint8_t)(e >> 15);
            }
            if (masked || dots <= 0) break;
        }
        if (!next || next >= total) break;
        link = next;
    }
}

/* one line, M pixels a side past the screen: colour indices (0 the backdrop) into idx, and (at, when
 * given) where each came from. what: WIDE_ALL, WIDE_B (plane B alone) or WIDE_BS (plane B and the sprites) */
static void compose(const mdrt_vdp *v, int line, int M, int what, uint8_t *idx, uint32_t *at)
{
    int width = mdrt_vdp_width(v), ow = width + 2 * M;
    if (!(v->s.reg[1] & 0x40)) { memset(idx, 0, (size_t)ow); return; }
    uint32_t ta = (uint32_t)(v->s.reg[2] & 0x38) << 10, tb = (uint32_t)(v->s.reg[4] & 7) << 13;
    uint32_t tw = width == 320 ? (uint32_t)(v->s.reg[3] & 0x3C) << 10 : (uint32_t)(v->s.reg[3] & 0x3E) << 10;
    uint32_t hs = (uint32_t)(v->s.reg[13] & 0x3F) << 10;
    int wcells = width == 320 ? 64 : 32;
    static struct layer a, b, s;
    int hl = 0;
    if ((v->s.reg[11] & 3) == 2) hl = line & ~7;
    else if ((v->s.reg[11] & 3) == 3) hl = line;
    int hsa = vword(v, hs + (uint32_t)(hl << 2)) & 0x3FF, hsb = vword(v, hs + (uint32_t)(hl << 2) + 2) & 0x3FF;
    plane_line(v, line, tb, hsb, 1, width, M, &b);
    if (what == WIDE_ALL || what == WIDE_A) {
        int wy = (v->s.reg[18] & 0x1F) * 8, w0, w1;
        if ((v->s.reg[18] & 0x80) ? line >= wy : line < wy) { w0 = 0; w1 = width; }
        else {
            int wx = (v->s.reg[17] & 0x1F) * 16;
            if (wx > width) wx = width;
            if (v->s.reg[17] & 0x80) { w0 = wx; w1 = width; } else { w0 = 0; w1 = wx; }
        }
        plane_line(v, line, ta, hsa, 0, width, M, &a);
        for (int x = w0; x < w1; x++) {
            uint16_t e = vword(v, tw + (uint32_t)(((line >> 3) * wcells + (x >> 3)) << 1));
            int c = tile_pixel(v, e, x & 7, line & 7);
            a.col[x + M] = (uint8_t)(c ? (((e >> 13) & 3) << 4) | c : 0);
            a.pri[x + M] = (uint8_t)(e >> 15);
            a.at[x + M] = WIDE_AT(WIDE_FROM_WINDOW, x, line);
        }
    } else memset(a.col, 0, sizeof a.col);
    if (what == WIDE_A) { memcpy(idx, a.col, (size_t)ow); if (at) memcpy(at, a.at, (size_t)ow * sizeof *at); return; }
    if (what == WIDE_S) { sprite_line(v, line, width, M, &s); memcpy(idx, s.col, (size_t)ow); return; }
    if (what != WIDE_B) sprite_line(v, line, width, M, &s);
    else memset(s.col, 0, sizeof s.col);
    for (int x = 0; x < ow; x++) {
        uint8_t c = 0;
        uint32_t from = 0;
        if (s.col[x] && s.pri[x]) c = s.col[x];
        else if (a.col[x] && a.pri[x]) c = a.col[x], from = a.at[x];
        else if (b.col[x] && b.pri[x]) c = b.col[x], from = b.at[x];
        else if (s.col[x]) c = s.col[x];
        else if (a.col[x]) c = a.col[x], from = a.at[x];
        else if (b.col[x]) c = b.col[x], from = b.at[x];
        idx[x] = c;
        if (at) at[x] = from;
    }
}

void wide_line(const mdrt_vdp *v, int line, int M, int what, uint8_t *idx) { compose(v, line, M, what, idx, NULL); }
void wide_line_at(const mdrt_vdp *v, int line, uint8_t *idx, uint32_t *at) { compose(v, line, 0, WIDE_ALL, idx, at); }

uint32_t wide_rgb(const mdrt_vdp *v, uint8_t c)
{
    return whiteice_rgb(0xFF000000u | mdrt_vdp_rgb(c ? v->s.cram[c] : v->s.cram[v->s.reg[7] & 0x3F], MDRT_RGB_GPGX));
}

/* ---- the SPORTS backdrop ---------------------------------------------------------------- */

/* ScoutMap's map (40 x 28): column c of row r is column c - 20 of row r + 1, and column c + 10 of
 * row r + 4. Map row mr, column c (-8 to 47) -> the same place inside the map, in its rows below
 * rmax (28, or 4 for the player cards' picture, whose first 4 rows are ScoutMap's) */
static int lattice_cell(int c, int mr, int top4, int *c2, int *r2)
{
    static signed char t[2][28][56][2];
    static int made;
    if (!made) {
        made = 1;
        for (int k = 0; k < 2; k++)
            for (int r = 0; r < 28; r++)
                for (int cc = -8; cc < 48; cc++) {
                    int best = 1 << 30, bc = -1, br = -1, rmax = k ? 4 : 28;
                    for (int a = -40; a <= 40; a++)
                        for (int b = -12; b <= 12; b++) {
                            int x = cc - 20 * a + 10 * b, y = r + a + 4 * b, d = (a < 0 ? -a : a) + (b < 0 ? -b : b);
                            if (x >= 0 && x < 40 && y >= 0 && y < rmax && d < best) { best = d; bc = x; br = y; }
                        }
                    t[k][r][cc + 8][0] = (signed char)bc;
                    t[k][r][cc + 8][1] = (signed char)br;
                }
    }
    if (c < -8 || c >= 48 || mr < 0 || mr >= 28) return 0;
    *c2 = t[top4][mr][c + 8][0];
    *r2 = t[top4][mr][c + 8][1];
    return *c2 >= 0;
}

static uint32_t map_of(uint32_t img) { return img + rd32(rom_ptr(img + 4)); }
static uint16_t map_entry(uint32_t map, int c, int r) { return rd16(rom_ptr(map + 4 + 2 * (uint32_t)(r * rd16(rom_ptr(map)) + c))); }

/* a full-width picture of map's on plane row pr, still on screen there (its first or last column) */
static const struct view_row *backdrop_row(const mdrt_vdp *v, int pr)
{
    const struct view_row *r = &view_rows[pr & 31];
    if (!r->map || r->colshift != 6) return NULL;
    for (int c = 0; c < 40; c += 39) {
        uint16_t want = (uint16_t)((map_entry(r->map, c, r->maprow) + r->start) ^ r->attr);
        if (vword(v, (uint32_t)r->vram + (uint32_t)((pr << 6) + c) * 2) == want) return r;
    }
    return NULL;
}

/* the backdrop's colour index at screen x of plane row pr (pixel row py), x past the edges */
static uint32_t map_of(uint32_t img);
static uint8_t lattice_pixel(const mdrt_vdp *v, const struct view_row *r, int x, int py)
{
    int c2, r2;
    if (!lattice_cell(x >> 3, r->maprow, 0, &c2, &r2)) return 0;
    uint16_t e = (uint16_t)((map_entry(map_of(ART(ScoutMap)), c2, r2) + r->start) ^ r->attr);
    int p = tile_pixel(v, e, x & 7, py & 7);
    return (uint8_t)(p ? (((e >> 13) & 3) << 4) | p : 0);
}

/* is plane row pr's backdrop the SPORTS one (ScoutMap or the player cards' picture)? */
static int sports_row(const struct view_row *r)
{
    return r && (r->map == map_of(ART(ScoutMap)) || r->map == map_of(ART(PlayerCardsScreenPic)));
}

/* which plane the backdrop is on, for its vertical scroll: 0 A, 1 B, 2 the window (no scroll) */
static int backdrop_plane(const mdrt_vdp *v, const struct view_row *r)
{
    uint32_t ta = (uint32_t)(v->s.reg[2] & 0x38) << 10, tb = (uint32_t)(v->s.reg[4] & 7) << 13;
    return r->vram == ta ? 0 : r->vram == tb ? 1 : 2;
}

/* the sides of a SPORTS screen's line y: the backdrop's tiles; on the player cards, in the colours
 * the picture's own 16 columns at each edge have on that line (its lighter header band, the
 * band's lines) */
/* ScoutMap's tiles, unpacked from the cartridge (the player cards have their own picture's tiles in
 * VRAM): decompress94.asm's byte code into a buffer instead of VRAM */
static uint8_t scout_tiles[0x800 * 32];
static int scout_n;
static void unpack_scout(void)
{
    if (scout_n) return;
    uint32_t t = ART(ScoutMap) + 8;
    uint16_t n = rd16(rom_ptr(t));
    const uint8_t *s = rom_ptr(t + 2);
    long pos = 0, cap = (long)(n & 0x7FFF) * 32;
    if (cap > (long)sizeof scout_tiles) cap = sizeof scout_tiles;
    if (!(n & 0x8000)) { memcpy(scout_tiles, s, (size_t)cap); scout_n = n & 0x7FFF; return; }
#define PUT(b) do { if (pos < cap) scout_tiles[pos] = (uint8_t)(b); pos++; } while (0)
#define AT(i) ((i) >= 0 && (i) < cap ? scout_tiles[i] : 0)
    for (int guard = 0; guard < 1 << 20 && pos < cap; guard++) {
        uint8_t op = *s++, hi = op >> 4;
        int count = 0, dist = 0, rev = 0;
        if (hi <= 1) { for (int k = (op & 0x1F) + 1; k; k--) PUT(*s++); continue; }
        if (hi == 2) { for (int k = (op & 0xF) + 1; k; k--) PUT(0); continue; }
        if (hi == 3) { int k = (op & 0xF) + 3; uint8_t b = *s++; for (; k; k--) PUT(b); continue; }
        if (hi <= 7) { count = (op & 7) + 1; dist = ((op >> 3) & 7) + 1; }
        else if (hi == 8) { count = (op & 0xF) + 2; dist = *s++; }
        else if (hi == 9 || hi == 0xF) { count = (((op << 1) | (s[0] >> 7)) & 0x1F) + 2; dist = (*s++ & 0x7F) + 1; rev = hi == 0xF; }
        else if (hi == 0xA) { uint16_t w = (uint16_t)(op << 8 | s[0]); count = ((w >> 6) & 0x3F) + 2; dist = (*s++ & 0x3F) + 1; }
        else if (hi == 0xB) { uint16_t w = (uint16_t)(op << 8 | s[0]); count = ((w >> 5) & 0x7F) + 2; dist = (*s++ & 0x1F) + 1; }
        else if (hi <= 0xD) { count = (op & 3) + 1; dist = ((op >> 2) & 7) + 1; rev = 1; }
        else { count = (op & 0xF) + 2; dist = *s++; rev = 1; if (!dist) break; }
        long from = pos - dist;
        for (int k = 0; k <= count; k++) { PUT(AT(from)); from += rev ? -1 : 1; }
    }
#undef PUT
#undef AT
    scout_n = (int)(cap / 32);
}

/* the backdrop's colour index at x, line py of plane row pr, from ScoutMap's own tiles */
static uint8_t scout_pixel(const struct view_row *r, int x, int py)
{
    int c2, r2;
    unpack_scout();
    if (!lattice_cell(x >> 3, r->maprow, 0, &c2, &r2)) return 0;
    uint16_t e = map_entry(map_of(ART(ScoutMap)), c2, r2);
    int tx = x & 7, ty = py & 7;
    if (e & 0x0800) tx = 7 - tx;
    if (e & 0x1000) ty = 7 - ty;
    int t = e & 0x7FF;
    if (t >= scout_n) return 0;
    uint8_t b = scout_tiles[t * 32 + ty * 4 + (tx >> 1)];
    int p = (tx & 1) ? (b & 0xF) : (b >> 4);
    return (uint8_t)(p ? (((e >> 13) & 3) << 4) | p : 0);
}

static int sports_line(const mdrt_vdp *v, int y, int M, const uint32_t *mid, int w, uint32_t *o, const uint32_t *pic, int pitch, int h)
{
    int pl = -1;
    const struct view_row *r = NULL;
    for (int k = 0; k < 3 && !r; k++) {
        int vs = k < 2 ? v->s.vsram[k] : 0, pr = ((y + vs) >> 3) & 31;
        const struct view_row *t = backdrop_row(v, pr);
        if (t && backdrop_plane(v, t) == k) { r = t; pl = k; }
    }
    if (!sports_row(r)) return 0;
    int py = (y + (pl < 2 ? v->s.vsram[pl] : 0)) & 255;
    int cards = r->map == map_of(ART(PlayerCardsScreenPic));
    uint32_t lat[WIDE_MAX_W];
    for (int x = -M; x < w + M; x++)
        if (!cards) lat[x + M] = wide_rgb(v, lattice_pixel(v, r, x, py));
        else if (x < 16 || x >= w - 16) lat[x + M] = wide_rgb(v, scout_pixel(r, x, py));
    if (!cards) {
        for (int x = 0; x < M; x++) { o[x] = lat[x]; o[w + M + x] = lat[w + M + x]; }
        return 1;
    }
    /* the player cards: below its header plain; in the header (lines 0-59, the lighter band and its
     * lines) the colours the picture's own 8 columns at each edge have, against the backdrop's,
     * over the 12 lines either side */
    if (y >= 60) {
        for (int x = 0; x < M; x++) { o[x] = lat[x]; o[w + M + x] = lat[w + M + x]; }
        return 1;
    }
    static const int edge[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 312, 313, 314, 315, 316, 317, 318, 319 };
    int same = 0, solid = 1;
    for (int k = 0; k < 16; k++) {
        uint32_t f = mid[edge[k]];
        if (f == wide_rgb(v, scout_pixel(r, edge[k], py))) same++;
        if (f != mid[0]) solid = 0;
    }
    if (solid) { for (int x = 0; x < M; x++) o[x] = o[w + M + x] = mid[0]; return 1; }
    if (same * 10 >= 16 * 6) { for (int x = 0; x < M; x++) { o[x] = lat[x]; o[w + M + x] = lat[w + M + x]; } return 1; }
    uint32_t from[32], to[32];
    int cnt[32], n = 0;
    for (int yy = y - 12; yy <= y + 12; yy++) {
        if (yy < 0 || yy >= 60 || yy >= h) continue;
        const struct view_row *q = backdrop_row(v, yy >> 3);
        if (!q) continue;
        for (int k = 0; k < 16; k++) {
            uint32_t a = wide_rgb(v, scout_pixel(q, edge[k], yy)), b = pic[(long)yy * pitch + edge[k]];
            int i2 = 0;
            while (i2 < n && !(from[i2] == a && to[i2] == b)) i2++;
            if (i2 == n) { if (n == 32) continue; from[n] = a; to[n] = b; cnt[n++] = 0; }
            cnt[i2]++;
        }
    }
    for (int x = 0; x < 2 * M; x++) {
        int ox = x < M ? x : w + x;
        uint32_t a = lat[ox], c = a;
        int best = 0;
        for (int i2 = 0; i2 < n; i2++) if (from[i2] == a && cnt[i2] > best) { best = cnt[i2]; c = to[i2]; }
        o[ox] = c;
    }
    return 1;
}

/* ---- the frame ---------------------------------------------------------------------------- */

int wide_rink_frame(int w) { return w == 320 && view_wide() && (g.disflags & HI(df32c)) && !(g.sflags & HI(sfhor)); }

/* the main menu (the game setup screen): bars */
static int bars_screen(const mdrt_vdp *v)
{
    for (int pr = 0; pr < 28; pr++) {
        const struct view_row *r = backdrop_row(v, pr);
        if (r && r->map == map_of(ART(GameSetUpMap))) return 1;
    }
    return 0;
}

int wide_frame(const mdrt_vdp *v, const uint32_t *pic, int w, int h, int pitch, uint32_t *out, int opitch, int sega_cd)
{
    int M = w == 256 ? 32 : 40;
    if (!sega_cd && wide_rink_frame(w)) M = 0;
    int ow = w + 2 * M;
    for (int y = 0; y < h; y++) memcpy(out + (long)y * opitch + M, pic + (long)y * pitch, (size_t)w * 4);
    if (!M) return ow;
    if (sega_cd) {                                        /* its edges' two columns on to the screen's edges, when */
        int plain = 1;                                    /* both are one colour top to bottom (the EA SPORTS white): */
        for (int y = 0; y < h && plain; y++) {            /* else bars (the licence page's goalie at its edge) */
            const uint32_t *p = pic + (long)y * pitch;
            plain = p[0] == pic[0] && p[1] == pic[0] && p[w - 1] == pic[w - 1] && p[w - 2] == pic[w - 1];
        }
        for (int y = 0; y < h; y++) {
            uint32_t *o = out + (long)y * opitch;
            const uint32_t *p = pic + (long)y * pitch;
            for (int x = 0; x < M; x++) { o[x] = plain ? p[0] : 0xFF000000u; o[w + M + x] = plain ? p[w - 1] : 0xFF000000u; }
        }
        return ow;
    }
    int bars = w == 256 ? !(g.sflags & HI(sfhor)) : bars_screen(v);
    if (bars) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < M; x++) out[(long)y * opitch + x] = out[(long)y * opitch + w + M + x] = 0xFF000000u;
        return ow;
    }
    if (w == 256 && stands_frame(v, M, h, out, opitch)) return ow;
    static uint8_t idx[WIDE_MAX_W];
    for (int y = 0; y < h; y++) {
        uint32_t *o = out + (long)y * opitch;
        if (w == 320 && sports_line(v, y, M, pic + (long)y * pitch, w, o, pic, pitch, h)) continue;
        wide_line(v, y, M, WIDE_ALL, idx);
        for (int x = 0; x < M; x++) { o[x] = wide_rgb(v, idx[x]); o[w + M + x] = wide_rgb(v, idx[w + M + x]); }
    }
    return ow;
}

/* ---- the wide rink on a 4:3 screen (a session's, played wide for a 16:9 player) ------------------------
 * The middle 256 of the 320, panning with a camera of the picture's own that follows the game's target
 * (xc1) as the cartridge's does (checkwindow: a dead zone of $28, a 1/16 step, its reach $3C), kept inside
 * the wide picture. Plane A (the overlays and the boxes, which the wide view places for 256 and shifts) is
 * put back where the cartridge has it: 32 in, and the boxes' rows from the screen's edges. Only the
 * picture: the game plays the wide rink as every machine does */
static int cam, cam_known;
void wide_crop_follow(void)
{
    int hp = (int16_t)g.Hpos, t = (int16_t)g.xc1;
    if (!cam_known) { cam = hp; cam_known = 1; }
    int d = t - cam, to = cam;
    if (d > 0x28) to = t - 0x28;
    else if (d < -0x28) to = t + 0x28;
    if (to > 0x3C) to = 0x3C;
    if (to < -0x3C) to = -0x3C;
    if (to != cam) { int step = (to - cam) / 16; cam += step ? step : (to > cam ? 1 : -1); }
    if (cam > hp + 32) cam = hp + 32;
    if (cam < hp - 32) cam = hp - 32;
}

int wide_crop(const mdrt_vdp *v, const uint32_t *pic, int h, int pitch, uint32_t *out, int opitch)
{
    int hp = (int16_t)g.Hpos;
    if (!cam_known) wide_crop_follow();
    int x0 = 32 + cam - hp, boxes = view_boxes();
    static uint8_t idx[WIDE_MAX_W], am[WIDE_MAX_W];
    for (int y = 0; y < h; y++) {
        const uint32_t *p = pic + (long)y * pitch;
        uint32_t *o = out + (long)y * opitch;
        wide_line(v, y, 0, WIDE_BS, idx);                 /* the rink: plane B and the sprites */
        wide_line(v, y, 0, WIDE_A, am);                   /* plane A (and the window): the overlays */
        int boxrow = boxes && y >= VIEW_BOX_ROW * 8;
        for (int x = 0; x < 256; x++) {
            int ax = boxrow ? (x < 128 ? x : x + 64) : x + 32;
            o[x] = am[ax] ? p[ax] : wide_rgb(v, idx[x0 + x]);   /* (an overlay with what is drawn over it: the faceoff box's players) */
        }
    }
    return 256;
}
