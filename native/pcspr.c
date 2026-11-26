/* NHL 95 PC's skater sprites in place of the cartridge's (the Game page's "PC sprites").
 *
 * The PC game keeps its sprites in banks of 50 frames (000_049.PPV ... 1100_1149.PPV, SHPI banks of SPIT
 * images: a 16 byte header with the size and the hotspot, then PackBits-like runs of 8-bit pixels). Its frame
 * numbers are the cartridge's less one up to the Genesis' goalie ready frames, then less nine (frames95.asm /
 * frames94.asm: the same groups of frames in the same order, the cartridge's 24 goalie ready frames against
 * the PC's 16). The skaters' and goalies' pixels are colour numbers that the team's record in HOMEPALS.BIN /
 * AWAYPALS.BIN turns into the team's colours (64 colours placed at 0x80 in RINKPAL's palette, then a remap of
 * all 256 numbers). Where the PC has no such frame the cartridge's sprite stays.
 *
 * The files are the player's own (beside the program, or on NHL 95 PC's disc image), never shipped. The game
 * is not touched but for the sprite table (the hook skips a replaced sprite): its state is the same with the
 * option on or off. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include "game.h"
#include "view.h"
#include <math.h>
#include "rosters.h"
#include "native.h"
#include "pcspr.h"
#include "pcsong.h"
#include "rules.h"
#include "cardcolor_xfer.h"
#include "overbar/cdimage.h"

#define NFRAMES 1150
#define NTEAMS 28

typedef struct { int w, h, xo, yo; uint8_t *px; } spr;

typedef struct { int16_t pc, x, y, flip, away, sc, first, count; uint8_t sig[8]; } shown;   /* a sprite the PC draws: its sprite table entries and what they held */

static struct {
    SDL_atomic_t on, found, searched;
    spr f[NFRAMES];
    uint8_t pal[256][3];                    /* RINKPAL, 8 bits */
    uint8_t team[2][NTEAMS][448];           /* HOMEPALS, AWAYPALS */
    int have_team[2];
    SDL_SpinLock lock;
    shown show[16];                          /* what the game wrote */
    struct { int n; shown l[16]; } ring[4];     /* the last frames the game wrote (newest first): the picture draws the one its sprite table is */
    int nshow;
} P;

/* the centre ice logos of NHL 95 PC (<ABBR>.MAP / .TIL on the disc and RINK.PAL: tiles of 8 x 8 pixels of 256 colours, colour 255 clear) */
#define NLOGOS 26
struct lset { int have; uint8_t pal[256][3]; struct { int w, h; uint8_t *idx; } t[NLOGOS]; };
static struct {
    SDL_atomic_t on;                         /* 0 the cartridge's, 1 NHL 95 PC's, 2 NHL 94 PC's (95's where 94 has none) */
    struct lset L[2];
    int crest;                               /* the first of the cartridge's 24 crest tiles in the VDP's memory (-1: not found yet) */
    int seen;
} LGO = { .crest = -1 };
static const char *const logo_name[NLOGOS] = { "ANH", "BOS", "BUF", "CGY", "CHI", "DET", "EDM", "FLO", "HFD", "LA", "MIN", "MTL", "NJ",
    "NYI", "NYR", "OTT", "PHI", "PIT", "QUE", "SJ", "STL", "TB", "TOR", "VAN", "WPG", "WSH" };
/* the cartridge's art number of a team (ANA BOS BUF CGY CHI DAL DET EDM FLA HFD LA MTL NJ NYI NYR OTT PHI PIT QUE SJ STL TB TOR VAN WSH WPG ASE ASW)
 * as the index of logo_name (-1: none, the All-Star teams) */
static const signed char logo_of_art[28] = { 0, 1, 2, 3, 4, 10, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 25, 24, -1, -1 };

/* the referee: not in the cartridge's game at all, only in this picture (a visual one: nothing in the game knows of it) */
static struct {
    SDL_atomic_t on;
    int init, x, y, vx, vy;                 /* the rink position and velocity in 1/256 pixels */
    int dir, t, wasfo, drop;
    int sx, sy, valid;                      /* the sprite table position of the last frame */
} R;

static long file_len(const char *p) { struct stat st; return stat(p, &st) == 0 ? (long)st.st_size : -1; }
static uint8_t *slurp(const char *path, long *size)
{
    long n = file_len(path);
    FILE *f = n > 0 ? fopen(path, "rb") : NULL;
    if (!f) return NULL;
    uint8_t *b = malloc((size_t)n);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) *size = n;
    return b;
}

static uint8_t *loose(const char *dir, const char *name, long *n)
{
    char path[1100], low[64];
    size_t k = 0;
    for (; name[k] && k < sizeof low - 1; k++) low[k] = (char)tolower((unsigned char)name[k]);
    low[k] = 0;
    const char *names[2] = { name, low };
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s%s%s", dir, dir[0] && dir[strlen(dir) - 1] != '/' && dir[strlen(dir) - 1] != '\\' ? "/" : "", names[i]);
        uint8_t *b = slurp(path, n);
        if (b) return b;
    }
    return NULL;
}

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int s16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

/* one SPIT shape at b + o (n bytes in all) */
static int shape(const uint8_t *b, long n, long o, spr *out)
{
    if (o + 16 > n) return 0;
    int w = s16(b + o + 4), h = s16(b + o + 6);
    if (w <= 0 || h <= 0 || w > 512 || h > 512) return 0;
    long need = (long)w * h, got = 0, i = o + 16;
    uint8_t *px = malloc((size_t)need);
    if (!px) return 0;
    while (got < need && i < n) {
        int c = b[i++];
        if (c == 0) break;
        if (c >= 128) {
            int k = 256 - c;
            if (i + k > n) break;
            for (int j = 0; j < k && got < need; j++) px[got++] = b[i + j];
            i += k;
        } else {
            if (i >= n) break;
            for (int j = 0; j < c && got < need; j++) px[got++] = b[i];
            i++;
        }
    }
    if (got != need) { free(px); return 0; }
    out->w = w; out->h = h; out->xo = s16(b + o + 8); out->yo = s16(b + o + 10); out->px = px;
    return 1;
}

static int bank(const uint8_t *b, long n)
{
    if (n < 12 || memcmp(b, "SHPI", 4)) return 0;
    int count = (int)le32(b + 8), got = 0;
    for (int i = 0; i < count && 16 + 8 * i + 8 <= n; i++) {
        int num = atoi((const char[]){ (char)b[16 + 8 * i], (char)b[17 + 8 * i], (char)b[18 + 8 * i], (char)b[19 + 8 * i], 0 });
        uint32_t off = le32(b + 20 + 8 * i);
        if (b[16 + 8 * i] == '!' || num < 0 || num >= NFRAMES) continue;
        if (shape(b, n, off, &P.f[num])) got++;
    }
    return got;
}

/* NHL 95 PC's flags of the anthems (UFLAG.PPV, CFLAG.PPV: four pictures each, waving) */
static spr FLG[2][4];
static int have_flag, nflag[2];
static void load_flags(const char *dir, ob_cd *cd)
{
    static const char *const nm[2] = { "UFLAG.PPV", "CFLAG.PPV" };
    int got = 0;
    nflag[0] = nflag[1] = 0;
    for (int k = 0; k < 2; k++) {
        long n = 0;
        uint8_t *b = NULL;
        if (cd) n = ob_cd_read_file(cd, nm[k], &b); else b = loose(dir, nm[k], &n);
        if (b && n > 16 && !memcmp(b, "SHPI", 4)) {
            int count = (int)le32(b + 8);
            for (int i = 0; i < count && i < 4 && 16 + 8 * i + 8 <= n; i++)
                if (shape(b, n, le32(b + 20 + 8 * i), &FLG[k][nflag[k]])) { nflag[k]++; got++; }
        }
        free(b);
    }
    have_flag = nflag[0] > 0 && nflag[1] > 0;
    if (getenv("PCSPR_LOG")) fprintf(stderr, "pcspr: flags %d of 8\n", got);
}

static const char *bank_name(int k, char *buf)
{
    if (k < 20) snprintf(buf, 16, "%03d_%03d.PPV", k * 50, k * 50 + 49);
    else snprintf(buf, 16, "%04d%04d.PPV", k * 50, k * 50 + 49);
    return buf;
}

static int palette(const uint8_t *file, long n)
{
    long rn = 0;
    uint8_t *b = cc_refpack(file, n, &rn);
    int ok = 0;
    if (b && rn > 12 && !memcmp(b, "SHPI", 4)) {
        int count = (int)le32(b + 8);
        for (int i = 0; i < count; i++) {
            if (memcmp(b + 16 + 8 * i, "!pal", 4)) continue;
            long o = le32(b + 20 + 8 * i);
            if (o + 16 + 768 > rn) break;
            for (int c = 0; c < 256; c++)
                for (int k = 0; k < 3; k++) { int v = b[o + 16 + c * 3 + k] & 63; P.pal[c][k] = (uint8_t)(v << 2 | v >> 4); }
            ok = 1;
        }
    }
    free(b);
    return ok;
}


/* NHL 94 PC's EA Huffman + run-length packing (30FB / 31FB / 32FB; QPP94-To-PNG's unhuff): the palette bank is packed this way */
struct bits { const uint8_t *d; long n, p; uint32_t buf; int cnt; };
static uint32_t bget(struct bits *b, int n)
{
    if (n > 16) { uint32_t hi = bget(b, n - 16); return hi * 0x10000u + bget(b, 16); }
    uint32_t v = n ? b->buf >> (32 - n) : 0;
    b->buf = n == 32 ? 0 : b->buf << n;
    b->cnt -= n;
    while (b->cnt < 16) {
        uint32_t c = b->p < b->n ? b->d[b->p] : 0;
        b->p++;
        b->buf |= c << (24 - b->cnt);
        b->cnt += 8;
    }
    return v;
}
static int bvnum(struct bits *b)
{
    int v = 2, n = 1;
    do { v *= 2; n++; } while (bget(b, 1) == 0 && n < 30);
    return (int)bget(b, n) + v - 4;
}
static uint8_t *unhuff94(const uint8_t *d, long dn, long *out_n)
{
    if (dn < 8 || d[1] != 0xFB || d[0] < 0x30 || d[0] > 0x32) return NULL;
    struct bits b = { d, dn, 2 + (d[0] == 0x31 ? 3 : 0), 0, 0 };
    bget(&b, 0);
    long size = (long)bget(&b, 24);
    unsigned clue = bget(&b, 8);
    if (size <= 0 || size > 1L << 24) return NULL;
    int base[40] = { 0 }, maxlen;
    uint32_t lim[40] = { 0 };
    int num = 0, len = 1;
    uint32_t code = 0;
    for (;;) {
        if (len >= 17) return NULL;
        code = (code << 1) & 0xFFFF;
        base[len] = (int)code - num;
        int n = bvnum(&b);
        num += n; code += (uint32_t)n;
        uint32_t limit = n ? (code << (16 - len)) & 0xFFFF : 0;
        lim[len] = limit;
        len++;
        if (n != 0 && limit == 0) break;
    }
    maxlen = len - 1;
    if (num > 256) return NULL;
    uint8_t used[256] = { 0 }, sym[256];
    int cur = -1;
    for (int i = 0; i < num; i++) {
        int skip = bvnum(&b) + 1;
        if (skip > 100000) return NULL;
        while (skip) { cur = (cur + 1) & 0xFF; if (!used[cur]) skip--; }
        used[cur] = 1; sym[i] = (uint8_t)cur;
    }
    uint8_t *out = calloc(1, (size_t)size);
    if (!out) return NULL;
    long o = 0;
    for (;;) {
        uint32_t pk = b.buf >> 16;
        int l = 1;
        while (lim[l] <= pk && l < maxlen) l++;
        int si = (int)((bget(&b, l) - (uint32_t)base[l]) & 0xFFFF);
        if (si >= num) break;
        unsigned sy = sym[si];
        if (sy != clue) { if (o < size) out[o] = (uint8_t)sy; o++; continue; }
        int n = bvnum(&b);
        if (n) { uint8_t pv = o > 0 && o <= size ? out[o - 1] : 0; for (int i = 0; i < n; i++) { if (o < size) out[o] = pv; o++; } continue; }
        if (bget(&b, 1) == 0) { uint8_t v = (uint8_t)bget(&b, 8); if (o < size) out[o] = v; o++; continue; }
        break;
    }
    if (d[0] == 0x32) { uint8_t acc = 0; for (long i = 0; i < size; i++) { acc = (uint8_t)(acc + out[i]); out[i] = acc; } }
    *out_n = size;
    return out;
}

#define ICE(c) (kind ? ((c) == 106 || (c) == 107 || (c) == 105 || (c) == 104 || (c) == 23 || (c) == 91 || (c) == 90) : ((c) == 96 || (c) == 23 || (c) == 91 || (c) == 102 || (c) == 103))
static void load_logo_set(struct lset *S, const char *dir, ob_cd *cd, int kind)
{
    long pn = 0;
        uint8_t *pb = NULL;
        if (kind == 0) { if (cd) pn = ob_cd_read_file(cd, "RINK.PAL", &pb); else pb = loose(dir, "RINK.PAL", &pn); }
        else {                                                       /* (NHL 94 PC: RINKPAL.QPP, a SHPI with a !pal of 6 bit colours) */
            uint8_t *qb = NULL;
            long qn = 0;
            if (cd) qn = ob_cd_read_file(cd, "RINKPAL.QPP", &qb); else qb = loose(dir, "RINKPAL.QPP", &qn);
            long rn = 0;
            uint8_t *rb = qb && qn > 0 ? unhuff94(qb, qn, &rn) : NULL;
            if (rb && rn > 6) {                                       /* (the bank: u32 size, u16 count, 4 byte names, u32 offsets from 6 + 8 count) */
                int cnt = rb[4] | rb[5] << 8;
                for (int i = 0; i < cnt && 6 + 8 * cnt <= rn; i++)
                    if (!memcmp(rb + 6 + 4 * i, "!pal", 4)) {
                        long o = 6 + 8 * cnt + (long)le32(rb + 6 + 4 * cnt + 4 * i);
                        if (o + 16 + 768 <= rn) {
                            pb = malloc(512);
                            if (pb) for (int c = 0; c < 256; c++) {
                                int r6 = rb[o + 16 + c * 3] & 63, g6 = rb[o + 16 + c * 3 + 1] & 63, b6 = rb[o + 16 + c * 3 + 2] & 63;
                                int v = (r6 >> 1) | (g6 >> 1) << 5 | (b6 >> 1) << 10;
                                pb[c * 2] = (uint8_t)v; pb[c * 2 + 1] = (uint8_t)(v >> 8);
                            }
                            pn = 512;
                        }
                        break;
                    }
            }
            if (getenv("PCSPR_LOG") && cd == NULL) fprintf(stderr, "94 pal: file %ld unpacked %ld magic %.4s\n", qn, rn, rb ? (char *)rb : "-");
            free(rb); free(qb);
        }
        int got = 0;
        if (pb && pn >= 512) {
            for (int c = 0; c < 256; c++) {
                int v = pb[c * 2] | pb[c * 2 + 1] << 8;
                S->pal[c][0] = (uint8_t)((v & 31) * 255 / 31); S->pal[c][1] = (uint8_t)((v >> 5 & 31) * 255 / 31); S->pal[c][2] = (uint8_t)((v >> 10 & 31) * 255 / 31);
            }
            for (int k = 0; k < NLOGOS; k++) {
                char fm[16], ft[16];
                snprintf(fm, sizeof fm, "%s.MAP", logo_name[k]);
                snprintf(ft, sizeof ft, "%s.TIL", logo_name[k]);
                long mn = 0, tln = 0;
                uint8_t *mb = NULL, *tb = NULL;
                if (cd) { mn = ob_cd_read_file(cd, fm, &mb); tln = ob_cd_read_file(cd, ft, &tb); } else { mb = loose(dir, fm, &mn); tb = loose(dir, ft, &tln); }
                if (mb && tb && mn >= 6) {
                    int lw = mb[0] | mb[1] << 8, lh = mb[2] | mb[3] << 8;
                    if (lw > 0 && lh > 0 && lw <= 8 && lh <= 8 && mn >= 6 + 2 * lw * lh) {
                        uint8_t *ix = malloc((size_t)(lw * 8 * lh * 8));
                        if (ix) {
                            for (int ty = 0; ty < lh; ty++)
                                for (int tx = 0; tx < lw; tx++) {
                                    int wd = mb[6 + 2 * (ty * lw + tx)] | mb[7 + 2 * (ty * lw + tx)] << 8;
                                    int ti = kind ? wd & 0x7FF : wd & 0x1FFF, hfl = wd & (kind ? 0x0800 : 0x2000), vfl = wd & (kind ? 0x1000 : 0x4000);
                                    for (int y = 0; y < 8; y++)
                                        for (int x = 0; x < 8; x++) {
                                            int sx = hfl ? 7 - x : x, sy = vfl ? 7 - y : y;
                                            long at = (long)ti * (kind ? 16 : 64) + (kind ? (sx & 3) * (tln / 4) + sy * 2 + (sx >> 2) : sy * 8 + sx);   /* (94: the whole file is four Mode X planes, a tile 16 bytes of each) */
                                            ix[(ty * 8 + y) * lw * 8 + tx * 8 + x] = at < tln ? tb[at] : 255;
                                        }
                                }
                            {                                          /* (the ice, its lines and the centre line the tiles bake in round the crest: clear, found from the edge in) */
                                int fw = lw * 8, fh = lh * 8, *q = malloc(sizeof(int) * (size_t)(fw * fh));
                                int qn = 0;
                                if (q) {
                                    qn = 0;
                                    for (int y = 0; y < fh; y++)
                                        for (int x = 0; x < fw; x++)
                                            if ((x == 0 || y == 0 || x == fw - 1 || y == fh - 1)) { int c = ix[y * fw + x]; if (ICE(c)) { q[qn++] = y * fw + x; ix[y * fw + x] = 254; } }
                                    for (int i = 0; i < qn; i++) {
                                        int x = q[i] % fw, y = q[i] / fw;
                                        static const int d[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                                        for (int j = 0; j < 4; j++) {
                                            int nx2 = x + d[j][0], ny2 = y + d[j][1];
                                            if (nx2 < 0 || ny2 < 0 || nx2 >= fw || ny2 >= fh) continue;
                                            int c = ix[ny2 * fw + nx2];
                                            if (ICE(c)) { ix[ny2 * fw + nx2] = 254; q[qn++] = ny2 * fw + nx2; }
                                        }
                                    }
                                    free(q);
                                    for (int i = 0; i < fw * fh; i++) if (ix[i] == 254) ix[i] = 255;
                                    if (kind)                                                      /* (94 bakes the centre line in between its two crests: the game draws its own) */
                                        for (int y = 0; y < fh; y++)
                                            if (y >= fh / 2 - 4 && y < fh / 2 + 4) memset(ix + y * fw, 255, (size_t)fw);
                                }
                            }
                            S->t[k].w = lw * 8; S->t[k].h = lh * 8; S->t[k].idx = ix;
                            got++;
                        }
                    }
                }
                free(mb); free(tb);
            }
        }
        free(pb);
        S->have = got >= 20;
        if (getenv("PCSPR_LOG")) fprintf(stderr, "pcspr: %d centre ice logos (%s)\n", got, kind ? "NHL 94 PC" : "NHL 95 PC");
    }

static int load(const char *dir, ob_cd *cd)
{
    char nm[16];
    long n;
    uint8_t *b;
    int banks = 0, pal = 0;
    for (int k = 0; k < 23; k++) {
        bank_name(k, nm);
        n = 0;
        b = NULL;
        if (cd) n = ob_cd_read_file(cd, nm, &b);
        else b = loose(dir, nm, &n);
        int gb = b && n > 0 ? bank(b, n) : -1;
        if (getenv("PCSPR_LOG") && cd) fprintf(stderr, "pcspr: %s n=%ld got %d\n", nm, n, gb);
        if (gb > 0) banks++;
        free(b);
    }
    n = 0; b = NULL;
    if (cd) n = ob_cd_read_file(cd, "RINKPAL.QFS", &b); else b = loose(dir, "RINKPAL.QFS", &n);
    if (b && n > 0) pal = palette(b, n);
    free(b);
    static const char *const tn[2] = { "HOMEPALS.BIN", "AWAYPALS.BIN" };
    for (int s = 0; s < 2; s++) {
        n = 0; b = NULL;
        if (cd) n = ob_cd_read_file(cd, tn[s], &b); else b = loose(dir, tn[s], &n);
        if (b && n >= NTEAMS * 448) { memcpy(P.team[s], b, NTEAMS * 448); P.have_team[s] = 1; }
        free(b);
    }
    if (getenv("PCSPR_SHEET") && pal && P.have_team[0]) {          /* (a contact sheet of the frames FIRST..FIRST+99, for reading the art) */
        int first = atoi(getenv("PCSPR_SHEET")), cw = 64, ch = 80;
        static uint8_t im[10 * 64 * 10 * 80 * 3];
        const uint8_t *rec = P.team[0][9];
        for (int i = 0; i < 100; i++) {
            const spr *f = first + i < NFRAMES ? &P.f[first + i] : NULL;
            if (!f || !f->px) continue;
            for (int y = 0; y < f->h && y < ch - 2; y++)
                for (int x = 0; x < f->w && x < cw - 2; x++) {
                    int v = f->px[y * f->w + x];
                    if (v == 255) continue;
                    int r = v < 0x80 ? rec[192 + v] : rec[320 + v - 0x80];
                    uint8_t *d = im + (((i / 10) * ch + y + 1) * 10 * cw + (i % 10) * cw + x + 1) * 3;
                    d[0] = P.pal[r][0]; d[1] = P.pal[r][1]; d[2] = P.pal[r][2];
                }
        }
        FILE *o = fopen("/tmp/pcsheet.ppm", "wb");
        if (o) { fprintf(o, "P6\n%d %d\n255\n", 10 * cw, 10 * ch); fwrite(im, 1, sizeof im, o); fclose(o); }
    }

    if (!have_flag) load_flags(dir, cd);
    if (!LGO.L[0].have) load_logo_set(&LGO.L[0], dir, cd, 0);                           /* (the centre ice logos: optional) */
    if (getenv("PCSPR_LOG")) fprintf(stderr, "pcspr: banks %d pal %d team %d %d\n", banks, pal, P.have_team[0], P.have_team[1]);
    return banks >= 20 && pal && P.have_team[0] && P.have_team[1];
}

static int finder(void *arg)
{
    char *base = arg;
    int ok = load(base, NULL) || load(".", NULL);
    if (!ok) {
        static const char *const need[1] = { "000_049.PPV" };
        ob_cd *cd = ob_cd_find(base, need, 1, NULL, 0);
        if (!cd) cd = ob_cd_find(".", need, 1, NULL, 0);
        if (cd) {
            ok = load(NULL, cd);
            if (ok) fprintf(stderr, "PC sprites: NHL 95 PC sprites from %s\n", ob_cd_path(cd));
            ob_cd_close(cd);
        }
    }
    if (ok) {                                                    /* NHL 94 PC's logos: its folder beside the program, or NHL94PC_DIR */
        const char *e = getenv("NHL94PC_DIR");
        char cand[5][600];
        int nc = 0;
        if (e) snprintf(cand[nc++], 600, "%s", e);
        static const char *const sub[4] = { "NHL94PC", "nhl94pc", "NHL94.PC", "." };
        for (int i = 0; i < 4; i++) snprintf(cand[nc++], 600, "%s/%s", base, sub[i]);
        for (int i = 0; i < nc && !LGO.L[1].have; i++) load_logo_set(&LGO.L[1], cand[i], NULL, 1);
        if (LGO.L[1].have) fprintf(stderr, "PC sprites: NHL 94 PC centre logos found\n");
    }
    if (!ok) fprintf(stderr, "PC sprites: no NHL 95 PC sprite files found beside the program\n");
    SDL_MemoryBarrierRelease();
    SDL_AtomicSet(&P.found, ok);
    SDL_AtomicSet(&P.searched, 1);
    SDL_free(arg);
    return 0;
}

/* the PC's palette record of each of the cartridge's teams (the art's order: ANA BOS BUF ... WSH WPG ASE ASW). The records are in the
 * PC's own team order: BOS BUF CGY CHI DET EDM HFD LA DAL MTL NJ NYI NYR OTT PHI PIT QUE STL SJ TB TOR VAN WSH WPG, then
 * two All-Star records, Florida and Anaheim */
static const uint8_t pc_team[NTEAMS] = {
    26, 0, 1, 2, 3, 8, 4, 5, 25, 6, 7, 9, 10, 11, 12, 13, 14, 15, 16, 18, 17, 19, 20, 21, 22, 23, 24, 27,
};

/* the anthem's idle pictures (SPAanthem1-3 and SPArefanthem1 of NHL 95 PC: now and then a skater shifts his stance, the referee
 * glances about). Only what the eye sees: a hash of who and when, so no game state is touched. */
static unsigned idle_hash(int who, int slot) { unsigned h = (unsigned)who * 2654435761u + (unsigned)slot * 40503u + 12345u; h ^= h >> 13; h *= 1274126177u; return h ^ (h >> 16); }
static int idle_skater(int who, int t)
{
    unsigned h = idle_hash(who, t / 160);
    if (h % 100 >= 40) return 369;
    int dt = t % 160 - (int)(h / 100 % 10);
    static const int a[3][6] = { { 369, 370, 371, 370, 0, 0 }, { 369, 370, 371, 0, 0, 0 }, { 369, 370, 371, 370, 371, 0 } };
    int k = dt / 30, n = (int)(h / 10000 % 3);
    return dt >= 0 && k < 5 && a[n][k] ? a[n][k] : 369;
}
static int idle_ref(int t)
{
    unsigned h = idle_hash(99, t / 300);
    if (h % 100 >= 60) return 649;
    int dt = t % 300 - (int)(h / 100 % 10);
    static const int a[8] = { 649, 705, 706, 707, 706, 707, 705, 649 };
    int k = dt / 30;
    return dt >= 0 && k < 8 ? a[k] : 649;
}

/* the cartridge's frame number as the PC's (-1: none) */
/* A fade (between plays, the pause): colour RAM steps toward palfadenew, and the PC's pictures, which are not in colour RAM, are
 * dimmed by the same amount (0..256) */
static int fade256 = 256;
static uint32_t fadec(uint32_t c)
{
    if (fade256 >= 256) return c;
    uint32_t r = (c >> 16 & 255) * (uint32_t)fade256 >> 8, gg = (c >> 8 & 255) * (uint32_t)fade256 >> 8, b = (c & 255) * (uint32_t)fade256 >> 8;
    return 0xFF000000u | r << 16 | gg << 8 | b;
}
static int cram_sum(const uint16_t *cr, int ram)
{
    int t = 0;
    for (int i = 0; i < 64; i++) { unsigned c = ram ? g.palfadenew[i] : cr[i]; t += (c & 0xE) + (c >> 4 & 0xE) + (c >> 8 & 0xE); }
    return t;
}
static void fade_update(const mdrt_vdp *vdp)
{
    static int steady;
    int cur = cram_sum(vdp->s.cram, 0);
    if ((int16_t)g.palcount < 0) { steady = cur; fade256 = 256; return; }
    int tgt = cram_sum(0, 1), ref = tgt > 0 ? tgt : steady;
    fade256 = ref > 0 ? (cur >= ref ? 256 : cur * 256 / ref) : 256;
}

static int pc_frame(int fr)
{
    if (fr < 1 || fr > 657) return -1;
    if (fr <= 554) return fr - 1;
    if (fr <= 562) return -1;                          /* (the cartridge's extra goalie ready frames) */
    return fr - 9;
}

static int in_game(void) { return g.demoflag && ((g.disflags & HI(dfok)) || (g.sflags & HI(sfpz))); }   /* (the pause screen too: its rink strip has the skaters) */


/* where the referee stands, from the PC's assreffollow (the puck's zone picks a spot beside the boards; the view's middle gives the y)
 * and assrefatdot (at the face off spot, facing the dot) */
static void ref_tick(void)
{
    R.valid = 0;
    if (getenv("PCREF_LOG") && !(g.Vcount & 255)) fprintf(stderr, "ref? on %d found %d ingame %d hor %d gm2 %d\n", SDL_AtomicGet(&R.on), SDL_AtomicGet(&P.found), in_game(), !!(g.sflags & HI(sfhor)), g.gmode2 & 1);
    if (!SDL_AtomicGet(&R.on) || !SDL_AtomicGet(&P.found) || !g.demoflag || (g.sflags & HI(sfhor)) || (g.gmode2 & 1)) { R.init = 0; return; }
    const struct SortObj *pk = sortobj(puckscnum);
    int asn = pk->asslist[pk->assnum & 7];
    int fo = asn == pfaceoff || asn == pfaceoff2;
    int fx = (int16_t)g.fox, fy = (int16_t)g.foy;
    int px = pk->Xpos, py = pk->Ypos, cy = (int16_t)g.Vpos;
    int ax_, ay_, ad_, am_;
    if (hold_anthem_ref(&ax_, &ay_, &ad_, &am_)) {                           /* (the anthem: at the side with the players in their lines) */
        R.x = ax_ * 256; R.y = ay_ * 256; R.vx = R.vy = 0;
        R.dir = ad_; R.t = am_ ? R.t + 1 : 0; R.wasfo = 1; R.drop = 0; R.init = 1;
    } else if (fo) {
        int tx = (fx > 0 ? fx + 15 : fx - 15) * 256, ty = fy * 256;
        int pr = hold_skate_ref();                                           /* (the between-play pause: he skates to the dot with the players) */
        if (pr >= 0 && pr < 256 && R.init) {
            static int fx0, fy0, started;
            if (!started || !R.wasfo) { fx0 = R.x; fy0 = R.y; started = 1; }
            int pkx, pky, pick;
            hold_skate_puck(&pkx, &pky, &pick);
            pkx *= 256; pky *= 256;
            int sx, sy, ex, ey, k, kn;                                       /* (to the puck where it lay, then with it to the dot) */
            if (pr < pick) { sx = fx0; sy = fy0; ex = pkx; ey = pky; k = pr; kn = pick; }
            else { sx = pkx; sy = pky; ex = tx; ey = ty; k = pr - pick; kn = 256 - pick; }
            double ddx = ex - sx, ddy = ey - sy;
            R.x = (int)(sx + ddx * k / kn);
            R.y = (int)(sy + ddy * k / kn);
            if (ddx * ddx + ddy * ddy > 256.0 * 256.0 * 16) {
                int d = (int)floor(atan2(ddx, ddy) / (M_PI / 4) + 0.5);
                R.dir = ((d % 8) + 8) % 8;
                R.t++;
            }
        } else {
            R.x = tx;
            R.y = ty;
            R.t = 0;
            R.dir = fx > 0 ? 6 : 2;
        }
        R.vx = R.vy = 0;
        R.wasfo = 1; R.drop = 0; R.init = 1;
    } else {
        if (!R.init) { R.x = (px > 0 ? 0x7C : -0x7C) * 256; R.y = cy * 256; R.init = 1; R.vx = R.vy = 0; }
        if (R.wasfo) { R.wasfo = 0; R.drop = 24; }                           /* (the puck is dropped: the referee's drop) */
        int rx = R.x / 256, ry = R.y / 256, tx, ty;
        int ax = px < 0 ? -px : px, ay = py < 0 ? -py : py;
        if (ax < 0x32 && ay > 0xAC && ay < 0xE8) { tx = rx > 0 ? 0x46 : -0x46; ty = py > 0 ? 0xED : -0xED; }          /* behind the net */
        else if (ax > 0x62 && ay > 0x80) { tx = rx > 0 ? 0x6A : -0x6A; ty = py > 0 ? 0xE8 : -0xE8; }              /* in a corner */
        else { tx = rx > 0 ? 0x7C : -0x7C; ty = (ry - cy < 0 ? cy - ry : ry - cy) > 0x32 ? cy : ry; }          /* beside the play */
        int dx = tx * 256 - R.x, dy = ty * 256 - R.y;
        int wx = dx / 12, wy = dy / 12, m = 2 * 256;                              /* (the speed it wants: easing in, at most 2 a frame) */
        double l = sqrt((double)wx * wx + (double)wy * wy);
        if (l > m) { wx = (int)(wx * m / l); wy = (int)(wy * m / l); }
        R.vx += (wx - R.vx) / 6;
        R.vy += (wy - R.vy) / 6;
        R.x += R.vx; R.y += R.vy;
        double sp = sqrt((double)R.vx * R.vx + (double)R.vy * R.vy) / 256.0;
        if (sp > 0.45) {
            int d = (int)floor(atan2((double)R.vx, (double)R.vy) / (M_PI / 4) + 0.5);
            R.dir = ((d % 8) + 8) % 8;
        }
        if (R.drop > 0) { R.drop--; R.vx = R.vy = 0; }
        R.t = sp > 0.45 ? R.t + 1 : 0;
    }
    int16_t x = (int16_t)(R.x / 256), y = (int16_t)(R.y / 256);
    find3d(&x, &y, 0);
    if (y == 0x4E20) return;
    R.sx = x + view_shift();
    if (getenv("PCREF_AT")) { int ax, ay; if (sscanf(getenv("PCREF_AT"), "%d,%d", &ax, &ay) == 2) { R.sx = ax; R.sy = ay; } }   /* (a test: the referee put there) */
    R.sy = y;
    R.valid = 1;
    if (getenv("PCREF_LOG") && !(g.Vcount & 63)) fprintf(stderr, "ref: rink %d,%d screen %d,%d dir %d t %d fo %d puck %d,%d view %d,%d\n", R.x / 256, R.y / 256, R.sx, R.sy, R.dir, R.t, fo, px, py, (int16_t)g.Hpos, (int16_t)g.Vpos);
}

static int ref_pos(int *x, int *y)
{
    if (!SDL_AtomicGet(&R.on) || !R.init) return 0;
    *x = R.x / 256; *y = R.y / 256;
    return 1;
}

/* the frame the referee shows */
static int ref_frame(void)
{
    const struct SortObj *pk = sortobj(puckscnum);
    int asn = pk->asslist[pk->assnum & 7];
    int d = R.dir & 7;
    if (hold_anthem_time() >= 0) return idle_ref(hold_anthem_time());                                             /* (the anthem: the referee's standing picture) */
    if ((asn == pfaceoff || asn == pfaceoff2) && R.t == 0) return (d >= 3 && d <= 6) ? 711 : 703;       /* (still: skating to the dot, he is not) */       /* (SPAreffowait) */
    if (R.drop > 0) {                                                                    /* (SPArefdrop: 8 ticks each) */
        int k = (24 - R.drop) / 8;
        if (d >= 3 && d <= 6) return k == 0 ? 845 : k == 1 ? 846 : 711;
        return k == 0 ? 843 : k == 1 ? 844 : 703;
    }
    int b = 5 * ((8 - d) & 7);
    if (R.t == 0) return 650 + b;                                                      /* (SPArefglide) */
    static const int o[4] = { 2, 3, 0, 1 };
    return 651 + b + o[(R.t / 6) & 3];                                                   /* (SPArefskate) */
}

/* the sprite table entries of the last sprite the hook took */
static int hook(int ev, int sc, int16_t x, int16_t y, const struct SpriteHead *o)
{
    if (ev == SPR_BEGIN) { P.nshow = 0; return 0; }
    if (ev == SPR_END) {
        ref_tick();
        if (!P.nshow && (g.sflags & HI(sfpz))) return 0;                   /* (the pause: a pass that took none keeps the last ones) */
        SDL_AtomicLock(&P.lock);
        memmove(&P.ring[1], &P.ring[0], sizeof P.ring - sizeof P.ring[0]);
        memcpy(P.ring[0].l, P.show, sizeof P.show);
        P.ring[0].n = P.nshow;
        SDL_AtomicUnlock(&P.lock);
        return 0;
    }
    if (ev == SPR_MARK) {                                 /* sc = the first entry, x = how many */
        if (P.nshow > 0) {
            P.show[P.nshow - 1].first = (int16_t)sc; P.show[P.nshow - 1].count = x;
            if (o) memcpy(P.show[P.nshow - 1].sig, o, 8);
        }
        return 0;
    }
    if (!SDL_AtomicGet(&P.on) || !SDL_AtomicGet(&P.found) || !g.demoflag || sc > 13 || P.nshow >= 16) return 0;
    int pf = pc_frame(o->frame & 0x7FF);
    if (hold_anthem_time() >= 0 && sc < 12 && sortobj(sc)->position > 0) pf = idle_skater(sc, hold_anthem_time());      /* (the anthem: the PC's standing picture, number 369) */
    if (pf < 0 || pf >= NFRAMES || !P.f[pf].px) return 0;   /* (no such PC frame: the cartridge's sprite stays) */
    P.show[P.nshow].pc = (int16_t)pf;
    P.show[P.nshow].x = x;
    P.show[P.nshow].y = y;
    P.show[P.nshow].flip = (o->attribute & HI(3)) != 0;
    P.show[P.nshow].away = (sc >= 6 && sc != 12) ? 1 : 0;
    P.show[P.nshow].sc = (int16_t)sc;
    P.show[P.nshow].first = 0;
    P.show[P.nshow].count = 0;
    P.nshow++;
    return 1;
}

void pcspr_start(void)
{
    hold_ref_pos_fn = ref_pos;
    static int started;
    if (started) return;
    started = 1;
    spr_hook = hook;
    char *base = SDL_GetBasePath();
    SDL_Thread *t = SDL_CreateThread(finder, "pcspr", base ? SDL_strdup(base) : SDL_strdup("."));
    if (base) SDL_free(base);
    if (t) SDL_DetachThread(t);
}
void pcspr_wait(void) { while (!SDL_AtomicGet(&P.searched)) SDL_Delay(5); }
int pcspr_searched(void) { return SDL_AtomicGet(&P.searched); }
int pcspr_found(void) { return SDL_AtomicGet(&P.searched) && SDL_AtomicGet(&P.found); }
void pcspr_set(int on) { SDL_AtomicSet(&P.on, on ? 1 : 0); }
void pcspr_ref_set(int on) { SDL_AtomicSet(&R.on, on ? 1 : 0); }
void pcspr_logo_set(int on) { SDL_AtomicSet(&LGO.on, on < 0 ? 0 : on > 2 ? 2 : on); }
int pcspr_logos(void) { return LGO.L[0].have ? 1 + LGO.L[1].have : 0; }


/* Masks of where a solid sprite of nx x ny 32 pixel pieces at x0, y0 would show in the picture of vdp: back: put last in the sprite
 * order (it shows over only the ice: nothing with priority and no sprite is over it); front: put first (it shows over the sprites
 * too). Each a picture against base, the picture with it hidden. 0 when the sprite table has no room for it. */
static int sprite_masks(const mdrt_vdp *vdp, int x0, int y0, int nx, int ny, uint16_t *base, uint16_t *back, uint16_t *front, int want_front)
{
    static mdrt_vdp sc;
    unsigned sat = (unsigned)(mdrt_vdp_reg(vdp, 5) & (mdrt_vdp_width(vdp) == 320 ? 0x7E : 0x7F)) << 9;
    unsigned tile = 0x7E0;
    while (tile > 0x40 && tile * 32 < sat + 0x280 && sat < tile * 32 + 0x200) tile -= 0x10;     /* (the solid tiles: not over the sprite table) */
    int tail = 0;                                                   /* (the last entry of the sprite table's chain; the entries after it are free) */
    for (int k = 0; k < 80; k++) { int l = vdp->sat[tail * 8 + 3] & 0x7F; if (!l) break; tail = l; }
    int cnt = nx * ny, fb = tail + 1;                               /* the free entries fb ... : the pieces, then the moved first entry */
    if (fb + cnt + 1 > 80) return 0;
    for (int variant = 0; variant < (want_front ? 3 : 2); variant++) {                /* 0: the copy without it, 1: last, 2: first */
        sc = *vdp;
        for (int t = 0; t < 16; t++) memset(sc.s.vram + tile * 32 + t * 32, 0xFF, 32);
        sc.s.cram[15] = 0x0E0E;
        if (variant) {
            int old0 = fb + cnt, first;
            if (variant == 2) {                                    /* the old first entry moves behind the pieces */
                memcpy(sc.s.vram + ((sat + old0 * 8) & 0xFFFF), vdp->s.vram + ((sat + 0) & 0xFFFF), 8);
                memcpy(sc.sat + old0 * 8, vdp->sat, 8);
                first = 0;
            } else {
                first = fb;
                sc.sat[tail * 8 + 3] = (uint8_t)fb;
                sc.s.vram[(sat + tail * 8 + 3) & 0xFFFF] = (uint8_t)fb;
            }
            for (int i = 0; i < cnt; i++) {
                int e = (variant == 2 && i == 0) ? 0 : fb + i;
                int nxt = i + 1 < cnt ? fb + i + 1 : (variant == 2 ? old0 : 0);
                int sy = y0 + 128 + 32 * (i / nx), sx = x0 + 128 + 32 * (i % nx);
                uint8_t *a = sc.s.vram + ((sat + e * 8) & 0xFFFF);
                a[0] = (uint8_t)(sy >> 8); a[1] = (uint8_t)sy; a[2] = 0x0F; a[3] = (uint8_t)nxt;
                a[4] = (uint8_t)(tile >> 8); a[5] = (uint8_t)tile;                /* (palette 0, low priority, tiles solid: 16 cells) */
                a[6] = (uint8_t)(sx >> 8); a[7] = (uint8_t)sx;
                memcpy(sc.sat + e * 8, a, 4);
            }
            (void)first;
        }
        mdrt_vdp_render(&sc, variant == 0 ? base : variant == 1 ? back : front, 320);
    }
    return 1;
}

/* the referee over the finished picture: only where nothing of the picture's own is over the ice there. A solid sprite of the
 * referee's size is put into a copy of the VDP, last in the sprite order (where it shows, it is behind every sprite, over only
 * the ice) and first (where it shows, it is in front of the sprites and of what the planes draw with priority); the two pictures
 * against the copy without it say where. Where it is in front of the sprites, a skater below it (nearer) still covers it. */
static void ref_draw(const mdrt_vdp *vdp, uint32_t *pic, int w, int h, int pitch, const shown *list, int n)
{
    if (!SDL_AtomicGet(&R.on) || !R.valid) return;
    int fr = ref_frame();
    if (fr < 0 || fr >= NFRAMES || !P.f[fr].px) return;
    const spr *s = &P.f[fr];
    const uint8_t *rec = P.team[0][9];
    int x0 = R.sx - 128 - s->xo, y0 = R.sy - 128 - s->yo;
    int nx = (s->w + 31) / 32, ny = (s->h + 31) / 32;
    if (nx * ny > 6) return;
    static uint16_t base[320 * 240], back[320 * 240], front[320 * 240];
    if (!sprite_masks(vdp, x0, y0, nx, ny, base, back, front, 1)) return;
    uint8_t pal[256][3];
    memcpy(pal, P.pal, sizeof pal);
    if (getenv("PCREF_LOG")) { int nb = 0, nf = 0; for (int i = 0; i < 320 * 240; i++) { nb += back[i] != base[i]; nf += front[i] != base[i]; } fprintf(stderr, "refdraw: frame %d at %d,%d size %dx%d back %d front %d\n", fr, x0, y0, s->w, s->h, nb, nf); }
    int win = 0;                                                         /* (a window up in the corner: the penalty referee's or the face off's, drawn by the plane: nothing of the on ice referee over it) */
    {
        const uint8_t *vr = vdp->s.vram;
        unsigned a = (unsigned)g.maps[0].vram;
        for (int row = 2; row < 8 && !win; row++)
            for (int col = 1; col < 10 && !win; col++) {
                unsigned o = (a + (((unsigned)row << g.maps[0].colshift) + (unsigned)col) * 2u) & 0xFFFF;
                unsigned t = (unsigned)(vr[o] << 8 | vr[o + 1]);
                if ((t & 0x8000) && (t & 0x7FF) != 0x7FF) win = 1;
            }
    }
    static uint8_t okm[256 * 256];                                       /* which of the picture's pixels show; then the holes in it closed: a dither of the cartridge's own shading over the ice (the boards' corners) would speckle him */
    if (s->w > 256 || s->h > 256) return;
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            okm[y * 256 + x] = 0;
            int v = s->px[y * s->w + x];
            if (v == 255) continue;
            int px_ = x0 + x, py = y0 + y;
            if (px_ < 0 || px_ >= w || py < 0 || py >= h) continue;
            if (win && px_ < 112 && py < 112) continue;
            int p = py * 320 + px_;
            int ok = back[p] != base[p];
            if (!ok && front[p] != base[p]) {                      /* in front of a sprite or an overlay: if no skater nearer covers this */
                ok = 1;
                for (int i = 0; i < n && ok; i++) {
                    const spr *o = &P.f[list[i].pc];
                    int ox = list[i].x - 128 - (list[i].flip ? o->w - 1 - o->xo : o->xo), oy = list[i].y - 128 - o->yo;
                    if (px_ >= ox && px_ < ox + o->w && py >= oy && py < oy + o->h && list[i].y >= R.sy) ok = 0;
                }
            }
            okm[y * 256 + x] = (uint8_t)ok;
        }
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            int v = s->px[y * s->w + x];
            if (v == 255) continue;
            int px_ = x0 + x, py = y0 + y;
            if (px_ < 0 || px_ >= w || py < 0 || py >= h) continue;
            if (!okm[y * 256 + x]) {
                if (win && px_ < 112 && py < 112) continue;
                int near = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        if ((dx || dy) && x + dx >= 0 && x + dx < s->w && y + dy >= 0 && y + dy < s->h) near += okm[(y + dy) * 256 + x + dx];
                if (near < 4) continue;                              /* (a straight edge has 3 over it at most) */
            }
            int r = v < 0x80 ? rec[192 + v] : rec[320 + v - 0x80];
            pic[(long)py * pitch + px_] = fadec(0xFF000000u | (uint32_t)pal[r][0] << 16 | (uint32_t)pal[r][1] << 8 | pal[r][2]);
        }
}


/* the first of the 24 crest tiles the cartridge draws at centre ice for team art: found in the VDP's memory by their pixels (the art's
 * own 24 tiles, as the cartridge has them for the team) */
static int find_crest(const mdrt_vdp *v, int art)
{
    struct romsrc rs;
    static uint8_t px[16 * 8 * 16 * 8], tiles[ART_ICE_BYTES];
    uint16_t pal[16];
    int w, h;
    romsrc_game(&rs);
    if (art_ice(&rs, art, px, &w, &h, pal) < 0 || art_ice_make(&rs, px, w, h, tiles) < 0) return -1;
    for (unsigned a = 0; a + 24 * 32 <= 0x10000; a += 32)
        if (!memcmp(v->s.vram + a, tiles + 10, 24 * 32)) return (int)(a / 32);
    return -1;
}

/* the cartridge's crest at centre ice taken out of the picture and NHL 95 PC's logo of the home team put in its place, over only the
 * rink: under the windows, the scoreboard and the players as the crest was. Called before the PC's players are drawn. */
static void logo_draw(const mdrt_vdp *vdp, const uint16_t *px, int pitch_px, uint32_t *pic, int w, int h, int pitch)
{
    int mode = SDL_AtomicGet(&LGO.on);
    if (!mode || !LGO.L[0].have) return;
    int art = rosters_art(team_struct(0)->tmteam);
    if (art < 0 || art >= 28 || logo_of_art[art] < 0) return;
    int k = logo_of_art[art];
    const struct lset *LS = mode == 2 && LGO.L[1].have && LGO.L[1].t[k].idx ? &LGO.L[1] : &LGO.L[0];   /* (94 falls back to 95 where it has no such logo) */
    if (!LS->t[k].idx) return;
    if (LGO.crest < 0 || !(g.Vcount & 63) || LGO.seen != art) { LGO.crest = find_crest(vdp, art); LGO.seen = art; }
    if (getenv("PCLOGO_LOG") && !(g.Vcount & 63)) fprintf(stderr, "logo? art %d crest %d\n", art, LGO.crest);
    if (LGO.crest < 0) return;
    static mdrt_vdp sc;
    static uint16_t blank[320 * 240], base[320 * 240], back[320 * 240], front[320 * 240];
    sc = *vdp;
    for (int t = 0; t < 24; t++) memset(sc.s.vram + (((unsigned)(LGO.crest + t) * 32) & 0xFFFF), 0, 32);
    mdrt_vdp_render(&sc, blank, 320);
    int x0 = 1 << 20, y0 = 1 << 20, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++)                                      /* (the crest: where taking its tiles out changes the picture) */
        for (int x = 0; x < w; x++)
            if (blank[y * 320 + x] != px[y * pitch_px + x]) {
                pic[(long)y * pitch + x] = 0xFF000000u | mdrt_vdp_rgb(blank[y * 320 + x], MDRT_RGB_GPGX);
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    int16_t cx = 0, cy = 0;
    find3d(&cx, &cy, 0);
    if (getenv("PCLOGO_LOG") && !(g.Vcount & 63)) fprintf(stderr, "logo: art %d crest tile %d bbox %d,%d-%d,%d find3d %d,%d shift %d\n", art, LGO.crest, x0, y0, x1, y1, cx + view_shift(), cy, view_shift());
    int lw = LS->t[k].w, lh = LS->t[k].h;
    int lx, ly;
    if (cy == 0x4E20) return;
    lx = cx + view_shift() - 128 - lw / 2;
    ly = cy - 128 - lh / 2;
    if (x1 >= 0 && x1 - x0 >= 40) { lx = (x0 + x1 + 1) / 2 - lw / 2; ly = (y0 + y1 + 1) / 2 - lh / 2; }   /* (the crest's own place, when it is all in view) */
    int nx = (lw + 31) / 32, ny = (lh + 31) / 32;
    if (nx * ny > 6 || !sprite_masks(&sc, lx, ly, nx, ny, base, back, front, 0)) return;
    for (int y = 0; y < lh; y++)
        for (int x = 0; x < lw; x++) {
            int c = LS->t[k].idx[y * lw + x];
            if (c == 255) continue;                                                    /* (clear) */
            int ppx = lx + x, ppy = ly + y;
            if (ppx < 0 || ppx >= w || ppy < 0 || ppy >= h || back[ppy * 320 + ppx] == base[ppy * 320 + ppx]) continue;
            {
                const uint8_t *pc = LS == &LGO.L[0] && SDL_AtomicGet(&P.found) ? P.pal[c] : LS->pal[c];   /* (the PC's own rink palette: RINK.PAL is a rougher 5 bit copy of it) */
                pic[(long)ppy * pitch + ppx] = fadec(0xFF000000u | (uint32_t)pc[0] << 16 | (uint32_t)pc[1] << 8 | pc[2]);
            }
        }
    /* the red centre line was part of the crest's tiles: draw it again across the gap in each row, from the 8 columns of it left of the gap (period 8) */
    if (x1 - x0 >= 40 && x0 >= 48 && x1 + 48 < w)
        for (int y = y0; y <= y1 && y < h; y++) {
#define LINERED(xx) ({ uint32_t q_ = pic[(long)y * pitch + (xx)]; int r_ = (q_ >> 16) & 255, g_ = (q_ >> 8) & 255, b_ = q_ & 255; r_ > 190 && r_ - g_ > 60 && r_ - b_ > 60; })
            int xl = -1, xr = -1;
            for (int x = x0 + 8; x >= x0 - 40 && xl < 0; x--) if (LINERED(x)) xl = x;
            for (int x = x1 - 8; x <= x1 + 40 && xr < 0; x++) if (LINERED(x)) xr = x;
            if (xl < 0 || xr < 0 || xr - xl < 12) continue;
            int red = 0;
            for (int x = xl - 7; x <= xl; x++) red += LINERED(x);
            if (red < 3) continue;
            for (int x = xl + 1; x < xr; x++) pic[(long)y * pitch + x] = pic[(long)y * pitch + xl - 7 + ((x - xl - 1) & 7)];
#undef LINERED
        }
}

/* Over the picture the game drew (px: its colours, pic: the same as RGB). The cartridge's own sprites of the objects drawn here are
 * still in it: the picture is drawn again from a copy of the VDP with those sprite table entries hidden, and where the two differ
 * the cartridge's sprite was visible (not under a window, the face off box or another sprite). The PC's sprite is drawn only there
 * and a few pixels around (the two shapes differ a little); everything else of the cartridge's sprites is given back its background. */
/* the anthem's flag over the middle of the ice, between the two lines of players, waving (the PC's four pictures, each shown for two steps of 12/100 s) */
static void flag_draw(uint32_t *pic, int w, int h, int pitch)
{
    int t = hold_anthem_time();
    if (getenv("PCFLAG_LOG")) fprintf(stderr, "flag: t %d have %d flag %d\n", t, have_flag, t < 0 ? -9 : pcsong_flag_at(t));
    if (t < 0 || !have_flag) return;
    int k = pcsong_flag_at(t);
    if (k < 0) return;
    const spr *f = &FLG[k][(t * 100 / 60 / 12 / 2) % nflag[k]];
    if (!f->px) return;
    /* the PC's overlay box (DrawTextOverlay): top left at 12,12, 123 x 75, a 4 pixel black frame; it grows from its middle
     * (74,50) over the first 16 steps; the picture is drawn at the box's corner */
    int bx = 12, by = 12, bw = 0x7B, bh = 0x4B;
    int k16 = t < 16 ? t : 16;
    int gx0 = bx, gx1 = bx + bw, gy0 = by, gy1 = by + bh;
    if (k16 < 16) {
        int hw = k16 * 0x42 / 16, hh = k16 * 0x2A / 16;
        gx0 = 74 - hw; gx1 = 74 + hw; gy0 = 50 - hh; gy1 = 50 + hh;
    }
    for (int y = gy0 - 4; y < gy1 + 4; y++)
        for (int x = gx0 - 4; x < gx1 + 4; x++) {
            if (x < 0 || x >= w || y < 0 || y >= h) continue;
            int in = x >= gx0 && x < gx1 && y >= gy0 && y < gy1;
            if (in && k16 < 16) { uint32_t c = pic[(long)y * pitch + x]; pic[(long)y * pitch + x] = 0xFF000000u | ((c >> 1) & 0x7F7F7Fu); }
            else if (!in) pic[(long)y * pitch + x] = 0xFF000000u;
        }
    for (int y = 0; y < f->h && k16 >= 16; y++)
        for (int x = 0; x < f->w; x++) {
            int v = f->px[y * f->w + x];
            int px_ = bx + x, py = by + y;
            if (v == 255 || px_ < 0 || px_ >= w || py < 0 || py >= h) continue;
            pic[(long)py * pitch + px_] = fadec(0xFF000000u | (uint32_t)P.pal[v][0] << 16 | (uint32_t)P.pal[v][1] << 8 | P.pal[v][2]);
        }
}

/* Where the top of each PC skater's picture reaches past the cartridge sprite's own tiles (the PC's heads are taller): solid 32 pixel pieces
 * over the pictures' top rows are put last in the sprite order of a copy of the VDP; where they show against the real picture they are over
 * only the ice, with no sprite or priority tile over them. 0 when the sprite table has no room. */
static int ext_masks(const mdrt_vdp *vdp, const shown *list, int n, uint16_t *back)
{
    static mdrt_vdp sc;
    unsigned sat = (unsigned)(mdrt_vdp_reg(vdp, 5) & (mdrt_vdp_width(vdp) == 320 ? 0x7E : 0x7F)) << 9;
    unsigned tile = 0x7E0;
    while (tile > 0x40 && tile * 32 < sat + 0x280 && sat < tile * 32 + 0x200) tile -= 0x10;
    int tail = 0;
    for (int k = 0; k < 80; k++) { int l = vdp->sat[tail * 8 + 3] & 0x7F; if (!l) break; tail = l; }
    int fb = tail + 1, cnt = 0;
    sc = *vdp;
    for (int t = 0; t < 16; t++) memset(sc.s.vram + tile * 32 + t * 32, 0xFF, 32);
    sc.s.cram[15] = 0x0E0E;
    for (int i = 0; i < n; i++) {
        const spr *s = &P.f[list[i].pc];
        int x0 = list[i].x - 128 - (list[i].flip ? s->w - 1 - s->xo : s->xo), y0 = list[i].y - 128 - s->yo - 2;
        for (int k = 0; k * 32 < s->w && fb + cnt < 79; k++, cnt++) {
            int e = fb + cnt, sy = y0 + 128, sx = x0 + 32 * k + 128;
            uint8_t *a = sc.s.vram + ((sat + (unsigned)e * 8) & 0xFFFF);
            a[0] = (uint8_t)(sy >> 8); a[1] = (uint8_t)sy; a[2] = 0x0F; a[3] = 0;
            a[4] = (uint8_t)(tile >> 8); a[5] = (uint8_t)tile;
            a[6] = (uint8_t)(sx >> 8); a[7] = (uint8_t)sx;
            memcpy(sc.sat + e * 8, a, 4);
            if (cnt) { sc.s.vram[(sat + (unsigned)(e - 1) * 8 + 3) & 0xFFFF] = (uint8_t)e; sc.sat[(e - 1) * 8 + 3] = (uint8_t)e; }
        }
    }
    if (!cnt) return 0;
    sc.sat[tail * 8 + 3] = (uint8_t)fb;
    sc.s.vram[(sat + (unsigned)tail * 8 + 3) & 0xFFFF] = (uint8_t)fb;
    mdrt_vdp_render(&sc, back, 320);
    return 1;
}

void pcspr_present(const mdrt_vdp *vdp, const uint16_t *px, int pitch_px, uint32_t *pic, int w, int h, int pitch)
{
    if ((!SDL_AtomicGet(&P.on) && !SDL_AtomicGet(&R.on) && !SDL_AtomicGet(&LGO.on) && hold_anthem_time() < 0) || !SDL_AtomicGet(&P.found) || !in_game()) return;
    fade_update(vdp);
    shown list[16];
    int n = 0;
    unsigned sat0 = (unsigned)(mdrt_vdp_reg(vdp, 5) & (mdrt_vdp_width(vdp) == 320 ? 0x7E : 0x7F)) << 9;
    SDL_AtomicLock(&P.lock);
    for (int r = 0; r < 4; r++) {                                /* the newest frame whose sprites are the ones in the sprite table now */
        int ok = P.ring[r].n > 0;
        for (int i = 0; ok && i < P.ring[r].n; i++) {
            unsigned e = (unsigned)P.ring[r].l[i].first;
            if (e >= 80 || memcmp(vdp->s.vram + ((sat0 + e * 8) & 0xFFFF), P.ring[r].l[i].sig, 8)) ok = 0;
        }
        if (ok) { memcpy(list, P.ring[r].l, sizeof list); n = P.ring[r].n; if (getenv("PCSPR_LOG") && r) fprintf(stderr, "pcspr: frame %d old\n", r); break; }
    }
    SDL_AtomicUnlock(&P.lock);
    if (!n && getenv("PCSPR_LOG") && P.ring[0].n > 0) {
        fprintf(stderr, "pcspr: no match (%d, sat %04X, first %d)", P.ring[0].n, sat0, P.ring[0].l[0].first);
        for (int k = 0; k < 8; k++) fprintf(stderr, " %02X/%02X", P.ring[0].l[0].sig[k], vdp->s.vram[(sat0 + (unsigned)P.ring[0].l[0].first * 8 + (unsigned)k) & 0xFFFF]);
        fprintf(stderr, "\n");
    }
    if (w > 320 || h > 240) return;
    logo_draw(vdp, px, pitch_px, pic, w, h, pitch);
    static mdrt_vdp scratch;
    if (!SDL_AtomicGet(&P.on)) n = 0;
    if (n <= 0) { ref_draw(vdp, pic, w, h, pitch, list, 0); flag_draw(pic, w, h, pitch); return; }
    static uint16_t clean[320 * 240];
    static uint8_t vis[320 * 240];
    scratch = *vdp;
    unsigned sat = (unsigned)(mdrt_vdp_reg(vdp, 5) & (mdrt_vdp_width(vdp) == 320 ? 0x7E : 0x7F)) << 9;
    for (int i = 0; i < n; i++)
        for (int k = 0; k < list[i].count; k++) {
            unsigned e = (unsigned)(list[i].first + k);
            if (e >= 80) continue;
            scratch.s.vram[(sat + e * 8) & 0xFFFF] = 0;          /* y = 0: off the top */
            scratch.s.vram[(sat + e * 8 + 1) & 0xFFFF] = 0;
            scratch.sat[e * 8] = scratch.sat[e * 8 + 1] = 0;
        }
    mdrt_vdp_render(&scratch, clean, 320);
    /* Where the listed sprites really are on top, as the VDP sees it (plane priority, other sprites, window): their tiles are
     * filled solid in one exotic colour and the picture drawn once more. The PC sprites may cover only those pixels. */
    static uint16_t solid[320 * 240];
    scratch = *vdp;
    uint16_t exo = 0x0E0E;
    int lines_used[4] = {0, 0, 0, 0};
    for (int i = 0; i < n; i++)
        for (int k = 0; k < list[i].count; k++) {
            unsigned e = (unsigned)(list[i].first + k);
            if (e >= 80) continue;
            const uint8_t *a8 = vdp->s.vram + 0;
            unsigned o = (sat + e * 8) & 0xFFFF;
            int sz = a8[(o + 2) & 0xFFFF], tw = ((sz >> 2) & 3) + 1, th = (sz & 3) + 1;
            unsigned attr = (unsigned)a8[(o + 4) & 0xFFFF] << 8 | a8[(o + 5) & 0xFFFF];
            unsigned tile = attr & 0x7FF;
            lines_used[(attr >> 13) & 3] = 1;
            for (int t = 0; t < tw * th; t++)
                memset(scratch.s.vram + (((tile + (unsigned)t) * 32) & 0xFFFF), 0xFF, 32);
        }
    for (int l = 0; l < 4; l++) if (lines_used[l]) scratch.s.cram[l * 16 + 15] = exo;
    mdrt_vdp_render(&scratch, solid, 320);
    static uint16_t extb[320 * 240];
    int have_ext = ext_masks(vdp, list, n, extb);
    memset(vis, 0, sizeof vis);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (px[y * pitch_px + x] != clean[y * 320 + x]) {
                vis[y * 320 + x] = 1;
                pic[(long)y * pitch + x] = 0xFF000000u | mdrt_vdp_rgb(clean[y * 320 + x], MDRT_RGB_GPGX);
            }
    for (int i = n - 1; i >= 0; i--) {                           /* (the sprite table's first is on top: drawn last) */
        const spr *s = &P.f[list[i].pc];
        int team = list[i].away ? (int)rosters_art(team_struct(1)->tmteam) : (int)rosters_art(team_struct(0)->tmteam);
        if (team < 0 || team >= NTEAMS) team = 0;
        team = pc_team[team];
        const uint8_t *rec = P.team[list[i].away][team];
        uint8_t pal[256][3];
        memcpy(pal, P.pal, sizeof pal);
        for (int c = 0; c < 64; c++)
            for (int k = 0; k < 3; k++) { int v = rec[c * 3 + k] & 63; pal[0x80 + c][k] = (uint8_t)(v << 2 | v >> 4); }
        int x0 = list[i].x - 128 - (list[i].flip ? s->w - 1 - s->xo : s->xo), y0 = list[i].y - 128 - s->yo;
        for (int y = 0; y < s->h; y++)
            for (int x = 0; x < s->w; x++) {
                int v = s->px[y * s->w + (list[i].flip ? s->w - 1 - x : x)];
                if (v == 255) continue;
                int r = v < 0x80 ? rec[192 + v] : rec[320 + v - 0x80];
                int px_ = x0 + x, py = y0 + y;
                if (px_ < 0 || px_ >= w || py < 0 || py >= h) continue;
                if (solid[py * 320 + px_] == clean[py * 320 + px_] && !(have_ext && y < 30 && extb[py * 320 + px_] != px[py * pitch_px + px_])) continue;   /* (inside the cartridge sprite's tiles, or over the ice above them) */
                pic[(long)py * pitch + px_] = fadec(0xFF000000u | (uint32_t)pal[r][0] << 16 | (uint32_t)pal[r][1] << 8 | pal[r][2]);
            }
    }
    ref_draw(vdp, pic, w, h, pitch, list, n);
    flag_draw(pic, w, h, pitch);
}
