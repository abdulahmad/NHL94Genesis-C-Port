/* The Sega CD disc's sound effects (the Game page's "Sound effects" row): the disc's own, from
 * MAIN.BIN, in place of the cartridge's.
 *
 * The disc does not use the cartridge's sound driver for effects. MAIN.BIN has its own sequencer
 * (SndSlotsTickAll, NHL94Genesis src/segacd/main/mainsound294cd.asm) that the game's sfx call
 * starts with the cartridge's ids (id 4 is the horn). What is read (NHL9XStudio docs/SEGACD.md,
 * lib/audio/segacd-sfx.js, where each was checked against the real disc in an emulator):
 *   SndEffectTable      MAIN.BIN $863C  a word count (79), then records of type, id, length (BE word):
 *                                       type 3 a script (its id the effect), type 0 an FM patch
 *   SndEffectScripts    MAIN.BIN $877A  the type 3 records' scripts back to back, each as long as its record
 *   FmInstrumentPatches MAIN.BIN $8C6C  36 bytes a patch (26 register bytes, then the driver's own)
 *   the sample bank     GRAPH2.BIN $2D2000 (GTABLE record 96): u16 13, 13 pairs of tag and length, then
 *                                       the samples (RF5C164 sign-magnitude bytes, $FF stops)
 * A script is MIDI-like, a delta time (bit 7: more) after every event: $9n note vel duration, $Bn $12 v
 * (the channel's kind: 0 FM, 1 a sample on the PCM chip), $Cn program, $En bend ($40 the centre), $FF
 * the end. One tick is a 60 Hz frame.
 *
 * Each effect is rendered once, in a thread, when the row is first turned on: the FM notes through a
 * YM2612 of its own (PulseMan's core, the game's), the samples as the PCM chip plays them, from the
 * wave RAM the disc fills, so a sample with no $FF of its own runs on into the next. Playing one is
 * then a mix into the frame's sound. The game is not touched: its sfx call starts the cartridge's
 * effect as always (states, replays and online play are the same), and segacd.c keeps that effect's
 * notes from the chips. */
#include <SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mdrt/fm.h"
#include "mdrt/mix.h"
#include "segacd.h"

#define TABLE_AT 0x863C
#define SCRIPTS_AT 0x877A
#define PATCHES_AT 0x8C6C
#define PATCH_SIZE 36
#define BANK_REC 96                          /* GTABLE record of the sample bank (GRAPH2.BIN $2D2000) */
#define FM_RATE 53267.0                      /* the YM2612's rate, NTSC: the renders' */
#define TICK (FM_RATE / 60)
#define PCM_HZ (12500000.0 / 384)            /* the RF5C164's tick */
#define IDS 0x30
#define VOICES 6
/* a full-scale sample against the FM: the disc's recordings put a sample at full ENV at half of full scale where the FM is
 * 1.55 times the ymfm core's level (NHL9XStudio lib/audio/segacd-sfx.js, FM_GAIN); here the FM is at the game's own mix level */
#define PCM_GAIN (0.5f * MDRT_MIX_FM_GAIN / 32768 / 1.55f)

static const uint8_t patch_regs[26] = { 0x30, 0x34, 0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C, 0x50, 0x54, 0x58, 0x5C, 0x60,
                                        0x64, 0x68, 0x6C, 0x70, 0x74, 0x78, 0x7C, 0x80, 0x84, 0x88, 0x8C, 0xB0, 0xB4 };
/* carriers by algorithm: bits of the operator slot (its place in a register group) */
static const uint8_t carriers[8] = { 8, 8, 8, 8, 12, 14, 14, 15 };
/* program $60-$6F: the bank's sample (-1 none), and the note it plays at FD $200 */
static const int8_t prog_sample[16] = { 7, 2, 1, 6, 4, 0, 3, 5, -1, -1, -1, 12, 10, 8, 9, 11 };
static const uint8_t ref_note[13] = { 66, 60, 60, 66, 66, 60, 60, 60, 66, 66, 72, 60, 66 };

typedef struct { int16_t *pcm; long len; int have; } fx;   /* stereo pairs at FM_RATE; have: the disc has the effect (ids $26, $27: silence) */

static struct {
    SDL_atomic_t want, ready, started;
    fx fx[IDS];
    struct { int id; double at; } v[VOICES];
} X;

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }

static unsigned var(const uint8_t *s, long n, long *p)
{
    unsigned v = 0;
    while (*p < n) {
        uint8_t b = s[(*p)++];
        v = v << 7 | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    return v;
}

/* the patch record of program id: the type 0 records in order, the program their id */
static const uint8_t *patch_of(const uint8_t *m, int id)
{
    int n = 0;
    for (int i = 0; i < 79; i++) {
        const uint8_t *r = m + TABLE_AT + 2 + 4 * i;
        if (r[0] != 0) continue;
        if (r[1] == id) return m + PATCHES_AT + n * PATCH_SIZE;
        n++;
    }
    return m + PATCHES_AT + (id < 30 ? id : 30) * PATCH_SIZE;
}

typedef struct { int tick, kind, ch, a, b, c; } ev;  /* kind: 0 on (a note, b vel, c dur), 1 control, 2 program, 3 bend, 4 off */

static int ev_cmp(const void *x, const void *y)
{
    const ev *p = x, *q = y;
    if (p->tick != q->tick) return p->tick - q->tick;
    return (q->kind == 4) - (p->kind == 4);              /* (key offs first in a tick) */
}

/* the render: the chip, the samples so far (stereo floats), the idle level the chip's DAC sits at */
static struct { mdrt_fm *f; float *mix; long written, total; double dc[2]; } R;

/* run the chip for mclk master clocks, its samples into the render */
static void run(uint32_t mclk)
{
    int16_t buf[2 * 256];
    const float gain = (float)MDRT_MIX_FM_GAIN / 32768;
    while (mclk) {
        uint32_t m = mclk < 256 * MDRT_FM_SAMPLE_MCLKS ? mclk : 256 * MDRT_FM_SAMPLE_MCLKS;
        int got = mdrt_fm_run(R.f, m, buf, 256);
        mclk -= m;
        for (int j = 0; j < got; j++, R.written++) {
            if (!R.mix || R.written >= R.total) continue;
            R.mix[2 * R.written] += (float)(buf[2 * j] - R.dc[0]) * gain;
            R.mix[2 * R.written + 1] += (float)(buf[2 * j + 1] - R.dc[1]) * gain;
        }
    }
}

/* a register write, then the chip's busy time (32 of its clocks, as a driver waits) */
static void w(mdrt_fm *f, int part, int reg, int val)
{
    mdrt_fm_write(f, part * 2, (uint8_t)reg);
    mdrt_fm_write(f, part * 2 + 1, (uint8_t)val);
    run(32 * MDRT_FM_CYCLE_MCLKS);
}

static void pitch(mdrt_fm *f, int fm, double note)
{
    double hz = 440 * pow(2, (note - 69) / 12);
    int block = 0, fnum = 2047;
    for (; block < 8; block++) {
        fnum = (int)lround(hz * 144 * pow(2, 21 - block) / 7670454);
        if (fnum < 2048) break;
    }
    if (block == 8) { block = 7; fnum = 2047; }
    w(f, fm >= 3, 0xA4 + fm % 3, block << 3 | fnum >> 8);
    w(f, fm >= 3, 0xA0 + fm % 3, fnum & 255);
}

/* one effect's script into stereo samples at FM_RATE */
static void render(const uint8_t *m, const uint8_t *s, long n, const uint8_t *ram, fx *out)
{
    ev e[512];
    int ne = 0, t = 0, status = 0, end = 0;
    for (long p = 0; p < n && ne < 500;) {
        if (s[p] & 0x80) status = s[p++];
        if (status == 0xFF) break;
        int k = status & 0xF0, ch = status & 15;
        if (k == 0x90 && p + 2 <= n) {
            int note = s[p], vel = s[p + 1];
            p += 2;
            int dur = (int)var(s, n, &p);
            e[ne++] = (ev){ t, 0, ch, note, vel, dur };
            e[ne++] = (ev){ t + dur, 4, ch, t + dur, 0, 0 };
            if (t + dur > end) end = t + dur;
        } else if (k == 0xB0 && p + 2 <= n) { e[ne++] = (ev){ t, 1, ch, s[p], s[p + 1], 0 }; p += 2; }
        else if (k == 0xC0 && p < n) e[ne++] = (ev){ t, 2, ch, s[p++], 0, 0 };
        else if (k == 0xE0 && p < n) e[ne++] = (ev){ t, 3, ch, s[p++], 0, 0 };
        else break;
        t += (int)var(s, n, &p);
    }
    qsort(e, (size_t)ne, sizeof *e, ev_cmp);
    int ticks = end + 24;
    long total = (long)(ticks * TICK) + 1;
    float *mix = calloc((size_t)total * 2, sizeof *mix);
    mdrt_fm *f = mdrt_fm_new();
    if (!mix || !f) { free(mix); if (f) mdrt_fm_free(f); return; }
    mdrt_fm_reset(f);
    R.f = f;
    R.mix = NULL;                                              /* (set up, and the idle level measured: not kept) */
    w(f, 0, 0x22, 0); w(f, 0, 0x27, 0); w(f, 0, 0x2B, 0);
    {
        int16_t buf[2 * 512];
        int got = mdrt_fm_run(f, 512 * MDRT_FM_SAMPLE_MCLKS, buf, 512);
        R.dc[0] = R.dc[1] = 0;
        for (int i = 0; i < got; i++) { R.dc[0] += buf[2 * i]; R.dc[1] += buf[2 * i + 1]; }
        if (got) { R.dc[0] /= got; R.dc[1] /= got; }
    }
    R.mix = mix;
    R.written = 0;
    R.total = total;
    struct { int kind, program, note, bend, until; double shift, range; const uint8_t *patch; } st[16];
    memset(st, 0, sizeof st);
    for (int c = 0; c < 16; c++) { st[c].note = -1; st[c].bend = 64; st[c].until = -1; st[c].range = 2; }
    int i = 0;
    for (int tick = 0; tick <= ticks; tick++) {
        for (; i < ne && e[i].tick <= tick; i++) {
            ev *x = &e[i];
            int fm = x->ch % 6, key = fm < 3 ? fm : fm + 1;
            switch (x->kind) {
            case 1: if (x->a == 0x12) st[x->ch].kind = x->b; break;
            case 2:
                st[x->ch].program = x->a;
                if (!st[x->ch].kind) {
                    const uint8_t *pt = patch_of(m, x->a);
                    st[x->ch].patch = pt;
                    st[x->ch].shift = 12 + (int8_t)pt[28];
                    st[x->ch].range = pt[30] ? pt[30] : 2;
                }
                break;
            case 3:
                st[x->ch].bend = x->a;
                if (!st[x->ch].kind && st[x->ch].note >= 0) pitch(f, fm, st[x->ch].note + st[x->ch].shift + (st[x->ch].bend - 64) / 64.0 * st[x->ch].range);
                break;
            case 0:
                st[x->ch].note = x->a;
                st[x->ch].until = x->tick + x->c;
                if (!st[x->ch].kind && st[x->ch].patch) {
                    const uint8_t *pt = st[x->ch].patch;
                    int quieter = x->b > 0 && x->b < 127 ? (int)lround(19.3 * log2(127.0 / x->b)) : 0;
                    w(f, 0, 0x28, key);
                    for (int r = 0; r < 24; r++) {
                        int v = pt[r];
                        if (r >= 4 && r < 8 && (carriers[pt[24] & 7] >> (r - 4) & 1)) { v = (v & 127) + quieter; if (v > 127) v = 127; }
                        w(f, fm >= 3, patch_regs[r] + fm % 3, v);
                    }
                    w(f, fm >= 3, 0xB0 + fm % 3, pt[24]);
                    w(f, fm >= 3, 0xB4 + fm % 3, pt[25] ? pt[25] : 0xC0);
                    pitch(f, fm, x->a + st[x->ch].shift + (st[x->ch].bend - 64) / 64.0 * st[x->ch].range);
                    w(f, 0, 0x28, 0xF0 | key);
                } else if (st[x->ch].kind == 1 && st[x->ch].program >= 0x60 && st[x->ch].program < 0x70 && ram) {
                    int k = prog_sample[st[x->ch].program - 0x60];
                    if (k < 0) break;
                    /* the disc: FD from the note, ENV twice the velocity, PAN $FF ($99 for program 103) */
                    double step = 0x200 * pow(2, (x->a - ref_note[k]) / 12.0) / 0x800 * PCM_HZ / FM_RATE;
                    int pan = st[x->ch].program == 103 ? 0x99 : 0xFF;
                    float vol = x->b * 2 / 256.0f * PCM_GAIN;
                    unsigned at = (unsigned)ram[0x10000 + k] << 8;   /* (the sample's page, kept after the RAM) */
                    double pos = 0;
                    for (long j = (long)(x->tick * TICK); j < total; j++, pos += step) {
                        unsigned a = at + (unsigned)pos;
                        if (a >= 0x10000 || ram[a] == 0xFF) break;
                        unsigned b = a + 1 < 0x10000 && ram[a + 1] != 0xFF ? a + 1 : a;
                        float v0 = (float)((ram[a] & 0x7F) << 8) * (ram[a] & 0x80 ? 1 : -1);
                        float v1 = (float)((ram[b] & 0x7F) << 8) * (ram[b] & 0x80 ? 1 : -1);
                        float fr = (float)(pos - floor(pos)), v = (v0 + (v1 - v0) * fr) * vol;
                        mix[2 * j] += v * (float)(pan & 15) / 15;
                        mix[2 * j + 1] += v * (float)(pan >> 4) / 15;
                    }
                }
                break;
            case 4:
                if (!st[x->ch].kind && st[x->ch].until == x->a) w(f, 0, 0x28, key);
                break;
            }
        }
        long want = (long)((tick + 1) * TICK);
        if (want > total) want = total;
        if (want > R.written) run((uint32_t)(want - R.written) * MDRT_FM_SAMPLE_MCLKS);
    }
    mdrt_fm_free(f);
    int16_t *pcm = malloc((size_t)total * 2 * sizeof *pcm);
    if (pcm) {
        long last = 0;
        for (long j = 0; j < total * 2; j++) {
            float v = mix[j];
            pcm[j] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            if (pcm[j] > 8 || pcm[j] < -8) last = j / 2 + 1;
        }
        out->pcm = pcm;
        out->len = last;                                       /* (the silent tail off) */
    }
    free(mix);
}

static int renderer(void *path)
{
    static const char *const need[2] = { "HOCKEY.BIN", "GRAPH2.BIN" };
    ob_cd *cd = ob_cd_open((const char *)path, need, 2);      /* (its own: the game's is the window's) */
    SDL_free(path);
    uint8_t *m = NULL, *g2 = NULL, *gt = NULL;
    long mn = cd ? ob_cd_read_file(cd, "MAIN.BIN", &m) : -1;
    long gn = cd ? ob_cd_read_file(cd, "GRAPH2.BIN", &g2) : -1, tn = cd ? ob_cd_read_file(cd, "GTABLE.BIN", &gt) : -1;
    if (mn > PATCHES_AT + 31 * PATCH_SIZE && be16(m + TABLE_AT) == 79 && m[TABLE_AT + 2] == 3) {
        /* the wave RAM as the game fills it: each sample from its own page ($0100 the first), $FF elsewhere */
        static uint8_t ram[0x10000 + 13];
        memset(ram, 0xFF, sizeof ram);
        const uint8_t *bank = NULL;
        if (gn > 0 && tn >= 6L * (BANK_REC + 1)) {
            long at = (long)be16(gt + 6 * BANK_REC) * 2048;
            if (at + 2 + 13 * 4 <= gn && be16(g2 + at) == 13) bank = g2 + at;
        }
        if (bank) {
            long pos = 2 + 13 * 4;
            int page = 1;
            for (int k = 0; k < 13; k++) {
                long len = be16(bank + 2 + 4 * k + 2);
                if ((bank - g2) + pos + len > gn || page * 256 + len > 0x10000) break;
                memcpy(ram + page * 256, bank + pos, (size_t)len);
                ram[0x10000 + k] = (uint8_t)page;
                page += (int)((len + 255) / 256);
                pos += len;
            }
        }
        long pos = SCRIPTS_AT;
        for (int i = 0; i < 79; i++) {
            const uint8_t *r = m + TABLE_AT + 2 + 4 * i;
            if (r[0] != 3) break;
            long len = be16(r + 2), head = 0;
            if (pos + len > mn) break;
            while (head < len && !(m[pos + head] & 0x80)) head++;   /* (flags and priority before the first event) */
            if (r[1] < IDS) {
                X.fx[r[1]].have = 1;
                if (len - head > 1) render(m, m + pos + head, len - head, bank ? ram : NULL, &X.fx[r[1]]);
            }
            pos += len;
        }
    }
    free(m);
    free(g2);
    free(gt);
    if (cd) ob_cd_close(cd);
    SDL_MemoryBarrierRelease();
    SDL_AtomicSet(&X.ready, 1);
    return 0;
}

void segacd_set_sfx(int v)
{
    SDL_AtomicSet(&X.want, v ? 1 : 0);
    ob_cd *cd = segacd_disc();
    if (!v || !cd || !SDL_AtomicCAS(&X.started, 0, 1)) return;
    char *path = SDL_strdup(ob_cd_path(cd));
    SDL_Thread *t = SDL_CreateThread(renderer, "segacd-sfx", path);
    if (t) SDL_DetachThread(t);
    else renderer(path);
}

int segacd_sfx_on(int id)
{
    if (!SDL_AtomicGet(&X.want)) return 0;
    if (!SDL_AtomicGet(&X.started)) segacd_set_sfx(1);      /* (the disc was found after the row was set) */
    if (!SDL_AtomicGet(&X.ready)) return 0;
    SDL_MemoryBarrierAcquire();
    return id >= 0 && id < IDS && X.fx[id].have;
}

void segacd_sfx_play(int id)
{
    if (!segacd_sfx_on(id)) return;
    int k = 0;
    for (int i = 0; i < VOICES; i++) {
        if (X.v[i].id == id + 1) { k = i; break; }           /* (the same effect again: it starts over) */
        if (!X.v[i].id) k = i;
        else if (X.v[k].id && X.v[i].at > X.v[k].at) k = i;   /* (else the one furthest on) */
    }
    X.v[k].id = id + 1;
    X.v[k].at = 0;
}

void segacd_sfx_stop(void) { for (int i = 0; i < VOICES; i++) X.v[i].id = 0; }

void segacd_sfx_mix(int16_t *out, int frames, unsigned rate)
{
    if (!rate) return;
    if (!SDL_AtomicGet(&X.want)) { segacd_sfx_stop(); return; }
    double step = FM_RATE / rate;
    for (int k = 0; k < VOICES; k++) {
        if (!X.v[k].id) continue;
        const fx *e = &X.fx[X.v[k].id - 1];
        if (!e->pcm) { X.v[k].id = 0; continue; }
        for (int i = 0; i < frames; i++) {
            long j = (long)X.v[k].at;
            if (j + 1 >= e->len) { X.v[k].id = 0; break; }
            float fr = (float)(X.v[k].at - j);
            for (int c = 0; c < 2; c++) {
                float a = e->pcm[2 * j + c], b = e->pcm[2 * j + 2 + c], x = a + (b - a) * fr + out[2 * i + c];
                out[2 * i + c] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
            }
            X.v[k].at += step;
        }
    }
}

#ifdef SEGACD_SFX_TEST
/* (a test's) effect id's render: stereo pairs at 53267 Hz */
long segacd_sfx_test(const char *path, int id, const int16_t **pcm)
{
    renderer(SDL_strdup(path));
    *pcm = X.fx[id].pcm;
    return X.fx[id].len;
}
#endif

void segacd_sfx_wait(void) { while (segacd_disc() && SDL_AtomicGet(&X.started) && !SDL_AtomicGet(&X.ready)) SDL_Delay(5); }
