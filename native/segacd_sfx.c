/* The Sega CD disc's sound effects (the Game page's "Sound effects" row). The disc does not use the cartridge's
 * sound code: MAIN.BIN has its own sequencer and Z80 driver. What it shares with the cartridge is the effect
 * numbers (the game's sfx call posts the same ids) and the 26 byte FM patch. This is a reconstruction from the
 * data, the way NHL9XStudio's lib/audio/segacd-sfx.js (the format notes are in its docs/SEGACD.md) reads it: each
 * effect's script is played through a YM2612 of its own, and its sample channels (the disc's RF5C164 PCM chip)
 * from the sample bank in GRAPH2.BIN (GTABLE record 96). The disc's Z80 driver is not run, so the FM envelopes
 * differ a little from the real chip. Each effect is rendered once (all of them ahead of time on a thread of their own, or on first use) and kept.
 *
 * Retail MAIN.BIN offsets: SndEffectTable $863C (a word count, 79, then records of type, id and a big endian
 * length: type 3 a script, type 0 an FM patch), SndEffectScripts $877A, FmInstrumentPatches $8C6C (36 bytes each).
 * Script events, each followed by a delta time in ticks (60 Hz): $9n note vel duration, $Bn $12 v (channel type:
 * 0 FM, 1 sample), $Cn program, $En bend, $FF end. None of this touches the game: it only adds sound. */
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mdrt/fm.h"
#include "mdrt/mdrt.h"
#include "segacd.h"

#define TABLE_AT 0x863E
#define TABLE_COUNT 79
#define SCRIPTS_AT 0x877A
#define SCRIPTS_END 0x8C6A
#define PATCHES_AT 0x8C6C
#define PATCH_SIZE 36
#define TICK_HZ 60
#define SAMPLE_BANK_REC 96                  /* GTABLE.BIN's record: GRAPH2.BIN $2D2000 */
#define NSAMPLES 13
#define PCM_RATE_HZ (12500000.0 / 384.0)    /* the RF5C164's tick: 32,552 Hz */
#define FD_BASE 0x200
#define YM_CLOCK 7670454.0
#define TL_PER_OCTAVE 19.3
#define NOTE_OFFSET 12
#define SFX_GAIN 0.8f
#define FM_GAIN 2.5                         /* the chip's FM against the disc's samples (the disc's FM is about this much louder than this core's) */
#define VOICES 12

static const int program_sample[16] = { 7, 2, 1, 6, 4, 0, 3, 5, -1, -1, -1, 12, 10, 8, 9, 11 };   /* programs 96-111 */
static const int sample_ref_note[NSAMPLES] = { 66, 60, 60, 66, 66, 60, 60, 60, 66, 66, 72, 60, 66 };
static const uint8_t patch_regs[26] = { 0x30, 0x34, 0x38, 0x3c, 0x40, 0x44, 0x48, 0x4c, 0x50, 0x54, 0x58, 0x5c, 0x60,
                                        0x64, 0x68, 0x6c, 0x70, 0x74, 0x78, 0x7c, 0x80, 0x84, 0x88, 0x8c, 0xb0, 0xb4 };
static const uint8_t carriers[8] = { 8, 8, 8, 8, 12, 14, 14, 15 };   /* the carrier operators of an algorithm, a bit each in register group order */

static struct {
    int tried, ok;
    uint8_t *main;
    long main_n;
    const uint8_t *bank;                    /* GRAPH2's sample record (the disc's, kept by segacd_photos.c) */
    long bank_n;
    uint8_t ram[0x10000];                   /* the chip's wave RAM as the game fills it */
    int page[NSAMPLES];
    struct { int16_t *pcm; long n; int done; } fx[256];
    struct { const int16_t *pcm; long n; double at; } v[VOICES];
} X;

static int be16(const uint8_t *p) { return p[0] << 8 | p[1]; }

static int init(ob_cd *cd)
{
    if (X.tried) return X.ok;
    X.tried = 1;
    X.main_n = ob_cd_read_file(cd, "MAIN.BIN", &X.main);
    if (X.main_n < SCRIPTS_END || X.main[TABLE_AT] != 3 || X.main[TABLE_AT + 1] != 0x26) return 0;   /* (not the retail disc's) */
    long n;
    const uint8_t *g = segacd_graph2_record(cd, SAMPLE_BANK_REC, &n);
    if (!g || n < 2 + NSAMPLES * 4 || be16(g) != NSAMPLES) return 0;
    X.bank = g;
    X.bank_n = n;
    memset(X.ram, 0xFF, sizeof X.ram);
    long pos = 2 + NSAMPLES * 4;
    int pg = 1;
    for (int k = 0; k < NSAMPLES; k++) {    /* the samples one after another from the 256 byte page $01 */
        long len = be16(g + 2 + k * 4 + 2);
        if (pos + len > n || (pg * 256L + len) > (long)sizeof X.ram) return 0;
        memcpy(X.ram + pg * 256, g + pos, (size_t)len);
        X.page[k] = pg;
        pg += (int)((len + 255) / 256);
        pos += len;
    }
    X.ok = 1;
    return 1;
}

/* a variable length number: 7 bits a byte, bit 7 continues */
static int var(const uint8_t *s, long n, long *p)
{
    int v = 0;
    while (*p < n) { uint8_t b = s[(*p)++]; v = v << 7 | (b & 0x7F); if (!(b & 0x80)) break; }
    return v;
}

typedef struct { int tick, type, ch, a, b, dur, seq; } ev_t;   /* type: 0 on, 1 control, 2 program, 3 bend, 4 off */

static int ev_cmp(const void *x, const void *y)
{
    const ev_t *a = x, *b = y;
    if (a->tick != b->tick) return a->tick - b->tick;
    if ((a->type == 4) != (b->type == 4)) return (b->type == 4) - (a->type == 4);   /* key offs first */
    return a->seq - b->seq;                                /* (the script's order: qsort is not stable) */
}

/* the script of effect id: its bytes, or NULL */
static const uint8_t *script_of(int id, long *len)
{
    long pos = SCRIPTS_AT;
    for (int i = 0; i < TABLE_COUNT; i++) {
        const uint8_t *r = X.main + TABLE_AT + i * 4;
        if (r[0] != 3) break;
        long l = be16(r + 2);
        if (r[1] == id) {
            long head = 0;
            while (head < l && !(X.main[pos + head] & 0x80)) head++;
            *len = l - head;
            return X.main + pos + head;
        }
        pos += l;
    }
    return NULL;
}

static const uint8_t *patch_of(int program)
{
    int n = 0, idx = -1;
    for (int i = 0; i < TABLE_COUNT; i++) {
        const uint8_t *r = X.main + TABLE_AT + i * 4;
        if (r[0] != 0) continue;
        if (r[1] == program) { idx = n; break; }
        n++;
    }
    if (idx < 0) idx = program < 30 ? program : 30;
    return X.main + PATCHES_AT + idx * PATCH_SIZE;
}

typedef struct { int kind, program, note, bend, until, shift, range; const uint8_t *patch; } chan_t;
typedef struct { long from; int base; double step, vol; } smp_t;

static void w(mdrt_fm *f, int port, int reg, int val)
{
    mdrt_fm_write(f, port * 2, (uint8_t)reg);
    mdrt_fm_write(f, port * 2 + 1, (uint8_t)val);
    int16_t gap[8];                         /* the disc's driver waits between writes: the chip takes one at a time */
    mdrt_fm_run(f, 32 * MDRT_FM_CYCLE_MCLKS, gap, 4);
}

static void pitch(mdrt_fm *f, int ch, int note, int bend, int shift, int range)
{
    int fm = ch % 6, port = fm < 3 ? 0 : 1;
    double hz = 440.0 * pow(2.0, (note + shift + ((bend - 64) / 64.0) * range - 69) / 12.0);
    int block = 7, fnum = 2047;
    for (int b = 0; b < 8; b++) {
        int fn = (int)floor(hz * 144.0 * pow(2.0, 21 - b) / YM_CLOCK + 0.5);
        if (fn < 2048) { block = b; fnum = fn; break; }
    }
    w(f, port, 0xA4 + fm % 3, block << 3 | fnum >> 8);
    w(f, port, 0xA0 + fm % 3, fnum & 255);
}

/* render one effect: mono, at the chip's rate (MDRT_FM_RATE_NTSC); NULL when it has no script */
static int16_t *render(int id, long *out_n)
{
    long slen;
    const uint8_t *s = script_of(id, &slen);
    if (!s) return NULL;
    ev_t *ev = malloc(sizeof(ev_t) * (size_t)(slen + 1) * 2);
    if (!ev) return NULL;
    int nev = 0, ticks = 0, t = 0, status = 0;
    for (long p = 0; p < slen;) {
        if (s[p] & 0x80) status = s[p++];
        if (status == 0xFF) break;
        int kind = status & 0xF0, ch = status & 15;
        ev_t e = { t, 0, ch, 0, 0, 0, nev };
        if (kind == 0x90) { if (p + 2 > slen) break; e.a = s[p++]; e.b = s[p++]; e.dur = var(s, slen, &p); e.type = 0; }
        else if (kind == 0xB0) { if (p + 2 > slen) break; e.type = 1; e.a = s[p++]; e.b = s[p++]; }
        else if (kind == 0xC0) { if (p + 1 > slen) break; e.type = 2; e.a = s[p++]; }
        else if (kind == 0xE0) { if (p + 1 > slen) break; e.type = 3; e.a = s[p++]; }
        else break;
        ev[nev++] = e;
        if (e.type == 0) { ev_t o = { t + e.dur, 4, ch, 0, 0, 0, nev }; ev[nev++] = o; if (t + e.dur > ticks) ticks = t + e.dur; }
        t += var(s, slen, &p);
    }
    if (nev == 0) { free(ev); return NULL; }
    qsort(ev, (size_t)nev, sizeof *ev, ev_cmp);
    ticks += 24;
    const int rate = MDRT_FM_RATE_NTSC;
    long total = (long)(((double)ticks / TICK_HZ) * rate + 1);
    float *pcm = calloc((size_t)total, sizeof *pcm);
    smp_t sm[64];
    int nsm = 0;
    mdrt_fm *f = mdrt_fm_new();
    if (!pcm || !f) { free(ev); free(pcm); mdrt_fm_free(f); return NULL; }
    mdrt_fm_reset(f);
    w(f, 0, 0x22, 0); w(f, 0, 0x27, 0); w(f, 0, 0x2B, 0);
    static int16_t buf[2 * 4096];
    double idle = 0;                        /* the chip's idle level (its DAC bias): taken off */
    { int m = mdrt_fm_run(f, MDRT_FM_SAMPLE_MCLKS * 512, buf, 4096); for (int i = 0; i < m; i++) idle += (buf[2 * i] + buf[2 * i + 1]) * 0.5; idle = m ? idle / m : 0; }
    chan_t ch[16];
    memset(ch, 0, sizeof ch);
    for (int i = 0; i < 16; i++) { ch[i].bend = 64; ch[i].until = -1; }
    long written = 0;
    int idx = 0;
    double frame = (double)rate / TICK_HZ;
    for (int tick = 0; tick <= ticks; tick++) {
        while (idx < nev && ev[idx].tick <= tick) {
            ev_t *e = &ev[idx++];
            chan_t *c = &ch[e->ch];
            int fm = e->ch % 6, off = fm % 3, port = fm < 3 ? 0 : 1, key = fm < 3 ? fm : fm + 1;
            if (e->type == 1 && e->a == 0x12) c->kind = e->b;
            else if (e->type == 2) {
                c->program = e->a;
                if (!c->kind) {
                    c->patch = patch_of(e->a);
                    int sh = c->patch[28]; if (sh > 127) sh -= 256;
                    c->shift = NOTE_OFFSET + sh;
                    c->range = c->patch[30] ? c->patch[30] : 2;
                }
            } else if (e->type == 3) {
                c->bend = e->a;
                if (c->kind == 0 && c->note >= 0 && c->patch) pitch(f, e->ch, c->note, c->bend, c->shift, c->range);
            } else if (e->type == 0) {
                c->note = e->a;
                c->until = e->tick + e->dur;
                if (c->kind == 0 && c->patch) {
                    int vel = e->b, alg = c->patch[24] & 7;
                    int quieter = vel > 0 && vel < 127 ? (int)floor(TL_PER_OCTAVE * log2(127.0 / vel) + 0.5) : 0;
                    w(f, 0, 0x28, key);
                    for (int i = 0; i < 24; i++) {
                        int v = c->patch[i];
                        if (i >= 4 && i < 8 && (carriers[alg] >> (i - 4) & 1)) { v = (v & 127) + quieter; if (v > 127) v = 127; }
                        w(f, port, patch_regs[i] + off, v);
                    }
                    w(f, port, 0xB0 + off, c->patch[24]);
                    w(f, port, 0xB4 + off, c->patch[25] ? c->patch[25] : 0xC0);
                    pitch(f, e->ch, c->note, c->bend, c->shift, c->range);
                    w(f, 0, 0x28, 0xF0 | key);
                } else if (c->kind == 1 && nsm < 64) {
                    int k = c->program >= 96 && c->program < 112 ? program_sample[c->program - 96] : -1;
                    if (k >= 0) {
                        int fd = (int)floor(FD_BASE * pow(2.0, (e->a - sample_ref_note[k]) / 12.0) + 0.5);
                        if (fd < 1) fd = 1;
                        int pan = c->program == 103 ? 0x99 : 0xFF;
                        sm[nsm].from = (long)((double)e->tick / TICK_HZ * rate + 0.5);
                        sm[nsm].base = X.page[k] * 256;
                        sm[nsm].step = (fd / 2048.0) * PCM_RATE_HZ / rate;
                        sm[nsm].vol = (e->b * 2 / 256.0) * (((pan >> 4 & 15) + (pan & 15)) / 30.0);
                        nsm++;
                    }
                }
            } else if (e->type == 4 && c->kind == 0 && c->until == e->tick) w(f, 0, 0x28, key);
        }
        long want = (long)((tick + 1) * frame + 0.5);
        if (want > total) want = total;
        while (written < want) {
            long n = want - written;
            if (n > 4096) n = 4096;
            int m = mdrt_fm_run(f, (uint32_t)n * MDRT_FM_SAMPLE_MCLKS, buf, (int)n);
            for (int i = 0; i < m && written + i < total; i++) pcm[written + i] = (float)(((buf[2 * i] + buf[2 * i + 1]) * 0.5 - idle) * FM_GAIN / 32768.0);
            written += m ? m : n;           /* (a chip that gives fewer: time passes all the same) */
        }
    }
    for (int k = 0; k < nsm; k++) {         /* the samples: wave RAM from the page to the first $FF at the note's rate */
        double pos = 0;
        for (long i = sm[k].from; i < total; i++, pos += sm[k].step) {
            int a = sm[k].base + (int)pos;
            if (a >= (int)sizeof X.ram || X.ram[a] == 0xFF) break;
            int nx = a + 1 < (int)sizeof X.ram && X.ram[a + 1] != 0xFF ? a + 1 : a;
            double fr = pos - floor(pos);
#define SM(x) (((x) & 0x80) ? -((x) & 0x7F) : ((x) & 0x7F)) / 127.0
            double v0 = SM(X.ram[a]), v1 = SM(X.ram[nx]);
            pcm[i] += (float)((v0 * (1 - fr) + v1 * fr) * 0.5 * sm[k].vol);
        }
    }
    int16_t *out = malloc((size_t)total * sizeof *out);
    float peak = 0;
    for (long i = 0; i < total; i++) if (fabsf(pcm[i]) > peak) peak = fabsf(pcm[i]);
    float gain = peak > 0.9f ? 0.9f / peak : 1.f;
    if (out) for (long i = 0; i < total; i++) {
        float v = pcm[i] * gain * 32767.f;
        out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    free(pcm); free(ev); mdrt_fm_free(f);
    *out_n = total;
    return out;
}

static SDL_mutex *rmx;                      /* one render at a time (the prime thread's and the game's) */

/* effect id's PCM, rendered if it is not yet (NULL: the disc has no such effect) */
static const int16_t *effect(ob_cd *cd, int id, long *n)
{
    if (id < 0 || id > 255) return NULL;
    if (!rmx) rmx = SDL_CreateMutex();
    SDL_LockMutex(rmx);
    const int16_t *p = NULL;
    if (init(cd)) {
        if (!X.fx[id].done) {
            X.fx[id].pcm = render(id, &X.fx[id].n);
            SDL_MemoryBarrierRelease();
            X.fx[id].done = 1;
            if (SDL_getenv("SEGACD_LOG")) fprintf(stderr, "segacd: effect %02X rendered (%ld samples)\n", id, X.fx[id].pcm ? X.fx[id].n : 0L);
        }
        p = X.fx[id].pcm;
        *n = X.fx[id].n;
    }
    SDL_UnlockMutex(rmx);
    return p;
}

int segacd_sfx_play(ob_cd *cd, int id)
{
    long n = 0;
    const int16_t *p = cd ? effect(cd, id, &n) : NULL;
    if (!p) return 0;
    int k = 0;
    for (int i = 0; i < VOICES; i++) if (!X.v[i].pcm) { k = i; break; } else if (X.v[i].at > X.v[k].at) k = i;   /* (a free voice, else the furthest along) */
    X.v[k].pcm = p;
    X.v[k].n = n;
    X.v[k].at = 0;
    return 1;
}

/* the effects, rendered ahead on a thread of their own so that the first one heard does not stall the game */
static int primer(void *cd)
{
    long n;
    for (int id = 0; id < 0x30; id++) effect((ob_cd *)cd, id, &n);
    return 0;
}

void segacd_sfx_prime(ob_cd *cd)
{
    static int started;
    if (!cd || started) return;
    started = 1;
    SDL_Thread *t = SDL_CreateThread(primer, "segacd-sfx", cd);
    if (t) SDL_DetachThread(t);
}

void segacd_sfx_mix(int16_t *out, int frames, unsigned rate)
{
    if (!rate) return;
    double step = (double)MDRT_FM_RATE_NTSC / rate;
    for (int k = 0; k < VOICES; k++) {
        if (!X.v[k].pcm) continue;
        for (int i = 0; i < frames; i++) {
            long j = (long)X.v[k].at;
            if (j + 1 >= X.v[k].n) { X.v[k].pcm = NULL; break; }
            double fr = X.v[k].at - j;
            float v = (float)(X.v[k].pcm[j] + (X.v[k].pcm[j + 1] - X.v[k].pcm[j]) * fr) * SFX_GAIN;
            for (int c = 0; c < 2; c++) {
                float x = v + out[2 * i + c];
                out[2 * i + c] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
            }
            X.v[k].at += step;
        }
    }
}

void segacd_sfx_stop(void) { for (int k = 0; k < VOICES; k++) X.v[k].pcm = NULL; }

/* --segacd-sfx-dump DIR: every effect as a mono 16-bit WAV (the chip's rate), one line each: id, samples, peak */
void segacd_sfx_dump(ob_cd *cd, const char *dir)
{
    for (int id = 0; id < 0x30; id++) {
        if (!cd || !init(cd)) { fprintf(stderr, "sfx: not the retail disc\n"); return; }
        long n;
        int16_t *p = render(id, &n);
        if (!p) continue;
        int peak = 0;
        for (long i = 0; i < n; i++) { int a = p[i] < 0 ? -p[i] : p[i]; if (a > peak) peak = a; }
        char path[512];
        snprintf(path, sizeof path, "%s/sfx%02X.wav", dir, id);
        FILE *f = fopen(path, "wb");
        if (f) {
            uint32_t rate = MDRT_FM_RATE_NTSC, bytes = (uint32_t)n * 2, riff = 36 + bytes, br = rate * 2, sixteen = 16;
            uint16_t one = 1, two = 2, bits = 16;
            fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&sixteen, 4, 1, f);
            fwrite(&one, 2, 1, f); fwrite(&one, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f);
            fwrite(&two, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
            fwrite(p, 2, (size_t)n, f);
            fclose(f);
        }
        fprintf(stderr, "sfx %02X: %ld samples (%.2f s), peak %d\n", id, n, (double)n / MDRT_FM_RATE_NTSC, peak);
        free(p);
    }
}
