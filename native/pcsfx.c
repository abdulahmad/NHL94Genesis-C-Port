/* NHL 95 PC's goal siren and end of period horn (the Game page's "PC goal siren" and "PC end of period horn").
 *
 * The PC game's sound effects are programs of a patch map: the goal siren is sound $9C (program 156, the 7 second
 * Sound Blaster sample 564913A7) and the horn at the end of a period $90/$91 (the digital part, sample 1AB7BA4B, 2.6 s),
 * both signed 8-bit mono at 11025 Hz in PCFF001.DIG (the container is documented in NHL94PC docs/SFX_FORMAT.md). The
 * player's own file, found beside the program (loose, or on the NHL 95 PC disc image), never shipped. With a row on, the
 * cartridge's own siren or horn is not played: this plays instead. It only adds sound; the game is unchanged
 * (states, replays and online play are the same). */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include "game.h"
#include "native.h"
#include "pcsfx.h"
#include "announcer.h"
#include "overbar/cdimage.h"

#define RATE 11025
#define GAIN 90                              /* signed 8-bit to 16-bit */
#define WALK_DEPTH 3
#define PATHMAX 1024
#define LISTMAX 2048

enum { GOAL, HORN, MINUTE, CROWD8, CROWD7, NSND };     /* (the last two: the crowd's looping timbres 16 and 28, programs 7Dh and 7Eh) */
static const uint8_t sample_id[NSND][4] = { { 0x56, 0x49, 0x13, 0xA7 }, { 0x1A, 0xB7, 0xBA, 0x4B }, { 0x31, 0xB5, 0x3F, 0x4F },
                                            { 0x49, 0x7B, 0x87, 0x1B }, { 0x78, 0x68, 0xA9, 0xD2 } };
static const long loop_a[NSND] = { 0, 0, 0, 3856, 595 }, loop_b[NSND] = { 0, 0, 0, 9971, 9041 };   /* (bytes: PCFF001.TIM) */

static struct {
    SDL_atomic_t on[NSND], found, searched;
    SDL_mutex *lock;
    int16_t *pcm[NSND];
    long len[NSND];
    int playing[NSND];
    double at[NSND];
    SDL_atomic_t crowd_on, crowd_vol, crowd_live;        /* the Game page's row, its volume (percent), frames the game's crowd is still on */
    SDL_atomic_t v8, v7, pan7;                           /* the PC's voices: volume 0-127 and (voice 7) the pan */
    float g8, g7;                                        /* (the mixer's smoothed gains) */
    int smooth;                                          /* Crowd_Noise's crowdsmooth */
} S;

static int parse(const uint8_t *d, long n)
{
    if (n < 6) return 0;
    unsigned count = (unsigned)(d[4] | d[5] << 8);
    long base = 6 + 8L * count;
    if (!count || base > n) return 0;
    for (int s = 0; s < NSND; s++) {
        int idx = -1;
        for (unsigned i = 0; i < count; i++) if (!memcmp(d + 6 + 4 * i, sample_id[s], 4)) idx = (int)i;
        if (idx < 0) return 0;
        uint32_t off = (uint32_t)d[6 + 4 * count + 4 * idx] | (uint32_t)d[7 + 4 * count + 4 * idx] << 8 | (uint32_t)d[8 + 4 * count + 4 * idx] << 16 | (uint32_t)d[9 + 4 * count + 4 * idx] << 24;
        uint32_t end = (uint32_t)(n - base);                  /* an item runs to the next offset */
        for (unsigned i = 0; i < count; i++) {
            uint32_t o = (uint32_t)d[6 + 4 * count + 4 * i] | (uint32_t)d[7 + 4 * count + 4 * i] << 8 | (uint32_t)d[8 + 4 * count + 4 * i] << 16 | (uint32_t)d[9 + 4 * count + 4 * i] << 24;
            if (o > off && o != 0xFFFFFFFFu && o < end) end = o;
        }
        if (off >= end || base + (long)end > n) return 0;
        int16_t *p = malloc((size_t)(end - off) * sizeof *p);
        if (!p) return 0;
        for (long k = 0; k < (long)(end - off); k++) p[k] = (int16_t)((int8_t)d[base + off + k] * GAIN);
        S.pcm[s] = p;
        S.len[s] = (long)(end - off);
    }
    return 1;
}

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return !*a && !*b;
}
static const char *base_name(const char *p)
{
    const char *q = p;
    for (; *p; p++) if (*p == '/' || *p == '\\') q = p + 1;
    return q;
}
static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static long file_len(const char *p) { struct stat st; return stat(p, &st) == 0 ? (long)st.st_size : -1; }
static int ends(const char *s, const char *e)
{
    size_t a = strlen(s), b = strlen(e);
    return a >= b && ieq(s + a - b, e);
}
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
static uint8_t *slurp(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    long n = file_len(path);
    uint8_t *b = n > 0 && n < 64L << 20 ? malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    *size = b ? n : 0;
    return b;
}

static int find_in(const char *dir)
{
    static char list[LISTMAX][PATHMAX];
    int n = walk(dir, WALK_DEPTH, list, 0);
    for (int i = 0; i < n; i++)
        if (ieq(base_name(list[i]), "PCFF001.DIG")) {
            long size;
            uint8_t *b = slurp(list[i], &size);
            int ok = b && parse(b, size);
            free(b);
            if (ok) { fprintf(stderr, "PC siren and horn: %s\n", list[i]); return 1; }
        }
    static const char *const kinds[3][5] = { { ".cue" }, { ".ccd" }, { ".iso", ".bin", ".img", ".mdf", ".raw" } };
    for (int k = 0; k < 3; k++)
        for (int i = 0; i < n; i++) {
            int match = 0;
            for (int e = 0; e < 5 && kinds[k][e]; e++) match |= ends(list[i], kinds[k][e]);
            if (!match || (k == 2 && file_len(list[i]) < 16L * 2048)) continue;
            ob_cd *cd = ob_cd_open(list[i], NULL, 0);
            if (!cd) continue;
            for (int f = 0; ob_cd_file_name(cd, f); f++)
                if (ieq(base_name(ob_cd_file_name(cd, f)), "PCFF001.DIG")) {
                    uint8_t *b;
                    long size = ob_cd_read_file(cd, ob_cd_file_name(cd, f), &b);
                    int ok = size > 0 && parse(b, size);
                    free(b);
                    ob_cd_close(cd);
                    if (ok) { fprintf(stderr, "PC siren and horn: %s\n", list[i]); return 1; }
                    cd = NULL;
                    break;
                }
            if (cd) ob_cd_close(cd);
        }
    return 0;
}

static int finder(void *dir)
{
    if (SDL_AtomicGet(&S.searched)) { SDL_free(dir); return 0; }
    int ok = find_in((const char *)dir) || find_in(".");
    if (!ok) fprintf(stderr, "PC siren and horn: no NHL 95 PC disc image found beside the program\n");
    SDL_MemoryBarrierRelease();
    SDL_AtomicSet(&S.found, ok);
    SDL_AtomicSet(&S.searched, 1);
    SDL_free(dir);
    return 0;
}

int pcsfx_load_file(const char *path)
{
    long size;
    uint8_t *b = slurp(path, &size);
    int ok = b && parse(b, size);
    free(b);
    SDL_AtomicSet(&S.found, ok);
    SDL_AtomicSet(&S.searched, 1);
    return ok;
}

/* the game's siren or horn (src/sound.c sfx_hook): 1 when this plays it and the cartridge's is left out */
static int hook(int ev)
{
    int s = ev == SFX_GOAL ? GOAL : ev == SFX_HORN ? HORN : MINUTE;
    if (ev == SFX_ONEMIN && announcer_active()) return 1;           /* (95 PC: the announcer says it instead of the beep or horn) */
    if (!SDL_AtomicGet(&S.on[s]) || !SDL_AtomicGet(&S.found) || !g.demoflag) return 0;
    SDL_MemoryBarrierAcquire();
    SDL_LockMutex(S.lock);
    S.playing[s] = 1;
    S.at[s] = 0;
    SDL_UnlockMutex(S.lock);
    return 1;
}

static void crowd_hook(int meter);
static int siren_busy(void) { return S.lock && S.playing[GOAL]; }

void pcsfx_start(void)
{
    siren_busy_hook = siren_busy;
    crowd_level_hook = crowd_hook;
    SDL_AtomicSet(&S.crowd_vol, 100);
    static int started;
    if (started) return;
    started = 1;
    S.lock = SDL_CreateMutex();
    sfx_hook = hook;
    char *base = SDL_GetBasePath();
    SDL_Thread *t = SDL_CreateThread(finder, "pcsfx", base ? SDL_strdup(base) : SDL_strdup("."));
    if (base) SDL_free(base);
    if (t) SDL_DetachThread(t);
}

void pcsfx_wait(void) { while (!SDL_AtomicGet(&S.searched)) SDL_Delay(5); }
int pcsfx_searched(void) { return SDL_AtomicGet(&S.searched); }
int pcsfx_found(void) { return SDL_AtomicGet(&S.searched) && SDL_AtomicGet(&S.found); }
void pcsfx_set_goal(int on) { SDL_AtomicSet(&S.on[GOAL], on ? 1 : 0); }
void pcsfx_set_horn(int on) { SDL_AtomicSet(&S.on[HORN], on ? 1 : 0); SDL_AtomicSet(&S.on[MINUTE], on ? 1 : 0); }   /* (and the one minute horn) */

void pcsfx_set_crowd(int on) { SDL_AtomicSet(&S.crowd_on, on ? 1 : 0); }
void pcsfx_set_crowd_vol(int pct) { SDL_AtomicSet(&S.crowd_vol, pct); }

/* the game's crowd, each frame (src/crowd.c): Crowd_Noise of NHL 95 PC (engine/sound/Crowd_Noise.c) on the PC's own scale:
 * the cartridge's crowd meter (decibels, 65 up) stands in for its Crowdlevel (0x200 up) */
static void crowd_hook(int meter)
{
    if (!SDL_AtomicGet(&S.found) || !SDL_AtomicGet(&S.crowd_on)) return;
    int level = 0x200 + (meter - 0x41) * 0x3DC / 60;
    if (level < 0) level = 0;
    int d = level - S.smooth;                              /* (eased toward it, at most 30 a frame) */
    S.smooth = (d < 0 ? -d : d) > 30 ? S.smooth + (d < 0 ? -30 : 30) : level;
    int v = S.smooth;
    int v8 = v < 0x200 ? 0 : v < 0x390 ? (v - 0x200) / 10 : (v - 0x390) / 4 + 0x28;
    if (v8 > 0x7F) v8 = 0x7F;
    int st = S.smooth / 12 + 0x0A;
    if (st > 0x7F) st = 0x7F;
    SDL_AtomicSet(&S.v8, v8);
    SDL_AtomicSet(&S.v7, st);
    SDL_AtomicSet(&S.pan7, st / 3 + 0x20);
    SDL_AtomicSet(&S.crowd_live, 30);
}

void pcsfx_stop(void)
{
    if (!S.lock) return;
    SDL_LockMutex(S.lock);
    for (int s = 0; s < NSND; s++) S.playing[s] = 0;
    SDL_UnlockMutex(S.lock);
}

void pcsfx_mix(int16_t *out, int frames, unsigned rate)
{
    if (!S.lock || !rate || !SDL_AtomicGet(&S.found)) return;
    SDL_LockMutex(S.lock);
    double step = (double)RATE / rate;
    for (int s = 0; s < GOAL + 3; s++) {
        if (!S.playing[s]) continue;
        for (int i = 0; i < frames; i++) {
            long j = (long)S.at[s];
            if (j + 1 >= S.len[s]) { S.playing[s] = 0; break; }
            double frac = S.at[s] - (double)j;
            float v = (float)(S.pcm[s][j] + (S.pcm[s][j + 1] - S.pcm[s][j]) * frac);
            for (int c = 0; c < 2; c++) {
                float x = v + out[2 * i + c];
                out[2 * i + c] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
            }
            S.at[s] += step;
        }
    }
    /* the crowd bed: the PC's two looping voices, kept going while the game's crowd is */
    int on = SDL_AtomicGet(&S.crowd_on) && SDL_AtomicGet(&S.crowd_live) > 0;
    if (!on) { S.g8 = S.g7 = 0; SDL_AtomicAdd(&S.crowd_live, 0); }
    float vol = (float)SDL_AtomicGet(&S.crowd_vol) / 100.0f;
    float t8 = on ? SDL_AtomicGet(&S.v8) / 127.0f * vol : 0, t7 = on ? SDL_AtomicGet(&S.v7) / 127.0f * vol : 0;
    float pan = (float)(SDL_AtomicGet(&S.pan7) - 0x40) / 64.0f;
    float l7 = 1.0f - (pan > 0 ? pan : 0), r7 = 1.0f + (pan < 0 ? pan : 0);
    for (int s = CROWD8; s <= CROWD7; s++) {
        if (!S.pcm[s] || (!on && S.g8 == 0 && S.g7 == 0)) continue;
        for (int i = 0; i < frames; i++) {
            float *gp = s == CROWD8 ? &S.g8 : &S.g7, tg = s == CROWD8 ? t8 : t7;
            *gp += (tg - *gp) * 0.0005f;
            long j = (long)S.at[s];
            if (j + 1 >= loop_b[s]) { S.at[s] -= (double)(loop_b[s] - loop_a[s]); j = (long)S.at[s]; }
            double frac = S.at[s] - (double)j;
            float v = (float)(S.pcm[s][j] + (S.pcm[s][j + 1] - S.pcm[s][j]) * frac) * *gp * 0.8f;
            for (int c = 0; c < 2; c++) {
                float pg = s == CROWD7 ? (c ? r7 : l7) : 1.0f;
                float x = v * pg + out[2 * i + c];
                out[2 * i + c] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
            }
            S.at[s] += step;
        }
    }
    int live = SDL_AtomicGet(&S.crowd_live);
    if (live > 0) SDL_AtomicSet(&S.crowd_live, live - (int)((double)frames * 60.0 / rate) - 0);
    SDL_UnlockMutex(S.lock);
}
