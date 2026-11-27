/* Fighting ratings for every skater (RULE_FIGHT_RATINGS: mods/fight.c's fight_rating, docs/FIGHT_RATINGS.md).
 *
 * The 94 cartridge's fighting values (bits 1-3 of the handed/fighting nibble) are a squeezed copy of NHLPA 93's: only 19 of its 619
 * skaters reach the 10 that NHL 93's SetInst needs to start a fight, against 83 of NHLPA 93's. This puts NHL 93's scale back, for
 * every roster set, by the player's name:
 *
 *   - NHLPA Hockey 93 (the player's own cartridge: NHL93_ROM, or a 512 KB file with "93" in its name beside the program or in the
 *     working folder): each player's handed/fighting nibble (byte 4 of his 8, the low nibble), bits 1-3, the scale the fights mod
 *     plays by;
 *   - NHL 96 PC (the player's own disc image: NHL96.iso and the like beside the program, or any image under it with DATA/KEY.DB and
 *     DATA/ATT.DB): KEY.DB's 60 byte records (number, position, first and last name, at +$24 the offset of his 21 bytes in ATT.DB),
 *     ATT.DB's byte 20, the fight rating 96 PC's SetInst reads (7: never fights; below 7 a fighter, low 3 bits his strength; 8 and up a
 *     reluctant one), mapped onto NHL 93's scale (M96 below);
 *   - in both: the mean of the two, on the even scale, a tie going NHLPA 93's way; in one: that one;
 *   - in neither: penalty minutes a game (fightrate_pim.inc, public 1992-93 stats) when there are some for him, else the cartridge's
 *     own value mapped onto NHL 93's scale (CART below).
 * The maps and thresholds are fitted on the players in both games (docs/FIGHT_RATINGS.md has the tables). No rating of either
 * game is in this repository: they are read from the player's files when the program starts.
 *
 * Names are matched loosely, because the cartridge cut many to fit (Alexnder Mogilny, Ken Baumgartnr, Scott Niedrmayer): letters
 * only, lower case; the last name equal, or one a subsequence of the other at 3/4 of its length, or alike (2 x common subsequence
 * / both lengths, 0.85 and up); the first name equal, a known short form (Gord, Gordon), a prefix of 3 letters or more, a
 * subsequence, or alike at 0.75. The best scoring name wins; two different names at the best score are no match. */
#include <SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "fightrate.h"
#include "game.h"
#include "rosters.h"
#include "overbar/cdimage.h"

struct ent { char first[24], last[40], raw[41]; uint8_t v; uint16_t gp, pim; };
struct map { struct ent *e; int n; };
static struct map local93, local96, sess93, sess96, pims;
static int use_session, loaded;
static unsigned generation;                        /* (the cache's: a new source, a new generation) */

/* ---- names ---- */

/* a Latin-1 letter without its accent, lower case; 0 when not a letter */
static char base_letter(uint8_t c)
{
    static const char hi[65] = "aaaaaaaceeeeiiiidnooooo\0ouuuuyts" "aaaaaaaceeeeiiiidnooooo\0ouuuuyty";   /* (64 and the string's own 0) */
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    if (c >= 'a' && c <= 'z') return (char)c;
    if (c >= 0xC0) return hi[c - 0xC0];
    return 0;
}

/* raw (n bytes, a 0 ends it sooner) into first (the first word) and last (the rest run together); 0 when not two words */
static int split_name(const char *raw, size_t n, char *first, size_t fs, char *last, size_t ls)
{
    size_t fl = 0, ll = 0;
    int word = 0, in = 0;
    for (size_t i = 0; i < n && raw[i]; i++) {
        uint8_t c = (uint8_t)raw[i];
        char b = base_letter(c);
        if (b) {
            if (!in) { word++; in = 1; }
            if (word == 1) { if (fl + 1 < fs) first[fl++] = b; }
            else if (ll + 1 < ls) last[ll++] = b;
        } else if (c == ' ' || c == '-' || c == '.' || c == '_') in = 0;     /* (an apostrophe or anything else: dropped, the word goes on) */
    }
    first[fl] = 0;
    last[ll] = 0;
    return fl > 0 && ll > 0;
}

static int subseq(const char *a, const char *b)            /* a's letters in b, in order */
{
    for (; *a; a++) {
        while (*b && *b != *a) b++;
        if (!*b) return 0;
        b++;
    }
    return 1;
}

static int lcs(const char *a, const char *b)
{
    int la = (int)strlen(a), lb = (int)strlen(b), p[48] = { 0 }, q[48];
    if (lb > 46) lb = 46;
    for (int i = 0; i < la; i++) {
        q[0] = 0;
        for (int j = 0; j < lb; j++) q[j + 1] = a[i] == b[j] ? p[j] + 1 : (p[j + 1] > q[j] ? p[j + 1] : q[j]);
        memcpy(p, q, sizeof(int) * (size_t)(lb + 1));
    }
    return p[lb];
}

/* 2 x lcs / (both lengths) at least pct / 100 */
static int alike(const char *a, const char *b, int pct) { int s = (int)(strlen(a) + strlen(b)); return s && 200 * lcs(a, b) >= pct * s; }

static int near_len(const char *a, const char *b) { size_t x = strlen(a), y = strlen(b), lo = x < y ? x : y, hi = x < y ? y : x; return 4 * lo >= 3 * hi; }

static int last_score(const char *a, const char *b)
{
    if (!strcmp(a, b)) return 3;
    if ((subseq(a, b) || subseq(b, a)) && near_len(a, b)) return 2;
    return alike(a, b, 85) ? 1 : 0;
}

static const char *const nick[][8] = {
    { "mike", "michael", "mikael", "mikhail" }, { "matt", "mathieu", "matthew" }, { "dan", "daniel", "danny" },
    { "gord", "gordon", "gordie" }, { "slava", "vyacheslav", "viacheslav", "vachslav", "vjateslav" }, { "dimitri", "dmitri", "dmitry" },
    { "jim", "james", "jimmy" }, { "bob", "robert", "rob", "bobby" }, { "tom", "thomas", "tommy" },
    { "alex", "alexander", "alexandre", "alexei", "aleksei" }, { "steve", "steven", "stephen" }, { "chris", "christopher" },
    { "greg", "gregory", "gregori", "grigori" }, { "joe", "joseph", "joey" }, { "ken", "kenneth", "kenny" }, { "andy", "andrew" },
    { "jeff", "jeffrey" }, { "pat", "patrick" }, { "ron", "ronald", "ronnie" }, { "doug", "douglas" }, { "dave", "david" },
    { "tony", "anthony" }, { "bill", "william", "billy" }, { "rick", "richard", "ricky", "rich" }, { "nick", "nicholas", "nicolas" },
    { "ed", "edward", "eddie" }, { "sergei", "serge", "sergey" }, { "jon", "john", "jonathan", "johnny" },
};

static int in_group(const char *const *grp, const char *s) { for (int i = 0; i < 8 && grp[i]; i++) if (!strcmp(grp[i], s)) return 1; return 0; }

static int first_score(const char *a, const char *b)
{
    if (!strcmp(a, b)) return 3;
    for (size_t k = 0; k < sizeof nick / sizeof *nick; k++) if (in_group(nick[k], a) && in_group(nick[k], b)) return 2;
    size_t la = strlen(a), lb = strlen(b);
    if ((la < lb ? la : lb) >= 3 && (!strncmp(a, b, la < lb ? la : lb))) return 2;
    if ((subseq(a, b) || subseq(b, a)) && near_len(a, b)) return 2;
    return alike(a, b, 75) ? 1 : 0;
}

/* the entry of m whose name is name's, or NULL (none, or two different names as good) */
static const struct ent *look(const struct map *m, const char *first, const char *last)
{
    const struct ent *best = NULL;
    int bs = 0, two = 0;
    for (int i = 0; i < m->n; i++) {
        const struct ent *e = &m->e[i];
        int a = last_score(last, e->last);
        if (!a) continue;
        int b = first_score(first, e->first);
        if (!b) continue;
        int s = a * 4 + b;
        if (s > bs) { bs = s; best = e; two = 0; }
        else if (s == bs && strcmp(best->raw, e->raw)) two = 1;
    }
    return two ? NULL : best;
}

static int add(struct map *m, const char *raw, size_t n, uint8_t v, uint16_t gp, uint16_t pim)
{
    struct ent e = { .v = v, .gp = gp, .pim = pim };
    if (n > 40) n = 40;
    memcpy(e.raw, raw, n);
    e.raw[n] = 0;
    for (size_t i = 0; i < n; i++) if (!e.raw[i]) break;
    if (!split_name(e.raw, n, e.first, sizeof e.first, e.last, sizeof e.last)) return 0;
    if (m->n % 256 == 0) {
        struct ent *x = realloc(m->e, sizeof *x * (size_t)(m->n + 256));
        if (!x) return 0;
        m->e = x;
    }
    m->e[m->n++] = e;
    return 1;
}

static void map_free(struct map *m) { free(m->e); m->e = NULL; m->n = 0; }

/* ---- the scales (docs/FIGHT_RATINGS.md) ---- */

/* NHL 96 PC's fight byte on NHL 93's scale: a fighter (below 7) 10, 12 for strength 5, 14 for 6; 7 (never) 0; a reluctant one
 * (8 and up) 2 to 6 by strength */
static const uint8_t M96[16] = { 10, 10, 10, 10, 10, 12, 14, 0, 2, 4, 4, 4, 6, 6, 6, 6 };
/* the cartridge's value (even) on NHL 93's scale: the median NHLPA 93 value of the cartridge's players at each */
static const uint8_t CART[8] = { 0, 2, 2, 6, 10, 12, 12, 14 };

/* penalty minutes a game (100ths) on NHL 93's scale: the median NHLPA 93 value of the players at each (NHL 96 PC's 1993-94 stats) */
static int from_pim(int gp, int pim)
{
    if (gp <= 0) return -1;
    int r = gp >= 20 ? 100 * pim / gp : (100 * pim + 100 * (20 - gp)) / 20;   /* (under 20 games: the rest counted at 1 a game) */
    return r < 25 ? 0 : r < 100 ? 2 : r < 150 ? 4 : r < 200 ? 6 : r < 300 ? 10 : r < 400 ? 12 : 14;
}

static const struct { const char *name; uint16_t gp, pim; } pim_table[] = {
#include "fightrate_pim.inc"
};

/* ---- reading the player's files ---- */

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]; }

/* NHLPA 93's teams: TeamList (26 long pointers to records whose name String reads as text), each record's word offsets (+0 the
 * players, +4 the name), the players (0, a length byte counting itself, the name, a 0 to make it even, 8 bytes), ended by 0, 2 */
static int read93(const uint8_t *d, size_t n, struct map *m)
{
#define W(a) ((a) + 2 <= n ? be16(d + (a)) : 0)
    for (size_t t = 0x200; t + 26 * 4 <= n && t < 0x10000; t += 2) {
        uint32_t ps[26];
        int ok = 1;
        for (int i = 0; ok && i < 26; i++) {
            ps[i] = be32(d + t + 4 * i);
            if (ps[i] < 0x100 || ps[i] + 12 > n) { ok = 0; break; }
            size_t s = ps[i] + W(ps[i] + 4), len = W(s);
            if (len <= 2 || len >= 40 || s + len > n) { ok = 0; break; }
            for (size_t k = s + 2; k < s + len; k++) if (d[k] && (d[k] < 32 || d[k] > 126)) { ok = 0; break; }
        }
        if (!ok) continue;
        int got = 0;
        for (int i = 0; i < 26; i++) {
            size_t a = ps[i] + W(ps[i]);
            for (int guard = 0; guard < 40 && a + 2 <= n; guard++) {
                uint8_t len = d[a + 1];
                if (d[a] == 0 && len == 2) break;
                if (len < 3 || a + len + 8 > n) break;
                got += add(m, (const char *)d + a + 2, (size_t)len - 2, (uint8_t)(d[a + len + 4] & 0xE), 0, 0);
                a += (size_t)len + 8;
            }
        }
        return got;
    }
    return 0;
#undef W
}

/* NHL 96 PC's KEY.DB and ATT.DB: the skaters (position not G) and their fight byte */
static int read96(const uint8_t *k, long kn, const uint8_t *a, long an, struct map *m)
{
    int got = 0;
    for (long i = 0; i + 60 <= kn; i += 60) {
        const uint8_t *r = k + i;
        if (r[2] == 'G' || r[2] == 0) continue;
        uint32_t off = le32(r + 36);
        if (off > (uint32_t)an || (uint32_t)an - off < 21) continue;
        char raw[34];
        size_t fl = strnlen((const char *)r + 3, 16), ll = strnlen((const char *)r + 19, 16);
        memcpy(raw, r + 3, fl);
        raw[fl] = ' ';
        memcpy(raw + fl + 1, r + 19, ll);
        got += add(m, raw, fl + 1 + ll, (uint8_t)(a[off + 20] & 0xF), 0, 0);
    }
    return got;
}

static uint8_t *slurp(const char *path, long max, long *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *b = malloc((size_t)max + 1);
    *n = b ? (long)fread(b, 1, (size_t)max + 1, f) : 0;
    fclose(f);
    if (b && (*n <= 0 || *n > max)) { free(b); b = NULL; }
    return b;
}

static int has93(const char *name)
{
    if (!strstr(name, "93")) return 0;
    const char *e = strrchr(name, '.');
    if (!e) return 0;
    char x[8] = { 0 };
    for (int i = 0; i < 7 && e[i + 1]; i++) x[i] = (char)tolower((unsigned char)e[i + 1]);
    return !strcmp(x, "bin") || !strcmp(x, "md") || !strcmp(x, "gen") || !strcmp(x, "smd");
}

static int try93(const char *path)
{
    long n;
    uint8_t *b = slurp(path, 0x80000, &n);
    int got = b && n == 0x80000 ? read93(b, (size_t)n, &local93) : 0;
    free(b);
    if (got) fprintf(stderr, "Fight ratings: NHLPA 93's from %s (%d players)\n", path, got);
    return got;
}

static int dir93(const char *dir)
{
    DIR *d = opendir(dir && *dir ? dir : ".");
    if (!d) return 0;
    struct dirent *e;
    int got = 0;
    char p[1200];
    while (!got && (e = readdir(d))) {
        if (!has93(e->d_name)) continue;
        snprintf(p, sizeof p, "%s%s%s", dir && *dir ? dir : "", dir && *dir && dir[strlen(dir) - 1] != '/' && dir[strlen(dir) - 1] != '\\' ? "/" : "", e->d_name);
        struct stat st;
        if (stat(p, &st) == 0 && st.st_size == 0x80000) got = try93(p);
    }
    closedir(d);
    return got;
}

static int from_cd(ob_cd *cd)
{
    uint8_t *k = NULL, *a = NULL;
    long kn = ob_cd_read_file(cd, "DATA/KEY.DB", &k), an = ob_cd_read_file(cd, "DATA/ATT.DB", &a);
    int got = kn > 0 && an > 0 ? read96(k, kn, a, an, &local96) : 0;
    free(k);
    free(a);
    if (got) fprintf(stderr, "Fight ratings: NHL 96 PC's from %s (%d skaters)\n", ob_cd_path(cd), got);
    return got;
}

static int find96(const char *base)
{
    static const char *const cand[] = { "NHL96.iso", "NHL96.cue", "nhl96.iso", "nhl96.cue", "NHL 96.iso", "NHL 96.cue", "NHL96.bin", "NHL96.img", "NHLHOCKEY96.iso" };
    static const char *const need[2] = { "DATA/KEY.DB", "DATA/ATT.DB" };
    const char *dirs[2] = { base && *base ? base : ".", "." };
    for (int d = 0; d < 2; d++) {                         /* (the disc's files copied out: DATA/KEY.DB beside the program) */
        char kp[1100], ap[1100];
        long kn, an;
        snprintf(kp, sizeof kp, "%s/DATA/KEY.DB", dirs[d]);
        snprintf(ap, sizeof ap, "%s/DATA/ATT.DB", dirs[d]);
        uint8_t *k = slurp(kp, 1 << 20, &kn), *a = k ? slurp(ap, 1 << 20, &an) : NULL;
        int got = k && a ? read96(k, kn, a, an, &local96) : 0;
        free(k);
        free(a);
        if (got) { fprintf(stderr, "Fight ratings: NHL 96 PC's from %s (%d skaters)\n", kp, got); return got; }
    }
    for (int d = 0; d < 2; d++)
        for (size_t i = 0; i < sizeof cand / sizeof *cand; i++) {
            char p[1100];
            snprintf(p, sizeof p, "%s/%s", dirs[d], cand[i]);
            ob_cd *cd = ob_cd_open(p, need, 2);
            if (!cd) continue;
            int got = from_cd(cd);
            ob_cd_close(cd);
            if (got) return got;
        }
    for (int d = 0; d < 2; d++) {
        ob_cd *cd = ob_cd_find(dirs[d], need, 2, NULL, 0);
        if (!cd) continue;
        int got = from_cd(cd);
        ob_cd_close(cd);
        if (got) return got;
    }
    return 0;
}

static void load_pims(void)
{
    if (pims.n) return;
    for (size_t i = 0; i < sizeof pim_table / sizeof *pim_table; i++)
        add(&pims, pim_table[i].name, strlen(pim_table[i].name), 0, pim_table[i].gp, pim_table[i].pim);
}

int fightrate_found93(void) { return local93.n; }
int fightrate_found96(void) { return local96.n; }

/* ---- the rating ---- */

static int value_of(const char *name, int cart, const char **how)
{
    char first[24], last[40];
    cart &= 0xE;
    if (!split_name(name, strlen(name), first, sizeof first, last, sizeof last)) { if (how) *how = "cart"; return CART[cart >> 1]; }
    const struct map *m93 = use_session ? &sess93 : &local93, *m96 = use_session ? &sess96 : &local96;
    const struct ent *a = look(m93, first, last), *b = look(m96, first, last);
    if (a && b) {
        int p = a->v, q = M96[b->v & 0xF], v = (p + q) / 2;
        if (v & 1) v += p > q ? 1 : -1;
        if (how) *how = "both";
        return v;
    }
    if (a) { if (how) *how = "93"; return a->v; }
    if (b) { if (how) *how = "96"; return M96[b->v & 0xF]; }
    load_pims();
    const struct ent *c = look(&pims, first, last);
    int v = c ? from_pim(c->gp, c->pim) : -1;
    if (v >= 0) { if (how) *how = "pim"; return v; }
    if (how) *how = "cart";
    return CART[cart >> 1];
}

/* a cache of the names asked for (setplayer asks for each skater he puts on the ice) */
#define CACHE 2048
static struct { unsigned gen; char name[32]; uint8_t cart, v; } cache[CACHE];

int fightrate_value(const char *name, int cart, const char **how)
{
    if (how) return value_of(name, cart, how);
    uint32_t h = 2166136261u;
    for (const char *s = name; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    h = (h ^ (uint32_t)cart) * 16777619u;
    for (int probe = 0; probe < 8; probe++) {
        unsigned i = (h + (unsigned)probe) % CACHE;
        if (cache[i].gen == generation + 1 && cache[i].cart == cart && !strncmp(cache[i].name, name, sizeof cache[i].name)) return cache[i].v;
        if (cache[i].gen != generation + 1) {
            int v = value_of(name, cart, NULL);
            if (strlen(name) < sizeof cache[i].name) {
                cache[i].gen = generation + 1;
                cache[i].cart = (uint8_t)cart;
                snprintf(cache[i].name, sizeof cache[i].name, "%s", name);
                cache[i].v = (uint8_t)v;
            }
            return v;
        }
    }
    return value_of(name, cart, NULL);
}

/* a String's text (its length word, the bytes, a 0 to make it even) */
static void roster_name_text(const struct RosterText *t, char *out, size_t size)
{
    size_t n = (size_t)(t->s[0] << 8 | t->s[1]), k = 0;
    n = n >= 2 ? n - 2 : 0;
    if (n > RS_TEXT - 2) n = RS_TEXT - 2;
    for (size_t i = 0; i < n && t->s[2 + i] && k + 1 < size; i++) out[k++] = (char)t->s[2 + i];
    out[k] = 0;
}

static int hook(const char *name, int cart) { return fightrate_value(name, cart, NULL); }

void fightrate_start(void)
{
    if (loaded) return;
    loaded = 1;
    char *base = SDL_GetBasePath();
    const char *env = getenv("NHL93_ROM");
    if (!(env && *env && try93(env)) && !(base && dir93(base)) && !dir93("."))
        fprintf(stderr, "Fight ratings: no NHLPA Hockey 93 cartridge found beside the program\n");
    const char *env96 = getenv("NHL96_DISC");
    int ok96 = 0;
    if (env96 && *env96) {
        static const char *const need[2] = { "DATA/KEY.DB", "DATA/ATT.DB" };
        ob_cd *cd = ob_cd_open(env96, need, 2);
        if (cd) { ok96 = from_cd(cd); ob_cd_close(cd); }
    }
    if (!ok96 && !find96(base)) fprintf(stderr, "Fight ratings: no NHL 96 PC disc image found beside the program\n");
    if (base) SDL_free(base);
    generation++;
    fight_rating_hook = hook;
}

/* ---- netplay: the host's sources ----
 * "FR1", then for NHLPA 93's and NHL 96's in turn: a count (u16, little-endian, at most FR_MAX), each a name (a length byte, 1-40
 * bytes, none 0 or below 32) and its value (93's: even, 0-14; 96's: 0-15) */
#define FR_MAX 4096

uint8_t *fightrate_pack(size_t *size)
{
    const struct map *m[2] = { &local93, &local96 };
    size_t total = 3;
    for (int k = 0; k < 2; k++) {
        total += 2;
        for (int i = 0; i < m[k]->n && i < FR_MAX; i++) total += 2 + strlen(m[k]->e[i].raw);
    }
    uint8_t *b = malloc(total), *o = b;
    if (!b) { *size = 0; return NULL; }
    memcpy(o, "FR1", 3); o += 3;
    for (int k = 0; k < 2; k++) {
        int n = m[k]->n < FR_MAX ? m[k]->n : FR_MAX;
        *o++ = (uint8_t)n; *o++ = (uint8_t)(n >> 8);
        for (int i = 0; i < n; i++) {
            size_t l = strlen(m[k]->e[i].raw);
            *o++ = (uint8_t)l;
            memcpy(o, m[k]->e[i].raw, l); o += l;
            *o++ = m[k]->e[i].v;
        }
    }
    *size = total;
    return b;
}

const char *fightrate_session_use(const uint8_t *b, size_t size)
{
    struct map m[2] = { { NULL, 0 }, { NULL, 0 } };
    const char *why = NULL;
    size_t at = 3;
    if (!b || size < 3 || memcmp(b, "FR1", 3)) why = "not this build's fight ratings";
    for (int k = 0; !why && k < 2; k++) {
        if (size - at < 2) { why = "fight ratings cut short"; break; }
        size_t n = (size_t)b[at] | (size_t)b[at + 1] << 8;
        at += 2;
        if (n > FR_MAX) { why = "too many fight ratings"; break; }
        for (size_t i = 0; i < n; i++) {
            if (size - at < 1) { why = "fight ratings cut short"; break; }
            size_t l = b[at++];
            if (l < 1 || l > 40) { why = "a fight rating's name of a bad length"; break; }
            if (l + 1 > size - at) { why = "fight ratings cut short"; break; }        /* (len > size - at: cannot wrap) */
            for (size_t j = 0; j < l; j++) if (b[at + j] < 32 || b[at + j] == 127) { why = "a fight rating's name that is not text"; break; }
            if (why) break;
            uint8_t v = b[at + l];
            if (k == 0 ? (v > 14 || (v & 1)) : v > 15) { why = "a fight rating out of range"; break; }
            add(&m[k], (const char *)b + at, l, v, 0, 0);                    /* (a name without two words is left out, as at home) */
            at += l + 1;
        }
    }
    if (!why && at != size) why = "fight ratings with bytes past their end";
    if (why) { map_free(&m[0]); map_free(&m[1]); return why; }
    map_free(&sess93);
    map_free(&sess96);
    sess93 = m[0];
    sess96 = m[1];
    use_session = 1;
    generation++;
    return NULL;
}

void fightrate_session_clear(void)
{
    if (!use_session) return;
    use_session = 0;
    map_free(&sess93);
    map_free(&sess96);
    generation++;
}

void fightrate_report(FILE *out)
{
    fprintf(out, "team,name,cartridge,nhlpa93,nhl96pc,played,from\n");
    for (int t = 0; t < rosters.teams && t < ROSTERS_MAX; t++) {
        const struct RosterTeam *T = &roster_team[t];
        for (int s = T->goalies; s < T->players && s < RS_ROSTER; s++) {
            char name[32];
            roster_name_text(&T->player[s].name, name, sizeof name);
            char first[24], last[40];
            int cart = T->player[s].rating[LR_HANDED_FIGHTING] & 0xE;
            const struct ent *a = NULL, *b = NULL;
            if (split_name(name, strlen(name), first, sizeof first, last, sizeof last)) {
                a = look(use_session ? &sess93 : &local93, first, last);
                b = look(use_session ? &sess96 : &local96, first, last);
            }
            const char *how;
            int v = fightrate_value(name, cart, &how);
            char abbr[8];
            roster_name_text(&T->abbr, abbr, sizeof abbr);
            fprintf(out, "%s,\"%s\",%d,", abbr, name, cart);
            if (a) fprintf(out, "%d", a->v);
            fprintf(out, ",");
            if (b) fprintf(out, "%d", b->v);
            fprintf(out, ",%d,%s\n", v, how);
        }
    }
}
