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
#include "rosters.h"
#include "native.h"
#include "pcspr.h"
#include "cardcolor_xfer.h"
#include "overbar/cdimage.h"

#define NFRAMES 1150
#define NTEAMS 28

typedef struct { int w, h, xo, yo; uint8_t *px; } spr;

static struct {
    SDL_atomic_t on, found, searched;
    spr f[NFRAMES];
    uint8_t pal[256][3];                    /* RINKPAL, 8 bits */
    uint8_t team[2][NTEAMS][448];           /* HOMEPALS, AWAYPALS */
    int have_team[2];
    SDL_SpinLock lock;
    struct { int16_t pc, x, y, flip, away, sc; } show[16], pub[16];   /* what the game wrote, what the picture draws */
    int nshow, npub;
} P;

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

static int hook(int ev, int sc, int16_t x, int16_t y, const struct SpriteHead *o)
{
    if (ev == SPR_BEGIN) { P.nshow = 0; return 0; }
    if (ev == SPR_END) {
        SDL_AtomicLock(&P.lock);
        memcpy(P.pub, P.show, sizeof P.show);
        P.npub = P.nshow;
        SDL_AtomicUnlock(&P.lock);
        return 0;
    }
    if (!SDL_AtomicGet(&P.on) || !SDL_AtomicGet(&P.found) || !g.demoflag || sc > 13 || P.nshow >= 16) return 0;
    int pf = pc_frame(o->frame & 0x7FF);
    if (pf < 0 || pf >= NFRAMES || !P.f[pf].px) return 0;
    P.show[P.nshow].pc = (int16_t)pf;
    P.show[P.nshow].x = x;
    P.show[P.nshow].y = y;
    P.show[P.nshow].flip = (o->attribute & HI(3)) != 0;
    P.show[P.nshow].away = (sc >= 6 && sc != 12) ? 1 : 0;
    P.show[P.nshow].sc = (int16_t)sc;
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

void pcspr_present(uint32_t *pic, int w, int h, int pitch)
{
    if (!SDL_AtomicGet(&P.on) || !SDL_AtomicGet(&P.found) || !in_game()) return;
    typeof(P.pub) list;
    int n;
    SDL_AtomicLock(&P.lock);
    memcpy(list, P.pub, sizeof list);
    n = P.npub;
    SDL_AtomicUnlock(&P.lock);
    for (int i = 0; i < n; i++) {
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
                int px = x0 + x, py = y0 + y;
                if (px < 0 || px >= w || py < 0 || py >= h) continue;
                pic[(long)py * pitch + px] = 0xFF000000u | (uint32_t)pal[r][0] << 16 | (uint32_t)pal[r][1] << 8 | pal[r][2];
            }
    }
}
