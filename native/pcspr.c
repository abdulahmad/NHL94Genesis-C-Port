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

/* the cartridge's frame number as the PC's (-1: none) */
static int pc_frame(int fr)
{
    if (fr < 1 || fr > 657) return -1;
    if (fr <= 554) return fr - 1;
    if (fr <= 562) return -1;                          /* (the cartridge's extra goalie ready frames) */
    return fr - 9;
}

static int in_game(void) { return g.demoflag && (g.disflags & HI(dfok)); }


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
    if (fo) {
        R.x = (fx > 0 ? fx + 15 : fx - 15) * 256;
        R.y = fy * 256;
        R.vx = R.vy = 0;
        R.dir = fx > 0 ? 6 : 2;
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
    R.sy = y;
    R.valid = 1;
    if (getenv("PCREF_LOG") && !(g.Vcount & 63)) fprintf(stderr, "ref: rink %d,%d screen %d,%d dir %d t %d fo %d puck %d,%d view %d,%d\n", R.x / 256, R.y / 256, R.sx, R.sy, R.dir, R.t, fo, px, py, (int16_t)g.Hpos, (int16_t)g.Vpos);
}

/* the frame the referee shows */
static int ref_frame(void)
{
    const struct SortObj *pk = sortobj(puckscnum);
    int asn = pk->asslist[pk->assnum & 7];
    int d = R.dir & 7;
    if (asn == pfaceoff || asn == pfaceoff2) return (d >= 2 && d <= 5) ? 711 : 703;       /* (SPAreffowait) */
    if (R.drop > 0) {                                                                    /* (SPArefdrop: 8 ticks each) */
        int k = (24 - R.drop) / 8;
        if (d >= 2 && d <= 5) return k == 0 ? 845 : k == 1 ? 846 : 711;
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
    if ((int16_t)g.palcount >= 0) return 0;               /* (a fade: the cartridge's sprites fade with the screen) */
    int pf = pc_frame(o->frame & 0x7FF);
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
    static mdrt_vdp sc;
    static uint16_t base[320 * 240], back[320 * 240], front[320 * 240];
    unsigned sat = (unsigned)(mdrt_vdp_reg(vdp, 5) & (mdrt_vdp_width(vdp) == 320 ? 0x7E : 0x7F)) << 9;
    unsigned tile = 0x7E0;
    while (tile > 0x40 && tile * 32 < sat + 0x280 && sat < tile * 32 + 0x200) tile -= 0x10;     /* (the solid tiles: not over the sprite table) */
    int tail = 0;                                                   /* (the last entry of the sprite table's chain; the entries after it are free) */
    for (int k = 0; k < 80; k++) { int l = vdp->sat[tail * 8 + 3] & 0x7F; if (!l) break; tail = l; }
    int cnt = nx * ny, fb = tail + 1;                               /* the free entries fb ... : the pieces, then the moved first entry */
    if (fb + cnt + 1 > 80) return;
    if (getenv("PCREF_LOG")) fprintf(stderr, "refdraw: tail %d\n", tail);
    for (int variant = 0; variant < 3; variant++) {                /* 0: the copy without it, 1: last, 2: first */
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
    uint8_t pal[256][3];
    memcpy(pal, P.pal, sizeof pal);
    if (getenv("PCREF_LOG")) { int nb = 0, nf = 0; for (int i = 0; i < 320 * 240; i++) { nb += back[i] != base[i]; nf += front[i] != base[i]; } fprintf(stderr, "refdraw: frame %d at %d,%d size %dx%d back %d front %d\n", fr, x0, y0, s->w, s->h, nb, nf); }
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            int v = s->px[y * s->w + x];
            if (v == 255) continue;
            int px_ = x0 + x, py = y0 + y;
            if (px_ < 0 || px_ >= w || py < 0 || py >= h) continue;
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
            if (!ok) continue;
            int r = v < 0x80 ? rec[192 + v] : rec[320 + v - 0x80];
            pic[(long)py * pitch + px_] = 0xFF000000u | (uint32_t)pal[r][0] << 16 | (uint32_t)pal[r][1] << 8 | pal[r][2];
        }
}

/* Over the picture the game drew (px: its colours, pic: the same as RGB). The cartridge's own sprites of the objects drawn here are
 * still in it: the picture is drawn again from a copy of the VDP with those sprite table entries hidden, and where the two differ
 * the cartridge's sprite was visible (not under a window, the face off box or another sprite). The PC's sprite is drawn only there
 * and a few pixels around (the two shapes differ a little); everything else of the cartridge's sprites is given back its background. */
void pcspr_present(const mdrt_vdp *vdp, const uint16_t *px, int pitch_px, uint32_t *pic, int w, int h, int pitch)
{
    if ((!SDL_AtomicGet(&P.on) && !SDL_AtomicGet(&R.on)) || !SDL_AtomicGet(&P.found) || !in_game() || (int16_t)g.palcount >= 0) return;
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
    static mdrt_vdp scratch;
    if (!SDL_AtomicGet(&P.on)) n = 0;
    if (n <= 0) { ref_draw(vdp, pic, w, h, pitch, list, 0); return; }
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
                if (px_ < 0 || px_ >= w || py < 0 || py >= h || solid[py * 320 + px_] == clean[py * 320 + px_]) continue;
                pic[(long)py * pitch + px_] = 0xFF000000u | (uint32_t)pal[r][0] << 16 | (uint32_t)pal[r][1] << 8 | pal[r][2];
            }
    }
    ref_draw(vdp, pic, w, h, pitch, list, n);
}
