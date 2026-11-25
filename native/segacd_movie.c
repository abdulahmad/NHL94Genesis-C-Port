/* The Sega CD's intro movie in the window (the Game page's "Intro movie"): HOCKEY.SEG played as an
 * Overbar full-window screen while the game's thread waits, its sound queued on the window's own
 * audio device and its pictures kept in time with that sound. Enter, Esc, a button or a click ends
 * it; left and right change the de-dither (Overbar's frame post-process, include/overbar/postfx.h)
 * while it plays. The cached de-dither is made on another thread the first time it is asked for,
 * from the disc's own movie, and kept at cache_path; until it is ready the movie plays live.
 *
 * Two more, both made on that thread from the user's own disc and kept beside the program (nothing derived is
 * shipped):
 *   30 fps     two pictures between each pair of the disc's (10 a second), drawn by Overbar's frame
 *              generation (overbar/framegen.h, the RIFE model in rife-v4.6/ beside the program) from the
 *              cached de-dither's pictures: <base>.smooth. A cut between two shots is held, not blended.
 *              It needs the de-dither on (live or cached); until it is made the movie plays at its own rate.
 *   colour     each shot's own correction (overbar/grade.h): <base>.colour, a few bytes a shot, applied live.
 * The movie and its decoder are native/segacd_film.c's. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "overbar/overbar.h"
#include "overbar/framegen.h"
#include "overbar/grade.h"
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
    char cache[1100], base[1100], model[1100];
    int smooth, colour;                     /* the options: 30 fps, the shots' colour */
    ob_ddcache *mids;                       /* the 30 fps pictures: 2 between each pair, 2x */
    int sub;                                /* which of the three the picture is (0 the disc's own) */
    ob_grade_scene *scenes;
    int nscenes, lut_scene, looked;
    ob_grade_lut lut;
} M;

/* the made passes' maker: one at a time, on its own thread; it keeps on after the movie ends. Whatever is
 * missing of: the de-dither cache, the shots' colour, the 30 fps pictures */
static SDL_atomic_t making, made_pct;   /* (made_pct: the 30 fps pictures, percent) */

typedef struct {
    segacd_film *film;
    char dd[1100], colour[1100], smooth[1100], model[1100];
    uint32_t id;
    int want_dd, want_colour, want_smooth;
} maker_job;

static void path_of(char *out, size_t n, const char *base, const char *ext)
{
    if (snprintf(out, n, "%s%s", base, ext) >= (int)n) out[0] = 0;     /* (too long: none) */
}

/* half size: each 2 x 2 averaged */
static void half(const uint32_t *in, int W, int H, uint32_t *out)
{
    for (int y = 0; y < H / 2; y++) for (int x = 0; x < W / 2; x++) {
        const uint32_t *p = in + (size_t)2 * y * W + 2 * x;
        uint32_t o = 0xFF000000u;
        for (int k = 0; k < 24; k += 8) o |= (uint32_t)(((p[0] >> k & 255) + (p[1] >> k & 255) + (p[W] >> k & 255) + (p[W + 1] >> k & 255) + 2) / 4) << k;
        out[y * (W / 2) + x] = o;
    }
}

static int maker(void *ud)
{
    maker_job *j = ud;
    int w, h, ok = 1, n = 0;
    uint32_t t0 = SDL_GetTicks();
    segacd_film_size(j->film, &w, &h);
    const uint8_t *idx;
    const uint32_t *col;
    uint32_t *frame = SDL_malloc((size_t)w * h * 4), *live = SDL_malloc((size_t)w * h * 4);
    uint8_t *scratch = SDL_malloc((size_t)w * h);
    ob_grade_scene *scenes = NULL;
    int nscenes = -1;
    ok = frame && live && scratch;
    /* the shots (the 30 fps pictures hold at a cut, so it needs them too) */
    if (ok && (j->want_colour || j->want_smooth) && (nscenes = ob_grade_load(j->colour, j->id, &scenes)) < 0) {
        segacd_film *f = segacd_film_dup(j->film);
        ob_grade_maker *gm = ob_grade_make(w, h);
        ok = f && gm;
        while (ok && segacd_film_next(f, &idx, &col) >= 0) {
            for (int i = 0; i < w * h; i++) frame[i] = col[idx[i]];
            ob_dedither_live(frame, live, scratch, w, h);
            ob_grade_add(gm, live);
        }
        nscenes = ok ? ob_grade_end(gm, &scenes) : -1;
        if (!ok && gm) { ob_grade_scene *none; ob_grade_end(gm, &none); }
        ok = nscenes > 0 && ob_grade_save(j->colour, j->id, scenes, nscenes);
        segacd_film_close(f);
        fprintf(stderr, "movie colour: %s, %d shots in %.1f s%s\n", j->colour, nscenes, (SDL_GetTicks() - t0) / 1000.0, ok ? "" : " (failed)");
    }
    /* the de-dither cache and the 30 fps pictures, from the same high-quality pass */
    ob_framegen *g = ok && j->want_smooth ? ob_framegen_load(j->model, 0) : NULL;
    if (j->want_smooth && !g) { if (ok) fprintf(stderr, "30 fps: no frame generation model in %s\n", j->model); j->want_smooth = 0; }
    if (ok && (j->want_dd || j->want_smooth)) {
        t0 = SDL_GetTicks();
        int W = 2 * w, H = 2 * h, cut_at = 0;
        ob_dedither_hq *q = ob_dedither_hq_new(w, h);
        ob_ddcache_maker *dd = j->want_dd ? ob_ddcache_make_done(j->dd, w, h, j->id) : NULL;
        ob_ddcache_maker *mid = j->want_smooth ? ob_frames_make(j->smooth, W, H, j->id) : NULL;
        uint32_t *prev = SDL_malloc((size_t)W * H * 4), *cur = SDL_malloc((size_t)W * H * 4), *out = SDL_malloc((size_t)W * H * 4);
        uint32_t *p1 = SDL_malloc((size_t)w * h * 4), *c1 = SDL_malloc((size_t)w * h * 4);
        float *flow = SDL_malloc((size_t)w * h * 16), *mask = SDL_malloc((size_t)w * h * 4);
        int total = nscenes > 0 ? scenes[nscenes - 1].end : 0;
        ok = q && (!j->want_dd || dd) && (!j->want_smooth || mid) && prev && cur && out && p1 && c1 && flow && mask;
        while (ok && segacd_film_next(j->film, &idx, &col) >= 0) {
            for (int i = 0; i < w * h; i++) frame[i] = col[idx[i]];
            memcpy(cur, ob_dedither_hq_frame(q, frame), (size_t)W * H * 4);
            if (dd) ok = ob_ddcache_add(dd, cur);
            if (ok && mid && n > 0) {
                while (cut_at < nscenes && scenes[cut_at].start < n) cut_at++;
                if (cut_at < nscenes && scenes[cut_at].start == n) ok = ob_ddcache_add(mid, prev) && ob_ddcache_add(mid, prev);   /* (a cut: held) */
                else {
                    half(prev, W, H, p1);
                    half(cur, W, H, c1);
                    for (int k = 1; ok && k <= 2; k++) {
                        ok = ob_framegen_motion(g, p1, c1, w, h, k / 3.0f, flow, mask);
                        if (ok) { ob_framegen_draw(g, prev, cur, W, H, flow, mask, w, h, out); ok = ob_ddcache_add(mid, out); }
                    }
                }
                if (total) SDL_AtomicSet(&made_pct, n * 100 / total);
            }
            uint32_t *t = prev; prev = cur; cur = t;
            n++;
        }
        if (dd) ok = ob_ddcache_end(dd, ok) && ok;
        if (mid) ok = ob_ddcache_end(mid, ok) && ok;
        ob_dedither_hq_free(q);
        SDL_free(prev); SDL_free(cur); SDL_free(out); SDL_free(p1); SDL_free(c1); SDL_free(flow); SDL_free(mask);
        fprintf(stderr, "movie: %s%s%s, %d frames in %.1f s%s\n", j->want_dd ? j->dd : "", j->want_dd && j->want_smooth ? " and " : "",
                j->want_smooth ? j->smooth : "", n, (SDL_GetTicks() - t0) / 1000.0, ok ? "" : " (failed)");
    }
    ob_framegen_free(g);
    free(scenes);
    SDL_free(frame); SDL_free(live); SDL_free(scratch);
    segacd_film_close(j->film);
    SDL_free(j);
    SDL_AtomicSet(&made_pct, 0);
    SDL_AtomicSet(&making, 0);
    return ok;
}

static int has_mids(void)
{
    if (!M.mids && M.base[0]) {
        char p[1100];
        path_of(p, sizeof p, M.base, ".smooth");
        M.mids = ob_frames_open(p, 2 * M.fw, 2 * M.fh, segacd_film_id(M.film));
    }
    return M.mids != NULL;
}

static int has_scenes(void)
{
    if (!M.scenes && M.base[0]) {
        char p[1100];
        path_of(p, sizeof p, M.base, ".colour");
        M.nscenes = ob_grade_load(p, segacd_film_id(M.film), &M.scenes);
        if (M.nscenes <= 0) { free(M.scenes); M.scenes = NULL; M.nscenes = 0; }
    }
    return M.scenes != NULL;
}

/* starts the maker for whatever the options want that is not made yet */
static void make_cache(void)
{
    int want_dd = M.cache[0] && SDL_AtomicGet(&M.mode) == OB_POSTFX_CACHED && !ob_postfx_has_cache(M.pp);
    int want_colour = M.colour && M.base[0] && !has_scenes();
    int want_smooth = M.smooth && M.base[0] && !has_mids();
    if (want_smooth && M.cache[0] && !ob_postfx_has_cache(M.pp)) want_dd = 1;   /* (the same pass makes it) */
    if (!want_dd && !want_colour && !want_smooth) return;
    if (!SDL_AtomicCAS(&making, 0, 1)) return;
    maker_job *j = SDL_calloc(1, sizeof *j);
    if (j && (j->film = segacd_film_dup(M.film))) {
        snprintf(j->dd, sizeof j->dd, "%s", M.cache);
        path_of(j->colour, sizeof j->colour, M.base, ".colour");
        path_of(j->smooth, sizeof j->smooth, M.base, ".smooth");
        snprintf(j->model, sizeof j->model, "%s", M.model);
        j->id = segacd_film_id(M.film);
        j->want_dd = want_dd; j->want_colour = want_colour || want_smooth; j->want_smooth = want_smooth;
        SDL_Thread *t = SDL_CreateThread(maker, "movie-extras", j);
        if (t) { SDL_DetachThread(t); return; }
        segacd_film_close(j->film);
    }
    SDL_free(j);
    SDL_AtomicSet(&making, 0);
}

/* the picture now: the disc's through the de-dither, or one of the 30 fps pictures between; then its shot's colour */
static void show(void)
{
    if (M.index < 0) return;
    const uint32_t *mid = M.sub && M.mids ? ob_ddcache_read(M.mids, 2 * M.index + M.sub - 1) : NULL;
    if (mid && M.pw == 2 * M.fw) memcpy(M.pic, mid, (size_t)M.pw * M.ph * 4);   /* (the size of the disc's picture before it) */
    else if (mid) half(mid, 2 * M.fw, 2 * M.fh, M.pic);
    else {
        int w, h;
        const uint32_t *p = ob_postfx_run(M.pp, SDL_AtomicGet(&M.mode), M.index, M.raw, &w, &h);
        if (!M.pp) { p = M.raw; w = M.fw; h = M.fh; }
        memcpy(M.pic, p, (size_t)w * h * 4);
        M.pw = w;
        M.ph = h;
    }
    int w = M.pw, h = M.ph;
    const ob_grade_scene *sc = M.colour && M.scenes ? ob_grade_find(M.scenes, M.nscenes, M.index) : NULL;
    if (sc) {
        int i = (int)(sc - M.scenes);
        if (i != M.lut_scene) { ob_grade_prepare(&sc->p, &M.lut); M.lut_scene = i; }
        ob_grade_apply(&M.lut, M.pic, M.pic, (long)w * h);
    }
    const char *dump = getenv("SEGACD_MOVIE_DUMP");          /* (tests: each picture shown, as a PPM) */
    if (dump) {
        static int n;
        char name[1200];
        snprintf(name, sizeof name, "%s/m%05d_f%03d_%d.ppm", dump, n++, M.index, M.sub);
        FILE *f = fopen(name, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", w, h);
            for (int i = 0; i < w * h; i++) { unsigned char c[3] = { (unsigned char)(M.pic[i] >> 16), (unsigned char)(M.pic[i] >> 8), (unsigned char)M.pic[i] }; fwrite(c, 1, 3, f); }
            fclose(f);
        }
    }
}

/* the picture, handed to Overbar: drawn like the game's own, through the player's look (Sharp, CRT, MiSTer) */
static int look_preset;
static const ob_look *look_game;
void segacd_movie_set_look(int preset, const ob_look *game) { look_preset = preset; look_game = game; }

static const uint32_t *picture(void *ud, int *w, int *h, int *lines)
{
    ob_look l;                                          /* (each frame, so a change to the player's look shows) */
    (void)ud;
    if (look_game && ob_video_look(look_preset, look_game, &l)) ob_screen_set_video_look(&l);
    else ob_screen_set_video_look(NULL);
    *w = M.pw; *h = M.ph; *lines = M.fh;
    return M.pic;
}

/* what goes over it: the notes */
static void draw(void *ud, uint32_t *px, int w, int h, int pitch)
{
    (void)ud;
    int bw = w, bh = w * 3 / 4;             /* (the Genesis picture is 4:3 whatever its width) */
    if (bh > h) { bh = h; bw = h * 4 / 3; }
    int x0 = (w - bw) / 2, y0 = (h - bh) / 2;
    float size = h / 24.0f;
    if (M.smooth && !M.mids && SDL_AtomicGet(&making)) {
        char text[80];
        snprintf(text, sizeof text, "30 fps: being made (%d%%)", SDL_AtomicGet(&made_pct));
        ob_vf_draw(px, w, h, pitch, x0 + size, y0 + size * 1.5f, OB_VF_SEMIBOLD, size * 0.75f, 0xFFFFFF, 160, text, 0, 2);
    }
    if (M.label_at && SDL_GetTicks() - M.label_at < LABEL_MS) {
        static const char *const names[OB_POSTFX_MODES] = { "De-dither: off", "De-dither: live", "De-dither: cached" };
        int mode = SDL_AtomicGet(&M.mode);
        char text[80];
        snprintf(text, sizeof text, "%s%s%s", names[mode], mode == OB_POSTFX_CACHED && M.pw == M.fw ? " (being made: live for now)" : "",
                 M.smooth && mode == OB_POSTFX_OFF ? " (30 fps needs it on)" : "");
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
        M.sub = 0;                                      /* (the disc's own picture; the next tick goes on from there) */
        show();
    }
}

int segacd_movie_start(ob_cd *cd, const char *name, uint32_t dev, unsigned rate, int dedither, int smooth, int colour, const char *base,
                       const char *model)
{
    if (M.film || !(M.film = segacd_film_open(cd, name))) return 0;
    segacd_film_size(M.film, &M.fw, &M.fh);
    snprintf(M.base, sizeof M.base, "%s", base ? base : "");
    snprintf(M.model, sizeof M.model, "%s", model ? model : "");
    if (M.base[0]) path_of(M.cache, sizeof M.cache, M.base, ".dedither");
    else M.cache[0] = 0;
    M.smooth = smooth;
    M.colour = colour;
    M.sub = 0;
    M.lut_scene = -1;
    M.looked = 0;
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
    make_cache();
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
    ob_screen s = { draw, input, NULL, picture };
    ob_screen_show(&s);
    return 1;
}

static void stop(void)
{
    ob_screen_show(NULL);
    ob_screen_set_video_look(NULL);
    if (M.dev) SDL_ClearQueuedAudio(M.dev);
    segacd_film_close(M.film);
    M.film = NULL;
    ob_postfx_free(M.pp);
    M.pp = NULL;
    SDL_free(M.raw);
    SDL_free(M.pic);
    M.raw = M.pic = NULL;
    ob_ddcache_close(M.mids);
    M.mids = NULL;
    free(M.scenes);
    M.scenes = NULL;
    M.nscenes = 0;
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
    if (fresh && ++M.looked >= 30) {                    /* (made while it plays: taken up when ready) */
        M.looked = 0;
        if (M.smooth) has_mids();
        if (M.colour) has_scenes();
    }
    int sub = 0;
    if (M.smooth && M.mids && SDL_AtomicGet(&M.mode) != OB_POSTFX_OFF && M.index >= 0 && M.ntime > M.last) {
        sub = (int)((now - M.last) * 3 / (M.ntime - M.last));
        sub = sub < 0 ? 0 : sub > 2 ? 2 : sub;
    }
    if (fresh || sub != M.sub) { M.sub = sub; show(); }
    if (M.ntime < 0 && now >= M.last + 0.1 && now * SEGACD_FILM_RATE >= M.pcm_len) { stop(); return 0; }
    return 1;
}

int segacd_movie_playing(void) { return M.film != NULL; }
