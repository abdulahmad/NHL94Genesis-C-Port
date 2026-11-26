/* NHL 95 PC's national anthems (the Game page's "National anthems").
 *
 * The PC game plays "O Canada" and the "Star Spangled Banner" before the first face off as AdLib songs: USA.KMS and
 * CANADA.KMS (EACSNDF sequences, a .CFG each for the tracks' channels), played through FM timbres (PCFF004.PAT maps a
 * program to a timbre of PCFF00n.TIM). Nothing of this is in the cartridge. The files are the player's own (loose beside
 * the program or on the NHL 95 PC disc image), never shipped. The sequencer follows NHL 95 PC's (sub_99925: a 100 Hz
 * timer, 32000 / tempo ticks to a step) as NHL 9X Studio's lib/audio/pc94-player.js has it; the sound is made by
 * Nuked OPL3 (LGPL 2.1+, nuked_opl3.c). Both anthems are rendered once, when the files are found; nothing of the game
 * is touched but its wait before the first face off (src/hold.c) and its own songs, which stay quiet while an anthem
 * plays. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "game.h"
#include "native.h"
#include "pcsong.h"
#include "nuked_opl3.h"
#include "overbar/cdimage.h"

#define RATE 49716
#define TIMER_HZ 100
#define NOTE_OFFSET 24
#define MAXSEC 90

static struct {
    SDL_atomic_t on, found, searched;
    SDL_mutex *lock;
    int16_t *pcm[2];                          /* USA, CANADA: stereo 16 bit at RATE */
    long len[2];                              /* in frames */
    int playing, which;
    double at;
} S;

/* ---- the patch map and the FM timbres ----------------------------------------------------------- */

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

struct fm { int ok; uint8_t car[5], mod[5], fb; };
static struct { int ok; struct fm fm; int transpose; } ins[256];


/* a sound table's item number `id16` (the last two bytes of an id whose first is 0x80): its bytes, 0 when none */
static const uint8_t *tim_item(const uint8_t *d, long n, int id16, long *len)
{
    if (n < 6) return NULL;
    unsigned count = (unsigned)(d[4] | d[5] << 8);
    long base = 6 + 8L * count;
    if (!count || base > n) return NULL;
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *id = d + 6 + 4 * i;
        if (id[0] != 0x80 || (id[3] | id[2] << 8) != id16) continue;
        uint32_t off = le32(d + 6 + 4 * count + 4 * i), end = (uint32_t)(n - base);
        if (off == 0xFFFFFFFFu) return NULL;
        for (unsigned k = 0; k < count; k++) {
            uint32_t o = le32(d + 6 + 4 * count + 4 * k);
            if (o > off && o != 0xFFFFFFFFu && o < end) end = o;
        }
        if (base + (long)end > n || off >= end) return NULL;
        *len = (long)(end - off);
        return d + base + off;
    }
    return NULL;
}

static void instruments(const uint8_t *pat, long pn, const uint8_t *const *tim, const long *tn)
{
    memset(ins, 0, sizeof ins);
    if (pn < 0x102 + 20) return;
    for (int prog = 0; prog < 256; prog++) {
        int ri = pat[2 + prog];
        if (!ri || 0x102 + 20 * (ri + 1) > pn) continue;
        const uint8_t *rec = pat + 0x102 + 20 * ri;
        int bank = rec[0];
        ins[prog].ok = 1;
        ins[prog].transpose = (int8_t)rec[7];
        if (bank < 0 || bank > 3 || !tim[bank]) continue;
        long len = 0;
        const uint8_t *t = tim_item(tim[bank], tn[bank], rec[1], &len);
        if (!t || len < 13 || t[0] != 4) continue;
        ins[prog].fm.ok = 1;
        memcpy(ins[prog].fm.car, t + 2, 5);
        memcpy(ins[prog].fm.mod, t + 7, 5);
        ins[prog].fm.fb = t[12];
    }
}

/* ---- the OPL3 as a MIDI-like voice bank --------------------------------------------------------- */

static const int MOD_OP[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };
static const int CAR_OP[9] = { 3, 4, 5, 11, 12, 13, 19, 20, 21 };
static const int FNUM12[12] = { 0x56, 0x5b, 0x60, 0x66, 0x6c, 0x72, 0x79, 0x80, 0x88, 0x90, 0x99, 0xa2 };

struct synth { opl3_chip chip; int prog[16]; int blocklo[9]; };

static void wr(struct synth *s, int reg, int v) { OPL3_WriteReg(&s->chip, (uint16_t)reg, (uint8_t)v); }

static void write_op(struct synth *s, int op, const uint8_t r[5])
{
    wr(s, 0x20 + op, r[0]); wr(s, 0x40 + op, r[1]); wr(s, 0x60 + op, r[2]); wr(s, 0x80 + op, r[3]); wr(s, 0xE0 + op, r[4]);
}

static void apply(struct synth *s, int v, const struct fm *f)
{
    static const uint8_t def[5] = { 0x01, 0x00, 0xF0, 0x77, 0 };
    write_op(s, MOD_OP[v], f && f->ok ? f->mod : def);
    write_op(s, CAR_OP[v], f && f->ok ? f->car : def);
    wr(s, 0xC0 + v, f && f->ok ? f->fb : 0x30);
}

static void synth_init(struct synth *s)
{
    memset(s, 0, sizeof *s);
    OPL3_Reset(&s->chip, RATE);
    wr(s, 0x01, 0x20); wr(s, 0xBD, 0); wr(s, 0x08, 0);
    for (int v = 0; v < 9; v++) apply(s, v, NULL);
}

static int voice_of(int ch) { return ch == 9 ? 8 : ch % 9; }

static void midi(struct synth *s, int st, int a, int b)
{
    int cmd = st & 0xF0, ch = st & 15, v = voice_of(ch);
    if (cmd == 0xC0) {
        s->prog[ch] = a;
        apply(s, v, ins[a & 255].ok ? &ins[a & 255].fm : NULL);
    } else if (cmd == 0x90 && b) {
        int note = a, prog = ch == 9 ? (note + 0x5C) & 255 : s->prog[ch] & 255;
        if (ch == 9 && ins[prog].ok && ins[prog].fm.ok) apply(s, v, &ins[prog].fm);
        if (ins[prog].ok) { note += ins[prog].transpose; if (note < 0) note = 0; if (note > 127) note = 127; }
        int block = note / 12, fnum = FNUM12[note % 12];
        if (block > 7) { fnum <<= block - 7; if (fnum > 0x3FF) fnum = 0x3FF; block = 7; }
        int hi = (block & 7) << 2 | (fnum >> 8 & 3);
        s->blocklo[v] = hi;
        wr(s, 0xA0 + v, fnum & 255);
        wr(s, 0xB0 + v, hi | 0x20);
    } else if (cmd == 0x80 || (cmd == 0x90 && !b)) {
        wr(s, 0xB0 + v, s->blocklo[v] & ~0x20);
    }
}

/* ---- the EACSNDF sequence ----------------------------------------------------------------------- */

struct ev { int cmd, a1; uint32_t a2; int len, delta; };

static int vlq(const uint8_t *d, long n, long *p)
{
    int v = 0;
    for (int k = 0; k < 5 && *p < n; k++) { int b = d[(*p)++]; v = v << 7 | (b & 0x7F); if (!(b & 0x80)) break; }
    return v;
}

static struct ev decode(const uint8_t *d, long n, long p)
{
    struct ev e = { 0, 0, 0, 0, 0 };
    long start = p;
    e.delta = vlq(d, n, &p);
    e.cmd = p < n ? d[p++] : 0xD9;
    int c = e.cmd;
    if (c < 0xD9) {
        if (c > 0x80 && p < n) e.a1 = d[p++];
        e.a2 = (uint32_t)vlq(d, n, &p);
    } else if (c == 0xD9 || c == 0xDA || c == 0xDB || c == 0xE3) {
    } else if (c == 0xDF) {
        if (p + 1 < n) { e.a1 = d[p]; e.a2 = d[p + 1]; }
        p += 2;
    } else if (c == 0xE5) {
        if (p + 1 < n) e.a2 = (uint32_t)(d[p] | d[p + 1] << 8);
        p += 2;
    } else if (c == 0xE6) {
        if (p + 4 < n) { e.a1 = d[p]; e.a2 = le32(d + p + 1); }
        p += 5;
    } else if (c == 0xE7 || c == 0xE8) {
        int k = p < n ? d[p] : 0;
        p += 1 + k;
    } else if (c <= 0xEA) {
        if (p < n) e.a1 = d[p];
        p++;
    }
    e.len = (int)(p - start);
    return e;
}

struct track {
    long pos, start;
    int delta, acc, rate, chan, vel, loopdepth, calldepth, looped, active;
    struct { long pos; int n; } loops[8];
    long calls[8];
};
struct note { int t, chan, note, left; };
struct seq {
    const uint8_t *d;
    long n;
    int ntracks, loopcount;
    struct track t[16];
    struct note notes[32];
    int nnotes;
    struct synth *syn;
};

static void seq_init(struct seq *q, const uint8_t *k, long kn, const uint8_t *cfg, long cn, struct synth *syn)
{
    memset(q, 0, sizeof *q);
    q->d = k; q->n = kn; q->syn = syn;
    q->ntracks = k[6] | k[7] << 8;
    if (q->ntracks > 16) q->ntracks = 16;
    int rate = 32000 / (k[1] ? k[1] : 1);
    for (int i = 0; i < q->ntracks; i++) {
        struct track *t = &q->t[i];
        const uint8_t *rec = cn >= 8 + 16 * (i + 1) ? cfg + 8 + 16 * i : NULL;
        int mask = rec ? rec[0] | rec[1] << 8 : 1 << i, chan = 0;
        for (int b = 0; b < 16; b++) if (mask >> b & 1) { chan = b; break; }
        t->start = t->pos = k[8 + 2 * i] | k[9 + 2 * i] << 8;
        t->rate = rate; t->chan = chan; t->active = 1;
        midi(syn, 0xC0 | (chan & 15), rec ? rec[0x0C] : 0, 0);
    }
}

static int seq_done(const struct seq *q)
{
    for (int i = 0; i < q->ntracks; i++) if (q->t[i].active) return 0;
    return 1;
}

static void execute(struct seq *q, struct track *t, const struct ev *e)
{
    int c = e->cmd;
    if (c < 0xD9) {
        int vel = c > 0x80 ? e->a1 : t->vel, note = c & 0x7F;
        if (q->nnotes >= 32) return;
        int played = (note + NOTE_OFFSET) & 0x7F;
        q->notes[q->nnotes++] = (struct note){ (int)(t - q->t), t->chan & 15, played, e->a2 ? (int)e->a2 : 1 };
        midi(q->syn, 0x90 | (t->chan & 15), played, vel);
    } else if (c == 0xD9 || c == 0xDA) {
        if (t->calldepth) t->pos = t->calls[--t->calldepth]; else t->active = 0;
    } else if (c == 0xDB) {
        t->looped++;
        if ((t - q->t) == 0) q->loopcount++;
        t->active = 0;                                        /* (one pass: loops = 1) */
    } else if (c == 0xDC) {
        midi(q->syn, 0xC0 | (t->chan & 15), e->a1, 0);
    } else if (c == 0xDD) {
        int rate = 32000 / (e->a1 ? e->a1 : 1);
        for (int i = 0; i < q->ntracks; i++) q->t[i].rate = rate;
    } else if (c == 0xE2) {
        if (t->loopdepth < 8) { t->loops[t->loopdepth].pos = t->pos; t->loops[t->loopdepth].n = (e->a1 - 1) & 255; t->loopdepth++; }
    } else if (c == 0xE3) {
        if (t->loopdepth) {
            int top = t->loopdepth - 1, n = t->loops[top].n;
            t->pos = t->loops[top].pos;
            t->loops[top].n = (n - 1) & 255;
            if (n == 0) t->loopdepth--;
        }
    } else if (c == 0xE4) {
        t->vel = e->a1;
    } else if (c == 0xE6) {
        if (t->calldepth < 8) { t->calls[t->calldepth++] = t->pos; t->pos = (long)e->a2 + 4; }
    }
}

static void tick(struct seq *q, struct track *t)
{
    for (int i = q->nnotes - 1; i >= 0; i--) {
        struct note *n = &q->notes[i];
        if (&q->t[n->t] != t) continue;
        if (--n->left <= 0) {
            midi(q->syn, 0x80 | n->chan, n->note, 0);
            memmove(n, n + 1, (size_t)(q->nnotes - i - 1) * sizeof *n);
            q->nnotes--;
        }
    }
    if (t->delta) { t->delta--; return; }
    for (int guard = 0; guard < 4096; guard++) {
        if (t->pos < 0 || t->pos >= q->n) { t->active = 0; return; }
        struct ev e = decode(q->d, q->n, t->pos);
        t->pos += e.len;
        execute(q, t, &e);
        if (!t->active) return;
        if (t->pos >= q->n) { t->active = 0; return; }
        t->delta = decode(q->d, q->n, t->pos).delta;
        if (t->delta) { t->delta--; return; }
    }
}

static void interrupt(struct seq *q)
{
    for (int i = 0; i < q->ntracks; i++) {
        struct track *t = &q->t[i];
        if (!t->active) continue;
        t->acc += 0x80;
        while (t->acc >= t->rate && t->active) { tick(q, t); t->acc -= t->rate; }
    }
}

/* a song rendered whole: stereo 16 bit at RATE, its length in frames (0: not a song) */
static int16_t *render(const uint8_t *k, long kn, const uint8_t *cfg, long cn, long *frames)
{
    static struct synth syn;
    static struct seq q;
    if (kn < 0x28 || k[0] != 1 || (k[6] | k[7] << 8) < 1 || (k[6] | k[7] << 8) > 16) return NULL;
    synth_init(&syn);
    seq_init(&q, k, kn, cfg, cn, &syn);
    long cap = (long)MAXSEC * RATE, n = 0;
    int16_t *out = malloc((size_t)cap * 2 * sizeof *out);
    if (!out) return NULL;
    int spt = RATE / TIMER_HZ;
    for (int t = 0; t < MAXSEC * TIMER_HZ && (!seq_done(&q) || t < 8); t++) {
        interrupt(&q);
        for (int i = 0; i < spt && n < cap; i++, n++) OPL3_Generate(&syn.chip, out + 2 * n);
    }
    for (int i = 0; i < q.nnotes; i++) midi(&syn, 0x80 | q.notes[i].chan, q.notes[i].note, 0);
    for (int i = 0; i < RATE / 2 && n < cap; i++, n++) OPL3_Generate(&syn.chip, out + 2 * n);      /* (the notes' release) */
    *frames = n;
    return out;
}

/* ---- finding the files ------------------------------------------------------------------------- */

static uint8_t *slurp(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = n > 0 && n < 64L << 20 ? malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    *size = b ? n : 0;
    return b;
}

static uint8_t *get(const char *dir, ob_cd *cd, const char *name, long *n)
{
    *n = 0;
    uint8_t *b = NULL;
    if (cd) { *n = ob_cd_read_file(cd, name, &b); if (*n <= 0) { free(b); b = NULL; *n = 0; } return b; }
    char p[1100];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    return slurp(p, n);
}

static int load(const char *dir, ob_cd *cd)
{
    long pn, tn[4], un, ucn, cn, ccn;
    uint8_t *pat = get(dir, cd, "PCFF004.PAT", &pn), *tim[4], *usa = get(dir, cd, "USA.KMS", &un), *usac = get(dir, cd, "USA.CFG", &ucn),
            *can = get(dir, cd, "CANADA.KMS", &cn), *canc = get(dir, cd, "CANADA.CFG", &ccn);
    for (int b = 0; b < 4; b++) { char nm[16]; snprintf(nm, sizeof nm, "PCFF00%d.TIM", b); tim[b] = get(dir, cd, nm, &tn[b]); }
    int ok = 0;
    if (pat && usa && can) {
        const uint8_t *tp[4] = { tim[0], tim[1], tim[2], tim[3] };
        instruments(pat, pn, tp, tn);
        S.pcm[0] = render(usa, un, usac, ucn, &S.len[0]);
        S.pcm[1] = render(can, cn, canc, ccn, &S.len[1]);
        ok = S.pcm[0] && S.pcm[1];
        if (getenv("PCSONG_LOG")) fprintf(stderr, "pcsong: USA %.1f s, CANADA %.1f s\n", (double)S.len[0] / RATE, (double)S.len[1] / RATE);
    }
    free(pat); free(usa); free(usac); free(can); free(canc);
    for (int b = 0; b < 4; b++) free(tim[b]);
    return ok;
}

static int finder(void *arg)
{
    char *base = arg;
    int ok = load(base, NULL) || load(".", NULL);
    if (!ok) {
        static const char *const need[1] = { "USA.KMS" };
        ob_cd *cd = ob_cd_find(base, need, 1, NULL, 0);
        if (!cd) cd = ob_cd_find(".", need, 1, NULL, 0);
        if (cd) {
            ok = load(NULL, cd);
            if (ok) fprintf(stderr, "PC anthems: NHL 95 PC songs from %s\n", ob_cd_path(cd));
            ob_cd_close(cd);
        }
    }
    if (!ok) fprintf(stderr, "PC anthems: no NHL 95 PC disc image found beside the program\n");
    SDL_MemoryBarrierRelease();
    SDL_AtomicSet(&S.found, ok);
    SDL_AtomicSet(&S.searched, 1);
    SDL_free(arg);
    return 0;
}

/* ---- the game's side ---------------------------------------------------------------------------- */

/* the cartridge's art numbers of the Canadian teams (CGY EDM MTL OTT QUE TOR VAN WPG) */
static int canadian(int art) { return art == 3 || art == 7 || art == 11 || art == 15 || art == 18 || art == 22 || art == 23 || art == 25; }

static int playing_hook(void) { return S.playing; }

/* the first face off's anthem: the home team's. Its length in frames of the game (60 a second) */
static int anthem_hook(int home_art, int away_art)
{
    if (!SDL_AtomicGet(&S.on) || !SDL_AtomicGet(&S.found) || !g.demoflag) return 0;
    SDL_MemoryBarrierAcquire();
    int first = canadian(home_art) ? 1 : 0, second = first;           /* (the home team's anthem only) */
    SDL_LockMutex(S.lock);
    S.which = first | (first != second ? (second + 1) << 1 : 0);     /* (bit 0 the first anthem; bits 1-2: the second, +1) */
    S.playing = 1;
    S.at = 0;
    long total = S.len[first] + (first != second ? S.len[second] : 0);
    SDL_UnlockMutex(S.lock);
    return (int)(total * 60 / RATE) + 30;
}

/* the flag of the anthem playing at a frame of the game since it began (0 USA, 1 Canada; -1 none) */
int pcsong_flag_at(int frame)
{
    if (!SDL_AtomicGet(&S.on) || !SDL_AtomicGet(&S.found) || !S.playing) return -1;
    int first = S.which & 1, second = (S.which >> 1) - 1;
    if (second >= 0 && (long)frame * RATE / 60 >= S.len[first]) return second;
    return first;
}

void pcsong_start(void)
{
    static int started;
    if (started) return;
    started = 1;
    S.lock = SDL_CreateMutex();
    anthem_hook_fn = anthem_hook;
    song_block_hook = playing_hook;
    char *base = SDL_GetBasePath();
    SDL_Thread *t = SDL_CreateThread(finder, "pcsong", base ? SDL_strdup(base) : SDL_strdup("."));
    if (base) SDL_free(base);
    if (t) SDL_DetachThread(t);
}
void pcsong_wait(void) { while (!SDL_AtomicGet(&S.searched)) SDL_Delay(5); }
int pcsong_searched(void) { return SDL_AtomicGet(&S.searched); }
int pcsong_found(void) { return SDL_AtomicGet(&S.searched) && SDL_AtomicGet(&S.found); }
void pcsong_set(int on) { SDL_AtomicSet(&S.on, on ? 1 : 0); }
void pcsong_stop(void) { S.playing = 0; }

/* the anthems into a frame of sound (out: stereo 16 bit at rate, mixed in) */
void pcsong_mix(int16_t *out, int frames, unsigned rate)
{
    if (!S.playing || !S.lock) return;
    SDL_LockMutex(S.lock);
    int first = S.which & 1, second = S.which >> 1 ? (S.which >> 1) - 1 : -1;
    long l0 = S.len[first], total = l0 + (second >= 0 ? S.len[second] : 0);
    double step = (double)RATE / rate;
    for (int i = 0; i < frames; i++) {
        long k = (long)S.at;
        if (k >= total) { S.playing = 0; break; }
        const int16_t *p = k < l0 ? S.pcm[first] + 2 * k : S.pcm[second] + 2 * (k - l0);
        for (int c = 0; c < 2; c++) {
            int v = out[2 * i + c] + p[c];
            out[2 * i + c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        S.at += step;
    }
    SDL_UnlockMutex(S.lock);
}

/* testing: both anthems, USA then CANADA, as a wav (--pc-anthem-wav FILE) */
int pcsong_write_wav(const char *path)
{
    if (!pcsong_found()) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    long frames = S.len[0] + S.len[1];
    uint32_t data = (uint32_t)(frames * 4), hdr[11] = { 0x46464952u, 36 + data, 0x45564157u, 0x20746D66u, 16, 0x00020001u, RATE, RATE * 4, 0x00100004u, 0x61746164u, data };
    fwrite(hdr, 4, 11, f);
    fwrite(S.pcm[0], 4, (size_t)S.len[0], f);
    fwrite(S.pcm[1], 4, (size_t)S.len[1], f);
    fclose(f);
    return 1;
}
