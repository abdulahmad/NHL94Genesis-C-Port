/* The Sega CD's intro movie in the window (the Game page's "Intro movie"): HOCKEY.SEG played as an
 * Overbar full-window screen while the game's thread waits, its sound queued on the window's own
 * audio device and its pictures kept in time with that sound. Enter, Esc, a button or a click ends
 * it; left and right change the de-dither (Overbar's frame post-process, include/overbar/postfx.h)
 * while it plays. The cached de-dither is made on another thread the first time it is asked for,
 * from the disc's own movie, and kept at cache_path; until it is ready the movie plays live.
 * The movie and its decoder are native/segacd_film.c's. */
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include "overbar/postfx.h"
#include "overbar/screen.h"
#include "overbar/vfont.h"
#include "segacd.h"

#define AHEAD_MS 120                       /* sound queued ahead of the speaker */
#define LABEL_MS 2000                      /* the de-dither's name on screen after a change */

static struct {
    segacd_film *film;
    SDL_AudioDeviceID dev;
    unsigned rate;
    const int16_t *pcm;
    long pcm_len;
    double pcm_at;                          /* the next sample to queue (16000 Hz) */
    uint64_t out;                           /* frames queued since the start */
    uint32_t t0;                            /* SDL_GetTicks at the start (no sound device: the clock) */
    int fw, fh;                             /* the movie's picture */
    uint32_t *raw;                          /* the picture now as it came, ARGB */
    int index;                              /* its number (-1 before the first) */
    uint32_t *pic;                          /* the picture to show (fw x fh, or 2x from the cache) */
    int pw, ph;
    const uint8_t *nidx;                    /* the next picture, waiting for its time */
    const uint32_t *ncol;
    double ntime, last;                     /* its time (s, -1 none left), the last one's */
    ob_postfx *pp;
    SDL_atomic_t mode;                      /* the de-dither (ob_postfx_mode) */
    SDL_atomic_t skip;
    uint32_t label_at;                      /* SDL_GetTicks of the last change (0 none) */
    char cache[1100];
} M;

/* the cached pass's maker: one at a time, on its own thread; it keeps on after the movie ends */
static SDL_atomic_t making;

typedef struct { segacd_film *film; char path[1100]; uint32_t id; } maker_job;

static int maker(void *ud)
{
    maker_job *j = ud;
    int w, h, ok = 1;
    segacd_film_size(j->film, &w, &h);
    ob_ddcache_maker *m = ob_ddcache_make(j->path, w, h, j->id);
    uint32_t *frame = SDL_malloc((size_t)w * h * 4), t0 = SDL_GetTicks();
    const uint8_t *idx;
    const uint32_t *col;
    int n = 0;
    ok = m && frame;
    while (ok && segacd_film_next(j->film, &idx, &col) >= 0) {
        for (int i = 0; i < w * h; i++) frame[i] = col[idx[i]];
        ok = ob_ddcache_add(m, frame);
        n++;
    }
    ok = ob_ddcache_end(m, ok) && ok;
    fprintf(stderr, "de-dither cache: %s, %d frames in %.1f s%s\n", j->path, n, (SDL_GetTicks() - t0) / 1000.0, ok ? "" : " (failed)");
    SDL_free(frame);
    segacd_film_close(j->film);
    SDL_free(j);
    SDL_AtomicSet(&making, 0);
    return ok;
}

static void make_cache(void)
{
    if (!M.cache[0] || ob_postfx_has_cache(M.pp) || !SDL_AtomicCAS(&making, 0, 1)) return;
    maker_job *j = SDL_calloc(1, sizeof *j);
    if (j && (j->film = segacd_film_dup(M.film))) {
        snprintf(j->path, sizeof j->path, "%s", M.cache);
        j->id = segacd_film_id(M.film);
        SDL_Thread *t = SDL_CreateThread(maker, "dedither-cache", j);
        if (t) { SDL_DetachThread(t); return; }
        segacd_film_close(j->film);
    }
    SDL_free(j);
    SDL_AtomicSet(&making, 0);
}

/* the picture now through the de-dither */
static void show(void)
{
    if (M.index < 0) return;
    int w, h;
    const uint32_t *p = ob_postfx_run(M.pp, SDL_AtomicGet(&M.mode), M.index, M.raw, &w, &h);
    if (!M.pp) { p = M.raw; w = M.fw; h = M.fh; }
    memcpy(M.pic, p, (size_t)w * h * 4);
    M.pw = w;
    M.ph = h;
}

static void draw(void *ud, uint32_t *px, int w, int h, int pitch)
{
    (void)ud;
    int bw = w, bh = w * 3 / 4;             /* (the Genesis picture is 4:3 whatever its width) */
    if (bh > h) { bh = h; bw = h * 4 / 3; }
    int x0 = (w - bw) / 2, y0 = (h - bh) / 2;
    for (int y = 0; y < h; y++) {
        uint32_t *row = px + (long)y * pitch;
        int sy = (y - y0) * M.ph / (bh ? bh : 1);
        for (int x = 0; x < w; x++) {
            int sx = (x - x0) * M.pw / (bw ? bw : 1);
            row[x] = y < y0 || y >= y0 + bh || x < x0 || x >= x0 + bw ? 0xFF000000u : M.pic[sy * M.pw + sx];
        }
    }
    if (M.label_at && SDL_GetTicks() - M.label_at < LABEL_MS) {
        static const char *const names[OB_POSTFX_MODES] = { "De-dither: off", "De-dither: live", "De-dither: cached" };
        int mode = SDL_AtomicGet(&M.mode);
        char text[80];
        snprintf(text, sizeof text, "%s%s", names[mode], mode == OB_POSTFX_CACHED && M.pw == M.fw ? " (being made: live for now)" : "");
        float size = h / 24.0f;
        ob_vf_draw(px, w, h, pitch, x0 + size, y0 + bh - size, OB_VF_SEMIBOLD, size, 0xFFFFFF, 255, text, 0, 2);
    }
}

static void input(void *ud, const ob_screen_ev *e)
{
    (void)ud;
    if (e->kind == OB_SCREEN_OK || e->kind == OB_SCREEN_BACK || e->kind == OB_SCREEN_PRESS) SDL_AtomicSet(&M.skip, 1);
    if (e->kind == OB_SCREEN_LEFT || e->kind == OB_SCREEN_RIGHT) {
        int m = (SDL_AtomicGet(&M.mode) + (e->kind == OB_SCREEN_RIGHT ? 1 : OB_POSTFX_MODES - 1)) % OB_POSTFX_MODES;
        SDL_AtomicSet(&M.mode, m);
        M.label_at = SDL_GetTicks();
        if (!M.label_at) M.label_at = 1;
        if (m == OB_POSTFX_CACHED) make_cache();
        show();
    }
}

int segacd_movie_start(ob_cd *cd, const char *name, uint32_t dev, unsigned rate, int dedither, const char *cache_path)
{
    if (M.film || !(M.film = segacd_film_open(cd, name))) return 0;
    segacd_film_size(M.film, &M.fw, &M.fh);
    snprintf(M.cache, sizeof M.cache, "%s", cache_path ? cache_path : "");
    M.pp = ob_postfx_new(M.fw, M.fh, M.cache[0] ? M.cache : NULL, segacd_film_id(M.film));
    M.raw = SDL_calloc((size_t)M.fw * M.fh, 4);
    M.pic = SDL_calloc((size_t)M.fw * M.fh * 4, 4);
    if (!M.raw || !M.pic) { segacd_film_close(M.film); M.film = NULL; ob_postfx_free(M.pp); M.pp = NULL; SDL_free(M.raw); SDL_free(M.pic); return 0; }
    for (int i = 0; i < M.fw * M.fh; i++) M.pic[i] = M.raw[i] = 0xFF000000u;
    M.pw = M.fw;
    M.ph = M.fh;
    M.index = -1;
    M.label_at = 0;
    SDL_AtomicSet(&M.mode, dedither >= 0 && dedither < OB_POSTFX_MODES ? dedither : OB_POSTFX_OFF);
    if (dedither == OB_POSTFX_CACHED) make_cache();
    M.pcm_len = segacd_film_sound(M.film, &M.pcm);
    M.pcm_at = 0;
    M.out = 0;
    M.dev = dev;
    M.rate = rate ? rate : 48000;
    M.last = 0;
    M.ntime = segacd_film_next(M.film, &M.nidx, &M.ncol);
    SDL_AtomicSet(&M.skip, 0);
    if (M.dev) { SDL_ClearQueuedAudio(M.dev); SDL_PauseAudioDevice(M.dev, 0); }
    M.t0 = SDL_GetTicks();
    ob_screen s = { draw, input, NULL };
    ob_screen_show(&s);
    return 1;
}

static void stop(void)
{
    ob_screen_show(NULL);
    if (M.dev) SDL_ClearQueuedAudio(M.dev);
    segacd_film_close(M.film);
    M.film = NULL;
    ob_postfx_free(M.pp);
    M.pp = NULL;
    SDL_free(M.raw);
    SDL_free(M.pic);
    M.raw = M.pic = NULL;
}

/* (the window's loop) the sound topped up and the picture brought to its time; 0 once it has ended */
int segacd_movie_tick(void)
{
    if (!M.film) return 0;
    if (SDL_AtomicGet(&M.skip)) { stop(); return 0; }
    double now;
    if (M.dev) {
        static int16_t buf[2 * 1024];
        double step = (double)SEGACD_FILM_RATE / M.rate;
        while (SDL_GetQueuedAudioSize(M.dev) < M.rate * 4 * AHEAD_MS / 1000) {
            int k = 0;
            for (; k < 1024; k++, M.pcm_at += step) {
                long i = (long)M.pcm_at;
                int v = 0;
                if (i + 1 < M.pcm_len) { double f = M.pcm_at - i; v = (int)(M.pcm[i] * (1 - f) + M.pcm[i + 1] * f); }
                buf[2 * k] = buf[2 * k + 1] = (int16_t)v;
            }
            SDL_QueueAudio(M.dev, buf, (Uint32)k * 4);
            M.out += (uint64_t)k;
        }
        now = (double)(M.out - SDL_GetQueuedAudioSize(M.dev) / 4) / M.rate;   /* (what the speaker has played) */
    } else now = (SDL_GetTicks() - M.t0) / 1000.0;
    int fresh = 0;
    while (M.ntime >= 0 && M.ntime <= now) {
        for (int i = 0; i < M.fw * M.fh; i++) M.raw[i] = M.ncol[M.nidx[i]];
        M.index++;
        M.last = M.ntime;
        M.ntime = segacd_film_next(M.film, &M.nidx, &M.ncol);
        fresh = 1;
    }
    if (fresh) show();
    if (M.ntime < 0 && now >= M.last + 0.1 && now * SEGACD_FILM_RATE >= M.pcm_len) { stop(); return 0; }
    return 1;
}

int segacd_movie_playing(void) { return M.film != NULL; }
