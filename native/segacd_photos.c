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
#include <stdio.h>
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
    uint8_t *graph2, *gtable, *player;   /* (PLAYER.BIN read when first asked for) */
    long size, gsize, psize;
    int n;
    struct { char abbr[8], name[32]; long at; } photo[MAX_PHOTOS];
    struct { char abbr[8]; long careers[2], end[2]; } team[28];   /* its skaters' and goalies' careers (offsets in graph2, 0 none) */
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
    P.gtable = gt;
    P.gsize = gn;
    for (int t = 0; t < 28; t++) {
        const uint8_t *r = gt + 6 * (FIRST_TEAM_RECORD + t);
        long at = (long)be16(r) * 2048, len = (long)be32(r + 2), k = 0;
        if (at + len > P.size || len < 8) continue;
        const uint8_t *blk = P.graph2 + at;
        while (4 * (k + 1) <= len && !(blk[4 * k] & 0x80)) k++;            /* the offsets, to the $FFFF */
        long cards = k - 4;
        char nm[32];
        retail(t, 0, P.team[t].abbr, nm);
        for (int c = 0; c < 2 && k >= 4; c++) {                          /* the third and fourth of the four: the careers */
            long from = (long)be32(blk + 4 * (k - 2 + c)), to = c ? len : (long)be32(blk + 4 * (k - 1));
            if (from + 4 <= len && to <= len && from < to && be32(blk + from) != 0x12345678) { P.team[t].careers[c] = at + from; P.team[t].end[c] = at + to; }
        }
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
}

const uint8_t *segacd_graph2_record(ob_cd *cd, int rec, long *len)
{
    if (!cd) return NULL;
    if (!P.loaded) load(cd);
    if (!P.graph2 || rec < 0 || 6L * (rec + 1) > P.gsize) return NULL;
    long at = (long)be16(P.gtable + 6 * rec) * 2048, n = (long)be32(P.gtable + 6 * rec + 2);
    if (n <= 0 || at + n > P.size) return NULL;
    if (len) *len = n;
    return P.graph2 + at;
}

const uint8_t *segacd_player_bin(ob_cd *cd, long *len)
{
    if (!cd) return NULL;
    if (!P.player) P.psize = ob_cd_read_file(cd, "PLAYER.BIN", &P.player);
    if (!P.player || P.psize <= 0) return NULL;
    if (len) *len = P.psize;
    return P.player;
}

int segacd_team_index(ob_cd *cd, const char *abbr)
{
    if (!cd) return -1;
    if (!P.loaded) load(cd);
    if (!P.graph2) return -1;
    for (int t = 0; t < 28; t++) if (!strcmp(P.team[t].abbr, abbr)) return t;
    return -1;
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

/* ---- careers: a team's skaters' (and its goalies') in two lists, by name. A record is a word (its length, itself
 * counted), a count of strings, 0, then (for more than one) where each after the first starts, and the strings: a
 * player's name, his position and size, birthplace, birthday and draft (one string each, in quotes), the table's
 * heading (REGULAR SEASON / PLAYOFFS), its columns, a row a season; "en" ends a player. In the strings $ is an
 * inch mark and _ a space. PLAYER.BIN's PlDrawCareerStats (NHL94Genesis src/segacd/player/playercareer94cd.asm). */

/* the strings of the record at r (len bytes) into out (each cap bytes); how many */
static int strings(const uint8_t *r, long len, char out[][SEGACD_CELL], int max)
{
    int n = r[2], m = 0;
    if (len < 4 || n < 1) return 0;
    long base = 4 + (n - 1);
    for (int i = 0; i < n && m < max; i++) {
        long at = base + (i ? r[4 + i - 1] : 0), k = 0;
        if (at >= len) break;
        char *o = out[m++];
        for (; at < len && r[at] && k < SEGACD_CELL - 1; at++) {
            char ch = (char)r[at];
            if (ch == '"' ) continue;
            o[k++] = ch == '$' ? '"' : ch == '_' ? ' ' : ch;
        }
        o[k] = 0;
    }
    return m;
}

static void upper_letters(const char *s, char *o, int cap)
{
    int k = 0;
    for (; *s && k < cap - 1; s++) if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) o[k++] = (char)(*s & ~0x20);
    o[k] = 0;
}

/* how well the disc's "SURNAME, GIVEN NAMES (NICK)" fits the cartridge's "First Last": 3 surname and first name,
 * 2 the surname, 1 the start of the surname and the initial, 0 no */
static int fits(const char *disc, const char *cart)
{
    char sur[40], given[80], first[40], last[40], tmp[80];
    const char *comma = strchr(disc, ',');
    if (!comma) return 0;
    snprintf(tmp, sizeof tmp, "%.*s", (int)(comma - disc), disc);
    upper_letters(tmp, sur, sizeof sur);
    upper_letters(comma + 1, given, sizeof given);
    const char *sp = strrchr(cart, ' ');
    if (!sp) return 0;
    upper_letters(sp + 1, last, sizeof last);
    snprintf(tmp, sizeof tmp, "%.*s", (int)(sp - cart), cart);
    upper_letters(tmp, first, sizeof first);
    int initial = first[0] && given[0] == first[0];
    const char *paren = strchr(comma, '(');
    char nick[40] = "";
    if (paren) upper_letters(paren, nick, sizeof nick);
    if (!strcmp(sur, last)) return initial || (nick[0] && nick[0] == first[0]) ? 3 : 2;
    if (strlen(last) >= 5 && !strncmp(sur, last, 4) && initial) return 1;   /* (the cartridge's shortened names: Vanbiesbrk) */
    return 0;
}

int segacd_career_get(ob_cd *cd, const char *abbr, const char *name, int goalie, segacd_career *out)
{
    if (!cd) return 0;
    if (!P.loaded) load(cd);
    if (!P.graph2) return 0;
    for (int t = 0; t < 28; t++) {
        if (strcmp(P.team[t].abbr, abbr) || !P.team[t].careers[goalie ? 1 : 0]) continue;
        long p = P.team[t].careers[goalie ? 1 : 0], end = P.team[t].end[goalie ? 1 : 0], best_at = -1;
        int best = 0;
        while (p + 4 <= end) {                                          /* each player: his first record is his name */
            long n = (long)be16(P.graph2 + p);
            char s[1][SEGACD_CELL];
            if (n < 4 || p + n > end) break;
            if (strings(P.graph2 + p, n, s, 1) == 1 && !strcmp(s[0], "el")) break;
            int f = strings(P.graph2 + p, n, s, 1) == 1 ? fits(s[0], name) : 0;
            if (f > best) { best = f; best_at = p; }
            for (;;) {                                                  /* to the next player */
                n = (long)be16(P.graph2 + p);
                if (n < 4 || p + n > end) { p = end; break; }
                int one = strings(P.graph2 + p, n, s, 1) == 1 && !strcmp(s[0], "en");
                p += n;
                if (one) break;
            }
        }
        if (best_at < 0) return 0;
        memset(out, 0, sizeof *out);
        for (p = best_at; p + 4 <= end;) {
            long n = (long)be16(P.graph2 + p);
            if (n < 4 || p + n > end) break;
            const uint8_t *r = P.graph2 + p;
            p += n;
            char s[SEGACD_COLS][SEGACD_CELL];
            int m = strings(r, n, s, SEGACD_COLS);
            if (m == 1 && (!strcmp(s[0], "en") || !strcmp(s[0], "el"))) break;   /* (the list's last player ends with its "el") */
            if (m == 1 && !out->nrows && out->nbio < SEGACD_BIO) snprintf(out->bio[out->nbio++], sizeof out->bio[0], "%s", s[0]);
            else if (r[2] == 4 && !out->nrows && !out->heading[0][0]) {
                snprintf(out->heading[0], sizeof out->heading[0], "%.15s %.15s", s[1], s[2]);
                snprintf(out->heading[1], sizeof out->heading[1], "%.23s", s[3]);
            } else if (out->nrows < SEGACD_ROWS) {
                for (int i = 0; i < m; i++) snprintf(out->cell[out->nrows][i], SEGACD_CELL, "%s", s[i]);
                out->ncells[out->nrows++] = m;
            }
        }
        return out->nbio > 0;
    }
    return 0;
}
