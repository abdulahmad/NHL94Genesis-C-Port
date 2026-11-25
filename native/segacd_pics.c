/* The Sega CD disc's opening screens as pictures (native/segacd.c draws them over the
 * cartridge's): the High Score Productions page and the title with its credits (STARTER.BIN,
 * the boot pictures LoadBootImages sends: .map.jim, the first packed), and the license page
 * over the goalie (GRAPH2.BIN $DD800: a $0808 word, the tile count, the tiles, 128 bytes of
 * CRAM, the width and height, the name table). NHL94Genesis src/segacd/starter/starterdata94cd.asm
 * and docs/segacd.md. */
#include <stdlib.h>
#include <string.h>
#include "segacd.h"

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)be16(p) << 16 | be16(p + 2); }

/* the games' packed tiles (decompress94.asm's bytecode, a 256 byte ring): want bytes into out;
 * how many it made */
static long unpack(const uint8_t *in, long n, uint8_t *out, long want)
{
    uint8_t ring[256];
    unsigned idx = 0;
    long pos = 0, made = 0;
    memset(ring, 0, sizeof ring);
#define PUT(v) do { ring[idx] = (uint8_t)(v); idx = (idx + 1) & 255; if (!idx) { long k_ = want - made < 256 ? want - made : 256; if (k_ > 0) memcpy(out + made, ring, (size_t)k_); made += 256; } } while (0)
#define FWD(cnt, dist) do { unsigned s_ = (idx - (unsigned)(dist)) & 255; for (int i_ = 0; i_ < (int)(cnt); i_++) { uint8_t b_ = ring[s_]; PUT(b_); s_ = (s_ + 1) & 255; } } while (0)
#define REV(cnt, dist) do { unsigned s_ = (idx - (unsigned)(dist)) & 255; for (int i_ = 0; i_ < (int)(cnt); i_++) { uint8_t b_ = ring[s_]; PUT(b_); s_ = (s_ - 1) & 255; } } while (0)
    while (pos < n && made < want) {
        unsigned c = in[pos++], hi = c >> 4, lo = c & 15;
        if (hi <= 1) {
            unsigned k = (c & 0x1F) + 1;
            if (pos + (long)k > n) break;
            for (unsigned i = 0; i < k; i++) PUT(in[pos++]);
        } else if (hi == 2) {
            for (unsigned i = 0; i <= lo; i++) PUT(0);
        } else if (hi == 3) {
            if (pos >= n) break;
            uint8_t f = in[pos++];
            for (unsigned i = 0; i < lo + 3; i++) PUT(f);
        } else if (hi <= 7) {
            FWD((c & 7) + 2, ((c >> 3) & 7) + 1);
        } else if (hi == 8) {
            if (pos >= n) break;
            unsigned d = in[pos++];
            FWD(lo + 3, d);
        } else if (hi == 9 || hi == 0xF) {
            if (pos >= n) break;
            unsigned nx = in[pos++], cnt = (((c << 1) | (nx >> 7)) & 0x1F) + 3, d = (nx & 0x7F) + 1;
            if (hi == 9) FWD(cnt, d); else REV(cnt, d);
        } else if (hi == 0xA || hi == 0xB) {
            if (pos >= n) break;
            unsigned nx = in[pos++], p = c << 8 | nx;
            if (hi == 0xA) FWD(((p >> 6) & 0x3F) + 3, (nx & 0x3F) + 1);
            else FWD(((p >> 5) & 0x7F) + 3, (nx & 0x1F) + 1);
        } else if (hi == 0xC || hi == 0xD) {
            REV((c & 3) + 2, ((c >> 2) & 7) + 1);
        } else {                                           /* $E: a reverse copy, or (distance 0) the end */
            if (pos >= n) break;
            unsigned d = in[pos++];
            if (!d) {
                long k = (long)idx < want - made ? (long)idx : want - made;
                if (k > 0) memcpy(out + made, ring, (size_t)k);
                made += idx;
                break;
            }
            REV(lo + 3, d);
        }
    }
#undef PUT
#undef FWD
#undef REV
    return made;
}

/* a 4-colour-line CRAM block to ARGB */
static void cram(const uint8_t *p, uint32_t col[64])
{
    for (int i = 0; i < 64; i++) {
        unsigned w = be16(p + 2 * i), r = (w >> 1) & 7, gr = (w >> 5) & 7, b = (w >> 9) & 7;
        col[i] = 0xFF000000u | (r * 255 / 7) << 16 | (gr * 255 / 7) << 8 | (b * 255 / 7);
    }
}

/* tiles (32 bytes each, ntiles), a w x h name table (big-endian words at names) and colours: the picture, out 320 x 224 */
static int draw(const uint8_t *tiles, unsigned ntiles, const uint8_t *names, unsigned w, unsigned h, const uint32_t col[64], uint32_t *out)
{
    if (w > 40 || h > 28) return 0;
    for (int i = 0; i < 320 * 224; i++) out[i] = col[0];
    for (unsigned ty = 0; ty < h; ty++)
        for (unsigned tx = 0; tx < w; tx++) {
            unsigned e = be16(names + 2 * (ty * w + tx)), t = e & 0x7FF, pal = (e >> 13) & 3;
            if (t >= ntiles) continue;
            for (unsigned y = 0; y < 8; y++)
                for (unsigned x = 0; x < 8; x++) {
                    unsigned sx = e & 0x800 ? 7 - x : x, sy = e & 0x1000 ? 7 - y : y;
                    uint8_t b = tiles[t * 32 + sy * 4 + sx / 2];
                    unsigned c = sx & 1 ? b & 15 : b >> 4;
                    out[(ty * 8 + y) * 320 + tx * 8 + x] = col[pal * 16 + c];
                }
        }
    return 1;
}

/* a .map.jim at data (size bytes): header (palette offset, map offset), tile word (bit 15 packed), tiles */
static int jim(const uint8_t *d, long size, uint32_t *out)
{
    if (size < 12) return 0;
    uint32_t pal = be32(d), map = be32(d + 4);
    unsigned tw = be16(d + 8), n = tw & 0x7FFF;
    if (!n || pal + 128 > (uint32_t)size || map + 4 > (uint32_t)size) return 0;
    unsigned w = be16(d + map), h = be16(d + map + 2);
    if (map + 4 + w * h * 2 > (uint32_t)size) return 0;
    uint8_t *tiles = malloc((size_t)n * 32);
    if (!tiles) return 0;
    int ok;
    if (tw & 0x8000) ok = unpack(d + 10, size - 10, tiles, (long)n * 32) >= (long)n * 32;
    else ok = 10 + (long)n * 32 <= size && (memcpy(tiles, d + 10, (size_t)n * 32), 1);
    uint32_t col[64];
    cram(d + pal, col);
    ok = ok && draw(tiles, n, d + map + 4, w, h, col, out);
    free(tiles);
    return ok;
}

/* GRAPH2's stored screen at data: $0808, tiles, CRAM, w, h, names */
static int stored(const uint8_t *d, long size, uint32_t *out)
{
    if (size < 4 || be16(d) != 0x0808) return 0;
    unsigned n = be16(d + 2);
    long c = 4 + (long)n * 32, m = c + 128;
    if (m + 4 > size) return 0;
    unsigned w = be16(d + m), h = be16(d + m + 2);
    if (m + 4 + (long)w * (long)h * 2 > size) return 0;
    uint32_t col[64];
    cram(d + c, col);
    return draw(d + 4, n, d + m + 4, w, h, col, out);
}

int segacd_picture(ob_cd *cd, int which, uint32_t *out)
{
    static const struct { const char *file; long at, len; int kind; } P[] = {
        [SEGACD_PIC_HSP] = { "STARTER.BIN", 0xA942, 4896, 0 },
        [SEGACD_PIC_TITLE] = { "STARTER.BIN", 0x5BD8, 18030, 0 },
        [SEGACD_PIC_LICENSE] = { "GRAPH2.BIN", 0xDD800, 23496, 1 },
    };
    uint8_t *b;
    if (which < 0 || which >= (int)(sizeof P / sizeof P[0]) || !P[which].file || !cd) return 0;
    long size = ob_cd_read_file(cd, P[which].file, &b);
    if (size < P[which].at + P[which].len) { free(b); return 0; }
    int ok = P[which].kind ? stored(b + P[which].at, P[which].len, out) : jim(b + P[which].at, P[which].len, out);
    free(b);
    return ok;
}

/* ---- the disc's sprite sheets (STARTER.BIN): a palette offset, a frame table offset, the tile word (bit 15 = packed), the tiles,
 * then the frame table: words, frame n's sprites from entry n - 1 to entry n, 8 bytes a sprite (x, y, tile, flags in the top 5 bits,
 * the size in the low byte), x and y on the VDP's sprite plane. A frame put at an object's place (ox, oy) has its sprites at
 * x + ox - 128 on the screen (the starter's SpriteListAddObject). ---- */
#define SHEET_MAX 1952L
struct sheet { uint8_t d[SHEET_MAX]; long len; uint8_t *tiles; unsigned ntiles; uint32_t col[64]; int loaded; };   /* loaded: 1, -1 not on this disc */

static int sheet_load(ob_cd *cd, struct sheet *sh, long at, long len)
{
    if (sh->loaded || !cd) return sh->loaded == 1;
    uint8_t *b;
    long size = ob_cd_read_file(cd, "STARTER.BIN", &b);
    sh->loaded = -1;
    if (size >= at + len && len <= SHEET_MAX) {
        memcpy(sh->d, b + at, (size_t)len);
        sh->len = len;
        uint32_t pal = be32(sh->d);
        unsigned tw = be16(sh->d + 8), n = tw & 0x7FFF;
        if (n && pal + 128 <= (uint32_t)len && be32(sh->d + 4) + 4 <= (uint32_t)len && (sh->tiles = malloc((size_t)n * 32))) {
            if (tw & 0x8000 ? unpack(sh->d + 10, len - 10, sh->tiles, (long)n * 32) >= (long)n * 32
                            : 10 + (long)n * 32 <= len && (memcpy(sh->tiles, sh->d + 10, (size_t)n * 32), 1)) {
                cram(sh->d + pal, sh->col);
                sh->ntiles = n;
                sh->loaded = 1;
            } else { free(sh->tiles); sh->tiles = NULL; }
        }
    }
    free(b);
    return sh->loaded == 1;
}

static unsigned sheet_frames(const struct sheet *sh) { return be16(sh->d + be32(sh->d + 4)) / 2 - 1; }

/* frame f (1 ..) of the sheet at the object's place (ox, oy): 0 when the table is broken */
static int sheet_draw(const struct sheet *sh, unsigned f, int ox, int oy, uint32_t *out)
{
    uint32_t tab = be32(sh->d + 4);
    if (f < 1 || f > sheet_frames(sh)) return 1;
    unsigned a = tab + be16(sh->d + tab + 2 * (f - 1)), e = tab + be16(sh->d + tab + 2 * f);
    if (e > (unsigned)sh->len || a > e) return 0;
    for (unsigned s = e; s >= a + 8; s -= 8) {             /* (a frame's first sprite on top: drawn last) */
        const uint8_t *p = sh->d + s - 8;
        int x0 = (int16_t)be16(p) + ox - 128, y0 = (int16_t)be16(p + 2) + oy - 128;
        unsigned t0 = be16(p + 4), fl = be16(p + 6), size = fl & 0xFF, cw = (size >> 2 & 3) + 1, ch = (size & 3) + 1;
        unsigned pal = (fl >> 13) & 3, hf = fl & 0x800, vf = fl & 0x1000;
        for (unsigned cx = 0; cx < cw; cx++) for (unsigned cy = 0; cy < ch; cy++) {
            unsigned t = (t0 + cx * ch + cy) & 0x7FF;
            if (t >= sh->ntiles) continue;
            int dx = x0 + 8 * (int)(hf ? cw - 1 - cx : cx), dy = y0 + 8 * (int)(vf ? ch - 1 - cy : cy);
            for (unsigned y = 0; y < 8; y++) for (unsigned x = 0; x < 8; x++) {
                unsigned sx = hf ? 7 - x : x, sy = vf ? 7 - y : y;
                uint8_t b = sh->tiles[t * 32 + sy * 4 + sx / 2];
                unsigned c = sx & 1 ? b & 15 : b >> 4;
                int px = dx + (int)x, py = dy + (int)y;
                if (c && px >= 0 && px < 320 && py >= 0 && py < 224) out[py * 320 + px] = sh->col[pal * 16 + c];
            }
        }
    }
    return 1;
}

/* ---- the disc's EA SPORTS logo (EaLogoSheetA, $BC62, 1952 bytes). ScreenSetupA (starterscreen94cd.asm) builds the logo a piece
 * at a time: with n pieces up it draws frames n down to 1 (frame n on top), at (128, 128), from the third on 8 lower; tile 0 of the
 * sheet is the first it loads. The backdrop is colour 0 of line 0. ---- */
int segacd_ea_logo(ob_cd *cd, int pieces, uint32_t *out)
{
    static struct sheet a;
    if (!sheet_load(cd, &a, 0xBC62L, 1952L)) return 0;
    for (int i = 0; i < 320 * 224; i++) out[i] = a.col[0];
    if (pieces > (int)sheet_frames(&a)) pieces = (int)sheet_frames(&a);
    for (int f = 1; f <= pieces; f++)                      /* (frame 1 first: each later one over it) */
        if (!sheet_draw(&a, (unsigned)f, 128, f >= 3 ? 136 : 128, out)) return 0;
    return 1;
}

/* ---- the High Score Productions page with its four sparkles: ScreenSetupB draws the picture and runs four scripts (StarterBootGapA,
 * file $57D0 / $5822 / $5874 / $58C6: x, y, then (frames - 1, sheet frame) pairs, -1 none; a negative count: 0 or more goes back to
 * the first pair, less holds the last) on the frames of EaLogoSheetB ($C402, 362 bytes: 8 frames of one 16 x 16 sparkle), one step
 * a frame (AnimNextFrame). ---- */
int segacd_hsp(ob_cd *cd, int t, uint32_t *out)
{
    static struct sheet b;
    static int16_t script[4][40];
    static int have = -1;
    if (have < 0 && cd) {
        uint8_t *f;
        long size = ob_cd_read_file(cd, "STARTER.BIN", &f);
        static const long at[4] = { 0x57D0, 0x5822, 0x5874, 0x58C6 };
        have = size >= 0x58C6 + 80;
        for (int k = 0; have && k < 4; k++) for (int i = 0; i < 40; i++) script[k][i] = (int16_t)be16(f + at[k] + 2 * i);
        free(f);
    }
    if (!segacd_picture(cd, SEGACD_PIC_HSP, out)) return 0;
    if (have != 1 || !sheet_load(cd, &b, 0xC402L, 362L)) return 1;
    for (int k = 0; k < 4; k++) {
        const int16_t *s = script[k];
        int e = 2, count = s[2], frame = s[3];               /* (the pair now, its count down, its frame) */
        for (int step = 0; step <= t; step++) {
            frame = s[e + 1];
            if (--count < 0) {
                e += 2;
                if (e + 1 >= 40) break;
                if (s[e] < 0) {
                    if (s[e + 1] >= 0) e = 2;
                    else e -= 2;
                }
                count = s[e];
                frame = s[e + 1];
            }
        }
        if (frame >= 0 && !sheet_draw(&b, (unsigned)frame, s[0], s[1], out)) return 1;
    }
    return 1;
}

/* ---- the disc's title page with its credits (ScreenSetupC, starterscreen94cd.asm). The main CPU's screen (command 2: plane A
 * $E000, plane B $C000, window $B000; command $1C: the window over rows 0-18, lines 0-151, and a scroll a line): the TitleCredits
 * picture whole on the window with its four palettes, its rows 19-27 again on plane B, and the credits on plane A (command $B,
 * PrintStringBox: BootFont's tiles, its map's tile for each character code, palette line 0, so the title's colours). Plane A's
 * vertical scroll (command $1D) brings each page up from below the screen; the window hides plane A above line 152. The first
 * page (STARTER.BIN $1AF0E) is up at once and stays 241 frames, then each page of the list ($1AF36) comes up 64 lines, a line
 * every two frames, and stays 121 frames, until two page ends in a row. A page is Strings (a length word, counting itself, then
 * the text) to one whose text starts with $FF; each is centred on column 16 ($10 - length / 2). The page's line scroll moves
 * plane B's lines 0-150 sideways ($7F + a moving count), but plane B is empty there, so nothing shows of it. Start ends it (the
 * caller's). ---- */
#define T_FIRST 0x1AF0EL
#define T_LIST 0x1AF36L
#define T_TEXT_END 0x1B400L
static struct {
    int ready;                                  /* 1 loaded, -1 not on this disc */
    uint8_t text[T_TEXT_END - T_FIRST];
    uint32_t pic[320 * 224];                    /* the picture (colour 0: see-through) */
    uint8_t pix[320 * 224];                     /* its pixels' colour numbers */
    uint8_t *font;                              /* BootFont's tiles and map */
    unsigned nfont;
    uint8_t fmap[128 * 2];
    uint32_t col[64];
    uint8_t ring[32][64];                       /* the credits' plane: 32 rows of characters */
    int vscroll, at, phase, t;                  /* at: the next page in text; phase 0 the first page, 1 scrolling, 2 staying, 3 done */
} T;

static int title_rec(int at) { return at + 2 <= (int)sizeof T.text ? (int16_t)be16(T.text + at) : -1; }

/* CalcScrollPhase: the page at at, printed below the screen; where its end is */
static int title_page(int at)
{
    int row = ((T.vscroll >> 3) + 0x1C) & 31;
    for (int k = 0; k < 9; k++) memset(T.ring[(row + k) & 31], 0, 64);
    for (int n = 0; n < 32; n++) {
        int len = title_rec(at);
        if (len < 2 || at + len > (int)sizeof T.text) return -1;
        if (len >= 4 && T.text[at + 2] == 0xFF) return at;
        int x = 0x10 - len / 2;
        for (int i = 2; i < len && T.text[at + i]; i++, x++) if (x >= 0 && x < 64) T.ring[row][x] = T.text[at + i];
        row = (row + 1) & 31;
        at += len;
    }
    return -1;
}

int segacd_title_begin(ob_cd *cd)
{
    if (!T.ready && cd) {
        uint8_t *b;
        long size = ob_cd_read_file(cd, "STARTER.BIN", &b);
        T.ready = -1;
        if (size >= T_TEXT_END && size >= 0xA942) {
            memcpy(T.text, b + T_FIRST, sizeof T.text);
            const uint8_t *f = b + 0xA246;                     /* BootFont: palette, map, tiles */
            uint32_t map = be32(f + 4);
            unsigned tw = be16(f + 8), n = tw & 0x7FFF;
            if (map + 4 + 256 <= 0xA942 - 0xA246 && be16(f + map) == 32 && be16(f + map + 2) == 4 && n && (T.font = malloc((size_t)n * 32))
                && (tw & 0x8000 ? unpack(f + 10, 0xA942 - 0xA246 - 10, T.font, (long)n * 32) >= (long)n * 32 : 0)) {
                memcpy(T.fmap, f + map + 4, 256);
                T.nfont = n;
                const uint8_t *p = b + 0x5BD8;                 /* TitleCredits */
                cram(p + be32(p), T.col);
                if (jim(p, 18030, T.pic)) {
                    for (int i = 0; i < 320 * 224; i++) T.pix[i] = T.pic[i] != T.col[0];
                    T.ready = title_rec(0) > 0 && title_rec(T_LIST - T_FIRST) > 0 ? 1 : -1;
                }
            }
        }
        free(b);
    }
    if (T.ready != 1) return 0;
    memset(T.ring, 0, sizeof T.ring);
    T.vscroll = 0;
    title_page(0);
    T.vscroll += 0x40;
    T.at = T_LIST - T_FIRST;
    T.phase = 0;
    T.t = 0;
    return 1;
}

/* the next page up from below, or the end (0) */
static int title_next(void)
{
    int end = title_page(T.at);
    if (end < 0) return 0;
    T.at = end + title_rec(end);
    T.phase = 1;
    T.t = 0;
    return 1;
}

int segacd_title_step(void)
{
    if (T.ready != 1 || T.phase == 3) return 0;
    T.t++;
    if (T.phase == 0 && T.t >= 241 && !title_next()) T.phase = 3;
    else if (T.phase == 1 && T.t % 2 == 0) {
        T.vscroll = (T.vscroll + 1) & 255;
        if (T.t == 128) { T.phase = 2; T.t = 0; }
    } else if (T.phase == 2 && T.t >= 121 && (title_rec(T.at + 2) < 0 || !title_next())) T.phase = 3;   /* (two page ends in a row: the end) */
    return T.phase != 3;
}

void segacd_title_draw(uint32_t *out)
{
    if (T.ready != 1) { for (int i = 0; i < 320 * 224; i++) out[i] = 0xFF000000u; return; }
    for (int i = 0; i < 320 * 224; i++) out[i] = T.pix[i] ? T.pic[i] : T.col[0];
    for (int y = 152; y < 224; y++) {                       /* (plane A, below the window: over the picture's lower rows on plane B) */
        int py = (y + T.vscroll) & 255, row = py >> 3, ly = py & 7;
        for (int c = 0; c < 40; c++) {
            unsigned ch = T.ring[row][c];
            if (!ch || ch >= 128) continue;
            unsigned e = be16(T.fmap + 2 * ch), t = e & 0x7FF, pal = e >> 13 & 3;
            if (t >= T.nfont) continue;
            unsigned sy = e & 0x1000 ? 7 - (unsigned)ly : (unsigned)ly;
            for (unsigned x = 0; x < 8; x++) {
                unsigned sx = e & 0x800 ? 7 - x : x;
                uint8_t b = T.font[t * 32 + sy * 4 + sx / 2];
                unsigned v = sx & 1 ? b & 15 : b >> 4;
                if (v) out[y * 320 + c * 8 + (int)x] = T.col[pal * 16 + v];   /* (the title's colours: BootFont's own are not loaded) */
            }
        }
    }
}
