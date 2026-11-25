/* The hat trick hats of NHL 94 SNES beside the cartridge game (the Game page's "Hat trick hats").
 *
 * On the SNES, a home skater's third goal sends hats flying onto the ice from both sides until the next
 * face off (StartHatTrick / UpdateHatTrick in NHL94SNES): ten slots, each thrown at random on its own arc
 * (from either edge, a height that falls with gravity, a landing place), spinning through 3 frames of one
 * of 3 kinds of hat, then sliding to a stop, with a shadow under it. The hats are 8 x 8 tiles in the
 * SNES ROM's sprite frames, in the home team's uniform colours (HomeUniformPalettes). This is that,
 * drawn over the cartridge's picture. The ROM is the player's own, found beside the program, never
 * shipped. The game is not touched: states, replays and online play are the same with it on or off (the
 * hats have a random generator of their own, and the SNES's 10 frame wait before the next face off is not
 * added).
 *
 * ROM places (LoROM, 1 MB): the hat frames are the last of SpriteFrames' bank $8E, 61 bytes each from the
 * first hat's tile at $8E8000 + 22098; HomeUniformPalettes is at $96CD4E, 16 colours a team in team order
 * (the cartridge's team order is the same: Anaheim ... Winnipeg, then the All-Stars). */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include "game.h"
#include "hats.h"

#define HAT_TILE  (0x70000 + 22098)         /* hat 0's tile (file offset, no header) */
#define HAT_STRIDE 61                       /* a one tile frame: 29 bytes of header and piece, 32 of tile */
#define HAT_FRAMES 10                       /* nine hats, then the shadow */
#define PAL_AT    0xB4D4E                   /* HomeUniformPalettes */
#define NTEAMS    28
#define PATHMAX 1024
#define LISTMAX 2048
#define WALK_DEPTH 3

static struct {
    SDL_atomic_t on, found, searched;
    uint8_t tile[HAT_FRAMES][64];           /* 8 x 8 colour indices */
    uint16_t pal[NTEAMS][16];               /* BGR555 */
    /* the hats */
    int active;
    unsigned seed;
    struct { int step; int16_t x, y, z, vx, vy, vz; int kind, timer; } h[10];
} H;

/* ---- the ROM ---- */
static int parse(const uint8_t *r, long size)
{
    long off = size == 0x100200 ? 0x200 : size == 0x100000 ? 0 : -1;       /* (a copier's 512 byte header) */
    if (off < 0) return 0;
    r += off;
    static const uint8_t head[16] = { 0x01, 0x00, 0xFE, 0xFF, 0x03, 0x00, 0xFC, 0xFF, 0xFF, 0xFF, 0x20, 0x00, 0x00, 0x00, 0x1D, 0x00 };
    if (memcmp(r + HAT_TILE - 29 + 2, head, 16)) return 0;                  /* NHL 94 SNES's first hat frame */
    for (int k = 0; k < HAT_FRAMES; k++) {
        const uint8_t *t = r + HAT_TILE + HAT_STRIDE * k;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int b = 7 - x;
                H.tile[k][y * 8 + x] = (uint8_t)(((t[2 * y] >> b) & 1) | (((t[2 * y + 1] >> b) & 1) << 1) |
                                                 (((t[16 + 2 * y] >> b) & 1) << 2) | (((t[16 + 2 * y + 1] >> b) & 1) << 3));
            }
    }
    for (int t = 0; t < NTEAMS; t++)
        for (int c = 0; c < 16; c++) H.pal[t][c] = (uint16_t)(r[PAL_AT + t * 32 + c * 2] | r[PAL_AT + t * 32 + c * 2 + 1] << 8);
    return 1;
}

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return !*a && !*b;
}
static int ends(const char *s, const char *e) { size_t a = strlen(s), b = strlen(e); return a >= b && ieq(s + a - b, e); }
static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static long file_len(const char *p) { struct stat st; return stat(p, &st) == 0 ? (long)st.st_size : -1; }

static int walk(const char *dir, int depth, char (*list)[PATHMAX], int n)
{
    /* a folder's own files first, then its folders (never the build's obj): a big folder must not use up the list before the disc image */
    for (int pass = 0; pass < 2; pass++) {
        DIR *d = opendir(dir);
        struct dirent *e;
        if (!d) return n;
        while ((e = readdir(d)) && n < LISTMAX) {
            char p[PATHMAX];
            if (e->d_name[0] == '.') continue;
            snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
            if (is_dir(p)) {
                if (pass == 1 && depth > 0 && strcmp(e->d_name, "obj")) n = walk(p, depth - 1, list, n);
            } else if (pass == 0) snprintf(list[n++], PATHMAX, "%s", p);
        }
        closedir(d);
    }
    return n;
}

static int read_rom(const char *path)
{
    long n = file_len(path);
    if (n != 0x100000 && n != 0x100200) return 0;
    FILE *f = fopen(path, "rb");
    uint8_t *b = f ? malloc((size_t)n) : NULL;
    int ok = b && fread(b, 1, (size_t)n, f) == (size_t)n && parse(b, n);
    if (f) fclose(f);
    free(b);
    return ok;
}

static int find_in(const char *dir)
{
    static char list[LISTMAX][PATHMAX];
    int n = walk(dir, WALK_DEPTH, list, 0);
    for (int i = 0; i < n; i++)
        if ((ends(list[i], ".sfc") || ends(list[i], ".smc") || ends(list[i], ".bin") || ends(list[i], ".rom")) && read_rom(list[i])) {
            fprintf(stderr, "Hat trick hats: %s\n", list[i]);
            return 1;
        }
    return 0;
}

static int finder(void *dir)
{
    if (SDL_AtomicGet(&H.searched)) { SDL_free(dir); return 0; }
    int ok = find_in((const char *)dir) || find_in(".");
    if (!ok) fprintf(stderr, "Hat trick hats: no NHL 94 SNES ROM found beside the program\n");
    SDL_MemoryBarrierRelease();
    SDL_AtomicSet(&H.found, ok);
    SDL_AtomicSet(&H.searched, 1);
    SDL_free(dir);
    return 0;
}

int hats_load_file(const char *path)
{
    int ok = read_rom(path);
    SDL_AtomicSet(&H.found, ok);
    SDL_AtomicSet(&H.searched, 1);
    return ok;
}

/* ---- UpdateHatTrick ---- */
static unsigned rnd(void)
{
    H.seed ^= H.seed << 13; H.seed ^= H.seed >> 17; H.seed ^= H.seed << 5;
    return H.seed >> 8;
}
static const int frame_time[10] = { 4, 8, 9, 15, 12, 13, 10, 7, 11, 14 };

static void throw_hat(int i)
{
    int d0 = (int)(rnd() & 0x3F);
    d0 = ((d0 + (d0 >> 1) + (d0 & 1)) + 0x50) * 16;                  /* the landing x */
    int d1 = (int)((rnd() & 0x3F) + 0x60) * 16;                      /* the landing y */
    int16_t y0 = (int16_t)((rnd() & 0xFF) * 16);
    int d2 = (int)(rnd() & 0x3F) + 0x78;                            /* the flight, in frames */
    H.h[i].y = y0;
    H.h[i].vy = (int16_t)((d1 - y0) / d2);
    int x0 = (d0 & 0x100) ? 0x1100 : -0x100;
    H.h[i].x = (int16_t)x0;
    H.h[i].vx = (int16_t)((d0 - x0) / d2);
    int q = d2 >> 2;
    H.h[i].z = (int16_t)(q * q * 3);
    int k = (int)((rnd() >> 2) & 3);
    if (k) k--;
    H.h[i].kind = k * 3;
    H.h[i].vz = 0;
    H.h[i].step = 1;
    H.h[i].timer = frame_time[i];
}

static void move(int i)
{
    if (!(H.h[i].vx | H.h[i].vy)) return;
    H.h[i].x = (int16_t)(H.h[i].x + H.h[i].vx);
    H.h[i].y = (int16_t)(H.h[i].y + H.h[i].vy);
    if (H.h[i].z == 0) {                                             /* on the ice: slides, slowing */
        H.h[i].vz = 0;
        H.h[i].timer = 1000;
        if (H.h[i].vx < 0x10 && H.h[i].vx >= -0x10) { H.h[i].vx = H.h[i].vy = 0; return; }
        H.h[i].vx = (int16_t)(H.h[i].vx - (H.h[i].vx >> 4));
        H.h[i].vy = (int16_t)(H.h[i].vy - (H.h[i].vy >> 4));
        return;
    }
    int z = H.h[i].z - (H.h[i].vz >> 2);
    if (z < 0) { H.h[i].z = 0; return; }
    H.h[i].z = (int16_t)z;
    H.h[i].vz = (int16_t)(H.h[i].vz + 6);
}

static void frame(int steps)
{
    if (!H.active || (g.sflags & HI(sfhor)) || (g.sflags2 & HI(sf2replay))) return;
    for (int i = 0; i < 10; i++) {
        if (!H.h[i].step) {
            if (rnd() & 0x7C0) continue;                             /* 1 in 32 a frame */
            throw_hat(i);
            continue;
        }
        H.h[i].timer -= steps;
        if (H.h[i].timer < 0) {
            H.h[i].timer += frame_time[i];
            if (++H.h[i].step == 4) H.h[i].step = 1;
        }
        for (int s = 0; s < steps; s++) move(i);
    }
}

static void hook(int ev, int arg)
{
    if (!SDL_AtomicGet(&H.on) || !SDL_AtomicGet(&H.found)) return;
    if (ev == HAT_START) {
        memset(H.h, 0, sizeof H.h);
        if (!H.seed) H.seed = 2463534242u;
        H.active = 1;
    } else if (ev == HAT_END) H.active = 0;
    else if (ev == HAT_FRAME) {
        static int demo = -1;
        if (demo < 0) demo = SDL_getenv("HATS_DEMO") != NULL;   /* (testing: the hats from the first frame, as after a home hat trick) */
        if (demo && !H.active && g.demoflag) hook(HAT_START, 0);
        frame(arg > 0 ? arg : 1);
    }
}

void hats_start(void)
{
    static int started;
    if (started) return;
    started = 1;
    hat_hook = hook;
    char *base = SDL_GetBasePath();
    SDL_Thread *t = SDL_CreateThread(finder, "hats", base ? SDL_strdup(base) : SDL_strdup("."));
    if (base) SDL_free(base);
    if (t) SDL_DetachThread(t);
}

void hats_wait(void) { while (!SDL_AtomicGet(&H.searched)) SDL_Delay(5); }
int hats_searched(void) { return SDL_AtomicGet(&H.searched); }
int hats_found(void) { return SDL_AtomicGet(&H.searched) && SDL_AtomicGet(&H.found); }
void hats_set(int on) { SDL_AtomicSet(&H.on, on ? 1 : 0); if (!on) H.active = 0; }

/* ---- drawing ---- */
static void tile(uint32_t *pic, int w, int h, int pitch, int k, int x0, int y0, const uint32_t *rgb)
{
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int c = H.tile[k][y * 8 + x], px = x0 + x, py = y0 + y;
            if (c && px >= 0 && px < w && py >= 0 && py < h) pic[(long)py * pitch + px] = rgb[c];
        }
}

void hats_present(uint32_t *pic, int w, int h, int pitch)
{
    if (!H.active || !SDL_AtomicGet(&H.on) || !SDL_AtomicGet(&H.found) || !g.demoflag || !(g.disflags & HI(dfok))) return;
    int t = (int)g.HomeTeam;
    if (t < 0 || t >= NTEAMS) t = 0;
    uint32_t rgb[16];
    for (int c = 0; c < 16; c++) {
        unsigned v = H.pal[t][c], r = v & 31, gr = (v >> 5) & 31, b = (v >> 10) & 31;
        rgb[c] = 0xFF000000u | (r << 3 | r >> 2) << 16 | (gr << 3 | gr >> 2) << 8 | (b << 3 | b >> 2);
    }
    for (int i = 0; i < 10; i++) {
        if (!H.h[i].step) continue;
        int sx = H.h[i].x >> 4, sy = H.h[i].y >> 4, sz = H.h[i].z >> 4;
        tile(pic, w, h, pitch, 9, sx - 2, sy, rgb);                                 /* the shadow */
        tile(pic, w, h, pitch, H.h[i].kind + H.h[i].step - 1, sx - 2, sy - sz - 4, rgb);
    }
}
