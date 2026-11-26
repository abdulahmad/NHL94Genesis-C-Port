/* The Sega CD's movies (.SEG on the disc: HOCKEY.SEG the intro, GOAL / HIT / STNLY / THNDR the
 * clips): a FILM file of 'SM' pictures, each a frame of 4bpp Genesis tiles that changes only the
 * tiles that move, with the sound in records of its own. The disc plays them with STARTER.BIN's
 * FilmOpen / FilmTask and LoadSmImage (NHL94Genesis docs/segacd_film.md); the format is
 * NHL9XStudio lib/segacd-film.js's. No SDL in here. */
#include <stdlib.h>
#include <string.h>
#include "segacd.h"
#include "overbar/postfx.h"

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)be16(p) << 16 | be16(p + 2); }

/* one SM picture at p (n bytes) over the tiles the frame before left (tiles: w x h x 32 bytes);
 * its palette lines into cram (64 words) and each tile's line into line[]. 0 when it is not one */
int segacd_sm_decode(const uint8_t *c, long n, uint8_t *tiles, int *w, int *h, uint16_t cram[64], uint8_t *line, int max_tiles)
{
    if (n < 12 || c[0] != 'S' || c[1] != 'M') return 0;
    unsigned lines = (be16(c + 2) & 3) + 1, tw = be16(c + 8), th = be16(c + 10), nt = tw * th;
    if (!tw || !th || (int)nt > max_tiles) return 0;
    /* (offsets in 64 bits: Windows' long is 32, and a length read from the file near 2^31 wrapped them below the buffer) */
    int64_t d6 = (int64_t)(((2 * nt + 0x1F) >> 5) * 4), pos = 12, N = n;
    if (pos + 32 * (int64_t)lines > N) return 0;
    for (unsigned i = 0; i < 64; i++) cram[i] = i < lines * 16 ? (uint16_t)be16(c + pos + 2 * i) : 0;
    pos += 32 * lines;
    const uint8_t *map = NULL;
    if (lines > 1) { if (pos + d6 > N) return 0; map = c + pos; pos += d6; }
    for (unsigned t = 0; t < nt; t++)
        line[t] = map ? (uint8_t)((be32(map + (t >> 4) * 4) >> (30 - 2 * (t & 15))) & 3) : 0;
    if (pos + 4 > N) return 0;
    int64_t l1 = be32(c + pos); pos += 4;
    if (l1 > N - pos) return 0;
    int64_t a2 = pos; pos += l1;
    if (pos + 4 > N) return 0;
    int64_t l2 = be32(c + pos); pos += 4;
    if (l2 > N - pos) return 0;
    int64_t a3 = pos; pos += l2;
    int64_t a4 = pos; pos += d6;
    int64_t a1 = pos;
    if (a2 + 1024 > N || a3 + 512 > N || a4 + d6 > N) return 0;
    uint32_t d1 = 0, d2 = 0, d5 = 0;
    int d3 = 1;
    uint8_t *o = tiles;
#define BIT() (d5 & 0x80000000u ? (d5 <<= 1, 1) : (d5 <<= 1, 0))
#define NEED(k) do { if (a1 + (k) > N) return 0; } while (0)
    for (unsigned t = 0; t < nt; t++, o += 32) {
        if (--d3 == 0) { d5 = be32(c + a4); a4 += 4; d3 = 16; }
        if (!BIT()) {
            if (!BIT()) continue;                          /* keep: what the frame before left */
            NEED(32);
            memcpy(o, c + a1, 32);                          /* raw */
            a1 += 32;
        } else if (!BIT()) {                               /* four rows of two A longs */
            NEED(8);
            uint8_t *q = o;
            for (int k = 0; k < 4; k++) {
                uint32_t x = be32(c + a2 + 4 * c[a1]), y = be32(c + a2 + 4 * c[a1 + 1]);
                a1 += 2;
                q[0] = (uint8_t)(x >> 24); q[1] = (uint8_t)(x >> 16); q[2] = (uint8_t)(y >> 24); q[3] = (uint8_t)(y >> 16);
                q[4] = (uint8_t)(x >> 8); q[5] = (uint8_t)x; q[6] = (uint8_t)(y >> 8); q[7] = (uint8_t)y;
                q += 8;
            }
        } else {                                           /* four groups of four B words */
            NEED(16);
            uint8_t *q = o;
            for (int k = 0; k < 4; k++) {
                unsigned v = be16(c + a3 + 2 * c[a1++]);
                d1 = (d1 & 0xFFFF0000u) | v; d2 = (d2 & 0xFFFFFF00u) | (d1 & 255); d1 <<= 8; d2 <<= 8;
                v = be16(c + a3 + 2 * c[a1++]);
                d1 = (d1 & 0xFFFF0000u) | v; d2 = (d2 & 0xFFFFFF00u) | (d1 & 255); d1 <<= 8; d2 <<= 8;
                v = be16(c + a3 + 2 * c[a1++]);
                d1 = (d1 & 0xFFFF0000u) | v; d2 = (d2 & 0xFFFFFF00u) | (d1 & 255); d2 <<= 8;
                v = be16(c + a3 + 2 * c[a1++]);
                d2 = (d2 & 0xFFFFFF00u) | (v & 255); d1 = (d1 & 0xFFFFFF00u) | (v >> 8);
                for (int s = 24; s >= 0; s -= 8) *q++ = (uint8_t)(d1 >> s);
                for (int s = 24; s >= 0; s -= 8) *q++ = (uint8_t)(d2 >> s);
            }
        }
    }
#undef BIT
#undef NEED
    *w = (int)tw;
    *h = (int)th;
    return 1;
}

/* the tiles as a picture: w*8 x h*8 palette indexes (line * 16 + colour) into out */
void segacd_sm_indexes(const uint8_t *tiles, const uint8_t *line, int w, int h, uint8_t *out)
{
    for (int t = 0; t < w * h; t++) {
        int tx = t % w, ty = t / w;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                uint8_t b = tiles[t * 32 + y * 4 + x / 2];
                out[(ty * 8 + y) * w * 8 + tx * 8 + x] = (uint8_t)(line[t] * 16 + (x & 1 ? b & 15 : b >> 4));
            }
    }
}

/* ---- the FILM file: 'FILM', its header's length (the records' data start there), 'FDSC' (the
 * picture's height and width), 'STAB' (600 ticks a second, the record count, then 16 byte records:
 * offset from the header's end, length, time, duration). A picture's record has its time; a sound
 * record's time is $FFFFFFFF and it holds 8 bit sign-magnitude samples (bit 7 set: positive) at
 * 16000 Hz. */
struct segacd_film {
    uint8_t *file;
    long size, data;
    unsigned count, tps, next;           /* records, ticks a second, the next record to read */
    int w, h;                            /* in tiles */
    uint8_t *tiles, *line, *idx;
    uint16_t cram[64];
    uint32_t argb[64];
    int16_t *pcm;
    long pcm_len;
    long time;                           /* the picture's time in ticks (-1 before the first) */
};

static const uint8_t *film_rec(const segacd_film *f, unsigned i) { return f->file + 0x34 + 16 * (long)i; }

segacd_film *segacd_film_open(ob_cd *cd, const char *name)
{
    uint8_t *b = NULL;
    long size = cd ? ob_cd_read_file(cd, name, &b) : 0;
    segacd_film *f = NULL;
    if (size < 0x34 || memcmp(b, "FILM", 4) || memcmp(b + 16, "FDSC", 4) || memcmp(b + 36, "STAB", 4)) goto bad;
    f = calloc(1, sizeof *f);
    if (!f) goto bad;
    f->file = b;
    f->size = size;
    if (be32(b + 4) > (uint32_t)size || be32(b + 48) > (uint32_t)(size - 0x34) / 16) goto bad;
    f->data = (long)be32(b + 4);
    f->h = (int)be32(b + 28) / 8;
    f->w = (int)be32(b + 32) / 8;
    f->tps = be32(b + 44);
    f->count = be32(b + 48);
    f->time = -1;
    if (f->w <= 0 || f->h <= 0 || f->w > 64 || f->h > 64 || !f->tps || f->data > size || 0x34 + 16 * (long)f->count > size) goto bad;
    long nt = (long)f->w * f->h, samples = 0;
    f->tiles = calloc((size_t)nt, 32);
    f->line = calloc((size_t)nt, 1);
    f->idx = malloc((size_t)nt * 64);
    if (!f->tiles || !f->line || !f->idx) goto bad;
    for (unsigned i = 0; i < f->count; i++) {
        const uint8_t *r = film_rec(f, i);
        if (be32(r + 8) == 0xFFFFFFFFu && be32(r + 4) <= (uint32_t)size) samples += (long)be32(r + 4);   /* (no record is longer than the file) */
    }
    f->pcm = malloc((size_t)(samples ? samples : 1) * sizeof *f->pcm);
    if (!f->pcm) goto bad;
    for (unsigned i = 0; i < f->count; i++) {
        const uint8_t *r = film_rec(f, i);
        int64_t at = (int64_t)f->data + be32(r), len = be32(r + 4);   /* (64 bits: Windows' long is 32) */
        if (be32(r + 8) != 0xFFFFFFFFu || at < 0 || len < 0 || at + len > size || f->pcm_len + len > samples) continue;
        for (int64_t k = 0; k < len; k++) {
            uint8_t s = b[at + k];
            int v = (s & 0x7F) << 8;
            f->pcm[f->pcm_len++] = (int16_t)(s & 0x80 ? v : -v);
        }
    }
    return f;
bad:
    if (f) { f->file = NULL; segacd_film_close(f); }
    free(b);
    return NULL;
}

void segacd_film_close(segacd_film *f)
{
    if (!f) return;
    free(f->file); free(f->tiles); free(f->line); free(f->idx); free(f->pcm);
    free(f);
}

void segacd_film_size(const segacd_film *f, int *w, int *h) { *w = f->w * 8; *h = f->h * 8; }
long segacd_film_sound(const segacd_film *f, const int16_t **pcm) { *pcm = f->pcm; return f->pcm_len; }

/* the next picture: its time in seconds; the picture as palette indexes (w x h) and the colours (64 ARGB). -1 at the end */
double segacd_film_next(segacd_film *f, const uint8_t **idx, const uint32_t **argb)
{
    while (f->next < f->count) {
        const uint8_t *r = film_rec(f, f->next++);
        uint32_t t = be32(r + 8);
        int64_t at = (int64_t)f->data + be32(r), len = be32(r + 4);   /* (64 bits: Windows' long is 32) */
        if (t == 0xFFFFFFFFu || at < 0 || len < 12 || at + len > f->size) continue;
        int w, h;
        if (!segacd_sm_decode(f->file + at, len, f->tiles, &w, &h, f->cram, f->line, f->w * f->h) || w != f->w || h != f->h) continue;
        segacd_sm_indexes(f->tiles, f->line, w, h, f->idx);
        for (int i = 0; i < 64; i++) {
            unsigned c = f->cram[i], rr = (c >> 1) & 7, g = (c >> 5) & 7, bb = (c >> 9) & 7;
            f->argb[i] = 0xFF000000u | (rr * 255 / 7) << 16 | (g * 255 / 7) << 8 | (bb * 255 / 7);
        }
        f->time = (long)t;
        *idx = f->idx;
        *argb = f->argb;
        return (double)t / f->tps;
    }
    return -1;
}

/* the same movie from its start, for another thread (its own copy of the file) */
segacd_film *segacd_film_dup(const segacd_film *f)
{
    segacd_film *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    *g = *f;
    long nt = (long)f->w * f->h;
    g->file = malloc((size_t)f->size);
    g->tiles = calloc((size_t)nt, 32);
    g->line = calloc((size_t)nt, 1);
    g->idx = malloc((size_t)nt * 64);
    g->pcm = NULL;
    g->pcm_len = 0;
    g->next = 0;
    g->time = -1;
    if (!g->file || !g->tiles || !g->line || !g->idx) { segacd_film_close(g); return NULL; }
    memcpy(g->file, f->file, (size_t)f->size);
    return g;
}

uint32_t segacd_film_id(const segacd_film *f) { return ob_postfx_hash(f->file, f->size); }
