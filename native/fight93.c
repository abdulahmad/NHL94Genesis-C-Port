/* NHL 93's fight frames from the player's own NHLPA Hockey 93 cartridge (fight93.h).
 *
 * The 94 cartridge kept the fight frames' numbers (SPFgloves 353 to SPFfinjury+5 376, the fights of src/fight.c) but not their
 * art: each is one 8 x 8 sprite of a blank tile. NHL 93 has them all. Its Sprites.anim is the same format as 94's ($FFFFFFFF, the
 * long offset of the frame table, the tiles from +$A, 8 byte sprite entries), so the frames' sprite entries are read from it and
 * its tiles are served to the VDP's DMA at FIGHT93_BUS (read68k, main.c): the game's own addframe2 draws them, through the
 * spr_art_hook (video.c). Without the cartridge the fight frames are drawn with the 94 cartridge's nearest (fight_frame_shown).
 *
 * The file is the player's, never shipped: NHL93_ROM, or a 512 KB file whose name has "93" in it beside the program or in the
 * working folder (.bin, .md, .gen, .smd as a plain image). Rev B numbers its frames one lower from the holding frames on: the
 * offset is found by the wall jump frame that both have (SPFPen). */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include "game.h"
#include "anim_gen.h"
#include "fight93.h"

#define ROM93 0x80000
static struct {
    int have, shift;                    /* shift: NHL 93's frame number less ours */
    uint8_t *anim;                      /* Sprites.anim to the cartridge's end */
    uint32_t size, table;               /* its size, the frame table's offset in it */
    char path[600];
} F;

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

/* frame fr's entries in anim (NHL 93's numbering): 0 when it has none or they are out of the file */
static int frame_at(uint32_t fr, uint32_t *at, int *n)
{
    uint32_t t = F.table, i = t + fr * 2;
    if (!F.anim || t + 4 > F.size || i + 4 > F.size || fr * 2 >= be16(F.anim + t + 2)) return 0;
    uint32_t a = t + be16(F.anim + i), b = t + be16(F.anim + i + 2);
    if (b < a || b > F.size || (b - a) % 8) return 0;
    *at = a;
    *n = (int)((b - a) / 8);
    return *n > 0;
}

/* a cartridge image: NHL 93's Sprites.anim in it, with the wall jump frame where ours is (first entry at -21, -24, size 8) */
static int take(const uint8_t *rom, long n, const char *path)
{
    if (n != ROM93) return 0;
    for (long s = 0x200; s + 12 < n; s += 2) {
        if (rom[s] != 0xFF || rom[s + 1] != 0xFF || rom[s + 2] != 0xFF || rom[s + 3] != 0xFF) continue;
        uint32_t off = (uint32_t)rom[s + 4] << 24 | (uint32_t)rom[s + 5] << 16 | (uint32_t)rom[s + 6] << 8 | rom[s + 7];
        if (off < 0x10000 || off > (uint32_t)(n - s) || (uint32_t)(n - s) - off < 0x600) continue;   /* (unsigned: a long is 32 bits on Windows, so off >= 2 GB went negative and passed) */
        F.anim = (uint8_t *)(uintptr_t)(rom + s);
        F.size = (uint32_t)(n - s);
        F.table = off;
        for (int d = -1; d <= 1; d++) {
            uint32_t at;
            int k;
            if (!frame_at((uint32_t)(SPFPen + d), &at, &k)) continue;
            const uint8_t *e = F.anim + at;
            if (be16(e) == 0xFFEB && be16(e + 2) == 0xFFE8 && e[7] == 8) {
                F.shift = d;
                F.anim = malloc(F.size);
                if (!F.anim) return 0;
                memcpy(F.anim, rom + s, F.size);
                snprintf(F.path, sizeof F.path, "%s", path);
                return 1;
            }
        }
    }
    F.anim = NULL;                                       /* (none here: nothing left pointing at the file being freed) */
    F.size = F.table = 0;
    return 0;
}

static int try_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint8_t *b = malloc(ROM93 + 1);
    long n = b ? (long)fread(b, 1, ROM93 + 1, f) : 0;
    fclose(f);
    int ok = b && take(b, n, path);
    free(b);
    return ok;
}

static int has93(const char *name)
{
    size_t l = strlen(name);
    if (!strstr(name, "93") || l < 4) return 0;
    const char *e = strrchr(name, '.');
    if (!e) return 0;
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
        if (!has93(e->d_name)) continue;
        snprintf(p, sizeof p, "%s%s%s", dir && *dir ? dir : "", dir && *dir && dir[strlen(dir) - 1] != '/' ? "/" : "", e->d_name);
        struct stat st;
        if (stat(p, &st) == 0 && st.st_size == ROM93) ok = try_file(p);
    }
    closedir(d);
    return ok;
}

static int art(uint16_t frame, const uint8_t **pieces, int *n, uint32_t *tiles)
{
    if (!F.have || frame < SPFfight || frame >= SPFPen) return 0;
    uint32_t at;
    if (!frame_at((uint32_t)(frame + F.shift), &at, n) || *n > 8) return 0;
    *pieces = F.anim + at;
    *tiles = FIGHT93_BUS + 0xA;
    return 1;
}

void fight93_start(void)
{
    static int started;
    if (started) return;
    started = 1;
    const char *env = getenv("NHL93_ROM");
    int ok = env && *env && try_file(env);
    char *base = SDL_GetBasePath();
    if (!ok && base) ok = try_dir(base);
    if (base) SDL_free(base);
    if (!ok) ok = try_dir(".");
    if (!ok) return;
    F.have = 1;
    spr_art_hook = art;
    fprintf(stderr, "Fights: NHL 93's fight frames from %s\n", F.path);
}

int fight93_have(uint16_t frame) { const uint8_t *p; int n; uint32_t t; return art(frame, &p, &n, &t); }

int fight93_bus(uint32_t addr, uint8_t *v)
{
    if (!F.have || addr < FIGHT93_BUS || addr - FIGHT93_BUS >= F.size) return 0;
    *v = F.anim[addr - FIGHT93_BUS];
    return 1;
}
