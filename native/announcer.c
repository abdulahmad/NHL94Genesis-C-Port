/* The rink announcer of NHL 95 PC beside the cartridge game (the Game page's "Rink announcer").
 *
 * NHL 95 PC says a goal, a penalty, a penalty shot, the last minute and the three stars over the
 * crowd, from the clips in its XBRUCE2.VIV (one voice, one clip a word: "BOS.tea", "goalnum.cor",
 * "7.num"). The cartridge shows the same events in boxes; this says them. The clips are the player's
 * own: the file is found beside the program (loose, or on the NHL 95 PC disc image, any ripping, by
 * Overbar's cdimage reader), never shipped. 95 PC has the expansion teams (Anaheim, Florida), so its
 * voice knows every team of this game.
 *
 * What is said (the wording is 95 PC's: SayGoal, SayPenalty, SayPenaltyShot, SayPlayerNumber and
 * SayOneMinuteLeft in NHL95PC src/front/announcer):
 *   goal          "<team> goal number <n>" and "assisted by number <n>", "and number <n>"
 *   penalty       "<team> penalty on number <n>, 2 minutes, <name>" ("and number <n>" for another of the
 *                 same team's stoppage), then "at <minutes> <seconds>" after the last
 *   penalty shot  "<team> penalty shot number <n>, at <minutes> <seconds>"
 *   last minute   "one minute left"
 *   three stars   "first star: <team> number <n>" ...
 * The clips are 95 PC's rink clips: the ones its mixer plays through the 4-bit Fibonacci table (the
 * speech clips, .INT .BAR .HOM .AWA .RNK, are 47 FB packed and are not used). None of this is in the
 * game: it only adds sound, so states, replays and online play are the same with it on or off. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include "game.h"
#include "rom.h"
#include "rosters.h"
#include "native.h"
#include "announcer.h"
#include "overbar/cdimage.h"

#define RATE 11025                          /* the clips' rate (the mixer plays 95 PC's speech at 0x2B11) */
#define GAIN 0.9f
#define QUEUE 48                            /* clips waiting */
#define WALK_DEPTH 3

struct clip { char name[16]; int16_t *pcm; long len; };

static struct {
    SDL_atomic_t on, found, searched;       /* the row; the clips are read; the search ended */
    SDL_mutex *lock;
    struct clip *clips;                     /* sorted by name (upper case) */
    int nclips;
    struct { const struct clip *c; double at; } v[2];   /* the two channels the clips alternate on (95 PC's speechchannel) */
    int ch;                                 /* the channel the last clip started on */
    double wait;                            /* source samples until the next queued clip starts (SpeechTimerTick's delay) */
    const struct clip *queue[QUEUE];
    int nq;
    int log;                                /* ANNOUNCER_LOG: the clips said, to stderr */
    int last_pen_team;                      /* the team of the last penalty said in this stoppage, -1 none */
} A;

/* ---- the archive: C0 FB, a big-endian count at +4, then per clip a 24-bit offset, a 24-bit size and the name ---- */
static uint32_t be24(const uint8_t *p) { return (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

static int clip_cmp(const void *a, const void *b) { return strcmp(((const struct clip *)a)->name, ((const struct clip *)b)->name); }

/* word_D7414: the 4-bit deltas the mixer expands a rink clip with (low nibble, then high; the sum starts at 0) */
static const int8_t fib[16] = { -34, -21, -13, -8, -5, -3, -2, -1, 1, 2, 3, 5, 8, 13, 21, 34 };

/* A speech clip (Ron Barr's: 47 FB): byte pair coding (UnpackBytePair of NHL 95 PC) of a delta coded 8 bit signed sample (a 5 byte
 * header, then each byte the running sum so far, as ReadSpeechSample undoes it). Out: the samples, their number; NULL when it is not one. */
struct bp { uint8_t st[256], src[256], dst[256]; uint8_t *out; long n, cap; };
static void bp_put(struct bp *b, uint8_t c) { if (b->n < b->cap) b->out[b->n++] = c; }
static void bp_expand(struct bp *b, uint8_t c, int depth)
{
    while (b->st[c] && depth < 300) { bp_expand(b, b->src[c], depth + 1); c = b->dst[c]; depth++; }
    bp_put(b, c);
}
static int8_t *speech_clip(const uint8_t *s, long n, long *count)
{
    if (n < 16 || s[0] != 0x47 || s[1] != 0xFB) return NULL;
    long p = 5;
    long total = (long)be24(s + p);
    p += 3;
    if (total < 6 || total > 1 << 22) return NULL;
    struct bp *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    b->out = malloc((size_t)total + 8);
    b->cap = total;
    if (!b->out) { free(b); return NULL; }
    b->st[s[p++]] = 1;
    int pairs = s[p++];
    for (int i = 0; i < pairs && p + 3 <= n; i++, p += 3) { b->src[s[p]] = s[p + 1]; b->dst[s[p]] = s[p + 2]; b->st[s[p]] = 0xFF; }
    while (p < n && b->n < total) {
        uint8_t c = s[p++];
        if (!b->st[c]) bp_put(b, c);
        else if (b->st[c] == 1) { if (p >= n || !s[p]) break; bp_put(b, s[p++]); }
        else { bp_expand(b, b->src[c], 0); bp_expand(b, b->dst[c], 0); }
    }
    int8_t *pcm = NULL;
    if (b->n > 5) {
        pcm = malloc((size_t)b->n - 5);
        uint8_t acc = 0;
        for (long i = 0; pcm && i < b->n - 5; i++) { uint8_t v = b->out[i + 5]; pcm[i] = (int8_t)(uint8_t)(v + acc); acc = (uint8_t)(acc + v); }
        *count = b->n - 5;
    }
    free(b->out);
    free(b);
    return pcm;
}

/* the archive's clips into A.clips (more than one archive adds up); the number of them */
static int parse(const uint8_t *d, long n)
{
    if (n < 12) return 0;
    int count = d[0] == 0xC0 && d[1] == 0xFB ? be16(d + 4) : be16(d + 2);
    if (count < 1 || count > 1000) return 0;
    struct clip *cl = calloc((size_t)count + (size_t)A.nclips, sizeof *cl);
    if (!cl) return 0;
    int k = 0;
    if (A.nclips) { memcpy(cl, A.clips, (size_t)A.nclips * sizeof *cl); k = A.nclips; free(A.clips); A.clips = NULL; A.nclips = 0; }
    long o = 6;
    for (int i = 0; i < count && o + 7 <= n; i++) {
        uint32_t off = be24(d + o), size = be24(d + o + 3);
        o += 6;
        char name[16];
        int len = 0;
        while (o < n && d[o] && len < 15) name[len++] = (char)toupper(d[o++]);
        if (o >= n || d[o]) break;                       /* (a name too long, or cut) */
        name[len] = 0;
        o++;
        if (!size || off >= (uint32_t)n || size > (uint32_t)n - off) continue;
        if (d[off] == 0x47) {                              /* 'G' (47 FB): a speech clip, packed */
            long sn = 0;
            int8_t *sp = speech_clip(d + off, size, &sn);
            if (!sp) continue;
            int16_t *sc = malloc((size_t)sn * sizeof *sc);
            if (!sc) { free(sp); continue; }
            for (long j = 0; j < sn; j++) sc[j] = (int16_t)(sp[j] * 256);
            free(sp);
            memcpy(cl[k].name, name, (size_t)len + 1);
            cl[k].pcm = sc;
            cl[k].len = sn;
            k++;
            continue;
        }
        int16_t *pcm = malloc((size_t)size * 2 * sizeof *pcm);
        if (!pcm) continue;
        int acc = 0;
        for (uint32_t j = 0; j < size; j++) {
            uint8_t b = d[off + j];
            acc = (int8_t)(acc + fib[b & 15]);
            pcm[2 * j] = (int16_t)(acc * 256);
            acc = (int8_t)(acc + fib[b >> 4]);
            pcm[2 * j + 1] = (int16_t)(acc * 256);
        }
        memcpy(cl[k].name, name, (size_t)len + 1);
        cl[k].pcm = pcm;
        cl[k].len = (long)size * 2;
        k++;
    }
    if (!k) { free(cl); return 0; }
    qsort(cl, (size_t)k, sizeof *cl, clip_cmp);
    A.clips = cl;
    A.nclips = k;
    return 1;
}

/* ---- finding the file: loose beside the program, else on a disc image ---- */
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

#define PATHMAX 1024
#define LISTMAX 2048
static int ends(const char *s, const char *e)
{
    size_t a = strlen(s), b = strlen(e);
    return a >= b && ieq(s + a - b, e);
}

/* the files below dir (depth folders down) into list: how many */
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

/* the VIV under dir: loose, else in a disc image's data track (the disc's file may sit in a folder) */
static int find_in(const char *dir)
{
    static char list[LISTMAX][PATHMAX];
    int n = walk(dir, WALK_DEPTH, list, 0);
    for (int i = 0; i < n; i++)
        if (ieq(base_name(list[i]), "XBRUCE2.VIV")) {
            long size;
            uint8_t *b = slurp(list[i], &size);
            int ok = b && parse(b, size);
            free(b);
            if (ok) {
                char *bar = strdup(list[i]);                   /* (Ron Barr's, beside it, for the highlight lines) */
                if (bar) {
                    char *nm = (char *)base_name(bar);
                    memcpy(nm, "XRBARR2.VIV", 12);
                    uint8_t *rb = slurp(bar, &size);
                    if (rb) parse(rb, size);
                    free(rb);
                    free(bar);
                }
                fprintf(stderr, "Rink announcer: %s (%d clips)\n", list[i], A.nclips);
                return 1;
            }
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
                if (ieq(base_name(ob_cd_file_name(cd, f)), "XBRUCE2.VIV")) {
                    uint8_t *b;
                    long size = ob_cd_read_file(cd, ob_cd_file_name(cd, f), &b);
                    int ok = size > 0 && parse(b, size);
                    free(b);
                    if (ok)
                        for (int f2 = 0; ob_cd_file_name(cd, f2); f2++)
                            if (ieq(base_name(ob_cd_file_name(cd, f2)), "XRBARR2.VIV")) {
                                uint8_t *rb;
                                long rs = ob_cd_read_file(cd, ob_cd_file_name(cd, f2), &rb);
                                if (rs > 0) parse(rb, rs);
                                free(rb);
                                break;
                            }
                    ob_cd_close(cd);
                    if (ok) { fprintf(stderr, "Rink announcer: %s (%d clips)\n", list[i], A.nclips); return 1; }
                    cd = NULL;
                    break;
                }
            if (cd) ob_cd_close(cd);
        }
    return 0;
}

static void hook(int ev, struct Team *tm, int player, int a, int b);

static int finder(void *dir)
{
    if (SDL_AtomicGet(&A.searched)) { SDL_free(dir); return 0; }   /* (a file given on the command line) */
    if (!find_in((const char *)dir) && !find_in(".")) fprintf(stderr, "Rink announcer: no NHL 95 PC disc image found beside the program\n");
    SDL_MemoryBarrierRelease();
    SDL_AtomicSet(&A.found, A.nclips > 0);
    SDL_AtomicSet(&A.searched, 1);
    if (SDL_getenv("ANNOUNCER_HLTEST") && A.nclips) { SDL_AtomicSet(&A.on, 1); hook(PA_HIGHLIGHT, NULL, 0, 11, 1); }   /* (a test: the highlight line at once) */
    SDL_free(dir);
    return 0;
}

int announcer_load_file(const char *path)
{
    long size;
    uint8_t *b = slurp(path, &size);
    int ok = b && parse(b, size);
    free(b);
    if (ok) {                                           /* (Ron Barr's, beside it, for the highlight lines) */
        char *bar = strdup(path);
        if (bar && strlen(bar) >= 11 && ieq(bar + strlen(bar) - 11, "XBRUCE2.VIV")) {
            memcpy(bar + strlen(bar) - 11, "XRBARR2.VIV", 11);
            uint8_t *rb = slurp(bar, &size);
            if (rb) parse(rb, size);
            free(rb);
        }
        free(bar);
    }
    SDL_AtomicSet(&A.found, ok);
    SDL_AtomicSet(&A.searched, 1);
    if (ok && SDL_getenv("ANNOUNCER_HLTEST")) { SDL_AtomicSet(&A.on, 1); hook(PA_HIGHLIGHT, NULL, 0, 11, 1); }   /* (a test: the highlight line at once) */
    return ok;
}


void announcer_start(void)
{
    static int started;
    if (started) return;
    started = 1;
    A.lock = SDL_CreateMutex();
    A.log = SDL_getenv("ANNOUNCER_LOG") != NULL;
    A.last_pen_team = -1;
    pa_hook = hook;
    char *base = SDL_GetBasePath();
    SDL_Thread *t = SDL_CreateThread(finder, "announcer", base ? SDL_strdup(base) : SDL_strdup("."));
    if (base) SDL_free(base);
    if (t) SDL_DetachThread(t);
}

void announcer_wait(void) { while (!SDL_AtomicGet(&A.searched)) SDL_Delay(5); }
int announcer_searched(void) { return SDL_AtomicGet(&A.searched); }
int announcer_found(void) { return SDL_AtomicGet(&A.searched) && SDL_AtomicGet(&A.found); }
void announcer_set(int on) { SDL_AtomicSet(&A.on, on ? 1 : 0); if (!on) announcer_stop(); }

int announcer_active(void) { return SDL_AtomicGet(&A.on) && SDL_AtomicGet(&A.found) && g.demoflag; }

void announcer_stop(void)
{
    if (!A.lock) return;
    SDL_LockMutex(A.lock);
    A.v[0].c = A.v[1].c = NULL;
    A.nq = 0;
    SDL_UnlockMutex(A.lock);
}

/* ---- what is said ---- */
static const struct clip *find(const char *name)
{
    char up[16];
    int i = 0;
    for (; name[i] && i < 15; i++) up[i] = (char)toupper((unsigned char)name[i]);
    up[i] = 0;
    struct clip key;
    memcpy(key.name, up, (size_t)i + 1);
    return bsearch(&key, A.clips, (size_t)A.nclips, sizeof *A.clips, clip_cmp);
}

/* add the clip to the queue (a clip the bank lacks is skipped) */
static void add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void add(const char *fmt, ...)
{
    char name[24];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(name, sizeof name, fmt, ap);
    va_end(ap);
    const struct clip *c = find(name);
    if (A.log) fprintf(stderr, "announcer: %s%s\n", name, c ? "" : " (not in the bank)");
    if (c && A.nq < QUEUE) A.queue[A.nq++] = c;
}

/* the 95 PC abbreviation of each of the cartridge's teams, in its order (the art's: a roster set's team plays as one of them) */
static const char *const abbr95[28] = {
    "ANA", "BOS", "BUF", "CGY", "CHI", "DAL", "DET", "EDM", "FLA", "HFD", "LA", "MTL", "NJ", "NYI",
    "NYR", "OTT", "PHI", "PIT", "QUE", "SJ", "STL", "TB", "TOR", "VAN", "WSH", "WPG", "ASE", "ASW",
};

static const char *team_abbr(struct Team *tm)
{
    uint16_t t = rosters_art(tm->tmteam);
    return t < 28 ? abbr95[t] : NULL;
}

/* the player's number (the roster keeps it as BCD) */
static int jersey(int player, struct Team *tm)
{
    const struct RosterPlayer *p = (const struct RosterPlayer *)(const void *)GetPlayerNamePointer((uint16_t)player, tm);
    return (p->number >> 4) * 10 + (p->number & 15);
}

/* "at <minutes> <seconds>" (QueueTimeClips: 1 minute and 1 second have clips of their own; seconds after minutes are two digits) */
static void say_time(int min, int sec)
{
    add("at.cor");
    add("pause.cor");
    if (min) {
        if (!sec) { if (min == 1) add("1minute.cor"); else { add("%d.num", min); add("minutes.cor"); } }
        else add("%d.num", min);
    }
    if (sec == 1) { if (!min) add("1second.cor"); else add("%02d.num", sec); }
    else if (sec > 1) {
        if (!min) { add("%d.num", sec); add("seconds.cor"); }
        else add("%02d.num", sec);
    }
}

static const char *pen_clip(int pen)
{
    switch (pen) {
    case 0x12: case 0x1A: return "roughing";
    case 0x16: return "charging";
    case 0x18: return "slashing";
    case 0x1C: return "xcheck";
    case 0x1E: return "hooking";
    case 0x20: return "tripping";
    case 0x22: return "interfer";
    case 0x24: return "holding";
    }
    return NULL;                                        /* (fighting, the instigator: 95 PC has no clip) */
}

/* a new announcement: a goal and a penalty shot take over from what is being said; the rest wait their turn (or are dropped when
 * the voice is busy, as 95 PC's SpeechIdle) */
static int begin(int interrupt, int drop_if_busy)
{
    if (A.v[0].c || A.v[1].c || A.nq) {
        if (interrupt) { A.v[0].c = A.v[1].c = NULL; A.nq = 0; }
        else if (drop_if_busy) return 0;
    }
    return 1;
}

static void hook(int ev, struct Team *tm, int player, int a, int b)
{
    if (A.log) fprintf(stderr, "announcer: event %d (on %d, found %d, demo %d)\n", ev, SDL_AtomicGet(&A.on), SDL_AtomicGet(&A.found), g.demoflag);
    if (!SDL_AtomicGet(&A.on) || !SDL_AtomicGet(&A.found) || (!g.demoflag && ev != PA_HIGHLIGHT)) return;                 /* (a demo game is silent: demoflag is clear then) */
    SDL_MemoryBarrierAcquire();
    SDL_LockMutex(A.lock);
    const char *abbr = tm ? team_abbr(tm) : NULL;
    int away = tm == team_struct(1);
    switch (ev) {
    case PA_GOAL:                                       /* tm the scorer's team, player him, a and b the assists (-1 none) */
        if (!abbr || !begin(1, 0)) break;
        A.last_pen_team = -1;
        add("%s.tea", abbr);
        add("goalnum.cor");
        add("%d.num", jersey(player, tm));
        if (a >= 0) { add("pause.cor"); add("asstnum.cor"); add("%d.num", jersey(a, tm)); }
        if (b >= 0) { add("pause.cor"); add("andnum.cor"); add("%d.num", jersey(b, tm)); }
        break;
    case PA_PENALTY: {                                  /* a the penalty, b its minutes | remaining penalties << 8 | the next one is this team's << 16 */
        int mins = b & 0xFF, remaining = b >> 8 & 0xFF, next_same = b >> 16 & 1;
        if (!abbr || !begin(0, 0)) break;
        int idx = A.last_pen_team == away ? 2 : 1;
        A.last_pen_team = away;
        if (idx == 1) {
            add("%s.tea", abbr);
            add(next_same ? "pensnum.cor" : "pennum.cor");
        } else add("andnum.cor");
        add("%d.num", jersey(player, tm));
        add("pause.cor");
        if (mins == 4) { add("4.num"); add("minutes.cor"); }   /* (a double minor, RULE_PENALTIES: 95 PC has no clip of its own) */
        else add(mins == 5 ? "5min.cor" : "2min.cor");
        const char *pc = pen_clip(a);
        if (pc) add("%s.pen", pc);
        add("pause.cor");
        if (!remaining) { say_time(g.gameclock / 60, g.gameclock % 60); A.last_pen_team = -1; }
        break;
    }
    case PA_PENSHOT:                                    /* tm the shooter's team, player him */
        if (!abbr || !begin(1, 0)) break;
        A.last_pen_team = -1;
        add("%s.tea", abbr);
        add("penshot.cor");
        add("%d.num", jersey(player, tm));
        add("pause.cor");
        say_time(g.gameclock / 60, g.gameclock % 60);
        break;
    case PA_ONEMIN:
        if (!begin(0, 1)) break;
        add("oneleft.cor");
        break;
    case PA_HIGHLIGHT: {                                /* a the home team, b the away team: PaHighlightIntro's eight clips */
        uint16_t h = rosters_art((uint16_t)a), v = rosters_art((uint16_t)b);
        if (h >= 28 || v >= 28 || !begin(1, 0)) break;
        add("takeynow.bar");
        add("%s.rnk", abbr95[h]);
        add("highlite.bar");
        add("of.bar");
        add("gamebtwn.bar");
        add("%s.awa", abbr95[v]);
        add("and.bar");
        add("%s.hom", abbr95[h]);
        break;
    }
    case PA_STAR:                                       /* a the star, 0-2: "first star", "<team> number <n>" */
        if (!abbr) break;
        add("%s", a == 0 ? "1ststar.cor" : a == 1 ? "2ndstar.cor" : "3rdstar.cor");
        add("%s.frm", abbr);
        add("pause.cor");
        add("number.cor");
        add("%d.num", jersey(player, tm));
        break;
    }
    SDL_UnlockMutex(A.lock);
}

/* ---- the voice into the mix ---- */
void announcer_mix(int16_t *out, int frames, unsigned rate)
{
    if (!A.lock || !rate || !SDL_AtomicGet(&A.found)) return;
    SDL_LockMutex(A.lock);
    double step = (double)RATE / rate;
    for (int i = 0; i < frames; i++) {
        /* 95 PC's SpeechTimerTick: the next clip starts when the delay of the one before has run out, which is that clip's length
         * less 26 ticks of 1/100 s (OpenSpeechBank: size * 100 / 5512 - 1Ah): it starts over the tail of the last, on the other channel */
        while (A.nq && A.wait <= 0) {
            const struct clip *c = A.queue[0];
            memmove(A.queue, A.queue + 1, (size_t)--A.nq * sizeof A.queue[0]);
            A.ch ^= 1;
            A.v[A.ch].c = c;
            A.v[A.ch].at = 0;
            if (A.log) fprintf(stderr, "announcer: plays %s at %u Hz\n", c->name, rate);
            long ticks = (long)(c->len / 2) * 100 / 5512 - 26;
            A.wait = ticks > 0 ? (double)ticks * RATE / 100.0 : 0;
        }
        if (!A.v[0].c && !A.v[1].c && !A.nq) { A.wait = 0; break; }
        float sum = 0;
        for (int k = 0; k < 2; k++) {
            if (!A.v[k].c) continue;
            long j = (long)A.v[k].at;
            if (j + 1 >= A.v[k].c->len) { A.v[k].c = NULL; continue; }
            double frac = A.v[k].at - j;
            sum += (float)(A.v[k].c->pcm[j] + (A.v[k].c->pcm[j + 1] - A.v[k].c->pcm[j]) * frac) * GAIN;
            A.v[k].at += step;
        }
        if (A.nq && A.wait > 0) A.wait -= step;
        for (int c = 0; c < 2; c++) {
            float x = sum + out[2 * i + c];
            out[2 * i + c] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
        }
    }
    SDL_UnlockMutex(A.lock);
}
