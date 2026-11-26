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
#include "rosters.h"
#include "nuked_opl3.h"
#include "overbar/cdimage.h"

#define RATE 49716
#define TIMER_HZ 100
#define NOTE_OFFSET 24
#define MAXSEC 90

#define RATE96 22050

static struct {
    SDL_atomic_t on, found, searched;         /* on: 0 off, 1 NHL 95 PC's, 2 NHL 96 PC's; found: NHL 95 PC's */
    SDL_atomic_t found96;
    SDL_mutex *lock;
    int16_t *pcm[2];                          /* USA, CANADA: stereo 16 bit at RATE */
    long len[2];                              /* in frames */
    int16_t *p96[2];                          /* NHL 96 PC's: the same at RATE96 */
    long l96[2];
    int playing, which, cur;                  /* cur: the set playing, 0 NHL 95 PC's, 1 NHL 96 PC's */
    double at;
} S;

/* the set an anthem plays from: the one asked for, else the other one found (-1: none) */
static int choose(void)
{
    int on = SDL_AtomicGet(&S.on), f95 = SDL_AtomicGet(&S.found), f96 = SDL_AtomicGet(&S.found96);
    if (!on) return -1;
    if (on == 2 && f96) return 1;
    if (f95) return 0;
    return f96 ? 1 : -1;
}

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

/* ---- NHL 95 PC's rink organ and rock cues at the whistle (the Game page's PC crowd) ------------------------------------
 * LoadRockMusic loads six cues for the home team (teamrockcuetbl, by the PC's team number; 26 and up row 13), three random ones
 * of an 18 cue pool not on that row, the device's rock tune (SBROCKU with music on) and ROCKDITI, all .KMS songs as the anthems
 * are. CrowdOnStoppage (at a whistle for an offside, icing, the puck out of play or frozen) plays PlayCrowdSample: once a period,
 * late in it (clock under half, a minute or more left), the team's cue 2; otherwise one of 8: 1 the rock tune (cue 9), 2-4 a
 * random cue (6-8), 5 ROCKDITI (11), the rest nothing. A team cue the row lacks (FFh) is a random one. The PC's strength
 * difference cheer (cues 4 / 1 with the speech on) and its overlays are left out. */

#define NROCK 54
static const char *const rock_name[NROCK + 1] = { "BOS1", "BOS2", "BOS3", "HART2", "BUF2", "CAL1", "CAL2", "CAL3", "CHIC1", "CHIC2",
    "CHIC3", "DET1", "DET2", "ED1", "HART1", "HART2", "HART3", "LA1", "LA4", "MON1", "MON2", "MON3", "HART2", "NJER2", "LI2", "LI3",
    "NY3", "PHIL1", "PHIL2", "CGY2", "PITTS1", "PITTS2", "PITTS3", "PITTS4", "QU1", "SL1", "SL2", "SL3", "SHK1", "SHK2", "MON1",
    "MON3", "LA4", "SHK7", "SHK8", "BOS2", "TB2", "TOR1", "TOR2", "VAN1", "VAN3", "WASH3", "WINN2", "ROCKDITI", "SBROCKU" };   /* (rockcuefiles; the last: the rock tune) */
/* teamrockcuetbl: six cues by the PC's team (BOS BUF CGY CHI DET EDM HFD LA DAL MTL NJ NYI NYR OTT PHI PIT QUE STL SJ TB TOR VAN WSH WPG ANA FLA) */
static const signed char rock_team[26][6] = {
    { 1, 0, 2, -1, 31, 1 }, { 4, 15, 15, 4, 31, -1 }, { 5, 7, 5, 7, 6, 6 }, { 8, 10, 9, 8, 31, 43 }, { 11, 12, 10, -1, 31, 12 },
    { 13, 1, 1, -1, 31, -1 }, { 14, 16, 15, 16, 31, 14 }, { 17, 17, 10, 7, -1, 18 }, { 1, 24, 24, -1, 31, -1 }, { 12, 21, 12, 20, 31, 19 },
    { 15, 23, 15, -1, 31, 11 }, { 15, 24, 15, 25, 31, 25 }, { 1, 26, 15, -1, 31, -1 }, { 15, 52, 12, -1, 31, -1 }, { 27, 28, 27, 6, 31, 6 },
    { 30, 32, 30, 33, 31, 33 }, { 34, 6, 34, -1, 31, 6 }, { 35, 36, 36, 35, 31, 37 }, { 38, 39, 19, 21, 18, 43 }, { 1, 46, 46, -1, 31, -1 },
    { 47, 47, 48, -1, 31, -1 }, { 49, 38, 50, -1, 31, -1 }, { 12, 51, 12, -1, 31, 12 }, { 15, 52, 12, -1, 31, -1 }, { 17, 47, 50, -1, 6, 43 },
    { 15, 32, 50, -1, 31, 19 } };
static const signed char rock_pool[18] = { 0x10, 0x19, 0x21, 0x2B, 0x2C, 0x35, 0x11, 0x12, 0x13, 0x15, 0x1C, 0x1E, 0x20, 0x25, 0x2E, 0x2F, 0x32, 0x34 };
/* the PC's team number of the cartridge's art number (ANA BOS BUF CGY CHI DAL DET EDM FLA HFD LA MTL NJ NYI NYR OTT PHI PIT QUE SJ STL TB
 * TOR VAN WSH WPG ASE ASW); the All-Star teams use row 13 as the PC's 26 and up do */
static const signed char pc_team_of_art[28] = { 24, 0, 1, 2, 3, 8, 4, 5, 25, 6, 7, 9, 10, 11, 12, 13, 14, 15, 16, 18, 17, 19, 20, 21, 22, 23, 13, 13 };

static struct {
    SDL_atomic_t on, ready, busy;         /* the PC crowd row; the cues of team (rendered); a render running */
    uint8_t *kms[NROCK + 1], *cfg[NROCK + 1];
    long kn[NROCK + 1], cn[NROCK + 1];
    int team, late;                       /* the team rendered for (-1 none); the late period cue played this period */
    int16_t *pcm[12];                     /* the PlayCrowdSample cues: 0-5 the team's, 6-8 random, 9 the rock tune, 11 ROCKDITI */
    long len[12];
    int playing, cue, period;
    double at;
} K = { .team = -1 };

static void rock_read(const char *dir, ob_cd *cd)
{
    for (int i = 0; i <= NROCK; i++) {
        char nm[24];
        snprintf(nm, sizeof nm, "%s.KMS", rock_name[i]);
        K.kms[i] = get(dir, cd, nm, &K.kn[i]);
        snprintf(nm, sizeof nm, "%s.CFG", rock_name[i]);
        K.cfg[i] = get(dir, cd, nm, &K.cn[i]);
    }
}

static int rock_team_of; /* (the render thread's team) */
static int rock_render(void *arg)
{
    (void)arg;
    int t = rock_team_of, cues[12];
    for (int i = 0; i < 12; i++) cues[i] = -1;
    for (int i = 0; i < 6; i++) cues[i] = rock_team[t][i];
    for (int i = 0; i < 3; i++) {                                    /* (three random ones: not on the team's row, not twice) */
        for (int guard = 0; guard < 200; guard++) {
            int c = rock_pool[rand() % 18], ok = 1;
            for (int j = 0; j < 6; j++) if (rock_team[t][j] == c) ok = 0;
            for (int j = 0; j < i; j++) if (cues[6 + j] == c) ok = 0;
            if (ok) { cues[6 + i] = c; break; }
        }
    }
    cues[9] = NROCK;                                                 /* (SBROCKU) */
    cues[11] = 53;                                                   /* (ROCKDITI) */
    SDL_LockMutex(S.lock);
    K.playing = 0;
    SDL_UnlockMutex(S.lock);
    for (int i = 0; i < 12; i++) {
        free(K.pcm[i]); K.pcm[i] = NULL; K.len[i] = 0;
        int c = cues[i];
        if (c < 0 || !K.kms[c]) continue;
        K.pcm[i] = render(K.kms[c], K.kn[c], K.cfg[c], K.cn[c], &K.len[i]);
        if (getenv("PCSONG_LOG")) fprintf(stderr, "pcsong: cue %d %s %.1f s\n", i, rock_name[c], (double)K.len[i] / RATE);
    }
    K.team = t;
    SDL_MemoryBarrierRelease();
    SDL_AtomicSet(&K.ready, 1);
    SDL_AtomicSet(&K.busy, 0);
    return 0;
}

/* the game's whistle (src/penalty.c Stop4Pen, stoppage_hook): the stoppage's penalty number; -1 the drop of the puck, which
 * ends the cue as the PC's Endfaceoff does */
static void stoppage(int pen)
{
    if (pen < 0 && K.playing && S.lock) { SDL_LockMutex(S.lock); K.playing = 0; SDL_UnlockMutex(S.lock); }   /* (the drop: Endfaceoff's StopDigiSample) */
    if (!SDL_AtomicGet(&K.on) || !SDL_AtomicGet(&S.found) || !g.demoflag || !S.lock) return;
    if (pen >= 0 && pen != 0x10 && pen != 0xC && pen != 6 && pen != 8) return;   /* (offside, icing, out of play, frozen: the PC's 1Dh 1Eh 6 8) */
    int t = pc_team_of_art[rosters_art(team_struct(0)->tmteam) & 31];
    if (t < 0 || t > 25) t = 13;
    if (!SDL_AtomicGet(&K.ready) || K.team != t) {                         /* (the home team's cues: rendered on their own thread) */
        if (!SDL_AtomicGet(&K.busy) && pcsong_searched()) {
            SDL_AtomicSet(&K.ready, 0);
            SDL_AtomicSet(&K.busy, 1);
            rock_team_of = t;
            SDL_Thread *th = SDL_CreateThread(rock_render, "pcrock", NULL);
            if (th) SDL_DetachThread(th); else SDL_AtomicSet(&K.busy, 0);
        }
        return;
    }
    if (pen < 0) return;                                             /* (a drop: only the home team's cues readied, by the first one) */
    SDL_MemoryBarrierAcquire();
    if (K.period != (int16_t)g.gsp) { K.period = (int16_t)g.gsp; K.late = 0; }
    int n = -1, clock = (int16_t)g.gameclock, total = (int16_t)g.PerTimeTotal;
    if (!K.late && clock < total / 2 && clock >= 60) { K.late = 1; n = 2; }
    else {
        int r = rand() % 8;
        n = r == 1 ? 9 : r >= 2 && r <= 4 ? r + 4 : r == 5 ? 11 : -1;
    }
    if (n >= 0 && n < 6 && !K.pcm[n]) n = 6 + rand() % 3;                  /* (no such team cue: a random one) */
    if (n < 0 || !K.pcm[n]) return;
    SDL_LockMutex(S.lock);
    K.cue = n; K.at = 0; K.playing = 1;
    SDL_UnlockMutex(S.lock);
    if (getenv("PCSONG_LOG")) fprintf(stderr, "pcsong: whistle %d: cue %d\n", pen, n);
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
        if (ok) rock_read(dir, cd);
    }
    free(pat); free(usa); free(usac); free(can); free(canc);
    for (int b = 0; b < 4; b++) free(tim[b]);
    return ok;
}

/* ---- NHL 96 PC's anthems: DATA/ANTHUS.ASF and DATA/ANTHCAN.ASF, recordings in EA's 1SNh / EACS stream ----------------
 * A "1SNh" chunk holds the EACS header (sample rate, bits, channels, codec 2: IMA ADPCM, the sample count); each "1SNd"
 * chunk: its samples a channel, then for each channel the IMA step index and the last sample, then a byte a sample with
 * the left channel in the high nibble. (Each chunk's header is the state the one before it ends in.) "1SNe" ends it. */

static const int16_t IMA_STEP[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80,
    88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132,
    7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
static const int8_t IMA_INDEX[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

static void ima(int nib, int32_t *h, int32_t *ix)
{
    int st = IMA_STEP[*ix], d = st >> 3;
    if (nib & 1) d += st >> 2;
    if (nib & 2) d += st >> 1;
    if (nib & 4) d += st;
    if (nib & 8) d = -d;
    *h += d;
    if (*h > 32767) *h = 32767;
    if (*h < -32768) *h = -32768;
    *ix += IMA_INDEX[nib & 7];
    if (*ix < 0) *ix = 0;
    if (*ix > 88) *ix = 88;
}

/* an ASF decoded whole: stereo 16 bit at RATE96, its length in frames (NULL: not a stereo IMA stream at 22050 Hz) */
static int16_t *asf_decode(const uint8_t *d, long n, long *frames)
{
    if (n < 0x20 || memcmp(d, "1SNh", 4) || memcmp(d + 8, "EACS", 4)) return NULL;
    if ((long)le32(d + 12) != RATE96 || d[17] != 2 || d[18] != 2) return NULL;          /* (22050 Hz, two channels, IMA) */
    long cap = (long)le32(d + 20) + 4096, k = 0;
    if (cap > (long)MAXSEC * RATE96) cap = (long)MAXSEC * RATE96;
    int16_t *out = malloc((size_t)cap * 2 * sizeof *out);
    if (!out) return NULL;
    for (long p = 0; p + 8 <= n; ) {
        long len = (long)le32(d + p + 4);
        if (len < 8 || len > n - p) break;
        if (!memcmp(d + p, "1SNe", 4)) break;
        if (!memcmp(d + p, "1SNd", 4) && len >= 28) {
            long ns = (long)le32(d + p + 8);
            int32_t ix[2] = { (int32_t)le32(d + p + 12), (int32_t)le32(d + p + 16) }, h[2] = { (int32_t)le32(d + p + 20), (int32_t)le32(d + p + 24) };
            if (ns > len - 28) ns = len - 28;
            for (int c = 0; c < 2; c++) { if (ix[c] < 0 || ix[c] > 88) ix[c] = 0; if (h[c] > 32767) h[c] = 32767; if (h[c] < -32768) h[c] = -32768; }
            for (long i = 0; i < ns && k < cap; i++, k++) {
                int b = d[p + 28 + i];
                ima(b >> 4, &h[0], &ix[0]);
                ima(b & 15, &h[1], &ix[1]);
                out[2 * k] = (int16_t)h[0];
                out[2 * k + 1] = (int16_t)h[1];
            }
        }
        p += len;
    }
    if (!k) { free(out); return NULL; }
    *frames = k;
    return out;
}

static int load96(const char *dir, ob_cd *cd)
{
    static const char *const name[2][2] = { { "DATA/ANTHUS.ASF", "ANTHUS.ASF" }, { "DATA/ANTHCAN.ASF", "ANTHCAN.ASF" } };
    int16_t *pcm[2] = { NULL, NULL };
    long len[2] = { 0, 0 };
    for (int w = 0; w < 2; w++) {
        long n = 0;
        uint8_t *b = get(dir, cd, name[w][0], &n);
        if (!b) b = get(dir, cd, name[w][1], &n);
        if (b) pcm[w] = asf_decode(b, n, &len[w]);
        free(b);
    }
    if (!pcm[0] || !pcm[1]) { free(pcm[0]); free(pcm[1]); return 0; }
    for (int w = 0; w < 2; w++) { S.p96[w] = pcm[w]; S.l96[w] = len[w]; }
    if (getenv("PCSONG_LOG")) fprintf(stderr, "pcsong: NHL 96 PC USA %.1f s, CANADA %.1f s\n", (double)len[0] / RATE96, (double)len[1] / RATE96);
    return 1;
}

/* NHL 96 PC's disc: an image named as the game is first, then any image beside the program with the anthems on it */
static int find96(const char *base)
{
    static const char *const cand[] = { "NHL96.iso", "NHL96.cue", "nhl96.iso", "nhl96.cue", "NHL 96.iso", "NHL 96.cue", "NHL96.bin", "NHL96.img", "NHLHOCKEY96.iso" };
    static const char *const need[2] = { "DATA/ANTHUS.ASF", "DATA/ANTHCAN.ASF" };
    const char *dirs[2] = { base, "." };
    for (int k = 0; k < 2; k++) if (load96(dirs[k], NULL)) return 1;
    for (int k = 0; k < 2; k++)
        for (size_t i = 0; i < sizeof cand / sizeof *cand; i++) {
            char p[1100];
            snprintf(p, sizeof p, "%s/%s", dirs[k], cand[i]);
            ob_cd *cd = ob_cd_open(p, need, 2);
            if (!cd) continue;
            int ok = load96(NULL, cd);
            if (ok) fprintf(stderr, "PC anthems: NHL 96 PC anthems from %s\n", ob_cd_path(cd));
            ob_cd_close(cd);
            if (ok) return 1;
        }
    for (int k = 0; k < 2; k++) {
        ob_cd *cd = ob_cd_find(dirs[k], need, 2, NULL, 0);
        if (!cd) continue;
        int ok = load96(NULL, cd);
        if (ok) fprintf(stderr, "PC anthems: NHL 96 PC anthems from %s\n", ob_cd_path(cd));
        ob_cd_close(cd);
        if (ok) return 1;
    }
    return 0;
}

static int finder(void *arg)
{
    char *base = arg;
    int ok96 = find96(base);
    if (!ok96) fprintf(stderr, "PC anthems: no NHL 96 PC disc image found beside the program\n");
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
    SDL_AtomicSet(&S.found96, ok96);
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
    if (!SDL_AtomicGet(&S.searched) || !g.demoflag) return 0;
    SDL_MemoryBarrierAcquire();
    int set = choose();
    if (set < 0) return 0;
    int first = canadian(home_art) ? 1 : 0, second = first;           /* (the home team's anthem only) */
    SDL_LockMutex(S.lock);
    S.cur = set;
    S.which = first | (first != second ? (second + 1) << 1 : 0);     /* (bit 0 the first anthem; bits 1-2: the second, +1) */
    S.playing = 1;
    S.at = 0;
    const long *len = set ? S.l96 : S.len;
    long total = len[first] + (first != second ? len[second] : 0);
    SDL_UnlockMutex(S.lock);
    return (int)(total * 60 / (set ? RATE96 : RATE)) + 30;
}

/* the flag of the anthem playing at a frame of the game since it began (0 USA, 1 Canada; -1 none) */
int pcsong_flag_at(int frame)
{
    if (!SDL_AtomicGet(&S.on) || !S.playing) return -1;
    int first = S.which & 1, second = (S.which >> 1) - 1;
    const long *len = S.cur ? S.l96 : S.len;
    if (second >= 0 && (long)frame * (S.cur ? RATE96 : RATE) / 60 >= len[first]) return second;
    return first;
}

/* the crowd at the end of the anthem: NHL 95 PC's referee counts down (assintroline, temp5 from RockTuneIds by the home team's
 * teamrocktune) 1FEh game frames into "O Canada", 23Ah into the "Star Spangled Banner", then sets Crowdlevel to 320h: the
 * crowd cheers over the last bars. NHL 96 PC's (RunPreGameSkate) counts 23Ah for either recording. The frame of the anthem
 * playing (-1 when none) */
int pcsong_cheer_frame(void)
{
    if (!SDL_AtomicGet(&S.on) || !S.playing) return -1;
    return S.cur ? 0x23A : (S.which & 1) ? 0x1FE : 0x23A;
}

void pcsong_start(void)
{
    static int started;
    if (started) return;
    started = 1;
    S.lock = SDL_CreateMutex();
    anthem_hook_fn = anthem_hook;
    song_block_hook = playing_hook;
    stoppage_hook = stoppage;
    char *base = SDL_GetBasePath();
    SDL_Thread *t = SDL_CreateThread(finder, "pcsong", base ? SDL_strdup(base) : SDL_strdup("."));
    if (base) SDL_free(base);
    if (t) SDL_DetachThread(t);
}
void pcsong_wait(void) { while (!SDL_AtomicGet(&S.searched)) SDL_Delay(5); }
int pcsong_searched(void) { return SDL_AtomicGet(&S.searched); }
int pcsong_found(void) { return SDL_AtomicGet(&S.searched) && (SDL_AtomicGet(&S.found) || SDL_AtomicGet(&S.found96)); }
int pcsong_found95(void) { return SDL_AtomicGet(&S.searched) && SDL_AtomicGet(&S.found); }
int pcsong_found96(void) { return SDL_AtomicGet(&S.searched) && SDL_AtomicGet(&S.found96); }
void pcsong_set(int on) { SDL_AtomicSet(&S.on, on < 0 ? 0 : on > 2 ? 2 : on); }
void pcsong_stop(void) { S.playing = 0; K.playing = 0; }
void pcsong_rock_set(int on) { SDL_AtomicSet(&K.on, on ? 1 : 0); }

#define GAIN96 0.5f                         /* (NHL 96 PC's recordings run near full scale; the 95 FM songs about half of it) */

/* the anthems into a frame of sound (out: stereo 16 bit at rate, mixed in) */
void pcsong_mix(int16_t *out, int frames, unsigned rate)
{
    if (!S.lock || !rate) return;
    if (K.playing) {                                                 /* (a whistle's cue) */
        SDL_LockMutex(S.lock);
        const int16_t *p = K.pcm[K.cue];
        long n = K.len[K.cue];
        double st = (double)RATE / rate;
        for (int i = 0; i < frames && p; i++) {
            long k = (long)K.at;
            if (k >= n) { K.playing = 0; break; }
            for (int c = 0; c < 2; c++) {
                int v = out[2 * i + c] + p[2 * k + c];
                out[2 * i + c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
            K.at += st;
        }
        if (!p) K.playing = 0;
        SDL_UnlockMutex(S.lock);
    }
    if (!S.playing) return;
    SDL_LockMutex(S.lock);
    int first = S.which & 1, second = S.which >> 1 ? (S.which >> 1) - 1 : -1;
    int16_t *const *pcm = S.cur ? S.p96 : S.pcm;
    const long *len = S.cur ? S.l96 : S.len;
    long l0 = len[first], total = l0 + (second >= 0 ? len[second] : 0);
    double step = (double)(S.cur ? RATE96 : RATE) / rate;
    for (int i = 0; i < frames; i++) {
        long k = (long)S.at;
        if (k >= total) { S.playing = 0; break; }
        const int16_t *p = k < l0 ? pcm[first] + 2 * k : pcm[second] + 2 * (k - l0);
        for (int c = 0; c < 2; c++) {
            float v;
            if (S.cur) {                                        /* (22050 Hz: between two samples) */
                float fr = (float)(S.at - (double)k), b = k + 1 < total && k + 1 != l0 ? p[2 + c] : p[c];
                v = (p[c] + (b - p[c]) * fr) * GAIN96;
            } else v = p[c];
            float x = out[2 * i + c] + v;
            out[2 * i + c] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
        }
        S.at += step;
    }
    SDL_UnlockMutex(S.lock);
}

/* testing: both anthems, USA then CANADA, as a wav (--pc-anthem-wav FILE; with --pc-anthems-96, NHL 96 PC's) */
int pcsong_write_wav(const char *path)
{
    int set = choose();
    if (!pcsong_found() || set < 0) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int16_t *const *pcm = set ? S.p96 : S.pcm;
    const long *len = set ? S.l96 : S.len;
    uint32_t r = set ? RATE96 : RATE;
    long frames = len[0] + len[1];
    uint32_t data = (uint32_t)(frames * 4), hdr[11] = { 0x46464952u, 36 + data, 0x45564157u, 0x20746D66u, 16, 0x00020001u, r, r * 4, 0x00100004u, 0x61746164u, data };
    fwrite(hdr, 4, 11, f);
    fwrite(pcm[0], 4, (size_t)len[0], f);
    fwrite(pcm[1], 4, (size_t)len[1], f);
    fclose(f);
    return 1;
}
