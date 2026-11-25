/* The Sega CD's intro movie in the window (the Game page's "Intro movie"): HOCKEY.SEG played as an
 * Overbar full-window screen while the game's thread waits, its sound queued on the window's own
 * audio device and its pictures kept in time with that sound. Any key, button or click ends it.
 * The movie and its decoder are native/segacd_film.c's. */
#include <SDL.h>
#include <string.h>
#include "overbar/screen.h"
#include "segacd.h"

#define AHEAD_MS 120                       /* sound queued ahead of the speaker */

static struct {
    segacd_film *film;
    SDL_AudioDeviceID dev;
    unsigned rate;
    const int16_t *pcm;
    long pcm_len;
    double pcm_at;                          /* the next sample to queue (16000 Hz) */
    uint64_t out;                           /* frames queued since the start */
    uint32_t t0;                            /* SDL_GetTicks at the start (no sound device: the clock) */
    int pw, ph;                             /* the picture */
    uint32_t *pic;                          /* the picture now, ARGB */
    const uint8_t *nidx;                    /* the next picture, waiting for its time */
    const uint32_t *ncol;
    double ntime, last;                     /* its time (s, -1 none left), the last one's */
    SDL_atomic_t skip;
} M;

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
}

static void input(void *ud, const ob_screen_ev *e)
{
    (void)ud;
    if (e->kind == OB_SCREEN_OK || e->kind == OB_SCREEN_BACK || e->kind == OB_SCREEN_PRESS) SDL_AtomicSet(&M.skip, 1);
}

int segacd_movie_start(ob_cd *cd, const char *name, uint32_t dev, unsigned rate)
{
    if (M.film || !(M.film = segacd_film_open(cd, name))) return 0;
    segacd_film_size(M.film, &M.pw, &M.ph);
    M.pic = SDL_calloc((size_t)M.pw * M.ph, 4);
    if (!M.pic) { segacd_film_close(M.film); M.film = NULL; return 0; }
    for (int i = 0; i < M.pw * M.ph; i++) M.pic[i] = 0xFF000000u;
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
    SDL_free(M.pic);
    M.pic = NULL;
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
    while (M.ntime >= 0 && M.ntime <= now) {
        for (int i = 0; i < M.pw * M.ph; i++) M.pic[i] = M.ncol[M.nidx[i]];
        M.last = M.ntime;
        M.ntime = segacd_film_next(M.film, &M.nidx, &M.ncol);
    }
    if (M.ntime < 0 && now >= M.last + 0.1 && now * SEGACD_FILM_RATE >= M.pcm_len) { stop(); return 0; }
    return 1;
}

int segacd_movie_playing(void) { return M.film != NULL; }
