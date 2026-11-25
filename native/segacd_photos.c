/* The Sega CD's player photos on the player cards (the Game page's "Player photos"): the disc's
 * grey 48 x 48 photos in place of the cartridge's pictures and stock silhouettes, for the players
 * the disc has one of. The disc keeps a block a team in GRAPH2.BIN, found by GTABLE.BIN (u16
 * sector, u32 length a record; the cartridge's teams in their order from record 6). A block starts
 * with longs, offsets in it: a photo for each card the disc shows in order, then four more (its
 * stats pages), then a word $FFFF; after it a word a card, the disc's roster number of its player,
 * negative when he has no photo. A photo is a $0808 word, the tile count ($24), 36 tiles (6 x 6,
 * across then down), 32 bytes, then 16 colours. PLAYER.BIN's PlAllocObject / PlDrawPicture
 * (NHL94Genesis src/segacd/player, docs/segacd_stats.md). The disc's rosters are the cartridge's
 * less a few players (segacd_rosters.inc), so a photo is matched to the cartridge's player by team
 * and name: a roster set's players get theirs too. No SDL in here. */
#include <stdlib.h>
#include <string.h>
#include "rom.h"
#include "symbols_gen.h"
#include "segacd.h"

#include "segacd_rosters.inc"

#define FIRST_TEAM_RECORD 6
#define PHOTO_BYTES 0x504
#define MAX_PHOTOS 2048

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)be16(p) << 16 | be16(p + 2); }

static struct {
    int loaded;                          /* 1 looked (photos read, or none) */
    uint8_t *graph2;
    long size;
    int n;
    struct { char abbr[8], name[32]; long at; } photo[MAX_PHOTOS];
} P;

/* a String of the cartridge (length word counting itself, the bytes) into s */
static void text(uint32_t a, char *s, int cap)
{
    const uint8_t *p = rom_ptr(a);
    int n = (int)be16(p) - 2, k = 0;
    for (int i = 0; i < n && k < cap - 1 && p[2 + i]; i++) s[k++] = (char)p[2 + i];
    while (k && s[k - 1] == ' ') k--;
    s[k] = 0;
}

/* the cartridge's team t: its abbreviation, and player c's name (0 when it has no such player) */
static int retail(int t, int c, char *abbr, char *name)
{
    uint32_t b = be32(rom_ptr(SYM_TeamList + 4u * (unsigned)t)), p = b + be16(rom_ptr(b));
    for (int i = 0; be16(rom_ptr(p)) != 2; i++) {
        if (i == c) {
            text(p, name, 32);
            uint32_t q = b + be16(rom_ptr(b + 4));
            q += be16(rom_ptr(q));                       /* (past the city) */
            text(q, abbr, 8);
            return 1;
        }
        p += be16(rom_ptr(p)) + 8;
    }
    return 0;
}

static void load(ob_cd *cd)
{
    uint8_t *gt = NULL;
    P.loaded = 1;
    long gn = ob_cd_read_file(cd, "GTABLE.BIN", &gt);
    P.size = ob_cd_read_file(cd, "GRAPH2.BIN", &P.graph2);
    if (gn < 6 * (FIRST_TEAM_RECORD + 28) || P.size <= 0) { free(gt); free(P.graph2); P.graph2 = NULL; return; }
    for (int t = 0; t < 28; t++) {
        const uint8_t *r = gt + 6 * (FIRST_TEAM_RECORD + t);
        long at = (long)be16(r) * 2048, len = (long)be32(r + 2), k = 0;
        if (at + len > P.size || len < 8) continue;
        const uint8_t *blk = P.graph2 + at;
        while (4 * (k + 1) <= len && !(blk[4 * k] & 0x80)) k++;            /* the offsets, to the $FFFF */
        long cards = k - 4;
        const uint8_t *words = blk + 4 * k + 2;
        for (long i = 0; i < cards && P.n < MAX_PHOTOS && (words - blk) + 2 * i + 2 <= len; i++) {
            int d = (int16_t)be16(words + 2 * i);
            long off = (long)be32(blk + 4 * i);
            if (d < 0 || off + PHOTO_BYTES > len || be16(blk + off) != 0x0808 || be16(blk + off + 2) != 0x24) continue;
            int c = d;                                   /* the disc's roster number to the cartridge's: past the players it left out */
            for (int pass = 0; pass < 3; pass++) {
                int skip = 0;
                for (int m = 0; m < 3; m++) if (segacd_teams[t].removed[m] && segacd_teams[t].removed[m] - 1 <= c) skip++;
                c = d + skip;
            }
            if (!retail(t, c, P.photo[P.n].abbr, P.photo[P.n].name)) continue;
            P.photo[P.n++].at = at + off;
        }
    }
    free(gt);
}

int segacd_photo(ob_cd *cd, const char *abbr, const char *name, uint8_t *buf, uint8_t colours[32])
{
    if (!cd) return 0;
    if (!P.loaded) load(cd);
    for (int i = 0; i < P.n; i++)
        if (!strcmp(P.photo[i].abbr, abbr) && !strcmp(P.photo[i].name, name)) {
            const uint8_t *f = P.graph2 + P.photo[i].at;
            memcpy(buf, f + 2, 2 + 36 * 32);                 /* (the tile count and the tiles: what UnpackPicture leaves) */
            memcpy(colours, f + 4 + 36 * 32 + 32, 32);
            return 1;
        }
    return 0;
}

int segacd_photo_count(ob_cd *cd)
{
    if (cd && !P.loaded) load(cd);
    return P.n;
}

const char *segacd_photo_name(int i, const char **abbr)
{
    if (i < 0 || i >= P.n) return NULL;
    if (abbr) *abbr = P.photo[i].abbr;
    return P.photo[i].name;
}
