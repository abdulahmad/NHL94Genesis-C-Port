/* NHL 95 Genesis's player ratings from the player's own cartridge image, for the 1993-94 roster sets
 * (rosters.c: season_folder): a player who is not on the NHL 94 cartridge takes the ratings EA gave
 * him a season later, found by his name. NHL 95 keeps its teams as NHL 94 does (TeamList: a long a
 * team, each team's block six offset words, then its players: a String, the name, and 8 bytes, his
 * number and 14 ratings a nibble each, in 94's order and on 94's scale; NHL95Genesis
 * src/teamdata95.asm). The image is read, never run, and is the player's: never shipped.
 *
 * Found once, at the first ask: NHL95G_ROM, else a file beside the program or in the folder the game
 * runs from with "95" in its name (.bin .md .gen .smd) of 2 MB whose team table reads well. */
#ifndef ROM95G_NO_SDL                                    /* (make rosterstest: no window, the working folder only) */
#include <SDL.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include "native.h"

#define ROM95 0x200000
#define TEAMS95 26                              /* (the All-Star teams' players are the clubs') */
#define PLAYERS95 1024

static struct { int tried, have; int n; char name[PLAYERS95][28]; uint8_t nib[PLAYERS95][14]; char path[600]; } R;

static unsigned be16(const uint8_t *p) { return (unsigned)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* a name as names are compared: its letters and digits, lower case */
void rom95g_key(const char *s, char *out, size_t size)
{
    size_t k = 0;
    for (; *s && k + 1 < size; s++) if (isalnum((unsigned char)*s)) out[k++] = (char)tolower((unsigned char)*s);
    out[k] = 0;
}

/* the team table at a, if it is one (28 longs, each to a block of six small offset words): its players into R */
static int read_at(const uint8_t *b, uint32_t a)
{
    for (int t = 0; t < 28; t++) {
        uint32_t p = be32(b + a + 4 * t);
        if (!p || p & 1 || p + 16 > ROM95) return 0;
        for (int k = 0; k < 6; k++) { unsigned o = be16(b + p + 2 * k); if (o < 8 || o >= 0x800) return 0; }
    }
    R.n = 0;
    for (int t = 0; t < TEAMS95; t++) {
        uint32_t p = be32(b + a + 4 * t), q = p + be16(b + p);
        for (int i = 0; i < 40 && q + 2 < ROM95; i++) {
            unsigned len = be16(b + q);
            if (len <= 2) break;
            if (len > 40 || q + len + 8 > ROM95) return 0;
            if (R.n < PLAYERS95) {
                char raw[40];
                memcpy(raw, b + q + 2, len - 2);
                raw[len - 2] = 0;
                rom95g_key(raw, R.name[R.n], sizeof R.name[0]);
                for (int k = 0; k < 14; k++) R.nib[R.n][k] = (uint8_t)(b[q + len + 1 + k / 2] >> (k & 1 ? 0 : 4) & 15);
                R.n++;
            }
            q += len + 8;
        }
    }
    return R.n > 400;
}

static int try_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint8_t *b = malloc(ROM95 + 1);
    long n = b ? (long)fread(b, 1, ROM95 + 1, f) : 0;
    fclose(f);
    int ok = 0;
    if (n == ROM95 && !memcmp(b + 0x100, "SEGA", 4))
        for (uint32_t a = 0x200; a < 0x2000 && !ok; a += 2) ok = read_at(b, a);
    free(b);
    if (ok) snprintf(R.path, sizeof R.path, "%s", path);
    return ok;
}

static int has95(const char *name)
{
    const char *e = strrchr(name, '.');
    if (!strstr(name, "95") || !e) return 0;
    char x[8] = { 0 };
    for (int i = 0; i < 7 && e[i + 1]; i++) x[i] = (char)tolower((unsigned char)e[i + 1]);
    return !strcmp(x, "bin") || !strcmp(x, "md") || !strcmp(x, "gen") || !strcmp(x, "smd");
}

static int try_dir(const char *dir)
{
    DIR *d = opendir(dir && *dir ? dir : ".");
    if (!d) return 0;
    struct dirent *e;
    int ok = 0;
    char p[1200];
    while (!ok && (e = readdir(d))) {
        if (!has95(e->d_name)) continue;
        snprintf(p, sizeof p, "%s%s%s", dir && *dir ? dir : "", dir && *dir && dir[strlen(dir) - 1] != '/' && dir[strlen(dir) - 1] != '\\' ? "/" : "", e->d_name);
        struct stat st;
        if (stat(p, &st) == 0 && st.st_size == ROM95) ok = try_file(p);
    }
    closedir(d);
    return ok;
}

int rom95g_have(void)
{
    if (R.tried) return R.have;
    R.tried = 1;
    const char *env = getenv("NHL95G_ROM");
    int ok = env && *env && try_file(env);
#ifndef ROM95G_NO_SDL
    char *base = SDL_GetBasePath();
    if (!ok && base) ok = try_dir(base);
    if (base) SDL_free(base);
#endif
    if (!ok) ok = try_dir(".");
    R.have = ok;
    if (ok) fprintf(stderr, "1993-94 rosters: NHL 95's ratings from %s (%d players)\n", R.path, R.n);
    return ok;
}

int rom95g_ratings(const char *name, uint8_t nib[14])
{
    char k[32];
    if (!rom95g_have()) return 0;
    rom95g_key(name, k, sizeof k);
    for (int i = 0; i < R.n; i++) if (!strcmp(R.name[i], k)) { memcpy(nib, R.nib[i], 14); return 1; }
    return 0;
}
