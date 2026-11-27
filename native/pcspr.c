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
#include "fight93.h"
#include "anim_gen.h"
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

/* the referee: not in the cartridge's game, only in this picture (a visual one: nothing in the game knows of it), unless
 * RULE_REFEREE puts him in play (mods/referee.c): then, in play, he is drawn where the game has him */
static struct {
    SDL_atomic_t on, phys;                  /* phys: skaters bump him (ref_bump), the picture's alone */
    int init, x, y, vx, vy;                 /* the rink position and velocity in 1/256 pixels */
    int dir, t, wasfo, drop, fall;          /* fall: the frames into his fall (RULE_REFEREE), -1 standing; t: the frames into his picture */
    int anim;                               /* his picture: REF_GLIDE, REF_SKATE, REF_STOP, REF_TURNL / R (src/rules.h) */
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

#define ICE(c) (kind ? ((c) == 106 || (c) == 107 || (c) == 105 || (c) == 104 || (c) == 23 || (c) == 91 || (c) == 90) : ((c) == 102 || (c) == 103))   /* (95: the ice's two whites; its 96 is the centre line's red and 23 / 91 the blue centre dot, which stay) */
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
                    if (lw > 0 && lh > 0 && lw <= 12 && lh <= 12 && mn >= 6 + 2 * lw * lh) {
                        uint8_t *ix = malloc((size_t)(lw * 8 * lh * 8));
                        if (ix) {
                            for (int ty = 0; ty < lh; ty++)
                                for (int tx = 0; tx < lw; tx++) {
                                    int wd = mb[6 + 2 * (ty * lw + tx)] | mb[7 + 2 * (ty * lw + tx)] << 8;
                                    int ti = kind ? wd & 0x7FF : wd & 0x3FF, hfl = wd & (kind ? 0x0800 : 0x2000), vfl = wd & (kind ? 0x1000 : 0x4000);
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

/* A fade (between plays, the pause, to and from the penalty screen): cramfade steps each colour's components toward palfadenew, one
 * step of 2 a time, red, green and blue in turn. The PC's pictures, which are not in colour RAM, are dimmed the same way: toward black,
 * each component less by the steps taken so far (the most any colour has come down from the colours before the fade); from black, each
 * component at most as bright as the brightest the colours have come up to. Per component: fade_sub[k] / fade_cap[k] in 8 bits. */
static int fade_on, fade_black, fade_sub[3], fade_cap[3] = { 255, 255, 255 };
static uint32_t fadec(uint32_t c)
{
    if (!fade_on) return c;
    uint32_t o = 0xFF000000u;
    for (int k = 0; k < 3; k++) {
        int sh = 16 - 8 * k, v = (int)(c >> sh & 255) - fade_sub[k];
        if (v < 0) v = 0;
        if (v > fade_cap[k]) v = fade_cap[k];
        o |= (uint32_t)v << sh;
    }
    return o;
}
static int lvl(unsigned c, int k) { return (int)(c >> (4 * k + 1) & 7); }   /* (component k, 0 red, of a bus format colour: 0..7) */
static void fade_update(const mdrt_vdp *vdp)
{
    static uint16_t steady[64];                             /* the colours the last time no fade ran and they were lit */
    static int have_steady;
    int lit_cur = 0, lit_tgt = 0, lit_st = 0;
    for (int i = 0; i < 64; i++)
        for (int k = 0; k < 3; k++) { lit_cur += lvl(vdp->s.cram[i], k); lit_tgt += lvl(g.palfadenew[i], k); lit_st += have_steady ? lvl(steady[i], k) : 0; }
    int fading = (int16_t)g.palcount >= 0;
    int before = lit_st > 0 ? 1 : lit_tgt > 0 ? 2 : 0;      /* (the colours the fade comes from: the steady ones, else palfadenew) */
    if (!fading && !(before && lit_cur * 8 < (before == 1 ? lit_st : lit_tgt))) {   /* (no fade, and not left black by one: forceblack keeps palfadenew) */
        memcpy(steady, vdp->s.cram, sizeof steady);
        have_steady = lit_cur > 0;
        fade_on = 0; fade_black = lit_cur == 0;
        return;
    }
    fade_on = 1;
    fade_black = lit_cur == 0;
    for (int k = 0; k < 3; k++) {
        int down = 0, up = 0;
        for (int i = 0; i < 64; i++) {
            int c = lvl(vdp->s.cram[i], k), b = before == 1 ? lvl(steady[i], k) : before == 2 ? lvl(g.palfadenew[i], k) : c;
            if (b - c > down) down = b - c;
            if (c > up) up = c;
        }
        int brighten = fading && lit_tgt > lit_cur;          /* (toward brighter colours: up from black) */
        fade_sub[k] = brighten ? 0 : down * 255 / 7;
        fade_cap[k] = brighten ? up * 255 / 7 : 255;
    }
}

/* The wall jump frames, SPFPen 377-383 (the PC's SPFPen 376-382, frames.asm). The cartridge's 377-380 (SPAwallright / SPAwallleft
 * in directions 1-7: the bench gates) are the PC's 376-379, the same four pictures (up, leaning, over, down). Its 381-383 (direction
 * 0, the penalty box in the side view, sfhor: facing away to jump, over the boards, sitting) are not: the PC's 380 and 381 are a man
 * standing face on (its anthem's SPAanthem4 / 5 use them too) and only its 382 is one sitting in the box. So 381 and 382 keep the
 * cartridge's sprite and 383 is the PC's 382.
 *
 * The fight frames, SPFgloves 353 to SPFfinjury+5 376 (NHL 93's fights: mods/fight.c), have no PC frames: NHL 95 PC has no fights and
 * gave those numbers (352-375) to other pictures (frames.asm: the face off stances 360-368, the anthem's standing 369-371, the goalie's
 * wall jump 372-375, the cup 357-358), so a fighter was drawn as a face off or anthem man. The hook takes a fighter's frame through
 * fight_frame_shown first (the cartridge frame he is drawn with: holding, hooking, falling), whose PC frame is the real one. */
static int pc_frame(int fr)
{
    if (fr >= MODF_BASE) return mod_frame_pc((uint16_t)fr);   /* (the PC's pictures of NHL 95's moves and goalie, mods/anims.c) */
    if (fr < 1 || fr > 657) return -1;
    if (fr >= SPFgloves && fr < SPFPen) return -1;      /* (the fight frames: fight_frame_shown leaves only the gloves, 353) */
    if (fr == SPFPen + 4 || fr == SPFPen + 5) return -1; /* (the box's wall jump seen from the side, below) */
    if (fr <= 554) return fr - 1;
    if (fr <= 562) return -1;                          /* (the cartridge's extra goalie ready frames) */
    return fr - 9;
}

/* the referee is drawn with the Game page's row, or whenever he is in play (RULE_REFEREE: play must not have an unseen man in it) */
static int ref_on(void) { return SDL_AtomicGet(&R.on) || rules[RULE_REFEREE]; }

static int in_game(void) { return g.demoflag && ((g.disflags & HI(dfok)) || (g.sflags & HI(sfpz))); }   /* (the pause screen too: its rink strip has the skaters) */


/* NHL 95 PC's referee is SortCords 10h, one of the objects checkplcoll / checkcx collide: a skater (or goalie) within 10h on
 * both axes and 100h squared, closing on him, trades velocity with him weighted by weight + 8Ch (checkcoll then keeps both where
 * they were that frame). Here only the referee's side of it, and since the skater does not bounce back the referee is set 12 off
 * him instead of held: the skaters, the puck and the game are not touched (the PC's puck also
 * bounces off him, puckbody, and a checker can knock him down, FallDown, every third time for a penalty 16h: all of that is play,
 * and this referee is only the picture's). 1 when a skater ran into him */
#define REF_WEIGHT 8                            /* (the PC's object 10h; a skater's weight is 0-15 against the 8Ch both get) */
static int ref_bump(void)
{
    int hit = 0;
    for (int i = 0; i < 12; i++) {
        const struct SortObj *q = sortobj(i);
        if (q->pflags & 4) continue;                                     /* (out of play: no collisions) */
        int dx = R.x / 256 - q->Xpos, dy = R.y / 256 - q->Ypos;
        if (dx > 16 || dx < -16 || dy > 16 || dy < -16 || dx * dx + dy * dy > 0x100) continue;
        int dvx = q->Xvel - R.vx, dvy = q->Yvel - R.vy;
        int c = dy * dvy + dx * dvx;                                    /* (closing: the gap times the speed between them) */
        if (c < 0) continue;
        c >>= 4;
        int wq = q->weight + 0x8C, wr = REF_WEIGHT + 0x8C;
        int k = c * wq / (wq + wr);
        R.vx += dx * k >> 4;
        R.vy += dy * k >> 4;
        hit = 1;
        int d2 = dx * dx + dy * dy;                                     /* (the skater goes on: the referee is put out of his way, 12 off) */
        if (d2 < 144) {
            double dl = sqrt((double)d2);
            double ux = dl > 0.5 ? dx / dl : 0, uy = dl > 0.5 ? dy / dl : 1;
            R.x = (int)((q->Xpos + ux * 12) * 256);
            R.y = (int)((q->Ypos + uy * 12) * 256);
        }
        if (getenv("PCREF_LOG")) fprintf(stderr, "ref: bumped by %d at %d,%d gap %d,%d vel %d,%d\n", i, R.x / 256, R.y / 256, dx, dy, R.vx, R.vy);
    }
    int l = 4 * 256;                                                    /* (at most 4 a frame) */
    if (R.vx > l) R.vx = l;
    if (R.vx < -l) R.vx = -l;
    if (R.vy > l) R.vy = l;
    if (R.vy < -l) R.vy = -l;
    return hit;
}

/* doplayeracc's turn for the picture's referee where the game does not turn him (the walk to the dot, mods/hold.c): toward want, a
 * step of 45 degrees when 16 x 300h (from rest; 180h at speed) a frame has made 10000h, 95 PC's facedir as a 16.16 (pad_34). rate: the
 * share a frame, 10000h / rate frames a step (assrefdrop's and assrefwhistle's turn at the dot is a step every 8 ticks: 2000h).
 * Before, the walk's way snapped him round at once, by as much as half a turn */
static void ref_turn(int want, unsigned rate)
{
    static unsigned acc;
    int t = (want - R.dir) & 7;
    if (!t) { acc = 0; return; }
    acc += rate;
    if (acc < 0x10000) return;
    acc -= 0x10000;
    R.dir = (R.dir + (t <= 4 ? 1 : -1)) & 7;
}

/* the picture's own referee (the Visual only one, RULE_REFEREE off): NHL 95 PC's skating on a SortObj of his own (mods/referee.c:
 * referee_follow, referee_glide), which reads the game and changes nothing of it */
static struct SortObj VO;
static int vo_live;                                      /* (VO moved him last frame) */
static void vo_to_r(void)
{
    int K = g.PalMode ? 0x16 : 0x11;
    R.x = POS32(VO.Xpos, VO.Xfrac) >> 8; R.y = POS32(VO.Ypos, VO.Yfrac) >> 8;
    R.vx = VO.Xvel * K / 256; R.vy = VO.Yvel * K / 256;
    R.dir = VO.facedir & 7; R.anim = VO.SPA; R.t = VO.SPAcnt;
}

/* where the referee stands: NHL 95 PC's assreffollow (in play: mods/referee.c, the game's referee's or the picture's own), the walk
 * to the dot (mods/hold.c) and assrefatdot (at the face off spot, facing the dot) */
static void ref_live(void)
{
    struct referee_view gv;
    R.valid = 0;
    R.fall = -1;
    if (getenv("PCREF_LOG") && !(g.Vcount & 255)) fprintf(stderr, "ref? on %d found %d ingame %d hor %d gm2 %d\n", SDL_AtomicGet(&R.on), SDL_AtomicGet(&P.found), in_game(), !!(g.sflags & HI(sfhor)), g.gmode2 & 1);
    if (!ref_on() || !SDL_AtomicGet(&P.found) || !g.demoflag || (g.sflags & HI(sfhor)) || (g.gmode2 & 1)) { R.init = 0; return; }
    const struct SortObj *pk = sortobj(puckscnum);
    int asn = pk->asslist[pk->assnum & 7];
    int fo = asn == pfaceoff || asn == pfaceoff2;
    int fx = (int16_t)g.fox, fy = (int16_t)g.foy;
    int px = pk->Xpos, py = pk->Ypos, cy = (int16_t)g.Vpos;
    int ax_, ay_, ad_, am_, vis = 0;
    if (hold_anthem_ref(&ax_, &ay_, &ad_, &am_)) {                           /* (the anthem: at the side with the players in their lines) */
        R.x = ax_ * 256; R.y = ay_ * 256; R.vx = R.vy = 0;
        R.dir = ad_; R.t = am_ ? R.t + 1 : 0; R.anim = am_ ? REF_SKATE : REF_GLIDE; R.wasfo = 1; R.drop = 0; R.init = 1;
    } else if (fo) {
        int ox = R.x, oy = R.y, walk = hold_skate_ref() >= 0, put = 0;
        static uint16_t stepvc;
        if (referee_get(&gv)) { R.x = gv.x / 256; R.y = gv.y / 256; put = !walk && !hold_skate_pending(); }   /* (RULE_REFEREE: where the game has him) */
        else if (walk) {                                                     /* (the between-play pause: his walk, mods/hold.c, a step a frame) */
            if (!R.init) { int pick; hold_skate_puck(&R.x, &R.y, &pick); R.x *= 256; R.y *= 256; }   /* (not seen till now, the penalty screen: at the puck) */
            if (g.Vcount != stepvc) { int x = R.x, y = R.y; if (hold_skate_ref_step(&x, &y)) { R.x = x; R.y = y; } }
        } else if (R.init && hold_skate_pending()) {
            /* (the face off is set up a frame before the walk is: he stays where he is. Put at the dot that frame, the walk then
             * started from the dot (PR #167's walk) or from where the period's start had left him (the fx0 it kept): gone from
             * the view and back, or a jump) */
        } else { R.x = (fx > 0 ? fx + 15 : fx - 15) * 256; R.y = fy * 256; put = 1; }
        stepvc = g.Vcount;
        double ddx = R.x - ox, ddy = R.y - oy;
        int want = fx > 0 ? 6 : 2;
        if (walk && R.init && ddx * ddx + ddy * ddy > 77.0 * 77.0) {        /* (on his way, over 0.3 a frame: striding, turning to his way) */
            int d = (int)floor(atan2(ddx, ddy) / (M_PI / 4) + 0.5);
            ref_turn(((d % 8) + 8) % 8, 0x1800);
            R.anim = REF_SKATE; R.t = R.t + 1;
        } else {
            if (put || !R.init) R.dir = want;                                /* (put at the dot: assrefatdot's facing) */
            else ref_turn(want, 0x2000);                                     /* (there: a step every 8, assrefdrop's) */
            R.anim = REF_GLIDE; R.t = 0;
        }
        R.vx = R.vy = 0;
        R.wasfo = 1; R.drop = 0; R.init = 1;
    } else if (referee_get(&gv)) {                                          /* (RULE_REFEREE: in play, where the game has him) */
        R.x = gv.x / 256; R.y = gv.y / 256; R.vx = gv.vx / 256; R.vy = gv.vy / 256;
        R.wasfo = 0; R.drop = gv.drop; R.init = 1;
        R.dir = gv.dir; R.anim = gv.anim; R.t = gv.at;
        if (gv.fall >= 0) { R.fall = gv.fall; R.dir = gv.fall_dir; }
    } else {
        vis = 1;
        if (!R.init) { referee_skate_start(&VO, (int16_t)(px > 0 ? 0x7A : -0x7A), (int16_t)cy); vo_to_r(); R.init = 1; }
        else if (!vo_live || R.wasfo) {                                      /* (from the walk, the dot or the game's referee: he goes on from where he is) */
            referee_skate_start(&VO, (int16_t)(R.x / 256), (int16_t)(R.y / 256));
            VO.facedir = (int16_t)R.dir;
        }
        if (R.wasfo) { R.wasfo = 0; R.drop = 24; }                           /* (the puck is dropped: the referee's drop, then he skates from there) */
        if (R.drop > 0) { R.drop--; VO.Xvel = VO.Yvel = 0; }
        else referee_follow(&VO);
        if (SDL_AtomicGet(&R.phys) && !rules[RULE_REFEREE]) {
            int K = g.PalMode ? 0x16 : 0x11;
            vo_to_r();
            if (ref_bump()) {
                VO.Xpos = (int16_t)(R.x >> 8); VO.Xfrac = (uint16_t)(R.x << 8); VO.Ypos = (int16_t)(R.y >> 8); VO.Yfrac = (uint16_t)(R.y << 8);
                VO.Xvel = (int16_t)(R.vx * 256 / K); VO.Yvel = (int16_t)(R.vy * 256 / K);
            }
        }
        referee_glide(&VO);
        vo_to_r();
    }
    vo_live = vis;
    int16_t x = (int16_t)(R.x / 256), y = (int16_t)(R.y / 256);
    find3d(&x, &y, 0);
    if (y == 0x4E20) return;
    R.sx = x + view_shift();
    if (getenv("PCREF_AT")) { int ax, ay; if (sscanf(getenv("PCREF_AT"), "%d,%d", &ax, &ay) == 2) { R.sx = ax; R.sy = ay; } }   /* (a test: the referee put there) */
    R.sy = y;
    R.valid = 1;
    if (getenv("PCREF_LOG") && (!(g.Vcount & 63) || getenv("PCREF_ALL"))) fprintf(stderr, "ref: rink %d,%d screen %d,%d dir %d t %d fo %d puck %d,%d view %d,%d vblank %u anim %d\n", R.x / 256, R.y / 256, R.sx, R.sy, R.dir, R.t, fo, px, py, (int16_t)g.Hpos, (int16_t)g.Vpos, (unsigned)g.Vcount, R.anim);
}

/* The instant replay: the cartridge has no referee in its recorded frames, so the picture keeps its own beside them. Each tick of the game
 * puts the referee as he is into the slot of the frame being recorded (g.recbpr, one of the game's replay frames); in the replay the frame
 * on show is found by its skaters' places (RestoreReplayFrame put them there from it) and its referee drawn, turned with the reverse angle.
 * Paused, the same frame shows: he stands still. A frame recorded without him has none. Leaving, he is back as he was at the record point. */
#define REP_FRAME 0x62
#define REP_END 0xFFFFAF54u
#define REP_N ((REP_END - RAM_BASE) / REP_FRAME)
static struct { int8_t have, fo; int8_t dir, anim; int16_t fall, drop; int32_t x, y, t; } REP[REP_N];
static struct { int on, slot, fo; } RPL = { 0, -1, 0 };
static __typeof__(R) R_before;                                   /* (the referee at the record point, while the replay is on) */

static int rep_slot(uint32_t a) { return a >= RAM_BASE && a < REP_END && (a - RAM_BASE) % REP_FRAME == 0 ? (int)((a - RAM_BASE) / REP_FRAME) : -1; }
static int rep_matches(int slot, int rev)
{
    const struct ReplayFrame *f = RAM_AT(struct ReplayFrame, ram_at(RAM_BASE + (uint32_t)slot * REP_FRAME));
    for (int i = 0; i < 12; i++) {
        uint32_t l = f->obj[i];
        int x = (int)(l & 0x3FF), y = (int)((l >> 10) & 0x3FF);
        if (x & 0x200) x -= 0x400;
        if (y & 0x200) y -= 0x400;
        if (rev) { x = -x; y = -y; }
        if (sortobj(i)->Xpos != x || sortobj(i)->Ypos != y) return 0;
    }
    return 1;
}
static void ref_tick(void)
{
    if (!(g.sflags2 & HI(sf2replay))) {
        if (RPL.on) { R = R_before; RPL.on = 0; RPL.slot = -1; }
        ref_live();
        int k = rep_slot(g.recbpr);
        if (k >= 0) {
            const struct SortObj *pk = sortobj(puckscnum);
            int asn = pk->asslist[pk->assnum & 7];
            REP[k].have = (int8_t)R.valid; REP[k].x = R.x; REP[k].y = R.y; REP[k].dir = (int8_t)R.dir; REP[k].anim = (int8_t)R.anim; REP[k].t = R.t;
            REP[k].fall = (int16_t)R.fall; REP[k].drop = (int16_t)R.drop; REP[k].fo = (int8_t)(asn == pfaceoff || asn == pfaceoff2);
        }
        return;
    }
    if (!RPL.on) { R_before = R; RPL.on = 1; RPL.slot = -1; }
    R.valid = 0;
    if (!ref_on() || !SDL_AtomicGet(&P.found)) return;
    int rev = (g.sflags4 & HI(4)) != 0, k = -1;
    if (RPL.slot >= 0) for (int d = 0; d <= 8 && k < 0; d++)          /* (near the last one first: the replay steps a frame or a few) */
        for (int sg = -1; sg <= 1 && k < 0; sg += 2) { int j = ((RPL.slot + sg * d) % (int)REP_N + (int)REP_N) % (int)REP_N; if (rep_matches(j, rev)) k = j; }
    for (int j = 0; j < (int)REP_N && k < 0; j++) if (rep_matches(j, rev)) k = j;
    if (k < 0 || !REP[k].have) { RPL.slot = k; return; }
    RPL.slot = k;
    R.x = rev ? -REP[k].x : REP[k].x; R.y = rev ? -REP[k].y : REP[k].y;
    R.dir = rev ? (REP[k].dir + 4) & 7 : REP[k].dir; R.t = REP[k].t; R.anim = REP[k].anim; R.fall = REP[k].fall; R.drop = REP[k].drop; RPL.fo = REP[k].fo;
    int16_t x = (int16_t)(R.x / 256), y = (int16_t)(R.y / 256);
    find3d(&x, &y, 0);
    if (y == 0x4E20) return;
    R.sx = x + view_shift();
    R.sy = y;
    R.valid = 1;
    if (getenv("PCREF_REPLAY")) fprintf(stderr, "replay ref: slot %d at %d,%d screen %d,%d dir %d t %d\n", k, R.x / 256, R.y / 256, R.sx, R.sy, R.dir, R.t);
}

/* the frame the referee shows */
static int ref_frame(void)
{
    const struct SortObj *pk = sortobj(puckscnum);
    int asn = pk->asslist[pk->assnum & 7];
    int d = R.dir & 7;
    int fo = RPL.on ? RPL.fo : asn == pfaceoff || asn == pfaceoff2;   /* (the replay: as he was in the frame on show) */
    if (hold_anthem_time() >= 0 && !RPL.on) return idle_ref(hold_anthem_time());                                  /* (the anthem: the referee's standing picture) */
    if (fo && R.t == 0 && R.anim == REF_GLIDE) return (d >= 3 && d <= 6) ? 711 : 703;   /* (still: skating to the dot, he is not) */   /* (SPAreffowait) */
    if (R.fall >= 0) return 787 + 3 * d + (R.fall < 10 ? 0 : R.fall < 38 ? 1 : 2);     /* (SPAreffallback: 10, 28, 10 ticks) */
    if (R.drop > 0) {                                                                    /* (SPArefdrop: 8 ticks each) */
        int k = (24 - R.drop) / 8;
        if (d >= 3 && d <= 6) return k == 0 ? 845 : k == 1 ? 846 : 711;
        return k == 0 ? 843 : k == 1 ? 844 : 703;
    }
    int b = 5 * ((8 - d) & 7);
    static const int o[4] = { 2, 3, 0, 1 };
    switch (R.anim) {                                                                    /* (95 PC's frames.asm) */
    case REF_SKATE: return 651 + b + o[(R.t / 10) & 3];                                  /* (SPArefskate: 10 ticks a frame) */
    case REF_STOP: return 771 + 2 * d + ((R.t / 4) & 1);                                 /* (SPArefstop: 4 ticks a frame, looping) */
    case REF_TURNL: return 755 + d;                                                      /* (SPArefturnl) */
    case REF_TURNR: return 763 + d;                                                      /* (SPArefturnr) */
    default: return 650 + b;                                                             /* (SPArefglide) */
    }
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
    if (sc < 12 && fight93_have(o->frame & 0x7FF)) return 0;   /* (a fighter in NHL 93's own fight frames: his sprite stays, the PC has none) */
    int pf = pc_frame(sc < 12 ? fight_frame_shown(o->frame & 0x7FF, sortobj(sc)->facedir) : (o->frame & 0x7FF));   /* (a fighter: the cartridge frame his is drawn with, fight.c) */
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
void pcspr_ref_phys_set(int on) { SDL_AtomicSet(&R.phys, on ? 1 : 0); }
void pcspr_logo_set(int on) { SDL_AtomicSet(&LGO.on, on < 0 ? 0 : on > 2 ? 2 : on); }
int pcspr_logos(void) { return LGO.L[0].have || LGO.L[1].have ? 1 + LGO.L[1].have : 0; }


/* ---- The layers (docs/PC_LAYERS.md) ----------------------------------------------------------------------------------------------
 * The cartridge's picture is the VDP's, back to front: the backdrop, plane B's low tiles (the rink: its ice, lines and crest), plane
 * A's low tiles, the low sprites (the last in the link order at the back), plane B's high tiles, plane A's high tiles (the near boards,
 * the face off and penalty windows, the PK / PP banner, the clock and score boxes; the window plane in its area), the high sprites.
 * Each of the PC's pictures goes in at the place the cartridge's own would have:
 *   the centre ice logo and line   plane B, the crest's cells: under every sprite and every high tile;
 *   a skater, goalie or net        his own sprite's place in the link order, at its priority;
 *   the referee                    a low sprite, in the link order by his depth among the PC's skaters;
 *   the anthem's flag box          over everything (the PC's text overlay).
 * Where a picture shows is the VDP's own answer: solid pieces covering the picture's whole box are put into a copy of the sprite table at
 * its place (the cartridge's sprites the PC's replace taken out), the lines drawn twice with the pieces in two colours, and where the two
 * differ the piece shows. Nothing of the PC's picture is cut to the shape of the cartridge's sprite: only what is in front of it covers it. */
static int free_tiles(const mdrt_vdp *v, uint16_t *out, int max);
static struct {
    const mdrt_vdp *src;
    mdrt_vdp v;                             /* the frame's VDP, palette 0's colours 14 and 15 the marks */
    uint8_t order[80];                      /* the sprite chain in link order */
    int n, total, cw, ch, ok;
    unsigned sat, tile;                     /* tile: cw x ch free tiles in a row, a piece's */
} LM;
static uint16_t lm_a[320 * 240], lm_b[320 * 240];

static void layer_begin(const mdrt_vdp *vdp)
{
    static uint16_t fr[2048];
    int h40 = mdrt_vdp_width(vdp) == 320;
    LM.ok = 0;
    LM.src = vdp;
    LM.sat = (unsigned)(mdrt_vdp_reg(vdp, 5) & (h40 ? 0x7E : 0x7F)) << 9;
    LM.total = h40 ? 80 : 64;
    LM.n = 0;
    for (int e = 0, k = 0; k < LM.total; k++) {          /* (as the VDP walks it) */
        LM.order[LM.n++] = (uint8_t)e;
        e = vdp->sat[e * 8 + 3] & 0x7F;
        if (!e || e >= LM.total) break;
    }
    int nf = free_tiles(vdp, fr, 2048);
    static const int shapes[4][2] = { { 4, 4 }, { 2, 4 }, { 1, 4 }, { 1, 1 } };   /* (the biggest piece the free tiles allow) */
    for (int s = 0; s < 4 && !LM.ok; s++) {
        int need = shapes[s][0] * shapes[s][1];
        for (int i = 0, run = 0; i < nf && !LM.ok; i++) {
            run = i && fr[i] == fr[i - 1] + 1 ? run + 1 : 1;
            if (run >= need) { LM.tile = fr[i - need + 1]; LM.cw = shapes[s][0]; LM.ch = shapes[s][1]; LM.ok = 1; }
        }
    }
    if (!LM.ok) { if (getenv("PCSPR_LOG")) fprintf(stderr, "pcspr: no free tiles for the layer masks\n"); return; }
    LM.v = *vdp;
    LM.v.s.cram[15] = 0x0E0E;
    LM.v.s.cram[14] = 0x00E0;
}

/* where a picture in the box x0, y0, w x h (screen pixels) shows when it is a sprite put into the link order just before entry `before`
 * (-1: after the last), at priority pri, with the entries drop[e] taken out: out[y * 320 + x] = 1 there, 0 elsewhere in the box (only
 * the box is written). 0 when it cannot be told (no free tiles, no room in the sprite table). */
static int layer_mask(const uint8_t *drop, int before, int pri, int x0, int y0, int w, int h, int sw, int sh, uint8_t *out)
{
    if (!LM.ok) return 0;
    int bx0 = x0 < 0 ? 0 : x0, by0 = y0 < 0 ? 0 : y0, bx1 = x0 + w > sw ? sw : x0 + w, by1 = y0 + h > sh ? sh : y0 + h;
    if (bx0 >= bx1 || by0 >= by1) return 0;
    const mdrt_vdp *v = LM.src;
    uint8_t ent[80][8];
    int m = 0, placed = 0, pw = LM.cw * 8, ph = LM.ch * 8;
    for (int k = 0; k <= LM.n; k++) {
        int e = k < LM.n ? LM.order[k] : -1;
        if (!placed && (e == before || e < 0)) {          /* the pieces: from the box's on screen corner, so no piece is at X 0 (the VDP's line mask) */
            placed = 1;
            for (int y = by0; y < by1; y += ph)
                for (int x = bx0; x < bx1; x += pw) {
                    if (m >= LM.total) return 0;
                    uint8_t *a = ent[m++];
                    a[0] = (uint8_t)((y + 128) >> 8); a[1] = (uint8_t)(y + 128); a[2] = (uint8_t)((LM.cw - 1) << 2 | (LM.ch - 1)); a[3] = 0;
                    a[4] = (uint8_t)((pri ? 0x80 : 0) | LM.tile >> 8); a[5] = (uint8_t)LM.tile;
                    a[6] = (uint8_t)((x + 128) >> 8); a[7] = (uint8_t)(x + 128);
                }
        }
        if (e < 0) break;
        if (drop && drop[e]) continue;
        if (m >= LM.total) return 0;
        memcpy(ent[m], v->sat + e * 8, 4);
        memcpy(ent[m] + 4, v->s.vram + ((LM.sat + (unsigned)e * 8 + 4) & 0xFFFF), 4);
        m++;
    }
    for (int j = 0; j < m; j++) {
        ent[j][3] = (uint8_t)(j + 1 < m ? j + 1 : 0);
        for (int b = 0; b < 8; b++) LM.v.s.vram[(LM.sat + (unsigned)j * 8 + (unsigned)b) & 0xFFFF] = ent[j][b];
        memcpy(LM.v.sat + j * 8, ent[j], 4);
    }
    size_t tb = (size_t)LM.cw * (size_t)LM.ch * 32;
    memset(LM.v.s.vram + LM.tile * 32, 0xFF, tb);
    mdrt_vdp_render_lines(&LM.v, lm_a, 320, by0, by1);
    memset(LM.v.s.vram + LM.tile * 32, 0xEE, tb);
    mdrt_vdp_render_lines(&LM.v, lm_b, 320, by0, by1);
    for (int y = by0; y < by1; y++)
        for (int x = bx0; x < bx1; x++) out[y * 320 + x] = lm_a[y * 320 + x] != lm_b[y * 320 + x];
    return 1;
}

/* the entry after the last of a listed sprite's in the link order (-1: none) */
static int entry_after(const shown *s)
{
    int last = s->first + s->count - 1;
    for (int k = 0; k + 1 < LM.n; k++) if (LM.order[k] == last) return LM.order[k + 1];
    return -1;
}

/* the referee, a low sprite at `before` in the link order (the cartridge's sprites drop[] taken out: the PC's skaters, which are drawn
 * after him where they are nearer). A dither of the cartridge's high tiles over the ice (the boards' corners) would speckle him: a pixel
 * with most of its neighbours showing shows. */
static void ref_draw(uint32_t *pic, int w, int h, int pitch, const uint8_t *drop, int before)
{
    if (!ref_on() || !R.valid) return;
    int fr = ref_frame();
    if (fr < 0 || fr >= NFRAMES || !P.f[fr].px) return;
    const spr *s = &P.f[fr];
    const uint8_t *rec = P.team[0][9];
    int x0 = R.sx - 128 - s->xo, y0 = R.sy - 128 - s->yo;
    static uint8_t m[320 * 240];
    if (!layer_mask(drop, before, 0, x0, y0, s->w, s->h, w, h, m)) return;
    if (getenv("PCREF_LOG")) fprintf(stderr, "refdraw: frame %d at %d,%d size %dx%d before %d\n", fr, x0, y0, s->w, s->h, before);
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            int v = s->px[y * s->w + x];
            if (v == 255) continue;
            int px_ = x0 + x, py = y0 + y;
            if (px_ < 0 || px_ >= w || py < 0 || py >= h) continue;
            if (!m[py * 320 + px_]) {
                int near = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int qx = px_ + dx, qy = py + dy;
                        if ((dx || dy) && qx >= x0 && qx < x0 + s->w && qx >= 0 && qx < w && qy >= y0 && qy < y0 + s->h && qy >= 0 && qy < h) near += m[qy * 320 + qx];
                    }
                if (near < 4) continue;                              /* (a straight edge has 3 over it at most) */
            }
            int r = v < 0x80 ? rec[192 + v] : rec[320 + v - 0x80];
            pic[(long)py * pitch + px_] = fadec(0xFF000000u | (uint32_t)P.pal[r][0] << 16 | (uint32_t)P.pal[r][1] << 8 | P.pal[r][2]);
        }
}

/* the first of the 24 crest tiles the cartridge draws at centre ice for team t: found in the VDP's memory by their pixels (the 24 tiles
 * LoadHomeTeamGfx puts there: the team's centre ice by its roster table, the cartridge's or a roster set's own; a roster set has no
 * TeamGfxList in team order, so not through RS(TeamGfxList)) */
static int find_crest(const mdrt_vdp *v, int team)
{
    static uint8_t tiles[24 * 32];
    struct romsrc rs;
    romsrc_game(&rs);
    if (team < 0 || team >= ROSTERS_MAX || art_tiles(&rs, rosters_team((uint16_t)team)->ice + 8, tiles, 24) != 24) return -1;
    for (unsigned a = 0; a + 24 * 32 <= 0x10000; a += 32)
        if (!memcmp(v->s.vram + a, tiles, 24 * 32)) return (int)(a / 32);
    return -1;
}

static int find3d_visible(void) { int16_t x = 0, y = 0; find3d(&x, &y, 0); return y != 0x4E20; }

static void logo_why(int why, const char *text)
{
    static int said;
    if (said & (1 << why)) return;
    said |= 1 << why;
    fprintf(stderr, "Centre ice logo: %s\n", text);
}

/* a pixel of the tile of plane map entry e: its colour number (0 clear) */
static int tile_px(const mdrt_vdp *v, uint16_t e, int x, int y)
{
    if (e & 0x0800) x = 7 - x;
    if (e & 0x1000) y = 7 - y;
    uint8_t b = v->s.vram[((uint32_t)(e & 0x7FF) << 5) + (uint32_t)(y << 2) + (uint32_t)(x >> 1)];
    return (x & 1) ? (b & 0xF) : (b >> 4);
}

/* where the crest is in the planes: the cells of plane A (0) or B (1) whose tile is one of the crest's, in plane pixels (x0, y0 the top
 * left, x1, y1 past the bottom right; past the plane's right or bottom edge when the crest runs round it); 0 when no cell has one.
 * The cartridge streams the rink's rows into a plane shorter than the rink as it scrolls: near the screen's top or bottom edge the
 * crest's rows off the screen are the far end of the rink's, and only part of the crest is in the plane. Once the whole crest has
 * been seen (with a row of the rink in view past each of its edges, so none of its rows can be one the cartridge has written over), its
 * layout is kept (LC: the cell of each of its 24 tiles, and the centre line's rows in it), and from then on the crest's place is the
 * one the cells still in the plane give by that layout: full is the whole crest's, c only the part still in the plane. Returns 2 when
 * part of the crest is gone (full by the layout kept), else 1 (c and full the same). Only the VDP's state and the frames shown before
 * go in: the picture only, nothing of the game's */
struct crest_at { int plane, x0, y0, x1, y1, pw, ph; uint32_t table; };
static struct {
    int ok, crest, plane, pw, ph, cw, ch, n; uint32_t table;
    signed char dx[24], dy[24];              /* (each tile's cell from the crest's top left; -1: not in it, or in it twice) */
    int line, ly0, ly1, bl, b0, b1;           /* (the centre line's rows, from the crest's top: the red at 5 cells left of it, and its edges) */
    int at, ox, oy;                           /* (where the crest was last seen in the plane, by its layout: its top left in plane pixels) */
    int sy_ok, sy; uint16_t sy_at;            /* (its middle's row on the screen then, and the frame (Vcount)) */
} LC;
static int crest_cells(const mdrt_vdp *v, int crest, struct crest_at *c, struct crest_at *full)
{
    static const int cells[4] = { 32, 64, 32, 128 };
    int w = cells[v->s.reg[16] & 3], h = cells[(v->s.reg[16] >> 4) & 3];
    if (LC.ok && LC.crest != crest) LC.ok = 0;
    for (int plane = 1; plane >= 0; plane--) {
        uint32_t table = plane ? (uint32_t)(v->s.reg[4] & 7) << 13 : (uint32_t)(v->s.reg[2] & 0x38) << 10;
        int cx0 = 1 << 20, cy0 = 1 << 20, cx1 = -1, cy1 = -1, cn = 0;
        static short ck[512], cr[512], ct[512];
        for (int r = 0; r < h; r++)
            for (int k = 0; k < w; k++) {
                uint32_t a = (table + (uint32_t)((r * w + k) << 1)) & 0xFFFE;
                int t = (v->s.vram[a] << 8 | v->s.vram[a + 1]) & 0x7FF;
                if (t < crest || t >= crest + 24) continue;
                if (cn < 512) { ck[cn] = (short)k; cr[cn] = (short)r; ct[cn] = (short)(t - crest); }
                cn++;
                if (k < cx0) cx0 = k;
                if (k > cx1) cx1 = k;
                if (r < cy0) cy0 = r;
                if (r > cy1) cy1 = r;
            }
        if (!cn) continue;
        if (LC.ok && LC.plane == plane && LC.table == table && LC.pw == w * 8 && LC.ph == h * 8 && cn <= 512) {
            int bo = -1, bv = 0;                                       /* (the crest's top left the most cells give by the layout) */
            for (int i = 0; i < cn; i++) {
                if (LC.dx[ct[i]] < 0) continue;
                int o = ((cr[i] - LC.dy[ct[i]]) & (h - 1)) * w + ((ck[i] - LC.dx[ct[i]]) & (w - 1)), n = 0;
                if (o == bo) continue;
                for (int j = 0; j < cn; j++)
                    n += LC.dx[ct[j]] >= 0 && (((cr[j] - LC.dy[ct[j]]) & (h - 1)) * w + ((ck[j] - LC.dx[ct[j]]) & (w - 1))) == o;
                if (n > bv) { bv = n; bo = o; }
            }
            if (bv) {
                int ox = bo % w, oy = bo / w, x0 = 1 << 20, y0 = 1 << 20, x1 = -1, y1 = -1, in = 0;
                for (int i = 0; i < cn; i++) {                         /* (the cells of this crest: in its layout's box from there) */
                    int dx = (ck[i] - ox) & (w - 1), dy = (cr[i] - oy) & (h - 1);
                    if (dx >= LC.cw || dy >= LC.ch) continue;
                    in++;
                    if (dx < x0) x0 = dx;
                    if (dx > x1) x1 = dx;
                    if (dy < y0) y0 = dy;
                    if (dy > y1) y1 = dy;
                }
                *full = (struct crest_at){ plane, ox * 8, oy * 8, (ox + LC.cw) * 8, (oy + LC.ch) * 8, w * 8, h * 8, table };
                *c = (struct crest_at){ plane, (ox + x0) * 8, (oy + y0) * 8, (ox + x1 + 1) * 8, (oy + y1 + 1) * 8, w * 8, h * 8, table };
                return in == LC.n && x0 == 0 && y0 == 0 && x1 == LC.cw - 1 && y1 == LC.ch - 1 ? 1 : 2;
            }
        }
        if (cn < 12 || cx1 - cx0 >= w / 2 || cy1 - cy0 >= h / 2) continue;   /* (none, a stray tile of another screen, or not one crest: wrapped round the plane) */
        *c = *full = (struct crest_at){ plane, cx0 * 8, cy0 * 8, (cx1 + 1) * 8, (cy1 + 1) * 8, w * 8, h * 8, table };
        return 1;
    }
    return 0;
}

/* the crest's layout kept (LC) from the whole crest c in the plane, seen with a row of the rink past each of its edges on the screen
 * (vs, hsv the plane's scroll there; w, h the screen's): its tiles' cells. 1 when kept (logo_draw then keeps the line's rows) */
static int crest_keep(const mdrt_vdp *v, int crest, const struct crest_at *c, int vs, int hsv, int w, int h)
{
    int top = ((c->y0 - 8 - vs) & (c->ph - 1)), left = ((c->x0 - 8 + hsv) & (c->pw - 1));
    if (top >= h / 2 + c->ph / 2) top -= c->ph;
    if (left >= w / 2 + c->pw / 2) left -= c->pw;
    if (top < 0 || top + c->y1 - c->y0 + 16 > h || left < 0 || left + c->x1 - c->x0 + 16 > w) return 0;
    int cw = c->pw / 8, ch = c->ph / 8, n = 0;
    memset(LC.dx, -1, sizeof LC.dx);
    memset(LC.dy, -1, sizeof LC.dy);
    static unsigned char seen[24];
    memset(seen, 0, sizeof seen);
    for (int r = c->y0 / 8; r < c->y1 / 8; r++)
        for (int q = c->x0 / 8; q < c->x1 / 8; q++) {
            uint32_t a = (c->table + (uint32_t)(((r & (ch - 1)) * cw + (q & (cw - 1))) << 1)) & 0xFFFE;
            int t = ((v->s.vram[a] << 8 | v->s.vram[a + 1]) & 0x7FF) - crest;
            if (t < 0 || t >= 24) continue;
            n++;
            if (seen[t]++) { LC.dx[t] = LC.dy[t] = -1; continue; }   /* (a tile the crest has twice says nothing of where it is) */
            LC.dx[t] = (signed char)(q - c->x0 / 8);
            LC.dy[t] = (signed char)(r - c->y0 / 8);
        }
    LC.ok = 1; LC.crest = crest; LC.plane = c->plane; LC.table = c->table; LC.pw = c->pw; LC.ph = c->ph;
    LC.cw = (c->x1 - c->x0) / 8; LC.ch = (c->y1 - c->y0) / 8; LC.n = n;
    return 1;
}

/* pixel x, y of the tile of plane map entry e: the centre line's red (is_red), or its pink edge rows (is_pink: red over green and blue, not white) */
static unsigned px_colour(const mdrt_vdp *v, uint16_t e, int x, int y, int *c) { *c = tile_px(v, e, x, y); return *c ? v->s.cram[((e >> 13) & 3) * 16 + *c] : 0; }
static int is_red(const mdrt_vdp *v, uint16_t e, int x, int y)
{
    int c;
    unsigned u = px_colour(v, e, x, y, &c);
    return c && (u & 0xE) >= 0xA && (u >> 4 & 0xE) <= 6 && (u >> 8 & 0xE) <= 6;
}
static int is_pink(const mdrt_vdp *v, uint16_t e, int x, int y)
{
    int c;
    unsigned u = px_colour(v, e, x, y, &c);
    return c && (u & 0xE) >= 0x8 && (u & 0xE) > (u >> 4 & 0xE) + 2 && (u & 0xE) > (u >> 8 & 0xE) + 2;
}

/* tiles of the VDP's memory nothing shows: no plane, window or sprite uses them and no table is over them (at most max, into out) */
static int free_tiles(const mdrt_vdp *v, uint16_t *out, int max)
{
    static uint8_t used[2048];
    memset(used, 0, sizeof used);
    static const int cells[4] = { 32, 64, 32, 128 };
    int w = cells[v->s.reg[16] & 3], h = cells[(v->s.reg[16] >> 4) & 3], h40 = mdrt_vdp_width(v) == 320;
    uint32_t tab[3] = { (uint32_t)(v->s.reg[2] & 0x38) << 10, (uint32_t)(v->s.reg[4] & 7) << 13, (uint32_t)(v->s.reg[3] & (h40 ? 0x3C : 0x3E)) << 10 };
    int tn[3] = { w * h, w * h, h40 ? 64 * 32 : 32 * 32 };
    for (int t = 0; t < 3; t++) {
        for (int i = 0; i < tn[t]; i++) {
            uint32_t a = (tab[t] + (uint32_t)(i << 1)) & 0xFFFE;
            used[(v->s.vram[a] << 8 | v->s.vram[a + 1]) & 0x7FF] = 1;
        }
        for (uint32_t a = tab[t]; a < tab[t] + (uint32_t)tn[t] * 2 && a < 0x10000; a += 32) used[a >> 5] = 1;
    }
    uint32_t sat = (uint32_t)(v->s.reg[5] & (h40 ? 0x7E : 0x7F)) << 9, hs = (uint32_t)(v->s.reg[13] & 0x3F) << 10;
    for (uint32_t a = sat; a < sat + 640 && a < 0x10000; a += 32) used[a >> 5] = 1;
    for (uint32_t a = hs; a < hs + 1024 && a < 0x10000; a += 32) used[a >> 5] = 1;
    for (int k = 0, e = 0; k < 80; k++) {
        const uint8_t *q = v->s.vram + ((sat + (uint32_t)e * 8) & 0xFFFF);
        int t0 = (q[4] << 8 | q[5]) & 0x7FF, n = (((q[2] >> 2) & 3) + 1) * ((q[2] & 3) + 1);
        for (int i = 0; i < n; i++) used[(t0 + i) & 0x7FF] = 1;
        e = q[3] & 0x7F;
        if (!e) break;
    }
    int n = 0;
    for (int t = 1; t < 2048 && n < max; t++) if (!used[t]) out[n++] = (uint16_t)t;
    return n;
}

/* the cartridge's crest at centre ice taken out of the picture and NHL 95 PC's logo of the home team put in its place, over only the
 * rink: under the windows, the scoreboard and the players as the crest was. vdp: the VDP as it drew the picture, less the cartridge
 * sprites the PC's skaters replace; px its picture (pitch_px apart). Everything is from that VDP state, the same scroll and plane
 * map the picture was drawn with, so the logo moves with the rink to the pixel: the crest's cells in the plane map, put on the
 * screen by the plane's scroll. The crest's cells are given the tile left of them in their row (the ice, or the red centre line,
 * which the crest's tiles bake in) and the picture drawn again: the line runs on under the logo, drawn by the VDP itself. Called
 * before the PC's players are drawn. */
static void logo_draw(const mdrt_vdp *vdp, const uint16_t *px, int pitch_px, uint32_t *pic, int w, int h, int pitch, const uint8_t *drop)
{
    int mode = SDL_AtomicGet(&LGO.on);
    if (!mode) return;
    if (!LGO.L[0].have && !LGO.L[1].have) { logo_why(0, "no NHL 95 PC logos (RINK.PAL, <team>.MAP / .TIL) on the disc image or beside the program"); return; }
    if (mode == 2 && !LGO.L[1].have) logo_why(1, "no NHL 94 PC files (RINKPAL.QPP, <team>.MAP / .TIL) in NHL94PC beside the program or NHL94PC_DIR: NHL 95 PC's shown");
    int team = team_struct(0)->tmteam, art = rosters_art((uint16_t)team);
    if (art < 0 || art >= 28 || logo_of_art[art] < 0) { logo_why(2, "the home team has no PC logo (an All-Star team): the cartridge's crest shown"); return; }
    int k = logo_of_art[art];
    const struct lset *LS = (mode == 2 || !LGO.L[0].have) && LGO.L[1].have && LGO.L[1].t[k].idx ? &LGO.L[1] : &LGO.L[0];   /* (94 falls back to 95 where it has no such logo) */
    if (!LS->t[k].idx) { logo_why(3, "the PC files have no logo of the home team: the cartridge's crest shown"); return; }
    if (LGO.seen != team) LC.ok = LC.sy_ok = 0;                          /* (another team's crest: its layout seen again) */
    if (LGO.crest < 0 || !(g.Vcount & 63) || LGO.seen != team) { LGO.crest = find_crest(vdp, team); LGO.seen = team; }
    int log = getenv("PCLOGO_LOG") && (!(g.Vcount & 63) || getenv("PCLOGO_LOG")[0] == '2');
    if (log) fprintf(stderr, "logo? frame %u team %d art %d crest %d\n", (unsigned)g.Vcount, team, art, LGO.crest);
    if (LGO.crest < 0) { if (find3d_visible()) logo_why(4, "the cartridge's crest was not found in the VDP's memory: the cartridge's crest shown"); return; }
    struct crest_at C, F;
    int got = crest_cells(vdp, LGO.crest, &C, &F);
    if (!got && LC.ok && LC.at) {                                    /* (none of its cells left: its rows all written over, off the screen,
                                                                      * while the logo, taller than the crest, still shows: where it was) */
        static const int cells[4] = { 32, 64, 32, 128 };
        int pw = cells[vdp->s.reg[16] & 3] * 8, ph = cells[(vdp->s.reg[16] >> 4) & 3] * 8, vs0 = vdp->s.vsram[LC.plane] & 0x7FF, off = 1;
        uint32_t table = LC.plane ? (uint32_t)(vdp->s.reg[4] & 7) << 13 : (uint32_t)(vdp->s.reg[2] & 0x38) << 10;
        for (int y = LC.oy; y < LC.oy + LC.ch * 8 && off; y++) off = ((y - vs0) & (ph - 1)) >= h;
        if (off && pw == LC.pw && ph == LC.ph && table == LC.table) {
            F = (struct crest_at){ LC.plane, LC.ox, LC.oy, LC.ox + LC.cw * 8, LC.oy + LC.ch * 8, pw, ph, table };
            C = F; C.y1 = C.y0; got = 2;                             /* (none of its rows the crest's: none changed) */
        }
    }
    LC.at = got && LC.ok && F.plane == LC.plane && F.table == LC.table;
    if (!got) return;                                                /* (the crest is in no plane's map: not on the rink in view) */
    /* the crest's middle on the screen, by the plane's scroll as the VDP draws it (vertical: the whole screen's; horizontal: the
     * line's, the crest's middle line): the whole crest's, by its layout when part of it is gone, so the logo stays on the rink to
     * the pixel as the crest scrolls off the screen */
    int vs = vdp->s.vsram[F.plane] & 0x7FF;
    int sy = ((F.y0 + F.y1) / 2 - vs) & (F.ph - 1);
    /* (the plane is 256 high and the screen 224: a crest off the screen is as far off its top as off its bottom in the plane. Which:
     * the side of it the cartridge has written over is the one away from the screen (its top gone: above the screen, its bottom
     * gone: below); all of it there or none, the side nearer where it was the frame before (the camera moves a few pixels a
     * frame); with none, the nearer edge) */
    if (got == 2 && C.y1 > C.y0 && C.y0 > F.y0 && C.y1 == F.y1) { if (sy >= h / 2) sy -= F.ph; }
    else if (got == 2 && C.y1 > C.y0 && C.y0 == F.y0 && C.y1 < F.y1) { if (sy < h / 2) sy += F.ph; }
    else if (LC.sy_ok && (uint16_t)(g.Vcount - LC.sy_at) <= 2) { if (abs(sy - F.ph - LC.sy) < abs(sy - LC.sy)) sy -= F.ph; }
    else if (sy >= h / 2 + F.ph / 2) sy -= F.ph;
    LC.sy = sy; LC.sy_at = (uint16_t)g.Vcount; LC.sy_ok = 1;
    int hl = sy < 0 ? 0 : sy >= h ? h - 1 : sy;
    if ((vdp->s.reg[11] & 3) == 2) hl &= ~7;
    else if ((vdp->s.reg[11] & 3) != 3) hl = 0;
    uint32_t hs = ((uint32_t)(vdp->s.reg[13] & 0x3F) << 10) + (uint32_t)(hl << 2) + (F.plane ? 2u : 0u);
    int hsv = (vdp->s.vram[hs & 0xFFFE] << 8 | vdp->s.vram[(hs & 0xFFFE) + 1]) & 0x3FF;
    if (hsv & 0x200) hsv -= 0x400;
    int sx = ((F.x0 + F.x1) / 2 + hsv) & (F.pw - 1);
    if (sx >= w / 2 + F.pw / 2) sx -= F.pw;
    int keep = got == 1 && crest_keep(vdp, LGO.crest, &F, vs, hsv, w, h), part = got == 2;
    if (part) { C.x0 = F.x0; C.x1 = F.x1; }                         /* (C: the crest's rows still in the plane, the only ones changed below;
                                                                      * the others are the rink past the screen's edge now, some of them in view) */
    int ch = C.ph / 8;
#define ROW_OURS(y) (!part || ((y) >= C.y0 && (y) < C.y1))          /* (a row of the plane still the crest's: not one written over) */
    static mdrt_vdp sc;
    static uint16_t blank[320 * 240];
    static uint8_t shows[320 * 240];
    sc = *vdp;
    int cw = C.pw / 8;
    for (int r = C.y0 / 8; r < C.y1 / 8; r++)                        /* (each crest cell: the first cell left of it in the row that is not the crest's) */
        for (int q = C.x0 / 8; q < C.x1 / 8; q++) {
            uint32_t a = (C.table + (uint32_t)(((r & (ch - 1)) * cw + (q & (cw - 1))) << 1)) & 0xFFFE;
            int t = (vdp->s.vram[a] << 8 | vdp->s.vram[a + 1]) & 0x7FF;
            if (t < LGO.crest || t >= LGO.crest + 24) continue;
            uint32_t from = a;
            for (int l = 1; l <= cw; l++) {
                uint32_t b2 = (C.table + (uint32_t)(((r & (ch - 1)) * cw + ((q - l) & (cw - 1))) << 1)) & 0xFFFE;
                int t2 = (vdp->s.vram[b2] << 8 | vdp->s.vram[b2 + 1]) & 0x7FF;
                if (t2 < LGO.crest || t2 >= LGO.crest + 24) { from = b2; break; }
            }
            sc.s.vram[a] = vdp->s.vram[from];
            sc.s.vram[a + 1] = vdp->s.vram[from + 1];
        }
    /* the red centre line: the cartridge's stops short of its crest (the crest is over it), the PC's logo is narrower. A row of cells
     * whose cell 5 left of the crest has the line's red runs it on to 5 right of it, over the cells with no red (the ice and the
     * crest's); the cells the line has (where the circle crosses it) stay. Under the PC's logo itself (its columns, its rows) the
     * cells are plain ice: the logo brings its own centre line and dot, as on the PC, and none of the cartridge's line, its pink
     * edge rows or its dot shows through the logo's clear pixels */
    int lw = LS->t[k].w, lh = LS->t[k].h;
    int ly0 = 1 << 20, ly1 = -1;                                       /* (the line's rows in the plane: its red and its pink edges) */
    for (int r = C.y0 / 8; r < C.y1 / 8; r++) {
        uint32_t a = (C.table + (uint32_t)(((r & (ch - 1)) * cw + ((C.x0 / 8 - 5) & (cw - 1))) << 1)) & 0xFFFE;
        uint16_t e = (uint16_t)(sc.s.vram[a] << 8 | sc.s.vram[a + 1]);
        int red = 0;
        for (int i = 0; i < 64; i++) red += is_red(&sc, e, i & 7, i >> 3);
        if (red < 4) continue;
        for (int y = 0; y < 8; y++) {
            int n = 0, pink = 0;
            for (int x = 0; x < 8; x++) { n += is_red(&sc, e, x, y); pink += is_pink(&sc, e, x, y); }
            if (n >= 2 || pink >= 2) { if (r * 8 + y < ly0) ly0 = r * 8 + y; if (r * 8 + y > ly1) ly1 = r * 8 + y; }
        }
    }
    if (keep || LC.at) { LC.ox = F.x0; LC.oy = F.y0; LC.at = LC.ok; }
    if (keep) { LC.line = ly1 >= 0; LC.ly0 = ly0 - C.y0; LC.ly1 = ly1 - C.y0; }
    if (part) { ly0 = LC.line ? F.y0 + LC.ly0 : 1 << 20; ly1 = LC.line ? F.y0 + LC.ly1 : -1; }   /* (the line's rows as the whole crest had them) */
    int a0 = -1, a1 = -1;                                              /* (the logo's own centre line: the rows red at both its edges) */
    const uint8_t (*lp)[3] = LS == &LGO.L[0] && SDL_AtomicGet(&P.found) ? P.pal : LS->pal;
    for (int y = 0; y < lh; y++) {
        int c0 = LS->t[k].idx[y * lw], c1 = LS->t[k].idx[y * lw + lw - 1];
        if (c0 != 255 && c1 != 255 && lp[c0][0] > 150 && lp[c0][1] < 120 && lp[c1][0] > 150 && lp[c1][1] < 120) { if (a0 < 0) a0 = y; a1 = y; }
    }
    /* the logo in the plane: its middle column the crest's, its line on the cartridge's line (else its middle the crest's) */
    int pmx = (F.x0 + F.x1) / 2, px0 = pmx - lw / 2, py0 = ly1 >= 0 && a0 >= 0 ? (ly0 + ly1 + 1) / 2 - (a0 + a1 + 1) / 2 : (F.y0 + F.y1) / 2 - lh / 2;
    uint16_t ice = 0;                                                  /* (a cell of plain ice: one colour, the crest's neighbours give one) */
    int have_ice = 0;
    for (int r = C.y0 / 8; r < C.y1 / 8 && !have_ice; r++)
        for (int q = C.x0 / 8 - 1; q >= C.x0 / 8 - 3 && !have_ice; q--) {
            uint32_t a = (C.table + (uint32_t)(((r & (ch - 1)) * cw + (q & (cw - 1))) << 1)) & 0xFFFE;
            uint16_t e = (uint16_t)(sc.s.vram[a] << 8 | sc.s.vram[a + 1]);
            int c0 = tile_px(&sc, e, 0, 0), same = 1;
            for (int i = 1; i < 64 && same; i++) same = tile_px(&sc, e, i & 7, i >> 3) == c0;
            if (same) { ice = e; have_ice = 1; }
        }
    for (int r = C.y0 / 8; r < C.y1 / 8; r++) {
        uint16_t run = 0;
        for (int q = C.x0 / 8 - 5; q <= C.x1 / 8 + 4; q++) {
            uint32_t a = (C.table + (uint32_t)(((r & (ch - 1)) * cw + (q & (cw - 1))) << 1)) & 0xFFFE;
            uint16_t e = (uint16_t)(sc.s.vram[a] << 8 | sc.s.vram[a + 1]);
            int t = (vdp->s.vram[a] << 8 | vdp->s.vram[a + 1]) & 0x7FF, red = 0;
            int crest = t >= LGO.crest && t < LGO.crest + 24;
            for (int i = 0; i < 64 && !crest; i++) red += is_red(&sc, e, i & 7, i >> 3);
            if (red >= 4) run = e;
            else if (q == C.x0 / 8 - 5) break;                          /* (not a row of the line) */
            else { sc.s.vram[a] = (uint8_t)(run >> 8); sc.s.vram[a + 1] = (uint8_t)run; }
        }
    }
    /* the PC's centre line across the whole rink, not only the logo's width (NHL 94 / 95 PC: one line, red with light dashes, the
     * logo's own line its middle part). The cartridge's line is taken out of its cells (its colours, in the rows it has at a cell
     * left of the crest, become the ice: new tiles in the crest's, which the plane no longer shows), and a second picture, with the
     * rows of the PC's line marked in those tiles, says where it shows (not under a window, a sprite or a priority tile): there the
     * logo's own line rows are drawn, their 8 pixel pattern repeated out from the logo's edges */
    static mdrt_vdp mk;
    static uint16_t mark[320 * 240];
    int pl_top = -1, pl_n = 0;                                         /* (the PC line's rows in the plane, when it is drawn) */
    int b0 = 1 << 20, b1 = -1, nS = 0;
    unsigned S[16], ice_c = have_ice ? sc.s.cram[((ice >> 13) & 3) * 16 + tile_px(&sc, ice, 0, 0)] : 0;
    if (have_ice && ly1 >= 0 && a0 >= 0 && lw >= 16) {
        int mid = (ly0 + ly1 + 1) / 2;
        for (int y = mid - 6; y <= mid + 6; y++) {                     /* (the cartridge's line: the rows most cells left of the crest have, their colours) */
            if (!ROW_OURS(y)) continue;                                 /* (its colours from the crest's rows still in the plane) */
            int r = (y >> 3) & (C.ph / 8 - 1), cells = 0;
            for (int q = C.x0 / 8 - 12; q < C.x0 / 8; q++) {
                uint32_t a = (C.table + (uint32_t)((r * cw + (q & (cw - 1))) << 1)) & 0xFFFE;
                uint16_t e = (uint16_t)(sc.s.vram[a] << 8 | sc.s.vram[a + 1]);
                int n = 0;
                for (int x = 0; x < 8; x++) { int c; n += px_colour(&sc, e, x, y & 7, &c) != ice_c && c; }
                cells += n >= 4;
            }
            if (cells < 8) continue;                                    /* (a row of the ice, its sparkles, or the circle) */
            if (y < b0) b0 = y;
            if (y > b1) b1 = y;
            for (int q = C.x0 / 8 - 12; q < C.x0 / 8; q++) {
                uint32_t a = (C.table + (uint32_t)((r * cw + (q & (cw - 1))) << 1)) & 0xFFFE;
                uint16_t e = (uint16_t)(sc.s.vram[a] << 8 | sc.s.vram[a + 1]);
                for (int x = 0; x < 8; x++) {
                    int c;
                    unsigned u = px_colour(&sc, e, x, y & 7, &c);
                    if (!c || u == ice_c) continue;
                    int k2 = 0;
                    while (k2 < nS && S[k2] != u) k2++;
                    if (k2 == nS && nS < 16) S[nS++] = u;
                }
            }
        }
    }
    if (b1 >= b0 && b1 - b0 > 10) b1 = -1;
    if (keep) { LC.bl = b1 >= b0; LC.b0 = b0 - C.y0; LC.b1 = b1 - C.y0; }
    if (part) { b0 = LC.bl ? F.y0 + LC.b0 : 1 << 20; b1 = LC.bl ? F.y0 + LC.b1 : -1; }   /* (the cartridge's line's rows as the whole crest had them) */
    if (b1 >= b0) py0 = (b0 + b1 + 1) / 2 - (a0 + a1 + 1) / 2;          /* (the logo's line on the middle of the cartridge's, edges and all) */
    if (have_ice)                                                      /* (under the logo, in the crest's rows: the ice; the circle stays) */
        for (int r = (py0 > C.y0 ? py0 : C.y0) >> 3; r <= (py0 + lh < C.y1 ? py0 + lh - 1 : C.y1 - 1) >> 3; r++)
            for (int q = px0 >> 3; q <= (px0 + lw - 1) >> 3; q++) {
                uint32_t a = (C.table + (uint32_t)(((r & (C.ph / 8 - 1)) * cw + (q & (cw - 1))) << 1)) & 0xFFFE;
                sc.s.vram[a] = (uint8_t)(ice >> 8);
                sc.s.vram[a + 1] = (uint8_t)ice;
            }
    {
        if (b1 >= b0) {
            mk = sc;
            pl_top = py0 + a0; pl_n = a1 - a0 + 1;
            uint32_t key[160];
            uint16_t slots[160];
            int nslot = free_tiles(&sc, slots, 160), nk = 0, full = 0;
            /* how far the cartridge's line runs out from the crest: the cells next to each other that have its red or pink in its
             * rows. It stops at the boards; the boards, the benches and the crowd past them have the line's light colours (its
             * dashes', its edges') too, and a test by those colours alone ran the PC's line through them to the screen's edges */
            int q0 = C.x0 / 8 - 5, nearn = C.x1 / 8 + 4 - q0, run_l = 0, run_r = 0;
            for (int side = 0; side < 2; side++)
                for (int *run = side ? &run_r : &run_l; nearn + run_l + run_r + 1 < cw; ++*run) {
                    int q = side ? q0 + nearn + 1 + *run : q0 - 1 - *run, red = 0;
                    for (int y = b0; y <= b1; y++) {
                        if (!ROW_OURS(y)) continue;
                        uint32_t a = (C.table + (uint32_t)((((y >> 3) & (C.ph / 8 - 1)) * cw + (q & (cw - 1))) << 1)) & 0xFFFE;
                        uint16_t e = (uint16_t)(sc.s.vram[a] << 8 | sc.s.vram[a + 1]);
                        for (int x = 0; x < 8; x++) red += is_red(&sc, e, x, y & 7) || is_pink(&sc, e, x, y & 7);
                    }
                    if (red < 2) break;
                }
            for (int r = b0 >> 3; r <= b1 >> 3; r++) if (ROW_OURS(r * 8))
                for (int q = 0; q < cw; q++) {
                    int off = (q - q0) & (cw - 1);
                    if (off > nearn + run_r && off < cw - run_l) continue;   /* (past the boards) */
                    int rr = r & (C.ph / 8 - 1);
                    uint32_t a = (C.table + (uint32_t)((rr * cw + q) << 1)) & 0xFFFE;
                    uint16_t e = (uint16_t)(sc.s.vram[a] << 8 | sc.s.vram[a + 1]);
                    int n = 0;
                    for (int y = 0; y < 8; y++) {
                        if (r * 8 + y < b0 || r * 8 + y > b1) continue;
                        for (int x = 0; x < 8; x++) {
                            int c;
                            unsigned u = px_colour(&sc, e, x, y, &c);
                            for (int i = 0; c && i < nS; i++) n += u == S[i];
                        }
                    }
                    int span = (q - (px0 >> 3)) & (cw - 1), near = (q - (C.x0 / 8 - 5)) & (cw - 1);
                    if (span <= ((px0 + lw - 1) >> 3) - (px0 >> 3)) continue;   /* (the logo's own) */
                    near = near <= C.x1 / 8 + 4 - (C.x0 / 8 - 5);      /* (where the cartridge's line stops short of its crest: the line goes on) */
                    if (n < 4 && !near) continue;                       /* (no line here: the boards, the crowd) */
                    int slot = 0;
                    uint32_t kk = (uint32_t)near << 31 | (uint32_t)r << 16 | e;
                    while (slot < nk && key[slot] != kk) slot++;
                    if (slot == nk) {
                        if (nk == nslot) { full++; continue; }
                        key[nk++] = kk;
                        int pal = (e >> 13) & 3, ic = 1, bc = 1, bd = -1, id = 1 << 30;
                        for (int i = 1; i < 16 - !pal; i++) {           /* (the palette's ice, and the colour furthest from it: the mark; not
                                                                           * colour 15 of palette 0, which sprite_masks makes its own) */
                            unsigned u = sc.s.cram[pal * 16 + i];
                            int dr = (int)(u & 0xE) - (int)(ice_c & 0xE), dg = (int)(u >> 4 & 0xE) - (int)(ice_c >> 4 & 0xE), db = (int)(u >> 8 & 0xE) - (int)(ice_c >> 8 & 0xE);
                            int d = dr * dr + dg * dg + db * db;
                            if (d < id) { id = d; ic = i; }
                            if (d > bd) { bd = d; bc = i; }
                        }
                        uint8_t *t = sc.s.vram + (uint32_t)slots[slot] * 32, *tm = mk.s.vram + (uint32_t)slots[slot] * 32;
                        int cols = 0;                           /* (the columns with the line's red or pink: in the last cell before the
                                                                   * boards, not the boards' own pixels of the line's light colours) */
                        for (int y = 0; y < 8; y++)
                            for (int x = 0; x < 8 && r * 8 + y >= b0 && r * 8 + y <= b1; x++)
                                if (is_red(&sc, e, x, y) || is_pink(&sc, e, x, y)) cols |= 1 << x;
                        for (int y = 0; y < 8; y++)
                            for (int x = 0; x < 8; x++) {
                                int c, py = r * 8 + y, line = 0;
                                unsigned u = px_colour(&sc, e, x, y, &c);
                                int pcrow = py >= pl_top && py < pl_top + pl_n;
                                if (py >= b0 && py <= b1 && (near || cols >> x & 1)) for (int i = 0; c && i < nS; i++) line |= u == S[i];
                                line |= near && pcrow;                  /* (the PC's line over the circle, as on the PC) */
                                int v = line ? ic : c, vm = line || (pcrow && c && u == ice_c) ? bc : v;
                                uint8_t *d = t + y * 4 + (x >> 1), *dm = tm + y * 4 + (x >> 1);
                                *d = (uint8_t)(x & 1 ? (*d & 0xF0) | v : (*d & 0x0F) | v << 4);
                                *dm = (uint8_t)(x & 1 ? (*dm & 0xF0) | vm : (*dm & 0x0F) | vm << 4);
                            }
                    }
                    uint16_t ne = (uint16_t)((e & 0xE000) | slots[slot]);
                    sc.s.vram[a] = mk.s.vram[a] = (uint8_t)(ne >> 8);
                    sc.s.vram[a + 1] = mk.s.vram[a + 1] = (uint8_t)ne;
                }
            if (log) fprintf(stderr, "logo: PC line rows %d-%d over the cartridge's %d-%d (%d colours, %d tiles of %d free, %d cells left)\n", pl_top, pl_top + pl_n - 1, b0, b1, nS, nk, nslot, full);
            mdrt_vdp_render(&mk, mark, 320);
        }
    }
    mdrt_vdp_render(&sc, blank, 320);
    for (int y = 0; y < h; y++)                                      /* (the crest's pixels: where the picture changes) */
        for (int x = 0; x < w; x++)
            if (blank[y * 320 + x] != px[y * pitch_px + x]) pic[(long)y * pitch + x] = 0xFF000000u | mdrt_vdp_rgb(blank[y * 320 + x], MDRT_RGB_GPGX);
    int lx = sx - lw / 2, ly = sy + py0 - (F.y0 + F.y1) / 2;         /* (its rows as in the plane: the line's on the cartridge's) */
    uint32_t ice_rgb = 0xFF000000u | mdrt_vdp_rgb((uint16_t)ice_c, MDRT_RGB_GPGX);
    if (pl_n > 0)                                                      /* (the PC's line out from the logo's edges, where it shows; the ice where the cartridge's was) */
        for (int y = 0; y < h && y < 240; y++)
            for (int x = 0; x < w && x < 320; x++) {
                if (blank[y * 320 + x] == mark[y * 320 + x]) continue;
                int row = y - ly, d = (((x - hsv) & (C.pw - 1)) - pmx) & (C.pw - 1), col;
                if (d >= C.pw / 2) d -= C.pw;
                d += lw / 2;                                           /* (the logo's column) */
                if (d < 0) col = ((d % 8) + 8) % 8;
                else if (d >= lw) col = lw - 8 + (d - lw) % 8;
                else continue;                                         /* (the logo draws its own) */
                int c = row >= a0 && row <= a1 ? LS->t[k].idx[row * lw + col] : 255;
                pic[(long)y * pitch + x] = c == 255 ? ice_rgb : fadec(0xFF000000u | (uint32_t)lp[c][0] << 16 | (uint32_t)lp[c][1] << 8 | lp[c][2]);
            }
    if (log) fprintf(stderr, "logo: line plane rows %d-%d, art rows %d-%d, plane top %d, ice %d, screen %d,%d\n", ly0, ly1, a0, a1, py0, have_ice, lx, ly);
    if (log) fprintf(stderr, "logo: art %d crest tile %d plane %c cells %d,%d-%d,%d scroll %d,%d middle %d,%d%s%s\n", art, LGO.crest, C.plane ? 'B' : 'A', C.x0, C.y0, C.x1, C.y1, hsv, vs, sx, sy, part ? " (part gone: by the layout kept)" : "", keep ? " (layout kept)" : "");
#undef ROW_OURS
    if (lx >= w || ly >= h || lx + lw <= 0 || ly + lh <= 0) return;
    if (!layer_mask(drop, -1, 0, lx, ly, lw, lh, w, h, shows)) { if (getenv("PCLOGO_LOG")) fprintf(stderr, "logo: no mask (%dx%d)\n", lw, lh); return; }   /* (plane B's: behind the last sprite, under every high tile) */
    for (int y = 0; y < lh; y++)
        for (int x = 0; x < lw; x++) {
            int c = LS->t[k].idx[y * lw + x];
            if (c == 255) continue;                                                    /* (clear) */
            int ppx = lx + x, ppy = ly + y;
            if (ppx < 0 || ppx >= w || ppy < 0 || ppy >= h || !shows[ppy * 320 + ppx]) continue;
            const uint8_t *pc = LS == &LGO.L[0] && SDL_AtomicGet(&P.found) ? P.pal[c] : LS->pal[c];   /* (the PC's own rink palette: RINK.PAL is a rougher 5 bit copy of it) */
            pic[(long)ppy * pitch + ppx] = fadec(0xFF000000u | (uint32_t)pc[0] << 16 | (uint32_t)pc[1] << 8 | pc[2]);
        }
}

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

/* Over the picture the game drew (px: its colours, pic: the same as RGB). The cartridge's own sprites of the objects the PC draws are
 * still in it: the picture is drawn again from a copy of the VDP with those sprite table entries taken out, and where the two differ
 * the cartridge's sprite was visible: there its background is given back. Then each picture of the PC's in its layer (above), back to
 * front: the logo, the skaters in the sprite table's order from its last (the first is on top), the referee among them by his depth,
 * the anthem's flag. Nothing is drawn while the display is off or the colours are faded to black (the switch to and from the penalty
 * screen: the PC's pictures follow the cartridge's fade). */
void pcspr_present(const mdrt_vdp *vdp, const uint16_t *px, int pitch_px, uint32_t *pic, int w, int h, int pitch)
{
    if ((!SDL_AtomicGet(&P.on) && !ref_on() && !SDL_AtomicGet(&LGO.on) && hold_anthem_time() < 0) || !SDL_AtomicGet(&P.found) || !in_game()) return;
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
    if (!(vdp->s.reg[1] & 0x40) || fade_black) return;        /* (nothing of the cartridge's picture shows: black) */
    layer_begin(vdp);
    if (!SDL_AtomicGet(&P.on)) n = 0;
    static uint8_t drop[80];
    memset(drop, 0, sizeof drop);
    for (int i = 0; i < n; i++)
        for (int k = 0; k < list[i].count; k++) if (list[i].first + k < 80) drop[list[i].first + k] = 1;
    int rk = 0;                                                  /* the referee: in front of the listed ones from rk on (farther up the screen) */
    while (rk < n && list[rk].y >= R.sy) rk++;
    int ref_before = rk < n ? list[rk].first : n ? entry_after(&list[n - 1]) : -1;
    if (n <= 0) { logo_draw(vdp, px, pitch_px, pic, w, h, pitch, drop); ref_draw(pic, w, h, pitch, drop, ref_before); flag_draw(pic, w, h, pitch); return; }
    static mdrt_vdp hidden;
    static uint16_t clean[320 * 240];
    hidden = *vdp;                                               /* (the VDP without the cartridge sprites the PC's replace: the logo goes under the PC's, on the rink they uncover) */
    unsigned sat = sat0;
    for (int i = 0; i < n; i++)
        for (int k = 0; k < list[i].count; k++) {
            unsigned e = (unsigned)(list[i].first + k);
            if (e >= 80) continue;
            hidden.s.vram[(sat + e * 8) & 0xFFFF] = 0;           /* y = 0: off the top */
            hidden.s.vram[(sat + e * 8 + 1) & 0xFFFF] = 0;
            hidden.sat[e * 8] = hidden.sat[e * 8 + 1] = 0;
        }
    mdrt_vdp_render(&hidden, clean, 320);
    static uint8_t vis[320 * 240];                               /* (the cartridge sprite's own pixels: the fallback when no mask can be had) */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            vis[y * 320 + x] = px[y * pitch_px + x] != clean[y * 320 + x];
            if (vis[y * 320 + x]) pic[(long)y * pitch + x] = 0xFF000000u | mdrt_vdp_rgb(clean[y * 320 + x], MDRT_RGB_GPGX);
        }
    logo_draw(&hidden, clean, 320, pic, w, h, pitch, drop);
    if (rk == n) ref_draw(pic, w, h, pitch, drop, ref_before);
    static uint8_t m[320 * 240];
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
        unsigned e0 = (unsigned)list[i].first;
        int pri = e0 < 80 && (vdp->s.vram[(sat + e0 * 8 + 4) & 0xFFFF] & 0x80);
        const uint8_t *mask = layer_mask(drop, list[i].first, pri, x0, y0, s->w, s->h, w, h, m) ? m : vis;
        for (int y = 0; y < s->h; y++)
            for (int x = 0; x < s->w; x++) {
                int v = s->px[y * s->w + (list[i].flip ? s->w - 1 - x : x)];
                if (v == 255) continue;
                int px_ = x0 + x, py = y0 + y;
                if (px_ < 0 || px_ >= w || py < 0 || py >= h || !mask[py * 320 + px_]) continue;   /* (something in front of him there) */
                int r = v < 0x80 ? rec[192 + v] : rec[320 + v - 0x80];
                pic[(long)py * pitch + px_] = fadec(0xFF000000u | (uint32_t)pal[r][0] << 16 | (uint32_t)pal[r][1] << 8 | pal[r][2]);
            }
        if (i == rk) ref_draw(pic, w, h, pitch, drop, ref_before);
    }
    flag_draw(pic, w, h, pitch);
}
