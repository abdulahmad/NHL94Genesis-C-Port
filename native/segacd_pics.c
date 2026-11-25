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
    if (m + 4 + (long)w * h * 2 > size) return 0;
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
