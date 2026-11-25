/* Colour player photos: reading the PC portraits and colouring a card (cardcolor_xfer.h). The steps are
 * the approved prototype's (the plan doc's "Agreed method"), one for one:
 *
 *   the portrait   95 PC: its bevel (the 9 pixel frame) evened out with the picture inside it; mirrored
 *                  when the card is; scaled (Lanczos, as the prototype's) and placed on the card
 *   light          the card's grey eased 45% toward the portrait's
 *   colour         the portrait's, where it is trusted; skin never leaves the portrait sideways or up
 *                  (the neck may go on down); the rest of the card from the portrait's edge columns,
 *                  smoothed along like greys; dark shoulders the colour of the like-grey non-skin
 *                  pixels (a jersey the helmet's); grain pixels too light or dark to hold colour moved
 *                  a little so they keep it */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "cardcolor_xfer.h"

#define N CC_N
#define NN (CC_N * CC_N)
#define BEVEL 9                                   /* the 95 PC frame the alignment leaves out */

uint32_t cc_hash(const uint8_t *p, long n)
{
    uint32_t h = 0x811C9DC5u;
    for (long i = 0; i < n; i++) h = (h ^ p[i]) * 0x01000193u;
    return h;
}

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static unsigned le16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }
static int s16le(const uint8_t *p) { return (int16_t)le16(p); }

void cc_img_free(cc_img *im)
{
    free(im->px);
    im->px = NULL;
}

/* ---- the files ---------------------------------------------------------------------------- */

uint8_t *cc_refpack(const uint8_t *in, long n, long *out_n)
{
    if (n < 5 || in[1] != 0xFB || (in[0] & 0xFE) != 0x10) return NULL;
    long p = 2 + ((in[0] & 1) ? 3 : 0);
    if (p + 3 > n) return NULL;
    long size = (long)in[p] << 16 | (long)in[p + 1] << 8 | in[p + 2];
    p += 3;
    if (size <= 0 || size > 32L << 20) return NULL;
    uint8_t *out = malloc((size_t)size);
    if (!out) return NULL;
    long w = 0;
#define LIT(k) do { long k_ = (k); if (k_ < 0 || p + k_ > n || w + k_ > size) goto done; memcpy(out + w, in + p, (size_t)k_); p += k_; w += k_; } while (0)
#define REF(o, l) do { long o_ = (o), l_ = (l); if (o_ <= 0 || o_ > w || w + l_ > size) goto done; for (long i_ = 0; i_ < l_; i_++) out[w + i_] = out[w - o_ + i_]; w += l_; } while (0)
    while (w < size && p < n) {
        unsigned b0 = in[p++];
        if (!(b0 & 0x80)) {
            if (p >= n) break;
            unsigned b1 = in[p++];
            LIT(b0 & 3);
            REF((long)((b0 & 0x60) << 3) + b1 + 1, ((b0 & 0x1C) >> 2) + 3);
        } else if (!(b0 & 0x40)) {
            if (p + 1 >= n) break;
            unsigned b1 = in[p++], b2 = in[p++];
            LIT(b1 >> 6);
            REF((long)((b1 & 0x3F) << 8) + b2 + 1, (b0 & 0x3F) + 4);
        } else if (!(b0 & 0x20)) {
            if (p + 2 >= n) break;
            unsigned b1 = in[p++], b2 = in[p++], b3 = in[p++];
            LIT(b0 & 3);
            REF((long)((b0 & 0x10) << 12) + (long)(b1 << 8) + b2 + 1, (long)((b0 & 0x0C) << 6) + b3 + 5);
        } else if (b0 < 0xFC) {
            LIT(((b0 & 0x1F) + 1) * 4);
        } else {
            if (b0 & 3) LIT(b0 & 3);
            break;
        }
    }
done:
#undef LIT
#undef REF
    *out_n = w;
    return out;
}

/* a VGA colour (6 bits a channel) as 8 bits */
static uint8_t vga(uint8_t v) { return (uint8_t)lround((v & 63) * 255.0 / 63.0); }

/* the SHPI bank's entry with this tag: its offset, 0 none */
static long shpi_entry(const uint8_t *b, long n, const char *tag)
{
    if (n < 16 || memcmp(b, "SHPI", 4)) return 0;
    long count = (long)le32(b + 8);
    if (count <= 0 || 16 + count * 8 > n) return 0;
    for (long i = 0; i < count; i++) {
        const uint8_t *e = b + 16 + i * 8;
        if (!memcmp(e, tag, 4)) {
            long off = (long)le32(e + 4);
            return off > 0 && off + 16 < n ? off : 0;
        }
    }
    return 0;
}

int cc_qfs_shape(const uint8_t *file, long n, const char *tag, cc_img *out)
{
    long rn = 0;
    uint8_t *b = cc_refpack(file, n, &rn);
    memset(out, 0, sizeof *out);
    if (!b) return 0;
    char ptag[5] = { '!', tag[1], tag[2], tag[3], 0 };
    long s = shpi_entry(b, rn, tag), p = shpi_entry(b, rn, ptag);
    int ok = 0;
    if (s && p && b[s] == 0x7B && b[p] == 0x22) {
        int w = s16le(b + s + 4), h = s16le(b + s + 6), colours = s16le(b + p + 4);
        if (w > 0 && h > 0 && w <= 1024 && h <= 1024 && s + 16 + (long)w * h <= rn && colours > 0 && colours <= 256 && p + 16 + colours * 3 <= rn) {
            out->px = malloc((size_t)w * h);
            if (out->px) {
                out->w = w;
                out->h = h;
                memcpy(out->px, b + s + 16, (size_t)w * h);
                for (int i = 0; i < colours; i++)
                    for (int k = 0; k < 3; k++) out->pal[i][k] = vga(b[p + 16 + i * 3 + k]);
                ok = 1;
            }
        }
    }
    free(b);
    return ok;
}

int cc_nsh_shape(const uint8_t *b, long n, const char *tag, cc_img *out)
{
    memset(out, 0, sizeof *out);
    if (n < 6) return 0;
    long count = (long)le16(b + 4), table = 6 + 8 * count;
    if (count <= 0 || table > n) return 0;
    long s = 0, p = 0;
    for (long i = 0; i < count; i++) {
        const uint8_t *nm = b + 6 + 4 * i;
        long off = table + (long)le32(b + 6 + 4 * count + 4 * i);
        if (off + 16 > n) continue;
        if (!memcmp(nm, "!pal", 4)) p = off;
        else if (!memcmp(nm, tag, 4)) s = off;
    }
    if (!s || !p || le16(b + s + 14) || le16(b + p + 14)) return 0;     /* (flags 0: planes stored raw) */
    int q = s16le(b + s), h = s16le(b + s + 2), colours = s16le(b + p);
    if (q <= 0 || h <= 0 || q > 256 || h > 1024 || s + 16 + 4L * q * h > n || colours <= 0 || colours > 256 || p + 16 + colours * 3 > n) return 0;
    out->px = malloc((size_t)q * 4 * h);
    if (!out->px) return 0;
    out->w = q * 4;
    out->h = h;
    for (int plane = 0; plane < 4; plane++)
        for (int y = 0; y < h; y++)
            for (int i = 0; i < q; i++) out->px[y * out->w + i * 4 + plane] = b[s + 16 + (long)plane * q * h + y * q + i];
    for (int i = 0; i < colours; i++)
        for (int k = 0; k < 3; k++) out->pal[i][k] = vga(b[p + 16 + i * 3 + k]);
    return 1;
}

void cc_genesis(const uint8_t *tiles, const uint8_t *map, const uint8_t *pal, double g[NN])
{
    int count = (int)be16(tiles), mw = (int)be16(map), mh = (int)be16(map + 2);
    double grey[16];
    for (int i = 0; i < 16; i++) grey[i] = ((be16(pal + 2 * i) >> 1) & 7) * 36.0;
    for (int i = 0; i < NN; i++) g[i] = 0;
    for (int r = 0; r < 6 && r < mh; r++)
        for (int c = 0; c < 6 && c < mw; c++) {
            unsigned e = be16(map + 4 + 2 * (r * mw + c));
            int t = (int)(e & 0x7FF);
            if (t >= count) continue;
            for (int y = 0; y < 8; y++) {
                const uint8_t *row = tiles + 2 + (t * 8 + ((e & 0x1000) ? 7 - y : y)) * 3;
                uint32_t bits = (uint32_t)row[0] << 16 | (uint32_t)row[1] << 8 | row[2];
                for (int x = 0; x < 8; x++) {
                    int sx = (e & 0x0800) ? 7 - x : x;
                    g[(r * 8 + y) * N + c * 8 + x] = grey[((bits >> (21 - 3 * sx)) & 7) + 5];
                }
            }
        }
}

/* ---- the portrait on the card ------------------------------------------------------------- */

/* Python's round(): halves to even */
static int pyround(double v) { return (int)nearbyint(v); }

/* 95 PC: the bevel band of each side, each channel brought to the picture just inside it (the
 * contrast of the band's inner 3 rows matched to the 4 rows inside, then the level) */
static void debevel(uint8_t *rgb, int W, int H)
{
    enum { B = 10 };
    if (W < 2 * B + 6 || H < 2 * B + 6) return;
    double *a = malloc(sizeof(double) * (size_t)W * H * 3);
    if (!a) return;
    for (long i = 0; i < (long)W * H * 3; i++) a[i] = rgb[i];
    for (int side = 0; side < 4; side++) {
        /* near: the band's 3 rows (or columns) inside its edge; ref: 4 just inside the band */
        int nx0, nx1, ny0, ny1, rx0, rx1, ry0, ry1;
        if (side == 0) { ny0 = B - 4; ny1 = B - 1; nx0 = B; nx1 = W - B; ry0 = B + 1; ry1 = B + 5; rx0 = B; rx1 = W - B; }
        else if (side == 1) { ny0 = H - B + 1; ny1 = H - B + 4; nx0 = B; nx1 = W - B; ry0 = H - B - 5; ry1 = H - B - 1; rx0 = B; rx1 = W - B; }
        else if (side == 2) { ny0 = B; ny1 = H - B; nx0 = B - 4; nx1 = B - 1; ry0 = B; ry1 = H - B; rx0 = B + 1; rx1 = B + 5; }
        else { ny0 = B; ny1 = H - B; nx0 = W - B + 1; nx1 = W - B + 4; ry0 = B; ry1 = H - B; rx0 = W - B - 5; rx1 = W - B - 1; }
        for (int ch = 0; ch < 3; ch++) {
            double sn = 0, sn2 = 0, sr = 0, sr2 = 0;
            long cn = 0, cr = 0;
            for (int y = ny0; y < ny1; y++)
                for (int x = nx0; x < nx1; x++) { double v = a[((long)y * W + x) * 3 + ch]; sn += v; sn2 += v * v; cn++; }
            for (int y = ry0; y < ry1; y++)
                for (int x = rx0; x < rx1; x++) { double v = a[((long)y * W + x) * 3 + ch]; sr += v; sr2 += v * v; cr++; }
            double mn = sn / cn, mr = sr / cr, sdn = sqrt(fmax(0, sn2 / cn - mn * mn)), sdr = sqrt(fmax(0, sr2 / cr - mr * mr));
            double k = sdr / (sdn + 1e-3);
            if (k < 0.3) k = 0.3;
            if (k > 4) k = 4;
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    int dt = y, db = H - 1 - y, dl = x, dr = W - 1 - x;
                    int dmin = dt < db ? dt : db;
                    if (dl < dmin) dmin = dl;
                    if (dr < dmin) dmin = dr;
                    int d = side == 0 ? dt : side == 1 ? db : side == 2 ? dl : dr;
                    if (dmin >= B - 1 || d != dmin) continue;
                    double *v = &a[((long)y * W + x) * 3 + ch];
                    *v = (*v - mn) * k + mr;
                }
        }
    }
    for (long i = 0; i < (long)W * H * 3; i++) rgb[i] = (uint8_t)(a[i] < 0 ? 0 : a[i] > 255 ? 255 : a[i]);
    free(a);
}

/* Pillow's Lanczos resize (its fixed point, the across pass first), one 8-bit channel set of RGB */
#define PREC 22
static double sinc(double x) { if (x == 0.0) return 1.0; x *= M_PI; return sin(x) / x; }
static double lanczos(double x) { return (x >= -3.0 && x < 3.0) ? sinc(x) * sinc(x / 3) : 0.0; }
static int coeffs(int in, int outn, int **bounds, int **kk)
{
    double scale = (double)in / outn, fs = scale < 1.0 ? 1.0 : scale, support = 3.0 * fs;
    int ksize = (int)ceil(support) * 2 + 1;
    double *k = malloc(sizeof(double) * (size_t)ksize);
    *bounds = malloc(sizeof(int) * 2 * (size_t)outn);
    *kk = malloc(sizeof(int) * (size_t)outn * ksize);
    if (!k || !*bounds || !*kk) { free(k); return 0; }
    for (int xx = 0; xx < outn; xx++) {
        double center = (xx + 0.5) * scale, ww = 0.0, ss = 1.0 / fs;
        int xmin = (int)(center - support + 0.5), xmax;
        if (xmin < 0) xmin = 0;
        xmax = (int)(center + support + 0.5);
        if (xmax > in) xmax = in;
        xmax -= xmin;
        int x;
        for (x = 0; x < xmax; x++) { double w = lanczos((x + xmin - center + 0.5) * ss); k[x] = w; ww += w; }
        for (x = 0; x < xmax; x++) if (ww != 0.0) k[x] /= ww;
        for (; x < ksize; x++) k[x] = 0;
        for (x = 0; x < ksize; x++) (*kk)[xx * ksize + x] = k[x] < 0 ? (int)(-0.5 + k[x] * (1 << PREC)) : (int)(0.5 + k[x] * (1 << PREC));
        (*bounds)[xx * 2] = xmin;
        (*bounds)[xx * 2 + 1] = xmax;
    }
    free(k);
    return ksize;
}
static uint8_t clip8(int v) { return v >= (1 << PREC << 8) ? 255 : v <= 0 ? 0 : (uint8_t)(v >> PREC); }
static uint8_t *resize(const uint8_t *in, int w, int h, int w2, int h2)
{
    uint8_t *tmp = NULL, *out = NULL;
    int *bh = NULL, *kh = NULL, *bv = NULL, *kv = NULL;
    int ksh = coeffs(w, w2, &bh, &kh), ksv = coeffs(h, h2, &bv, &kv);
    if (!ksh || !ksv) goto end;
    const uint8_t *src = in;
    if (w2 != w) {
        tmp = malloc((size_t)w2 * h * 3);
        if (!tmp) goto end;
        for (int y = 0; y < h; y++)
            for (int xx = 0; xx < w2; xx++)
                for (int ch = 0; ch < 3; ch++) {
                    int ss = 1 << (PREC - 1), xmin = bh[xx * 2], xmax = bh[xx * 2 + 1];
                    for (int x = 0; x < xmax; x++) ss += in[((long)y * w + x + xmin) * 3 + ch] * kh[xx * ksh + x];
                    tmp[((long)y * w2 + xx) * 3 + ch] = clip8(ss);
                }
        src = tmp;
    }
    out = malloc((size_t)w2 * h2 * 3);
    if (!out) goto end;
    if (h2 != h) {
        for (int yy = 0; yy < h2; yy++)
            for (int x = 0; x < w2; x++)
                for (int ch = 0; ch < 3; ch++) {
                    int ss = 1 << (PREC - 1), ymin = bv[yy * 2], ymax = bv[yy * 2 + 1];
                    for (int y = 0; y < ymax; y++) ss += src[((long)(y + ymin) * w2 + x) * 3 + ch] * kv[yy * ksv + y];
                    out[((long)yy * w2 + x) * 3 + ch] = clip8(ss);
                }
    } else memcpy(out, src, (size_t)w2 * h2 * 3);
end:
    free(tmp); free(bh); free(kh); free(bv); free(kv);
    return out;
}

/* the portrait scaled and placed on the card: A (rgb) and m (covered, less its outermost pixel) */
static int warp(const cc_img *src, const cc_card *c, double A[NN][3], uint8_t m[NN])
{
    int W = src->w, H = src->h, b = c->src == CC_PC95 ? BEVEL : 0;
    uint8_t *rgb = malloc((size_t)W * H * 3);
    if (!rgb) return 0;
    for (long i = 0; i < (long)W * H; i++) memcpy(rgb + i * 3, src->pal[src->px[i]], 3);
    if (c->src == CC_PC95) debevel(rgb, W, H);
    if (c->flip)
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W / 2; x++)
                for (int ch = 0; ch < 3; ch++) {
                    uint8_t t = rgb[((long)y * W + x) * 3 + ch];
                    rgb[((long)y * W + x) * 3 + ch] = rgb[((long)y * W + W - 1 - x) * 3 + ch];
                    rgb[((long)y * W + W - 1 - x) * 3 + ch] = t;
                }
    int Hi = H - 2 * b, Wi = W - 2 * b;
    double sy = (double)c->h / Hi, wi = pyround((double)c->h * Wi / Hi * c->a), sx = (wi < 1 ? 1 : wi) / Wi;
    int w2 = pyround(W * sx), h2 = pyround(H * sy);
    if (w2 < 1 || h2 < 1 || w2 > 4096 || h2 > 4096) { free(rgb); return 0; }
    uint8_t *R = resize(rgb, W, H, w2, h2);
    free(rgb);
    if (!R) return 0;
    int px = pyround(-c->ox - b * sx), py = pyround(-c->oy - b * sy);
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            int sxx = x - px, syy = y - py, in = sxx >= 0 && sxx < w2 && syy >= 0 && syy < h2;
            for (int ch = 0; ch < 3; ch++) A[y * N + x][ch] = in ? R[((long)syy * w2 + sxx) * 3 + ch] : 0;
            m[y * N + x] = (uint8_t)(x >= px + 1 && x < px + w2 - 1 && y >= py + 1 && y < py + h2 - 1);
        }
    free(R);
    return 1;
}

/* ---- the colour ----------------------------------------------------------------------------- */

typedef double img[NN];
typedef uint8_t msk[NN];

/* the mean of a over the (2k+1)^2 square, counting pixels by weight w (0 where none) */
static void box(const double *a, const double *w, int k, double *out)
{
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            double s = 0, n = 0;
            for (int dy = -k; dy <= k; dy++)
                for (int dx = -k; dx <= k; dx++) {
                    int yy = y + dy, xx = x + dx;
                    if (yy < 0 || yy >= N || xx < 0 || xx >= N) continue;
                    s += a[yy * N + xx] * w[yy * N + xx];
                    n += w[yy * N + xx];
                }
            out[y * N + x] = s / (n > 1e-6 ? n : 1e-6);
        }
}
static void boxm(const double *a, const uint8_t *m, int k, double *out)
{
    static img w;
    for (int i = 0; i < NN; i++) w[i] = m ? m[i] : 1.0;
    box(a, w, k, out);
}

/* grow (dil) or shrink a mask by k, the edge pixels repeated past the picture */
static void morph(const uint8_t *m, int k, int dil, uint8_t *out)
{
    msk t;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            int v = dil ? 0 : 1;
            for (int dy = -k; dy <= k; dy++)
                for (int dx = -k; dx <= k; dx++) {
                    int yy = y + dy, xx = x + dx;
                    yy = yy < 0 ? 0 : yy >= N ? N - 1 : yy;
                    xx = xx < 0 ? 0 : xx >= N ? N - 1 : xx;
                    if (dil) v |= m[yy * N + xx];
                    else v &= m[yy * N + xx];
                }
            t[y * N + x] = (uint8_t)v;
        }
    memcpy(out, t, NN);
}

static int rows_of(const uint8_t *m, int *y0, int *y1)
{
    *y0 = -1;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++)
            if (m[y * N + x]) { if (*y0 < 0) *y0 = y; *y1 = y; }
    return *y0 >= 0;
}
static int cols_of(const uint8_t *m, int *x0, int *x1)
{
    *x0 = -1;
    for (int x = 0; x < N; x++)
        for (int y = 0; y < N; y++)
            if (m[y * N + x]) { if (*x0 < 0) *x0 = x; *x1 = x; break; }
    return *x0 >= 0;
}

static int dcmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
/* numpy's quantile (linear) of v[0..n-1], sorted in place */
static double quantile(double *v, int n, double q)
{
    qsort(v, (size_t)n, sizeof *v, dcmp);
    double pos = q * (n - 1);
    int lo = (int)floor(pos), hi = lo + 1 < n ? lo + 1 : lo;
    double t = pos - lo;
    return v[lo] + (v[hi] - v[lo]) * t;
}
static double median(double *v, int n) { return n ? quantile(v, n, 0.5) : 0; }

/* how y follows x over sel: the means of 8 slices between x's 2% and 98% points, y never falling */
static int curve(const double *x, const double *y, const uint8_t *sel, double *cx, double *cy)
{
    static double v[NN];
    int n = 0, k = 0;
    for (int i = 0; i < NN; i++) if (sel[i]) v[n++] = x[i];
    if (n < 2) return 0;
    double q[9];
    for (int i = 0; i < 9; i++) q[i] = quantile(v, n, 0.02 + 0.96 * i / 8);
    for (int i = 0; i < 8; i++) {
        double sx = 0, sy = 0;
        int c = 0;
        for (int j = 0; j < NN; j++)
            if (sel[j] && x[j] >= q[i] && x[j] <= q[i + 1]) { sx += x[j]; sy += y[j]; c++; }
        if (c > 4) { cx[k] = sx / c; cy[k] = sy / c; if (k && cy[k] < cy[k - 1]) cy[k] = cy[k - 1]; k++; }
    }
    return k;
}
/* numpy's interp */
static double interp(double x, const double *xp, const double *fp, int n)
{
    if (n <= 0) return x;
    if (x <= xp[0]) return fp[0];
    if (x >= xp[n - 1]) return fp[n - 1];
    int j = 0;
    while (j + 1 < n && xp[j + 1] <= x) j++;
    if (j + 1 >= n) return fp[n - 1];
    double d = xp[j + 1] - xp[j];
    return d == 0 ? fp[j] : fp[j] + (x - xp[j]) * (fp[j + 1] - fp[j]) / d;
}

/* the portrait's outside (and its untrusted pixels): colour from its outermost 4 columns, rows close
 * by, ok pixels only, weighted toward like greys */
static void fill_edge(const double *c, const uint8_t *m, const uint8_t *ok, const double *g, double *out)
{
    enum { DEPTH = 4, VR = 3 };
    const double sig = 28.0;
    int x0, x1, y0, y1;
    memcpy(out, c, sizeof(img));
    if (!cols_of(m, &x0, &x1) || !rows_of(m, &y0, &y1)) return;
    static double v[NN];
    double side[2], all;
    int n = 0;
    for (int i = 0; i < NN; i++) if (ok[i]) v[n++] = c[i];
    all = median(v, n);
    for (int s = 0; s < 2; s++) {
        int a = s ? x1 - DEPTH + 1 : x0, b = s ? x1 + 1 : x0 + DEPTH, k = 0;
        for (int y = 0; y < N; y++)
            for (int x = a; x < b; x++) if (x >= 0 && x < N && ok[y * N + x]) v[k++] = c[y * N + x];
        side[s] = k > 3 ? median(v, k) : all;
    }
    for (int y = 0; y < N; y++) {
        int yy = y < y0 ? y0 : y > y1 ? y1 : y;
        for (int x = 0; x < N; x++) {
            if (m[y * N + x]) continue;
            int left = x < (x0 + x1) / 2.0;
            int a = left ? x0 : x1 - DEPTH + 1, b = left ? x0 + DEPTH : x1 + 1;
            int ya = yy - VR > y0 ? yy - VR : y0, yb = (yy + VR < y1 ? yy + VR : y1) + 1, k = 0;
            double s = 0, w = 0;
            for (int j = ya; j < yb; j++)
                for (int i = a; i < b; i++) {
                    if (i < 0 || i >= N || !ok[j * N + i]) continue;
                    double d = (g[j * N + i] - g[y * N + x]) / sig, wt = exp(-d * d) + 0.02;
                    s += c[j * N + i] * wt;
                    w += wt;
                    k++;
                }
            out[y * N + x] = k >= 3 ? s / w : side[left ? 0 : 1];
        }
    }
}

/* smooth c where um is set, mixing only pixels of like grey; k+1 rows up and down, k across */
static void joint(double *c, const double *g, const uint8_t *um, int k, int it)
{
    const double sig = 30.0;
    static img s, n;
    for (int t = 0; t < it; t++) {
        for (int i = 0; i < NN; i++) s[i] = n[i] = 0;
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++)
                for (int dy = -k - 1; dy <= k + 1; dy++)
                    for (int dx = -k; dx <= k; dx++) {
                        int yy = y + dy, xx = x + dx;
                        if (yy < 0 || yy >= N || xx < 0 || xx >= N) continue;
                        double d = (g[yy * N + xx] - g[y * N + x]) / sig, w = exp(-d * d);
                        s[y * N + x] += c[yy * N + xx] * w;
                        n[y * N + x] += w;
                    }
        for (int i = 0; i < NN; i++) if (um[i]) c[i] = s[i] / n[i];
    }
}

/* how far each channel can move from y along d before leaving 0-255 (1 = all of it) */
static double fitk(double y, double cb, double cr, double d[3])
{
    d[0] = 1.403 * cr;
    d[2] = 1.773 * cb;
    d[1] = (-.299 * d[0] - .114 * d[2]) / .587;
    double k = INFINITY;
    for (int i = 0; i < 3; i++) {
        double t = d[i] > 0 ? (255 - y) / d[i] : d[i] < 0 ? (0 - y) / d[i] : INFINITY;
        if (t < k) k = t;
    }
    return k < 0 ? 0 : k > 1 ? 1 : k;
}

int cc_colour(const double g[NN], const cc_img *src, const cc_card *c, uint8_t rgb[NN * 3])
{
    static double A[NN][3];
    static msk m, sk, bad, halo, ok, m2, zone, below, nk, low, um;
    static img ys, cb, cr, ysb, gb, cbs, crs, r, cbr, crr, c_b, c_r, t0, t1, edge, t, yf, Y, Y0;
    static double cx[8], cy[8];
    if (!warp(src, c, A, m)) return 0;
    int pc95 = c->src == CC_PC95;
    if (pc95) morph(m, 2, 0, m);                                         /* (bevel leftovers made lines) */
    for (int i = 0; i < NN; i++) {
        ys[i] = A[i][0] * .299 + A[i][1] * .587 + A[i][2] * .114;
        cb[i] = (A[i][2] - ys[i]) * 0.564;
        cr[i] = (A[i][0] - ys[i]) * 0.713;
    }
    boxm(ys, m, 2, ysb);
    boxm(g, NULL, 2, gb);
    int y0 = 0, y1 = 0;
    if (!rows_of(m, &y0, &y1)) return 0;
    /* skin: warm, not dark, above the jersey (the photo's bottom fifth) */
    for (int i = 0; i < NN; i++) sk[i] = (uint8_t)(m[i] && cr[i] > 7 && cb[i] < 2 && cr[i] > -0.9 * cb[i] && ys[i] > 45 && i / N < (int)(y0 + (y1 - y0) * 0.80));
    /* untrusted: the two pictures disagree on light (not lined up there) */
    int nc = curve(ysb, gb, m, cx, cy);
    for (int i = 0; i < NN; i++) bad[i] = (uint8_t)(m[i] && fabs(gb[i] - interp(ysb[i], cx, cy, nc)) > 42);
    {
        msk e, d, out;
        morph(bad, 1, 0, e);
        morph(e, 1, 1, d);
        for (int i = 0; i < NN; i++) out[i] = (uint8_t)!m[i];
        morph(out, 2, 1, out);
        for (int i = 0; i < NN; i++) bad[i] = (uint8_t)(d[i] || (bad[i] && out[i]));
    }
    morph(sk, 2, 1, halo);
    for (int i = 0; i < NN; i++) { halo[i] &= m[i]; ok[i] = (uint8_t)(m[i] && !halo[i] && !bad[i]); m2[i] = (uint8_t)(m[i] && !bad[i]); }
    if (pc95) {                                                          /* blur skin and the rest apart */
        msk ns;
        for (int i = 0; i < NN; i++) ns[i] = (uint8_t)(m[i] && !sk[i]);
        boxm(cb, sk, 1, t0); boxm(cb, ns, 1, t1);
        for (int i = 0; i < NN; i++) cbs[i] = sk[i] ? t0[i] : ns[i] ? t1[i] : cb[i];
        boxm(cr, sk, 1, t0); boxm(cr, ns, 1, t1);
        for (int i = 0; i < NN; i++) crs[i] = sk[i] ? t0[i] : ns[i] ? t1[i] : cr[i];
    } else {
        memcpy(cbs, cb, sizeof cbs);
        memcpy(crs, cr, sizeof crs);
    }
    for (int i = 0; i < NN; i++) {
        double v = (gb[i] + 32) / (ysb[i] + 32);
        v = v < 0.6 ? 0.6 : v > 1.3 ? 1.3 : v;
        r[i] = sk[i] ? v : (v < 1.0 ? v : 1.0);                          /* (only skin may be strengthened) */
        cbr[i] = cbs[i] * r[i];
        crr[i] = crs[i] * r[i];
        t0[i] = m2[i] ? cbr[i] : 0;
        t1[i] = m2[i] ? crr[i] : 0;
    }
    fill_edge(t0, m2, ok, gb, c_b);
    fill_edge(t1, m2, ok, gb, c_r);
    /* dark shoulders below the face: the colour of the portrait's ok pixels of like grey */
    for (int i = 0; i < NN; i++) zone[i] = (uint8_t)(!m[i] && i / N >= (int)(y0 + (y1 - y0) * 0.78) && gb[i] < 90);
    for (int pass = 0; pass < 2; pass++) {
        double *cc = pass ? c_r : c_b;
        int n = 0;
        for (int i = 0; i < NN; i++) n += ok[i];
        if (n < 20) continue;
        for (int i = 0; i < NN; i++) t0[i] = cc[i];
        for (int i = 0; i < NN; i++) {
            if (!zone[i]) continue;
            double s = 0, w = 0;
            for (int j = 0; j < NN; j++) {
                if (!ok[j]) continue;
                double d = (gb[j] - gb[i]) / 18.0, wt = exp(-d * d);
                s += t0[j] * wt;
                w += wt;
            }
            if (w > 1) cc[i] = s / (w + 1e-6);
        }
    }
    /* below the portrait's last row: its colour goes on down while the grey stays alike, fading */
    {
        int a0, a1, b0, b1;
        if (rows_of(m2, &a0, &a1) && cols_of(m2, &b0, &b1))
            for (int pass = 0; pass < 2; pass++) {
                const double *src_c = pass ? crr : cbr;
                double *cc = pass ? c_r : c_b, *o = pass ? t1 : t0;
                memcpy(o, cc, sizeof(img));
                for (int x = 0; x < N; x++) if (m2[a1 * N + x]) o[a1 * N + x] = src_c[a1 * N + x];
                double keep = 1.0;
                for (int y = a1 + 1; y < N; y++) {
                    keep *= 0.93;
                    for (int x = b0; x <= b1; x++) {
                        double s = 0, w = 0, wmax = 0;
                        for (int xx = (x - 1 > b0 ? x - 1 : b0); xx <= (x + 1 < b1 ? x + 1 : b1); xx++) {
                            double d = (gb[(y - 1) * N + xx] - gb[y * N + x]) / 28.0, wt = exp(-d * d);
                            s += o[(y - 1) * N + xx] * wt;
                            w += wt;
                            if (wt > wmax) wmax = wt;
                        }
                        double tt = (wmax < 1.0 ? wmax : 1.0) * keep;
                        o[y * N + x] = s / (w + 1e-6) * tt + cc[y * N + x] * (1 - tt);
                    }
                }
            }
        for (int i = 0; i < NN; i++) {
            below[i] = (uint8_t)(i / N > y1 && !zone[i]);
            if (below[i] && rows_of(m2, &a0, &a1)) { c_b[i] = t0[i]; c_r[i] = t1[i]; }
        }
    }
    /* the neck: under the lower face, skin-like grey joined to the portrait's bottom, the lower face's colour */
    {
        int s0, s1, l0 = 0, l1 = 0;
        if (rows_of(sk, &s0, &s1) && s1 - s0 + 1 >= 0) {
            int nrows = 0;
            for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) if (sk[y * N + x]) { nrows++; break; }
            for (int i = 0; i < NN; i++) low[i] = (uint8_t)(sk[i] && i / N >= s0 + (int)((s1 - s0) * 0.6));
            if (nrows >= 6 && cols_of(low, &l0, &l1) && l1 - l0 + 1 >= 4) {
                int lc = 0;
                for (int x = 0; x < N; x++) for (int y = 0; y < N; y++) if (low[y * N + x]) { lc++; break; }
                static double v[NN];
                int n = 0;
                for (int i = 0; i < NN; i++) if (low[i]) v[n++] = gb[i];
                double lo = quantile(v, n, 0.10) - 45, hi = quantile(v, n, 0.95) + 15;
                msk cand;
                for (int i = 0; i < NN; i++) {
                    int y = i / N, x = i % N;
                    cand[i] = (uint8_t)(gb[i] >= lo && gb[i] <= hi && y >= y1 - 2 && x >= l0 + 1 && x < l1);
                }
                if (lc < 4) memset(cand, 0, sizeof cand);
                memset(nk, 0, sizeof nk);
                static int stack[NN * 4];
                int sp = 0;
                for (int y = y1 - 2; y <= y1; y++) for (int x = 0; x < N; x++) if (y >= 0 && cand[y * N + x]) stack[sp++] = y * N + x;
                while (sp) {
                    int i = stack[--sp];
                    if (nk[i]) continue;
                    nk[i] = 1;
                    int y = i / N, x = i % N;
                    const int dy[4] = { 1, -1, 0, 0 }, dx[4] = { 0, 0, 1, -1 };
                    for (int d = 0; d < 4; d++) {
                        int yy = y + dy[d], xx = x + dx[d];
                        if (yy >= 0 && yy < N && xx >= 0 && xx < N && cand[yy * N + xx] && !nk[yy * N + xx] && sp < NN * 4) stack[sp++] = yy * N + xx;
                    }
                }
                int cnt = 0;
                for (int i = 0; i < NN; i++) { nk[i] = (uint8_t)(nk[i] && !m[i]); cnt += nk[i]; }
                if (cnt > 3) {
                    n = 0;
                    for (int i = 0; i < NN; i++) if (low[i]) v[n++] = cbr[i];
                    double mb = median(v, n) * 0.85;
                    n = 0;
                    for (int i = 0; i < NN; i++) if (low[i]) v[n++] = crr[i];
                    double mr = median(v, n) * 0.85;
                    for (int i = 0; i < NN; i++) if (nk[i]) { c_b[i] = mb; c_r[i] = mr; }
                    joint(c_b, gb, nk, 1, 1);
                    joint(c_r, gb, nk, 1, 1);
                }
            }
        }
    }
    for (int i = 0; i < NN; i++) um[i] = (uint8_t)(!m2[i] && !below[i]);
    joint(c_b, gb, um, 2, 3);
    joint(c_r, gb, um, 2, 3);
    /* the seam: softened where the inside is not skin */
    boxm(c_b, NULL, 1, t0);
    boxm(c_r, NULL, 1, t1);
    {
        static img mf;
        for (int i = 0; i < NN; i++) mf[i] = m2[i];
        boxm(mf, NULL, 1, edge);
    }
    for (int i = 0; i < NN; i++) {
        double v = (edge[i] - 0.6) / 0.4;
        t[i] = sk[i] && m2[i] ? 1 : v < 0 ? 0 : v > 1 ? 1 : v;
        c_b[i] = c_b[i] * t[i] + t0[i] * (1 - t[i]);
        c_r[i] = c_r[i] * t[i] + t1[i] * (1 - t[i]);
    }
    /* light: inside the portrait its own, outside the grey-to-light curve measured inside */
    nc = curve(gb, ysb, m, cx, cy);
    for (int i = 0; i < NN; i++) yf[i] = m2[i] ? ysb[i] : interp(gb[i], cx, cy, nc);
    boxm(yf, NULL, 1, t0);
    for (int i = 0; i < NN; i++) {
        if (t[i] < 1) yf[i] = t0[i];
        double v = g[i] * (1 - 0.45) + (yf[i] + (g[i] - gb[i]) * 0.6) * 0.45;
        Y[i] = Y0[i] = v < 0 ? 0 : v > 255 ? 255 : v;
    }
    /* grain too light or dark to hold colour: moved up to 45 so it keeps 70% */
    for (int it = 0; it < 9; it++)
        for (int i = 0; i < NN; i++) {
            double d[3], k = fitk(Y[i], c_b[i], c_r[i], d);
            if (k < 0.7 && hypot(c_b[i], c_r[i]) > 6) Y[i] -= 5 * (Y[i] > 128 ? 1 : Y[i] < 128 ? -1 : 0);
        }
    for (int i = 0; i < NN; i++) {
        double v = Y[i];
        if (v < Y0[i] - 45) v = Y0[i] - 45;
        if (v > Y0[i] + 45) v = Y0[i] + 45;
        v = v < 0 ? 0 : v > 255 ? 255 : v;
        double d[3], k = fitk(v, c_b[i], c_r[i], d);
        for (int ch = 0; ch < 3; ch++) {
            double o = v + d[ch] * k;
            o = o < 0 ? 0 : o > 255 ? 255 : o;
            rgb[i * 3 + ch] = (uint8_t)lround(o);
        }
    }
    return 1;
}
