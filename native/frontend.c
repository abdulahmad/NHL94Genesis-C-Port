/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * The Genesis port's window, replacing PulseMan's SDL2 front end (mdrt_sdl_run) with Overbar's
 * platform layer (docs/PLATFORM.md). The same entry point (mdrt/platform_sdl2.h): the game runs on its
 * own thread against the mdrt platform handshake, this thread runs the window.
 *
 * The ROM is looked for beside the program (any name, a common Genesis extension: .gen .md .bin
 * .smd) and each candidate is asked whether it is the right cartridge. Not in this front end (PulseMan's
 * window had them): the ROM browser, screenshots and GIF recording, rewind, state slots, the pixel
 * filters (Scale2x, MMPX, xBR). Online play: the overlay's Online page (net.c's window sessions);
 * once a session's match begins the console powers on again for it, from a new cartridge (no save RAM
 * read or written while it lasts).
 */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "mdrt/platform_sdl2.h"
#include "mdrt/settings.h"
#include "mdrt/vdp.h"
#include "overbar/plat.h"
#include "overbar/box.h"
#include "overbar/plat_pads.h"
#include "overbar/net.h"
#include "native.h"
#include "optionsui.h"
#include "rules.h"
#include "overbar/postfx.h"
#include "overbar/screen.h"
#include "segacd.h"
#include "announcer.h"
#include "pcsfx.h"
#include "pcsong.h"
#include "whiteice.h"
#include "hats.h"
#include "pcspr.h"
#include "cardcolor.h"
#include "wide.h"
#include "overbar/sides.h"
#include "mdrt/menu.h"

#define SETTINGS "settings.ini"


#define SQUEUE 3                                      /* frames of sound queued before the game is let go on */

enum { BUTTONS = 12 };
static const ob_pad_button buttons[BUTTONS] = {
    { "up", MDRT_PAD_UP, "Up", "I", "", NULL },       { "down", MDRT_PAD_DOWN, "Down", "K", "", NULL },
    { "left", MDRT_PAD_LEFT, "Left", "J", "", NULL }, { "right", MDRT_PAD_RIGHT, "Right", "L", "", NULL },
    { "a", MDRT_PAD_A, "Z", "Q", "x", "A" },          { "b", MDRT_PAD_B, "X", "W", "a", "B" },
    { "c", MDRT_PAD_C, "C", "E", "b", "C" },          { "start", MDRT_PAD_START, "Return", "Tab", "start", "pause" },
    { "x", MDRT_PAD_X, "", "", "leftshoulder", "X" }, { "y", MDRT_PAD_Y, "", "", "y", "Y" },
    { "z", MDRT_PAD_Z, "", "", "rightshoulder", "Z" }, { "mode", MDRT_PAD_MODE, "", "", "", "Mode" },
};

static struct {
    const mdrt_sdl_options *o;
    mdrt_config *cfg;
    mdrt_game_fn game;
    void *user;
    mdrt *m;
    SDL_Thread *thread;
    SDL_atomic_t done, stop, quit, fresh;
    SDL_mutex *lock;
    uint32_t picture[WIDE_MAX_W * 240], shown[WIDE_MAX_W * 240];   /* (rows WIDE_MAX_W apart) */
    int width, height, wide;                          /* wide: the picture is a widescreen one (wide.h) */
    SDL_atomic_t buttons[MDRT_PADS];
    SDL_AudioDeviceID dev;
    unsigned rate, queued, waits;
    uint64_t next_tick;
    mdrt_settings *set;
    char set_path[1100], sram_file[1100];
    uint8_t *rom;
    int *opt_values;
    int adaptor;
    int session;                                      /* the console is a session's */
} F;

/* ---- settings: settings.ini beside the program ---- */

static const char *beside(const char *name)
{
    static char path[1100];
    char *base = SDL_GetBasePath();
    snprintf(path, sizeof path, "%s%s", base ? base : "", name);
    SDL_free(base);
    return path;
}

static void settings_write(void)
{
    size_t n;
    char *text;
    FILE *f;
    if (!F.set || !mdrt_settings_dirty(F.set)) return;
    n = mdrt_settings_format(F.set, NULL, 0);
    text = malloc(n);
    f = text ? fopen(F.set_path, "wb") : NULL;
    if (f) {
        mdrt_settings_format(F.set, text, n);
        if (fwrite(text, 1, n - 1, f) == n - 1) mdrt_settings_clean(F.set);
        fclose(f);
    }
    free(text);
}

static void settings_load(void)
{
    FILE *f;
    F.set = mdrt_settings_new();
    snprintf(F.set_path, sizeof F.set_path, "%s", F.o->settings_path ? F.o->settings_path : beside(SETTINGS));
    f = fopen(F.set_path, "rb");
    if (f) {
        long n;
        char *text;
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        text = n > 0 ? malloc((size_t)n) : NULL;
        if (text && fread(text, 1, (size_t)n, f) == (size_t)n) mdrt_settings_parse(F.set, text, (size_t)n);
        free(text);
        fclose(f);
    } else {
        static const char head[] =
            "# NHL '94's settings: edit them while the game is closed. Keys are SDL's key names, empty for\n"
            "# none (A, 1, F1, Up, Return, Space, Tab, Left Shift, Keypad 8 ...). Controller buttons are SDL's\n"
            "# names: a b x y back start leftshoulder rightshoulder leftstick rightstick guide, and\n"
            "# lefttrigger righttrigger. playerN.controller: the controller slot player N plays with (0: none;\n"
            "# controllers take slots 1-8 as they are plugged in). Esc or ` opens and closes the Overbar overlay (Backspace is Back)\n"
            "# (Controllers sets a controller up). F12 shows the stats. The EA 4 Way Play adaptor is in when\n"
            "# player 3 or 4 has keys or a controller at power on.\n";
        mdrt_settings_parse(F.set, head, sizeof head - 1);
    }
}

static const char *pads_get(const char *key, const char *def)
{
    mdrt_settings_default(F.set, key, def ? def : "");
    return mdrt_settings_get(F.set, key);
}
static void pads_set(const char *key, const char *value) { mdrt_settings_set(F.set, key, value); }

/* ---- the game's options (the rules pages' values), from the settings ---- */

static int named(const char *v, const char *const *names, int n, int fallback)
{
    for (int i = 0; v && i < n; i++) if (!strcmp(v, names[i])) return i;
    return fallback;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void options_from_settings(void)
{
    const mdrt_sdl_options *o = F.o;
    if (o->noptions <= 0) return;
    F.opt_values = calloc((size_t)o->noptions, sizeof *F.opt_values);
    if (!F.opt_values) return;
    for (int i = 0; i < o->noptions; i++) {
        const mdrt_sdl_option *op = &o->options[i];
        if (op->count > 0) {
            int def = clampi(op->def, 0, op->count - 1);
            const char *v;
            mdrt_settings_default(F.set, op->key, op->names[def]);
            v = mdrt_settings_get(F.set, op->key);
            F.opt_values[i] = named(v, op->names, op->count, def);
            if (op->browse && o->options_pick && v && *v && named(v, op->names, op->count, -1) < 0) {   /* a path found before */
                char why[200] = "";
                int k = o->options_pick(i, v, why, sizeof why, F.user);
                if (k >= 0 && k < op->count) F.opt_values[i] = k;
                else fprintf(stderr, "%s: %s (%s instead)\n", v, why[0] ? why : "not one it takes", op->names[def]);
            }
            if (op->numbers) {
                char *end;
                long n = v ? strtol(v, &end, 10) : 0;
                if (named(v, op->names, op->count, -1) < 0 && v && end != v && !*end && n >= op->min && n <= op->max) F.opt_values[i] = (int)n;
                else F.opt_values[i] = op->numbers[F.opt_values[i]];
            }
        } else {
            char v[24];
            snprintf(v, sizeof v, "%d", op->def);
            mdrt_settings_default(F.set, op->key, v);
            F.opt_values[i] = mdrt_settings_get_int(F.set, op->key, op->def);
        }
    }
    if (o->options_changed) o->options_changed(-1, F.opt_values, F.user);   
}

/* ---- the ROM beside the program ---- */

static int ext_ok(const char *name)
{
    static const char *const ext[] = { ".gen", ".md", ".bin", ".smd" };
    size_t n = strlen(name);
    for (size_t i = 0; i < sizeof ext / sizeof *ext; i++) {
        size_t e = strlen(ext[i]);
        if (n > e && !SDL_strcasecmp(name + n - e, ext[i])) return 1;
    }
    return 0;
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = n > 0 && n <= (8 << 20) ? malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    *size = b ? (size_t)n : 0;
    return b;
}

/* the file if it is the cartridge (the game's rom_check says so); else NULL and why */
static uint8_t *try_rom(const char *path, size_t *size, char *why, size_t why_size)
{
    size_t n;
    uint8_t *b = read_file(path, &n);
    const char *bad;
    if (!b) { snprintf(why, why_size, "%s: cannot read", path); return NULL; }
    if (n % 0x4000 == 512) { memmove(b, b + 512, n - 512); n -= 512; }   /* (a copier's header) */
    bad = F.o->rom_check ? F.o->rom_check(b, n, F.user) : NULL;
    if (bad) { snprintf(why, why_size, "%s: %s", path, bad); free(b); return NULL; }
    *size = n;
    return b;
}

static int scan_dir(const char *dir, char *why, size_t why_size)
{
    DIR *d = opendir(dir && *dir ? dir : ".");
    struct dirent *e;
    int seen = 0;
    if (!d) return 0;
    while ((e = readdir(d))) {
        char path[1300];
        size_t n;
        uint8_t *b;
        if (!ext_ok(e->d_name)) continue;
        seen++;
        snprintf(path, sizeof path, "%s%s", dir ? dir : "", e->d_name);
        if ((b = try_rom(path, &n, why, why_size))) {
            F.rom = b;
            if (F.o->rom_use) F.o->rom_use(b, n, F.user);
            closedir(d);
            return 1;
        }
    }
    closedir(d);
    if (!seen) snprintf(why, why_size, "no %s file is beside the program", ".gen, .md, .bin or .smd");
    return 0;
}

static int find_rom(char *why, size_t why_size)
{
    char *base = SDL_GetBasePath();
    int ok = 0;
    why[0] = 0;
    if (F.o->rom_path && *F.o->rom_path) {              /* --rom: that file */
        size_t n;
        uint8_t *b = try_rom(F.o->rom_path, &n, why, why_size);
        if (b) { F.rom = b; if (F.o->rom_use) F.o->rom_use(b, n, F.user); return 1; }
        return 0;
    }
    ok = scan_dir(base, why, why_size);
    if (!ok) {                                             /* (the working directory too: a run from the source folder) */
        char w2[400] = "";
        ok = scan_dir(NULL, w2, sizeof w2);
        if (!ok && !strstr(w2, "no ")) snprintf(why, why_size, "%s", w2);
    }
    SDL_free(base);
    return ok;
}

/* ---- the game's thread and the platform handshake ---- */

static int ending(void) { return SDL_AtomicGet(&F.quit) || SDL_AtomicGet(&F.stop); }

static int hold_other;                                /* the same, less a full-window screen (the intro movie is one) */
static int hold_state;                                /* the overlay, the setup screen or a full-window screen (the box) is open */

static void sram_save(void)
{
    mdrt_sram *r = F.m ? mdrt_get_sram(F.m) : NULL;
    FILE *f;
    if (!r || !F.sram_file[0] || !r->size || !r->dirty) return;
    f = fopen(F.sram_file, "wb");
    if (!f) {                                          /* (said once: it is tried again every 300 frames) */
        static int said;
        if (!said++) fprintf(stderr, "save RAM: %s cannot be written\n", F.sram_file);
        return;
    }
    if (fwrite(r->bytes, 1, r->size, f) == r->size) r->dirty = 0;
    fclose(f);
}

static void sram_load(void)
{
    mdrt_sram *r = mdrt_get_sram(F.m);
    FILE *f;
    if (!F.sram_file[0] || !r->size) return;
    f = fopen(F.sram_file, "rb");
    if (!f) return;
    if (fread(r->bytes, 1, r->size, f) != r->size) fprintf(stderr, "%s: short save RAM file\n", F.sram_file);
    fclose(f);
}

static ob_host host;                                  /* (Overbar's settings: defined below) */
static void present(void *user, const uint16_t *px, int w, int h, int pitch)
{
    (void)user;
    static uint32_t pic[320 * 240];
    if (w > 320) w = 320;
    if (h > 240) h = 240;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) pic[y * 320 + x] = 0xFF000000u | mdrt_vdp_rgb(px[y * pitch + x], MDRT_RGB_GPGX);
    if (w <= 320 && h <= 240) pcspr_present(mdrt_get_vdp(F.m), px, pitch, pic, w, h, 320);   /* (NHL 95 PC's skater sprites, when on) */
    if (w <= 320 && h <= 240) whiteice_present(pic, w, h, 320);                  /* (white ice, when on) */
    if (w <= 320 && h <= 240) hats_present(pic, w, h, 320);                      /* (the SNES's hat trick hats, when on) */
    if (w <= 320 && h <= 240) cardcolor_present(mdrt_get_vdp(F.m), pic, w, h, 320);   /* (the players' photos in colour, when on) */
    int cd = w <= 320 && h <= 224 && segacd_present(pic, w, h);   /* (the Sega CD's opening screens, when they are on) */
    if (cd) { w = 320; h = 224; }
    SDL_LockMutex(F.lock);
    F.wide = host.widescreen;
    rules_widescreen(host.widescreen);           /* (the wide rink from the next power on: rules.c) */
    if (F.wide) w = wide_frame(mdrt_get_vdp(F.m), pic, w, h, 320, F.picture, WIDE_MAX_W, cd);   /* (the sides: wide.c) */
    else if (!cd && native_net_on() && (wide_rink_frame(w) || wide_hor_frame(w))) wide_crop_follow(), w = wide_crop(mdrt_get_vdp(F.m), pic, h, 320, F.picture, WIDE_MAX_W);   /* (a 16:9 player's wide rink, here 4:3) */
    else for (int y = 0; y < h; y++) memcpy(F.picture + y * WIDE_MAX_W, pic + y * 320, (size_t)w * 4);
    {   /* a new picture width (the 320-wide screens and the 256-wide ones, or the wide rink's: the window's shape changes): the old picture
         * fades out first, then black while the game's own picture is still fading out (no flash of it back), then the new one fades in,
         * so the shape never changes under a picture that is showing */
        enum { FADE = 15, STEADY = 6 };
        static uint32_t last[WIDE_MAX_W * 240];
        static int last_w, last_h, out, in, dark, still;
        static long prev;
        if (last_w && (w != last_w) && !out && !dark && !native_net_on()) {
            long sum = 0;
            for (int y = 0; y < last_h; y += 4)
                for (int x = 0; x < last_w; x += 4) { uint32_t c = last[y * WIDE_MAX_W + x]; sum += (c >> 16 & 255) + (c >> 8 & 255) + (c & 255); }
            if (sum / ((last_h / 4 + 1) * (last_w / 4 + 1)) > 24) out = FADE, in = FADE;   /* (a picture already black needs no fade) */
        }
        if (out || dark) {
            long sum = 0, n = 0;                      /* (the game's picture now: dark, or no longer getting darker, ends the black) */
            for (int y = 0; y < h; y += 4)
                for (int x = 0; x < w; x += 4, n++) { uint32_t c = F.picture[y * WIDE_MAX_W + x]; sum += (c >> 16 & 255) + (c >> 8 & 255) + (c & 255); }
            sum = n ? sum / n : 0;
            if (out) {                                /* the old picture, darker each frame */
                if (!--out) dark = 1, still = 0, prev = sum;
                for (int y = 0; y < last_h; y++)
                    for (int x = 0; x < last_w; x++) {
                        uint32_t c = last[y * WIDE_MAX_W + x];
                        F.picture[y * WIDE_MAX_W + x] = 0xFF000000u | ((c >> 16 & 255) * out / FADE) << 16 | ((c >> 8 & 255) * out / FADE) << 8 | (c & 255) * out / FADE;
                    }
                w = last_w; h = last_h;
            } else {
                still = sum < prev ? 0 : still + 1;
                prev = sum;
                if (sum <= 24 || still >= STEADY) dark = 0;
                else {                                /* black, in the old shape */
                    for (int y = 0; y < last_h; y++)
                        for (int x = 0; x < last_w; x++) F.picture[y * WIDE_MAX_W + x] = 0xFF000000u;
                    w = last_w; h = last_h;
                }
            }
        }
        if (!out && !dark) {
            for (int y = 0; y < h; y++) memcpy(last + y * WIDE_MAX_W, F.picture + y * WIDE_MAX_W, (size_t)w * 4);
            last_w = w; last_h = h;
            if (in) {                                 /* the new one, brighter each frame */
                in--;
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++) {
                        uint32_t c = F.picture[y * WIDE_MAX_W + x];
                        F.picture[y * WIDE_MAX_W + x] = 0xFF000000u | ((c >> 16 & 255) * (FADE - in) / FADE) << 16 | ((c >> 8 & 255) * (FADE - in) / FADE) << 8 | (c & 255) * (FADE - in) / FADE;
                    }
            }
        }
    }
    F.width = w;
    F.height = h;
    {   static int trace = -1;                       /* (test aid NHL94_FRAMELOG: each picture's width and brightness, as shown) */
        if (trace < 0) trace = getenv("NHL94_FRAMELOG") != NULL;
        if (trace) {
            long sum = 0, n = 0;
            for (int y = 0; y < h; y += 2)
                for (int x = 0; x < w; x += 3, n++) { uint32_t c = F.picture[y * WIDE_MAX_W + x]; sum += (c >> 16 & 255) + (c >> 8 & 255) + (c & 255); }
            fprintf(stderr, "present: frame %ld w %d lum %ld\n", native_frames(), w, n ? sum / n : 0);
        }
    }
    SDL_AtomicSet(&F.fresh, 1);
    SDL_UnlockMutex(F.lock);
}

static void audio(void *user, const int16_t *samples, int frames)
{
    (void)user;
    if (!F.dev || hold_state) return;
    static int16_t mixed[2 * 4096];
    if (frames > 0 && frames <= 4096) {                 /* (the Sega CD disc's music and crowd, when they play) */
        memcpy(mixed, samples, (size_t)frames * 4);
        segacd_mix(mixed, frames, F.rate);
        announcer_mix(mixed, frames, F.rate);
        pcsfx_mix(mixed, frames, F.rate);
        pcsong_mix(mixed, frames, F.rate);
        samples = mixed;
    }
    SDL_QueueAudio(F.dev, samples, (Uint32)frames * 4);
    if (++F.queued == SQUEUE) SDL_PauseAudioDevice(F.dev, 0);
}

static int frame_wait(void *user)
{
    (void)user;
    if (++F.waits % 300 == 0) sram_save();
    while (hold_state && !ending()) SDL_Delay(5);
    if (F.dev) {
        if (F.queued >= SQUEUE) {
            Uint32 enough = (Uint32)(SQUEUE * F.rate / 60 * 4);
            while (SDL_GetQueuedAudioSize(F.dev) > enough && !ending()) SDL_Delay(1);
        }
        F.next_tick = 0;
    } else {                                           /* no sound: the console's clock */
        uint64_t freq = SDL_GetPerformanceFrequency(), now = SDL_GetPerformanceCounter();
        uint64_t period = freq * mdrt_frame_mclks(F.m) / mdrt_master_clock(F.m);
        if (!F.next_tick || now > F.next_tick + period * 4) F.next_tick = now;
        F.next_tick += period;
        while ((now = SDL_GetPerformanceCounter()) < F.next_tick && !ending()) {
            uint64_t ms = (F.next_tick - now) * 1000 / freq;
            if (ms > 1) SDL_Delay((Uint32)(ms - 1));
        }
    }
    return ending();
}

void (*frontend_script)(void *m, uint16_t *b);
static void input(void *user, uint16_t b[MDRT_PADS])
{
    (void)user;
    if (frontend_script) { frontend_script(F.m, b); return; }
    for (int p = 0; p < MDRT_PADS; p++) b[p] = (uint16_t)SDL_AtomicGet(&F.buttons[p]);
}

static int idle(void *user)
{
    (void)user;
    SDL_Delay(1);
    return ending();
}

static int game_thread(void *user)
{
    (void)user;
    if (!F.session) sram_load();
    F.game(F.m, F.user);
    if (!F.session) sram_save();
    SDL_AtomicSet(&F.done, 1);
    return 0;
}

static int start_game(void)
{
    const char *save;
    F.m = mdrt_new(F.cfg);
    if (!F.m) return 0;
    mdrt_pads_set_adaptor(mdrt_get_pads(F.m), F.session ? (native_net_four_way() ? MDRT_ADAPTOR_EA4WAY : MDRT_ADAPTOR_NONE) : F.adaptor);
    if (F.o->options_use && F.opt_values) F.o->options_use(F.opt_values, F.user);
    save = F.o->sram_file ? F.o->sram_file(F.user) : NULL;
    snprintf(F.sram_file, sizeof F.sram_file, "%s", F.session ? "" : save ? save : F.o->sram_path ? F.o->sram_path : "");
    F.thread = SDL_CreateThread(game_thread, "game", NULL);
    if (!F.thread) { mdrt_free(F.m); F.m = NULL; }
    return F.thread != NULL;
}

/* ---- the window ---- */

static void wide_rows_sync(void);
static void ref_row_sync(void);
static void anthem_rule_sync(int row_changed);
static void siren_rule_sync(int row_changed);
static const uint32_t *hook_frame(int *w, int *h)
{
    wide_rows_sync();
    ref_row_sync();
    anthem_rule_sync(0);
    siren_rule_sync(0);
    SDL_LockMutex(F.lock);
    memcpy(F.shown, F.picture, sizeof F.shown);
    *w = F.width > 0 ? F.width : 320;
    *h = F.height > 0 ? F.height : 224;
    SDL_UnlockMutex(F.lock);
    {                                                   /* the picture's rows are WIDE_MAX_W apart; give them packed */
        static uint32_t packed[WIDE_MAX_W * 240];
        for (int y = 0; y < *h; y++) memcpy(packed + y * *w, F.shown + y * WIDE_MAX_W, (size_t)*w * 4);
        return packed;
    }
}

/* a widescreen picture is 5 / 4 as wide as the 4:3 one, whatever its width (wide.h) */
static void hook_wide(int *num, int *den) { if (F.wide) { *num = 5; *den = 4; } }
static int hook_captures(void) { return ob_pads_setup_active() || ob_pads_sides_active() || options_ui_active(); }
static void hook_raw_key(int sc) { if (options_ui_active()) options_ui_key(sc); else ob_pads_setup_key(sc); }
static void hook_raw_button(int slot, int b) { if (options_ui_active()) options_ui_button(slot, b); else ob_pads_setup_button(slot, b); }
static int hook_action(int id) { if (id == OB_ID_CONTROLLERS) ob_pads_setup_open(-1); return 1; }
static void hook_draw_over(const SDL_Rect *dst) { ob_pads_setup_draw(dst); ob_pads_mouse_draw(dst); options_ui_draw(dst); }
static void hook_changed(void) { settings_write(); }
/* the game stopped, its save RAM and the settings written */
static void shutdown_game(void)
{
    SDL_AtomicSet(&F.quit, 1);
    SDL_AtomicSet(&F.stop, 1);
    for (int i = 0; F.thread && i < 200 && !SDL_AtomicGet(&F.done); i++) SDL_Delay(10);
    if (F.thread && SDL_AtomicGet(&F.done)) { SDL_WaitThread(F.thread, NULL); F.thread = NULL; }
    settings_write();
    if (F.dev) { SDL_CloseAudioDevice(F.dev); F.dev = 0; }
}

static void hook_quit(void) { native_net_window_leave(); shutdown_game(); }

/* ---- online play: the Online page (net.c's window sessions) ---- */

/* the k-th pad this machine plays: the window's pad k (its first players) */
static int test_press;                                /* (test aid NHL94_PRESS: the local pads press buttons by themselves) */
static uint16_t window_pad(int pad, int k)
{
    (void)pad;
    if (test_press) {
        static uint32_t x = 12345;
        static int n, b;
        if (n-- <= 0) { x = x * 1103515245u + 12345u; n = 5 + (int)(x >> 16) % 40; b = (int)(x >> 8) & 0xFFF; }
        return (uint16_t)(b & ~MDRT_PAD_START);
    }
    return k >= 0 && k < MDRT_PADS ? (uint16_t)SDL_AtomicGet(&F.buttons[k]) : 0;
}

static int online_start(const ob_online *o, char *msg, int msg_size)
{
    mdrt_net_options n;
    mdrt_net_options_init(&n);
    n.on = 1;
    n.role = o->role;                                 /* (the same numbers: host, join, both) */
    snprintf(n.peer, sizeof n.peer, "%.95s", o->addr);
    n.port = (uint16_t)(o->port > 0 ? o->port : 0);
    n.delay = o->delay;
    snprintf(n.name, sizeof n.name, "%s", o->name[0] ? o->name : "Player");
    n.here = o->here > 0 ? o->here : 1;
    n.players = o->all > 0 ? o->all : 2;
    {
        const char *sim = getenv("NHL94_NETSIM");    /* (test aid) */
        if (sim) sscanf(sim, "%d,%d,%d", &n.sim[0], &n.sim[1], &n.sim[2]);
    }
    return native_net_window_start(&n, window_pad, msg, (size_t)msg_size);
}
static void online_leave(void) { native_net_window_leave(); }
static int online_status(char *line, int line_size) { return native_net_window_status(line, (size_t)line_size); }

/* test aid: NHL94_ONLINE=host|join arms a session on 127.0.0.1 (NHL94_PORT, default 41994) as the Online page
 * would; NHL94_QUIT_AFTER=SECONDS closes the window then; NHL94_PRESS=1 has the local pads press buttons by
 * themselves; NHL94_NETSIM=MS,MS,% a bad link; each new status line goes to stderr */
static Uint32 test_quit_at;
static void test_aid_arm(void)
{
    const char *r = getenv("NHL94_ONLINE"), *port = getenv("NHL94_PORT"), *q = getenv("NHL94_QUIT_AFTER");
    if (q && atoi(q) > 0) test_quit_at = SDL_GetTicks() + (Uint32)atoi(q) * 1000u;
    if (!r || (strcmp(r, "host") && strcmp(r, "join"))) return;
    test_press = getenv("NHL94_PRESS") != NULL;
    ob_online o;
    char msg[200];
    memset(&o, 0, sizeof o);
    snprintf(o.name, sizeof o.name, "%s", r);
    o.role = strcmp(r, "host") ? 1 : 0;
    snprintf(o.addr, sizeof o.addr, "127.0.0.1");
    o.port = port && atoi(port) > 0 ? atoi(port) : 41994;
    o.delay = 2;
    o.here = 1;
    o.all = 2;
    if (online_start(&o, msg, sizeof msg)) fprintf(stderr, "NHL94_ONLINE: %s\n", msg);
}
static void test_aid_tick(void)
{
    static char last[160];
    char line[160];
    if (getenv("NHL94_ONLINE")) {
        online_status(line, sizeof line);
        if (strcmp(line, last)) { fprintf(stderr, "online: %s\n", line); snprintf(last, sizeof last, "%s", line); }
    }
    if (test_quit_at && SDL_TICKS_PASSED(SDL_GetTicks(), test_quit_at)) SDL_AtomicSet(&F.quit, 1);
}

/* the console powered on again (the game on its thread anew): for a session's match (session), or
 * for a new roster set */
static void restart_console(int session)
{
    SDL_AtomicSet(&F.stop, 1);
    for (int i = 0; F.thread && i < 300 && !SDL_AtomicGet(&F.done); i++) SDL_Delay(10);
    if (!SDL_AtomicGet(&F.done)) { fprintf(stderr, "netplay: the game did not stop for the session\n"); native_net_window_leave(); SDL_AtomicSet(&F.stop, 0); return; }
    SDL_WaitThread(F.thread, NULL);
    F.thread = NULL;
    mdrt_free(F.m);
    F.m = NULL;
    SDL_AtomicSet(&F.stop, 0);
    SDL_AtomicSet(&F.done, 0);
    F.session = session;
    segacd_set_session(session);
    if (F.dev) { SDL_ClearQueuedAudio(F.dev); SDL_PauseAudioDevice(F.dev, 1); F.queued = 0; }
    if (!start_game()) { fprintf(stderr, "the game could not start\n"); SDL_AtomicSet(&F.quit, 1); }
}

static ob_host host;

/* ---- the overlay's Game page: the Sega CD's changes to the game, each off (the cartridge's) or on ----
 * Each row is a rule (src/rules.h RULE_CD_*), kept in overbar.ini (game_cd_...) and in settings.ini
 * (rules.cd_...). A change plays from the next frame (the rules are in every state); in an online
 * session the host's rules hold, so a change there waits for the session's end. */
static const char *const cd_off_on[2] = { "off", "on" };
static const char *game_help[64];                /* the Game page's description lines (ob_game_set_help) */
static const struct { int rule; const char *key; } cd_rows[] = {
    { RULE_CD_SKATING, "cd_skating" }, { RULE_CD_CORNERS, "cd_corners" }, { RULE_CD_PSBOX, "cd_penalty_shot_box" },
    { RULE_CD_PASSWORD, "cd_password" }, { RULE_CD_CLEARSHOT, "cd_clear_shots" }, { RULE_CD_COLOURS, "cd_colours" },
    { RULE_CD_SOUNDS, "cd_sounds" }, { RULE_CD_SONGS, "cd_songs" }, { RULE_CD_PERIOD_END, "cd_period_end" },
    { RULE_HOLD, "hold_play" },
    { RULE_FIGHT, "fighting" },                          /* (NHL 93's fighting: group 4) */
    { RULE_ANNOUNCE, "announce_timing" },                /* (when the rink announcer speaks, and play waits for it: group 4) */
};
#define CD_ROWS ((int)(sizeof cd_rows / sizeof cd_rows[0]))
#define CD_HOLD (CD_ROWS - 3)                              /* (RULE_HOLD) */
#define CD_FIGHT (CD_ROWS - 2)                             /* (RULE_FIGHT) */
#define CD_ANNOUNCE (CD_ROWS - 1)                          /* (the last rule row: RULE_ANNOUNCE) */
#define CD_ROSTERS CD_ROWS                                /* (the roster set, the built-in "sega-cd", list entry 1) */
#define CD_MUSIC (CD_ROWS + 1)                            /* (this machine's sound only: the disc's music, its crowd bed) */
#define CD_CROWD (CD_ROWS + 2)
#define CD_SFX (CD_ROWS + 3)
#define CD_VOICE (CD_ROWS + 4)
#define CD_SCREENS (CD_ROWS + 5)
#define CD_TITLE (CD_ROWS + 6)
#define CD_INTRO (CD_ROWS + 7)
#define CD_DEDITHER (CD_ROWS + 8)
#define CD_SMOOTH (CD_ROWS + 9)
#define CD_COLOUR (CD_ROWS + 10)
#define CD_MOVIELOOK (CD_ROWS + 11)                        /* (the movie's scaling: the game's or its own, Overbar's host.movie_own, also on Scaling (Movie)) */
#define CD_PHOTOS (CD_ROWS + 12)
#define CD_CAREER (CD_ROWS + 13)
#define CD_ANNOUNCER (CD_ROWS + 15)                        /* (NHL 95 PC's rink announcer, its own files: group 4) */
#define CD_PCPHOTOS (CD_ROWS + 14)                        /* (the cartridge's photos in colour, from the player's PC files: cardcolor.c) */
#define CD_WHITEICE (CD_ROWS + 16)                         /* (white ice: group 4) */
#define CD_HATS (CD_ROWS + 17)                            /* (NHL 94 SNES's hat trick hats, its own ROM: group 4) */
#define CD_WIDESCREEN (CD_ROWS + 18)                      /* (the picture 4:3 or 16:9: Overbar's host.widescreen, also on its Scaling page) */
#define CD_PCSPR (CD_ROWS + 19)                            /* (NHL 95 PC's skater sprites, its own files: group 4) */
#define CD_PCSIREN (CD_ROWS + 20)                          /* (NHL 95 PC's goal siren, its own file: group 4) */
#define CD_PCHORN (CD_ROWS + 21)                           /* (and its end of period horn) */
#define CD_PCREF (CD_ROWS + 22)                            /* (a referee on the ice, from NHL 95 PC's frames: group 4) */
#define CD_PCLOGO (CD_ROWS + 23)                           /* (NHL 95 PC's centre ice logos, its own files: group 4) */
#define CD_PCANTHEM (CD_ROWS + 24)                         /* (NHL 95 PC's national anthems, its own files: group 4) */
#define CD_CROWD_CARTVOL (CD_ROWS + 25)                    /* (the three crowd beds: CD_CROWD is the cartridge's, then each bed's volume) */
#define CD_CROWD_CD (CD_ROWS + 26)
#define CD_CROWD_CDVOL (CD_ROWS + 27)
#define CD_CROWD_PC (CD_ROWS + 28)
#define CD_CROWD_PCVOL (CD_ROWS + 29)
#define CD_SPLIT (CD_ROWS + 30)                          /* (the movie's 50/50 demo: the disc's own left, the enhanced right) */
#define CD_PCREFPHYS (CD_ROWS + 31)                        /* (the referee and the skaters, as NHL 95 PC's: off, the picture's alone (visual), or in play (RULE_REFEREE)) */
#define CD_OPTS (CD_ROWS + 32)
#define CROWD_VOLS 40                                     /* (the crowd volumes: 5% to 200% in steps of 5%) */
#define CROWD_VOL_100 19                                  /* (the choice for 100%) */
static int crowd_vol_of(int idx) { int v = ob_game_get(&host, idx); return 5 * ((v < 0 ? 0 : v >= CROWD_VOLS ? CROWD_VOLS - 1 : v) + 1); }
static void crowd_apply(void)
{
    segacd_set_crowd((ob_game_get(&host, CD_CROWD) ? 1 : 0) | (ob_game_get(&host, CD_CROWD_CD) ? 2 : 0));
    segacd_set_crowd_vol(crowd_vol_of(CD_CROWD_CARTVOL), crowd_vol_of(CD_CROWD_CDVOL));
    pcsfx_set_crowd(ob_game_get(&host, CD_CROWD_PC) != 0);
    pcsong_rock_set(ob_game_get(&host, CD_CROWD_PC) != 0);              /* (and its organ and rock cues at the whistles) */
    pcsfx_set_crowd_vol(crowd_vol_of(CD_CROWD_PCVOL));
}
static ob_game_opt cd_opts[CD_OPTS];
static const char *const cd_logo_pc[3] = { "Cartridge", "NHL 95 PC", "NHL 94 PC" };
/* the Widescreen rows (the Game page's, Overbar's Scaling page's) are one setting: the Scaling page's change shown on the Game page */
static void wide_rows_sync(void)
{
    static int seen = -1;
    if (!host.game_ready || host.widescreen == seen) return;
    if (seen >= 0 && ob_game_get(&host, CD_WIDESCREEN) != host.widescreen) ob_game_set(&host, CD_WIDESCREEN, host.widescreen);
    seen = host.widescreen;
    static int movie_seen = -1;                           /* (Movie scaling: the Game page's row and Scaling (Movie)'s are one setting too) */
    if (host.movie_own == movie_seen) return;
    if (movie_seen >= 0 && ob_game_get(&host, CD_MOVIELOOK) != host.movie_own) ob_game_set(&host, CD_MOVIELOOK, host.movie_own);
    movie_seen = host.movie_own;
}
static SDL_atomic_t cd_dedither;                          /* the movie's de-dither (Overbar's ob_postfx_mode) */
static SDL_atomic_t cd_smooth, cd_colour, cd_split;
static int cd_restart;                                    /* (the roster set changed: the console powers on again) */

/* the option of the rules page with this key, or -1 */
static int option_index(const char *key)
{
    for (int i = 0; i < F.o->noptions; i++) if (F.o->options[i].key && !strcmp(F.o->options[i].key, key)) return i;
    return -1;
}

static void rule_from_row(const char *bare, int rule, int value);
static void cd_changed(void *ctx, int index, int value)
{
    (void)ctx;
    if (index == CD_MUSIC) { segacd_set_music(value); return; }
    if (index == CD_CROWD || index == CD_CROWD_CARTVOL || index == CD_CROWD_CD || index == CD_CROWD_CDVOL || index == CD_CROWD_PC || index == CD_CROWD_PCVOL) { crowd_apply(); return; }
    if (index == CD_VOICE) { segacd_set_voice(value); return; }
    if (index == CD_SCREENS) { segacd_set_screens(value); return; }
    if (index == CD_TITLE) { segacd_set_title(value); return; }
    if (index == CD_INTRO) { segacd_set_intro(value); return; }
    if (index == CD_DEDITHER) { SDL_AtomicSet(&cd_dedither, value); return; }
    if (index == CD_SMOOTH) { SDL_AtomicSet(&cd_smooth, value); return; }
    if (index == CD_COLOUR) { SDL_AtomicSet(&cd_colour, value); return; }
    if (index == CD_SPLIT) { SDL_AtomicSet(&cd_split, value); return; }
    if (index == CD_MOVIELOOK) { host.movie_own = value != 0; return; }   /* (hook_frame keeps the two rows alike) */
    if (index == CD_PHOTOS) { segacd_set_photos(value); return; }
    if (index == CD_CAREER) { segacd_set_career(value, ending); return; }
    if (index == CD_PCPHOTOS) { cardcolor_set(value); return; }
    if (index == CD_SFX) { segacd_set_sfx(value); return; }
    if (index == CD_ANNOUNCER) {                          /* (turned on while the timing says "talks over the face off": the PC's, right after the goal) */
        announcer_set(value);
        if (value && ob_game_get(&host, CD_ANNOUNCE) == 0) ob_game_set(&host, CD_ANNOUNCE, 1);
        return;
    }
    if (index == CD_PCSIREN) { pcsfx_set_goal(value); siren_rule_sync(1); return; }
    if (index == CD_PCHORN) { pcsfx_set_horn(value); return; }
    if (index == CD_WHITEICE) { whiteice_set(value); return; }
    if (index == CD_HATS) { hats_set(value); return; }
    if (index == CD_PCSPR) { pcspr_set(value); return; }
    if (index == CD_PCREF) { pcspr_ref_set(value); return; }
    if (index == CD_PCREFPHYS) {                          /* (Visual only: the picture's; Full: RULE_REFEREE, a game rule) */
        pcspr_ref_phys_set(value == 1);
        rule_from_row("referee_in_play", RULE_REFEREE, value == 2);
        return;
    }
    if (index == CD_PCLOGO) { pcspr_logo_set(value); return; }
    if (index == CD_PCANTHEM) { pcsong_set(value); anthem_rule_sync(1); return; }
    if (index == CD_WIDESCREEN) { host.widescreen = value != 0; return; }   /* (hook_frame keeps the two rows alike) */
    if (index == CD_ROSTERS && F.opt_values) {             /* a roster set plays from a power on: the console's, now */
        int i = option_index("rules.rosters");
        if (i < 0) return;
        int now = F.opt_values[i], v = value ? 1 : now == 1 ? 0 : now;
        if (v == now) return;
        F.opt_values[i] = v;
        mdrt_settings_set(F.set, "rules.rosters", F.o->options[i].names[v]);
        settings_write();
        if (F.session) return;
        if (F.o->options_use) F.o->options_use(F.opt_values, F.user);
        cd_restart = F.thread != NULL;
        return;
    }
    if (index < 0 || index >= CD_ROWS) return;
    rule_from_row(cd_rows[index].key, cd_rows[index].rule, value);
}

/* a Game page row's rule (rules.<key>) set to value: kept in settings.ini, the next power on's and this game's from now on */
static void rule_from_row(const char *bare, int rule, int value)
{
    char key[64];
    if (!F.opt_values) return;
    snprintf(key, sizeof key, "rules.%s", bare);
    int i = option_index(key);
    if (i < 0) return;
    if (F.opt_values[i] == value) return;
    F.opt_values[i] = value;
    mdrt_settings_set(F.set, key, F.o->options[i].names[value]);
    settings_write();
    if (F.session) return;                              /* (a session plays by the host's rules) */
    if (F.o->options_use) F.o->options_use(F.opt_values, F.user);   /* (the next power on's) */
    rules[rule] = (uint8_t)value;                         /* (and this game's, from now on) */
}

/* the referee row reads Full while RULE_REFEREE is on (the Game rules page can turn it on and off too) */
static void ref_row_sync(void)
{
    if (!host.game_ready || !F.opt_values) return;
    int i = option_index("rules.referee_in_play");
    if (i < 0) return;
    int row = ob_game_get(&host, CD_PCREFPHYS), on = F.opt_values[i] != 0;
    if (on && row != 2) ob_game_set(&host, CD_PCREFPHYS, 2);
    else if (!on && row == 2) { ob_game_set(&host, CD_PCREFPHYS, 1); pcspr_ref_phys_set(1); }
}

/* the anthems' row and RULE_ANTHEM (the game's wait for the anthem, so every machine of a session waits the same): the rule
 * is the set the row asks for that this machine has (the other when only it was found, off when neither was); a change on
 * the Game rules page shows on the row */
static int anthem_rule_of(int row)
{
    int f95 = pcsong_found95(), f96 = pcsong_found96();
    if (row == 1) return f95 ? 1 : f96 ? 2 : 0;
    if (row == 2) return f96 ? 2 : f95 ? 1 : 0;
    return 0;
}
static void anthem_rule_sync(int row_changed)
{
    static int last = -1;
    if (!host.game_ready || !F.opt_values) return;
    int i = option_index("rules.anthems");
    if (i < 0) return;
    int rule = F.opt_values[i], row = ob_game_get(&host, CD_PCANTHEM);
    if (!row_changed && last >= 0 && rule != last) {       /* (the Game rules page changed it) */
        last = rule;
        if (row != rule) ob_game_set(&host, CD_PCANTHEM, rule);
        pcsong_set(rule);
        return;
    }
    if (!pcsong_searched()) return;                       /* (the files are still being looked for: the rule as it is) */
    int want = anthem_rule_of(row);
    if (want != rule) rule_from_row("anthems", RULE_ANTHEM, want);
    last = F.opt_values[i];
}

/* the PC goal siren's row and RULE_SIREN (the announcer's wait for the siren, RULE_ANNOUNCE 2, so every machine of a session
 * waits the same): the rule is on while the row is and this machine has the PC's file; a change on the Game rules page shows on
 * the row */
static void siren_rule_sync(int row_changed)
{
    static int last = -1;
    if (!host.game_ready || !F.opt_values) return;
    int i = option_index("rules.goal_siren");
    if (i < 0) return;
    int rule = F.opt_values[i], row = ob_game_get(&host, CD_PCSIREN);
    if (!row_changed && last >= 0 && rule != last) {       /* (the Game rules page changed it) */
        last = rule;
        if (row != rule) ob_game_set(&host, CD_PCSIREN, rule);
        pcsfx_set_goal(rule);
        return;
    }
    if (!pcsfx_searched()) return;                        /* (the file is still being looked for: the rule as it is) */
    int want = row && pcsfx_found();
    if (want != rule) rule_from_row("goal_siren", RULE_SIREN, want);
    last = F.opt_values[i];
}

/* the disc's intro movie (segacd_intro_hook, the game's thread): the window's loop plays it, this waits */
static SDL_atomic_t intro_state;                         /* 1 asked for, 2 playing, 0 none */
static void intro_hook(void)
{
    if (F.session || !segacd_disc()) return;           /* (a session's machines keep step: no movie) */
    SDL_AtomicSet(&intro_state, 1);
    while (SDL_AtomicGet(&intro_state) && !ending()) SDL_Delay(5);
}

static void intro_tick(void)
{
    int st = SDL_AtomicGet(&intro_state);
    char intro_base[1100];                               /* (the made files: segacd-intro.dedither, .smooth, .colour) */
    snprintf(intro_base, sizeof intro_base, "%s", beside("segacd-intro"));
    if (st == 1) SDL_AtomicSet(&intro_state, segacd_movie_start(segacd_disc(), "HOCKEY.SEG", F.dev, F.rate, SDL_AtomicGet(&cd_dedither),
                                                                        SDL_AtomicGet(&cd_smooth), SDL_AtomicGet(&cd_colour), SDL_AtomicGet(&cd_split), intro_base,
                                                                        beside("rife-v4.6")) ? 2 : 0);
    else if (st == 2 && !segacd_movie_tick()) SDL_AtomicSet(&intro_state, 0);
    if (st && !SDL_AtomicGet(&intro_state)) {           /* (over or skipped: black until the game's next frame, not the screen before the movie) */
        SDL_LockMutex(F.lock);
        for (int i = 0; i < WIDE_MAX_W * 240; i++) F.picture[i] = 0xFF000000u;
        SDL_AtomicSet(&F.fresh, 1);
        SDL_UnlockMutex(F.lock);
    }
}

/* the disc's rows say so when no disc was found beside the program (they do nothing then) */
static void cd_no_disc(void)
{
    static int done;
    if (done || !segacd_searched()) return;
    done = 1;
    if (segacd_disc()) return;
    static char labels[CD_OPTS][64];
    for (int k = CD_MUSIC; k <= CD_CAREER; k++) {
        if (!cd_opts[k].label) continue;
        snprintf(labels[k], sizeof labels[k], "%s (no disc found)", cd_opts[k].label);
        cd_opts[k].label = labels[k];
    }
}

/* the announcer's row says so when no XBRUCE2.VIV was found beside the program */
static void pa_no_file(void)
{
    static int done;
    static char label[64];
    if (done || !announcer_searched()) return;
    done = 1;
    if (announcer_found()) return;
    snprintf(label, sizeof label, "%s (no NHL 95 PC disc image found)", cd_opts[CD_ANNOUNCER].label);
    cd_opts[CD_ANNOUNCER].label = label;
}

/* the hats' row says so when no NHL 94 SNES ROM was found beside the program */
static void hats_no_file(void)
{
    static int done;
    static char label[64];
    if (done || !hats_searched()) return;
    done = 1;
    if (hats_found()) return;
    snprintf(label, sizeof label, "%s (no NHL 94 SNES ROM found)", cd_opts[CD_HATS].label);
    cd_opts[CD_HATS].label = label;
}

/* the PC sprites' row says so when no NHL 95 PC sprite files were found */
static void pcspr_no_file(void)
{
    static int done;
    static char label[64];
    if (done || !pcspr_searched()) return;
    done = 1;
    if (pcspr_found()) {                                  /* (the sprites are there: are the centre ice logos?) */
        static char lg[96];
        if (!pcspr_logos()) { snprintf(lg, sizeof lg, "%s (no logos in the NHL 95 PC files found)", cd_opts[CD_PCLOGO].label); cd_opts[CD_PCLOGO].label = lg; }
        else if (pcspr_logos() == 1) { snprintf(lg, sizeof lg, "%s (no NHL94PC folder found)", cd_opts[CD_PCLOGO].label); cd_opts[CD_PCLOGO].label = lg; }   /* (NHL 94 PC then shows NHL 95 PC's) */
        return;
    }
    snprintf(label, sizeof label, "%s (no NHL 95 PC disc image found)", cd_opts[CD_PCSPR].label);
    cd_opts[CD_PCSPR].label = label;
    static char lg2[96];
    snprintf(lg2, sizeof lg2, "%s (no NHL 95 PC disc image found)", cd_opts[CD_PCLOGO].label);
    cd_opts[CD_PCLOGO].label = lg2;
}

/* the PC siren and horn rows say so when no PCFF001.DIG was found */
static void pcsfx_no_file(void)
{
    static int done;
    static char label[2][64];
    if (done || !pcsfx_searched()) return;
    done = 1;
    if (pcsfx_found()) return;
    snprintf(label[0], sizeof label[0], "%s (no NHL 95 PC disc image found)", cd_opts[CD_PCSIREN].label);
    snprintf(label[1], sizeof label[1], "%s (no NHL 95 PC disc image found)", cd_opts[CD_PCHORN].label);
    cd_opts[CD_PCSIREN].label = label[0];
    cd_opts[CD_PCHORN].label = label[1];
    static char lg[64];
    snprintf(lg, sizeof lg, "%s (no NHL 95 PC disc image found)", cd_opts[CD_CROWD_PC].label);
    cd_opts[CD_CROWD_PC].label = lg;
}

/* the anthems' row says which game's discs were not found (the other's anthems play then) */
static void pcsong_no_file(void)
{
    static int done;
    static char label[96];
    if (done || !pcsong_searched()) return;
    done = 1;
    int f95 = pcsong_found95(), f96 = pcsong_found96();
    if (f95 && f96) return;
    snprintf(label, sizeof label, "%s (%s)", cd_opts[CD_PCANTHEM].label,
             !f95 && !f96 ? "no NHL 95 or 96 PC disc image found" : !f96 ? "no NHL 96 PC disc: 95's play" : "no NHL 95 PC disc: 96's play");
    cd_opts[CD_PCANTHEM].label = label;
}

/* the colour photos' row says so when neither PC game's portraits were found */
static void pc_no_files(void)
{
    static int done;
    if (done || !cardcolor_searched()) return;
    done = 1;
    if (!cardcolor_found()) cd_opts[CD_PCPHOTOS].label = "Colour player photos (no NHL 95 or 94 PC files found)";
}

static void game_page_init(void)
{
    static const char *const labels[CD_ROWS] = { "Sega CD skating", "Sega CD corner bounce", "Sega CD penalty shot box",
                                                  "Sega CD playoff password", "Sega CD shot data fix", "Sega CD uniforms",
                                                  "Sega CD sound choices", "Sega CD song timing", "Sega CD period end", "PC between-play pause", "Fighting", "Rink announcer: when he speaks" };
    for (int k = 0; k < CD_ROWS; k++) {
        char key[64];
        snprintf(key, sizeof key, "rules.%s", cd_rows[k].key);
        int i = option_index(key);
        cd_opts[k] = (ob_game_opt){ cd_rows[k].key, labels[k], i >= 0 ? F.o->options[i].names : cd_off_on, i >= 0 ? F.o->options[i].count : 2, i >= 0 && F.opt_values ? F.opt_values[i] : 0, 1, (cd_rows[k].rule == RULE_HOLD || cd_rows[k].rule == RULE_FIGHT || cd_rows[k].rule == RULE_ANNOUNCE) ? 4 : 1 };
    }
    int r = option_index("rules.rosters");
    cd_opts[CD_ROSTERS] = (ob_game_opt){ "cd_rosters", "Sega CD rosters (restarts)", cd_off_on, 2, r >= 0 && F.opt_values && F.opt_values[r] == 1, 1, 2 };
    static const char *const music[2] = { "cartridge", "Sega CD" }, *const crowd[3] = { "cartridge", "Sega CD", "both" };
    cd_opts[CD_MUSIC] = (ob_game_opt){ "cd_music", "Music", music, 2, 0, 0, 3 };
    (void)crowd;
    static char vol_names[CROWD_VOLS][8];                /* (saved by name: an older ini's "25%" .. "200%" still reads) */
    static const char *vols[CROWD_VOLS];
    for (int k = 0; k < CROWD_VOLS; k++) { snprintf(vol_names[k], sizeof vol_names[k], "%d%%", 5 * (k + 1)); vols[k] = vol_names[k]; }
    cd_opts[CD_CROWD] = (ob_game_opt){ "crowd_cart", "Cartridge crowd bed", cd_off_on, 2, 1, 0, 3 };   /* (the cartridge's own noise, following the crowd meter) */
    cd_opts[CD_CROWD_CARTVOL] = (ob_game_opt){ "crowd_cart_vol", "Cartridge crowd volume", (const char *const *)vols, CROWD_VOLS, CROWD_VOL_100, 0, 3 };
    cd_opts[CD_CROWD_CD] = (ob_game_opt){ "crowd_cd", "Sega CD crowd bed", cd_off_on, 2, 0, 0, 3 };     /* (the disc's track 2) */
    cd_opts[CD_CROWD_CDVOL] = (ob_game_opt){ "crowd_cd_vol", "Sega CD crowd volume", (const char *const *)vols, CROWD_VOLS, CROWD_VOL_100, 0, 3 };
    cd_opts[CD_CROWD_PC] = (ob_game_opt){ "crowd_pc", "PC crowd bed", cd_off_on, 2, 0, 0, 4 };         /* (NHL 95 PC's two crowd voices, on the same crowd meter) */
    cd_opts[CD_CROWD_PCVOL] = (ob_game_opt){ "crowd_pc_vol", "PC crowd volume", (const char *const *)vols, CROWD_VOLS, CROWD_VOL_100, 0, 4 };
    static const char *const voice[3] = { "off", "speech only", "speech and music" };   /* (speech only: Ron Barr alone as on the disc) */
    cd_opts[CD_VOICE] = (ob_game_opt){ "cd_voice", "Ron Barr scouting voice", voice, 3, 0, 0, 3 };
    cd_opts[CD_SCREENS] = (ob_game_opt){ "cd_screens", "Opening screens", music, 2, 0, 0, 3 };
    cd_opts[CD_TITLE] = (ob_game_opt){ "cd_title", "Title page", music, 2, 0, 0, 3 };
    cd_opts[CD_INTRO] = (ob_game_opt){ "cd_intro", "Intro movie", cd_off_on, 2, 0, 0, 3 };
    cd_opts[CD_DEDITHER] = (ob_game_opt){ "cd_dedither", "Movie de-dither", ob_postfx_names, OB_POSTFX_MODES, 0, 0, 3 };
    static const char *const smooth[2] = { "original", "30 fps" }, *const colour[2] = { "original", "enhanced" };
    cd_opts[CD_SMOOTH] = (ob_game_opt){ "cd_smooth", "Movie frame rate", smooth, 2, 0, 0, 3 };
    cd_opts[CD_COLOUR] = (ob_game_opt){ "cd_colour", "Movie colour", colour, 2, 0, 0, 3 };
    static const char *const split[2] = { "off", "50/50 split" };
    cd_opts[CD_SPLIT] = (ob_game_opt){ "cd_movie_split", "Movie 50/50 demo", split, 2, 0, 0, 3 };   /* (left the disc's own, right de-dithered, coloured, 30 fps) */
    static const char *const movie_scaling[2] = { "same as the game", "its own" };
    host.has_movie = 1;                                   /* (the overlay lists Scaling (Movie): the intro movie's own look) */
    cd_opts[CD_MOVIELOOK] = (ob_game_opt){ "movie_scaling", "Movie scaling", movie_scaling, 2, host.movie_own ? 1 : 0, 0, 3 };   /* (its own: the overlay's Scaling (Movie) page) */
    cd_opts[CD_PHOTOS] = (ob_game_opt){ "cd_photos", "Player photos", music, 2, 0, 0, 3 };
    static const char *const career[2] = { "off", "B on a player card" };
    cd_opts[CD_CAREER] = (ob_game_opt){ "cd_career", "Career stats", career, 2, 0, 0, 3 };
    cd_opts[CD_SFX] = (ob_game_opt){ "cd_sfx", "Sound effects", music, 2, 0, 0, 3 };
    cd_opts[CD_ANNOUNCER] = (ob_game_opt){ "pc_announcer", "Rink announcer", cd_off_on, 2, 1, 0, 4 };   /* (NHL 95 PC's voice: goals, penalties, the last minute, the three stars) */
    {   static const char *const hp[] = {
            "The Sega CD's skating speed, a touch different from the cartridge.", "The puck's bounce off the corners, as on the Sega CD.",
            "The penalty shot box takes the pad, as on the Sega CD.", "The Sega CD's playoff password rules.",
            "The Sega CD's fix for the shot data.", "The Sega CD's uniform colours for each team.",
            "The Sega CD's sounds: the ding at line changes and breaks.", "Songs play when the Sega CD plays them: in the second half of a period.",
            "At a period's end the Sega CD goes straight to the song.",
            "NHL 95 PC's between-play time: play waits a moment after a goal, penalty or penalty shot before the face off, and at every whistle the players and the referee skate from where they were to the face off spot instead of appearing there.",
            "NHL 93's fights: a hard check between two fighters can start one (B low punch, C high punch, A grab).",
            "When the rink announcer speaks. Right after the goal or penalty, as on NHL 95 PC: play waits until he is done (the goal celebration lasts as long as the speech, with or without the between-play pause). After the goal song: a home goal's speech starts when the song ends. Talks over the face off: the cartridge's pace." };
        for (int k = 0; k < CD_ROWS; k++) game_help[k] = hp[k];
        game_help[CD_ROSTERS] = "The Sega CD's rosters (the game restarts to change them).";
        game_help[CD_MUSIC] = "Where the songs come from: the cartridge or the Sega CD disc.";
        game_help[CD_CROWD] = "The cartridge's crowd noise, which swells with the crowd meter.";
        game_help[CD_CROWD_CARTVOL] = "How loud the cartridge's crowd is.";
        game_help[CD_CROWD_CD] = "The Sega CD disc's crowd bed (track 2).";
        game_help[CD_CROWD_CDVOL] = "How loud the Sega CD crowd is.";
        game_help[CD_CROWD_PC] = "NHL 95 PC's crowd, from your NHL 95 PC files; it follows the crowd meter the way the PC game does, and the rink organ plays the home team's cues at the whistles.";
        game_help[CD_CROWD_PCVOL] = "How loud NHL 95 PC's crowd is.";
        game_help[CD_VOICE] = "Ron Barr's scouting lines from the disc; speech and music plays them over the scouting song.";
        game_help[CD_SCREENS] = "The opening screens: the cartridge's or the disc's.";
        game_help[CD_TITLE] = "The title page: the cartridge's or the disc's.";
        game_help[CD_INTRO] = "The disc's opening movie before the title.";
        game_help[CD_SPLIT] = "A side by side demo of the intro movie: the left half as the disc plays it, the right half de-dithered, colour enhanced and at 30 fps.";
        game_help[CD_SFX] = "Sound effects: the cartridge's or the disc's.";
    }
    ob_game_set_help(game_help);
    announcer_start();
    hats_start();
    pcspr_start();
    pcsfx_start();
    pcsong_start();
    cd_opts[CD_PCSIREN] = (ob_game_opt){ "pc_goal_siren", "PC goal siren", cd_off_on, 2, 1, 0, 4 };   /* (NHL 95 PC's siren in place of the cartridge's) */
    cd_opts[CD_PCHORN] = (ob_game_opt){ "pc_period_horn", "PC end of period horn", cd_off_on, 2, 1, 0, 4 };
    cd_opts[CD_PCSPR] = (ob_game_opt){ "pc_sprites", "PC player sprites", cd_off_on, 2, 0, 0, 4 };   /* (NHL 95 PC's skaters and goalies, the cartridge's where the PC has none) */
    cd_opts[CD_PCREF] = (ob_game_opt){ "pc_referee", "Referee on the ice", cd_off_on, 2, 1, 0, 4 };   /* (NHL 95 PC's referee skating with the play and dropping the puck) */
    static const char *const ref_phys[3] = { "off", "visual only", "full" };   /* (an older ini's "on" is not one: the default, visual only) */
    cd_opts[CD_PCREFPHYS] = (ob_game_opt){ "pc_referee_bumps", "Referee physics", ref_phys, 3, 1, 0, 4 };   /* (NHL 95 PC's checkcx for the referee: his side only, or in play) */
    cd_opts[CD_PCLOGO] = (ob_game_opt){ "pc_centre_logo", "Centre ice logo", cd_logo_pc, 3, 1, 0, 4 };   /* (the cartridge's crest or NHL 95 PC's) */
    static const char *const anthem_of[3] = { "Off", "NHL 95 PC", "NHL 96 PC" };
    cd_opts[CD_PCANTHEM] = (ob_game_opt){ "pc_anthems", "National anthems", anthem_of, 3, 1, 0, 4 };   /* (NHL 95 PC's FM songs or NHL 96 PC's recordings, played before the first face off) */
    cd_opts[CD_HATS] = (ob_game_opt){ "snes_hats", "Hat trick hats", cd_off_on, 2, 1, 0, 4 };   /* (NHL 94 SNES: hats on the ice after a home skater's third goal) */
    cd_opts[CD_WHITEICE] = (ob_game_opt){ "white_ice", "White ice", cd_off_on, 2, 0, 0, 4 };   /* (the PC games' white rink in place of the cartridge's pale cyan) */
    static const char *const shape[2] = { "4:3", "16:9" };
    cd_opts[CD_WIDESCREEN] = (ob_game_opt){ "widescreen", "Widescreen", shape, 2, host.widescreen ? 1 : 0, 0, 4 };   /* (each machine its own: wide.c) */
    cd_opts[CD_PCPHOTOS] = (ob_game_opt){ "pc_photos", "Colour player photos", cd_off_on, 2, 1, 0, 4 };
        game_help[CD_ANNOUNCER] = "NHL 95 PC's rink announcer: goals, penalties, last minute.";
        game_help[CD_PCSIREN] = "NHL 95 PC's goal siren, from your NHL 95 PC files, in place of the cartridge's.";
        game_help[CD_PCHORN] = "NHL 95 PC's horn at the end of a period, from your NHL 95 PC files.";
        game_help[CD_PCSPR] = "NHL 95 PC's skaters and goalies, from your NHL 95 PC files.";
        game_help[CD_PCREF] = "A referee skating on the ice, NHL 95 PC's, from your NHL 95 PC files.";
        game_help[CD_PCREFPHYS] = "The referee on the ice and the play, as in NHL 95 PC. Visual only: skaters who run into him bump him aside, and play is unchanged. Full: he is in play: skaters bounce off him, the puck deflects off him, a pad player's check knocks him down, and every third knock down is a roughing penalty (a game rule: online, the host's).";
        game_help[CD_PCLOGO] = "The logo at centre ice: the cartridge's, NHL 95 PC's, or NHL 94 PC's (NHL 95's where 94 has none) from your PC game files.";
        game_help[CD_PCANTHEM] = "The national anthems before the first face off: NHL 95 PC's songs or NHL 96 PC's recordings, from your PC game discs (the other when one is missing). The crowd cheers over the last bars, as the PC's does.";
        game_help[CD_HATS] = "Hats thrown on the ice after a home skater's third goal.";
        game_help[CD_WHITEICE] = "The PC games' white rink instead of the pale cyan one.";
        game_help[CD_PCPHOTOS] = "Player photos in colour.";
    segacd_start();
    cardcolor_start();
    segacd_intro_hook = intro_hook;
    /* the presets: a quick select over the rows below (each row stays free to change; the row then reads Custom).
     * Cartridge = every row at its default; Sega CD = everything the disc gives, as the disc has it; Recommended =
     * the cartridge's game with the disc's presentation. -1 would leave a row alone (a new row starts there). */
    static signed char pv[3][CD_OPTS];
    for (int p = 0; p < 3; p++) for (int k = 0; k < CD_OPTS; k++) pv[p][k] = p == 1 ? 1 : 0;   /* Cartridge 0, Sega CD 1 */
    pv[1][CD_DEDITHER] = pv[1][CD_SMOOTH] = pv[1][CD_COLOUR] = pv[1][CD_MOVIELOOK] = 0;            /* (the movie as the disc has it) */
    pv[1][CD_SPLIT] = 0;                                                       /* (the 50/50 demo: off in every preset) */
    pv[1][CD_VOICE] = 1;
    pv[1][CD_PCPHOTOS] = 0;                                                    /* (not the disc's: the PC games') */
    pv[1][CD_ANNOUNCER] = 0;                                                  /* (95 PC's voice: not the Sega CD's) */
    pv[1][CD_HOLD] = 0;                                                        /* (95 PC's pause: not the Sega CD's) */
    pv[2][CD_HOLD] = 0;                                                        /* (the PC between-play pause: a taste, off in Recommended) */
    pv[1][CD_ANNOUNCE] = 0;                                                    /* (95 PC's timing: not the Sega CD's) */
    pv[2][CD_ANNOUNCE] = 1;                                                    /* (with the goal, as on the PC, in Recommended) */
    pv[1][CD_FIGHT] = 0;                                                       /* (NHL 93's fights: not the Sega CD's; off in Recommended too until tried) */
    pv[1][CD_HATS] = 0;                                                        /* (the SNES's: not the Sega CD's) */
    pv[1][CD_PCSIREN] = pv[1][CD_PCHORN] = 0;                                  /* (the PC games' sounds: not the Sega CD's) */
    pv[2][CD_PCSIREN] = pv[2][CD_PCHORN] = 1;                                  /* (on in Recommended; they do nothing without 95 PC's files) */
    pv[1][CD_PCANTHEM] = 0;                                                    /* (the PC games' presentation: not the Sega CD's) */
    pv[2][CD_PCANTHEM] = 1;
    pv[1][CD_PCLOGO] = 0;                                                      /* (the PC games' look: not the Sega CD's) */
    pv[2][CD_PCLOGO] = 1;
    pv[1][CD_PCREF] = 0;                                                       /* (the PC games' look: not the Sega CD's) */
    pv[2][CD_PCREF] = 1;
    pv[1][CD_PCREFPHYS] = 0;                                                   /* (the PC games' look: not the Sega CD's) */
    pv[2][CD_PCREFPHYS] = 1;                                                   /* (visual only in Recommended: Full changes play) */
    pv[1][CD_PCSPR] = 0;                                                       /* (the PC games' look: not the Sega CD's; off in Recommended until tried) */
    pv[2][CD_HATS] = 1;                                                        /* (on in Recommended; it does nothing without the SNES ROM) */
    pv[1][CD_WHITEICE] = 0;                                                    /* (the PC games' look: not the Sega CD's; off in Recommended too, it is a taste) */
    pv[2][CD_ANNOUNCER] = 1;                                                  /* (on in Recommended; it does nothing without 95 PC's files) */
    pv[2][CD_PCPHOTOS] = 1;
    pv[1][CD_WIDESCREEN] = 0;                                                  /* (16:9 in Recommended; the cartridge's and the disc's 4:3) */
    pv[2][CD_WIDESCREEN] = 1;                                                    /* (Abdul: on in Recommended) */
    static const int disc_look[] = { CD_VOICE, CD_SCREENS, CD_TITLE, CD_INTRO, CD_SFX, CD_PHOTOS, CD_CAREER };
    for (size_t k = 0; k < sizeof disc_look / sizeof *disc_look; k++) pv[2][disc_look[k]] = 1;
    pv[0][CD_CROWD] = pv[2][CD_CROWD] = 1; pv[1][CD_CROWD] = 0;                 /* (the cartridge's bed: on in Cartridge and Recommended, off on the Sega CD) */
    pv[0][CD_CROWD_CD] = 0; pv[1][CD_CROWD_CD] = 1; pv[2][CD_CROWD_CD] = 1;     /* (the disc's: on in Sega CD and Recommended) */
    for (int p = 0; p < 3; p++) { pv[p][CD_CROWD_CARTVOL] = pv[p][CD_CROWD_CDVOL] = pv[p][CD_CROWD_PCVOL] = CROWD_VOL_100; pv[p][CD_CROWD_PC] = 0; }   /* (100%; the PC's off in all) */
    pv[2][CD_DEDITHER] = OB_POSTFX_LIVE;                                       /* (the movie without the dither) */
    pv[2][CD_MOVIELOOK] = 1;                                                   /* (its own look: soft CRT unless changed on Scaling (Movie)) */
    for (int k = 0; k < CD_ROWS; k++) if (!strcmp(cd_rows[k].key, "cd_clear_shots")) pv[2][k] = 1;   /* (the one fix) */
    static const ob_game_preset presets[3] = { { "Recommended", pv[2] }, { "Cartridge", pv[0] }, { "Sega CD", pv[1] } };
    ob_game_ops ops = { NULL, cd_opts, CD_OPTS, cd_changed, NULL, presets, 3 };
    ob_game_set_ops(&host, &ops);
}

static void shell_controllers(void) { ob_pads_setup_open(-1); }
static void shell_sides(void) { ob_pads_sides_open(); }

/* ---- the select-sides screen in the cartridge's own art: its EA SPORTS pattern (ScoutMap) for the field, the title in the big font
 * (BigFontMap), the black box from the frame (FramerMap), each team's setup-screen banner, and the small font (SmallFontMap, the credits' look)
 * for the rest ---- */
struct gfont { uint32_t *pic; int w, h; int x[96], adv[96], ink[96]; int row_h; };   /* x: the glyph's left in the picture, ink: its width */
static struct gfont big_f, small_f;
static uint32_t *frame_pic;
static int fw_, fh_, sides_art_tried;
static int sides_team[2];

static void font_measure(struct gfont *f, int cell_w, int cell_h, int first_row)
{
    if (cell_w == 16) {                                       /* the big font: glyphs are cut where a column is empty (the cells are not all alike) */
        int seg[48][2], n = 0, start = -1;
        for (int x = 0; x <= f->w && n < 40; x++) {
            int ink = 0;
            for (int y = 0; x < f->w && y < f->h && !ink; y++) ink = f->pic[(size_t)y * f->w + x] >> 24;
            if (ink && start < 0) start = x;
            if (!ink && start >= 0) {
                if (x - start >= 28) { seg[n][0] = start; seg[n++][1] = 15; seg[n][0] = start + 15; seg[n++][1] = x - start - 15; }   /* two joined glyphs */
                else { seg[n][0] = start; seg[n++][1] = x - start; }
                start = -1;
            }
        }
        for (int c = 0; c < 96; c++) { f->x[c] = 0; f->ink[c] = 0; f->adv[c] = 8; }
        for (int c = 0; c < 36 && c < n; c++) { f->x[c] = seg[c][0]; f->ink[c] = seg[c][1]; f->adv[c] = seg[c][1] + 1; }
        return;
    }
    for (int c = 0; c < 96; c++) {
        int cx = c % 32 * 8, cy = (first_row + c / 32) * 8, lo = cell_w, hi = -1;
        f->x[c] = cx; f->ink[c] = 0; f->adv[c] = 8;
        for (int x = 0; x < cell_w; x++)
            for (int y = 0; y < cell_h; y++)
                if (f->pic[(size_t)(cy + y) * f->w + cx + x] >> 24) { if (x < lo) lo = x; if (x > hi) hi = x; }
        if (hi >= lo) { f->x[c] = cx; f->ink[c] = 8; f->adv[c] = 8; }     /* (the small font is eight wide each, as on the game's screens) */
        f->x[c] |= cy << 16;                                  /* (the row in the high bits) */
    }
}

static void sides_art(void)
{
    if (sides_art_tried) return;
    sides_art_tried = 1;
    big_f.pic = rosters_picture("BigFontMap", &big_f.w, &big_f.h, 1);
    small_f.pic = rosters_picture("SmallFontMap", &small_f.w, &small_f.h, 1);
    frame_pic = rosters_picture("FramerMap", &fw_, &fh_, 1);
    if (big_f.pic) font_measure(&big_f, 16, 16, 0);
    if (small_f.pic) font_measure(&small_f, 8, 8, 1);
}

static int glyph_index(const struct gfont *f, int big, unsigned char ch)
{
    if (big) { if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 32); return ch >= 'A' && ch <= 'Z' ? ch - 'A' : ch >= '0' && ch <= '9' ? 26 + ch - '0' : -1; }
    (void)f;
    return ch >= 32 && ch < 128 ? ch - 32 : -1;
}

static int sides_text_width(void *ud, const char *s, int big)
{
    const struct gfont *f = big ? &big_f : &small_f;
    int w = 0;
    (void)ud;
    sides_art();
    if (!f->pic) return (int)strlen(s) * 6;
    for (; *s; s++) { int i = glyph_index(f, big, (unsigned char)*s); w += i < 0 ? (big ? 15 : 8) : f->adv[i]; }
    return big && w > 0 ? w - 1 : w;
}

static void sides_text(void *ud, uint32_t *px, int w, int h, int pitch, int x, int y, const char *s, uint32_t argb, int big, int scale)
{
    const struct gfont *f = big ? &big_f : &small_f;
    int gh = big ? 16 : 8, pass;
    (void)ud;
    sides_art();
    if (!f->pic) return;
    for (pass = 1; pass < 2; pass++) {            /* (the small font: a black shadow first, then the letters) */
        int pen = 0;
        for (const char *p = s; *p; p++) {
            int i = glyph_index(f, big, (unsigned char)*p);
            if (i < 0) { pen += big ? 15 : 8; continue; }
            int gx = f->x[i] & 0xFFFF, gy = f->x[i] >> 16;
            for (int j = 0; j < gh; j++)
                for (int k = 0; k < f->ink[i]; k++) {
                    uint32_t c = f->pic[(size_t)(gy + j) * f->w + gx + k];
                    if (!(c >> 24)) continue;
                    if (!big) {
                        int shade = (c >> 16 & 0xFF) < 250;
                        c = pass == 0 || shade ? 0xFF000000u : argb;     /* (the font's second tone is its dark edge) */
                    }
                    int dx = (x + pen + k + (pass == 0 && !big)) * scale, dy = (y + j + (pass == 0 && !big)) * scale;
                    for (int v = 0; v < scale; v++)
                        for (int u = 0; u < scale; u++)
                            if (dx + u < w && dy + v < h) px[(size_t)(dy + v) * pitch + dx + u] = c;
                }
            pen += f->adv[i];
        }
    }
}

static void sides_frame(void *ud, uint32_t *px, int w, int h, int pitch, int x, int y, int bw, int bh, int scale)
{
    (void)ud;
    sides_art();
    for (int j = 0; j < bh; j++)
        for (int i = 0; i < bw; i++) {
            int gx = i / scale, gy = j / scale, gw = bw / scale, gh = bh / scale, sx, sy;
            if (x + i < 0 || x + i >= w || y + j < 0 || y + j >= h) continue;
            sx = gx < 8 ? gx : gx >= gw - 8 ? 24 - (gw - gx) : 8 + (gx - 8) % 8;
            sy = gy < 8 ? gy : gy >= gh - 8 ? 24 - (gh - gy) : 8 + (gy - 8) % 8;
            if (!frame_pic) px[(size_t)(y + j) * pitch + x + i] = 0xFF000000u;
            else if (sx >= 0 && sx < fw_ && sy >= 0 && sy < fh_ && frame_pic[sy * fw_ + sx] >> 24) px[(size_t)(y + j) * pitch + x + i] = frame_pic[sy * fw_ + sx];
        }
}

static void sides_team_plate(void *ud, uint32_t *px, int w, int h, int pitch, int x, int y, int team, int scale)
{
    static uint32_t banner[2][88 * 16];
    static int have[2] = { -2, -2 }, ok[2];
    (void)ud;
    if (have[team] != sides_team[team]) { have[team] = sides_team[team]; ok[team] = rosters_banner(have[team], banner[team]) == 0; }
    /* the art under the game's own bevel: a light edge top and left, grey, dark at the bottom and right (as on the game's screen) */
    for (int j = 0; j < 16 * scale; j++)
        for (int i = 0; i < 88 * scale; i++) {
            int gx = i / scale, gy = j / scale;
            uint32_t v = ok[team] ? banner[team][gy * 88 + gx] : 0xFF000000u;
            if (x + i < 0 || x + i >= w || y + j < 0 || y + j >= h) continue;
            if (gy == 0) v = 0xFFDBDBDBu;
            else if (gx == 0) { if (gy == 15) continue; v = 0xFFDBDBDBu; }
            else if (gy == 15) v = 0xFF242424u;
            else if (gx == 87) v = 0xFF242424u;
            else if (gx == 1 || gy == 1) v = 0xFF929292u;
            else if (gx == 86 || gy == 14) v = 0xFF494949u;
            px[(size_t)(y + j) * pitch + x + i] = v;
        }
}

static void sides_background(void *ud, uint32_t *px, int w, int h, int pitch)
{
    static uint32_t *pic;
    static int pw, ph, tried;
    (void)ud;
    if (!tried) { tried = 1; pic = rosters_picture("ScoutMap", &pw, &ph, 0); }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) px[(size_t)y * pitch + x] = pic ? pic[(y % ph) * pw + x % pw] : 0xFF0000B2u;
}
static SDL_atomic_t sides_state;                   /* 0 none, 1 asked for by the game, 2 open */
static volatile int seat_n, seat_pad[4];              /* the select-sides screen's seating: game pad j reads player seat_pad[j] (seat_n: none, no remap) */

/* (the game's thread, as a new game begins: lockstep.c) the select-sides screen first; the game waits for it. Not in a session, where the room
 * decides, and not with sides.at_start=0 in settings.ini. */
static void game_starting(void)
{
    seat_n = 0;
    if (native_net_on() || !F.o || SDL_AtomicGet(&F.quit) || !atoi(pads_get("sides.at_start", "1"))) return;
    sides_team[0] = native_game_teams[0];
    sides_team[1] = native_game_teams[1];
    SDL_AtomicSet(&sides_state, 1);
    while (SDL_AtomicGet(&sides_state) && !SDL_AtomicGet(&F.quit)) native_idle_frame();   /* (the main menu's song plays on behind it) */
    {   int team[4], n = 0;                            /* who plays what: game pad j is the j-th player with a device (visitors play team 2, home team 1) */
        for (int p = 0; p < 4 && p < MDRT_PADS; p++)
            if (ob_pads_seated(p)) { seat_pad[n] = p; team[n] = (p & 1) ? 1 : 2; n++; }
        seat_n = native_apply_seats(team, n) ? n : 0;
    }
}
static const ob_sides_skin sides_skin = { .background = sides_background, .text = sides_text, .text_width = sides_text_width, .frame = sides_frame,
                                        .team = sides_team_plate, .team_w = 88, .team_h = 16 };

/* --sides-shot FILE (headless): the select-sides screen in the game's own art, as the window shows it with two devices seated and one ready */
int frontend_sides_shot(const char *path)
{
    static ob_sides sd;
    static uint32_t pic[960 * 672];
    ob_sides_game g;
    memset(&g, 0, sizeof g);
    g.skin = sides_skin;
    g.teams = 2; g.seats = 2; g.per_team = 2;
    g.team_name[0] = "Away"; g.team_name[1] = "Home";
    sides_team[0] = 10; sides_team[1] = 11;
    ob_sides_init(&sd, &g, 0, NULL, NULL);
    ob_sides_open(&sd);
    ob_sides_add(&sd, 0, "Keyboard");
    ob_sides_add(&sd, 1, "Gamepad 1");
    ob_sides_input(&sd, 0, MDRT_MENU_LEFT);
    ob_sides_input(&sd, 1, MDRT_MENU_RIGHT);
    ob_sides_input(&sd, 0, MDRT_MENU_OK);
    ob_sides_draw(&sd, pic, 960, 672, 960);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n960 672\n255\n");
    for (int i = 0; i < 960 * 672; i++) { unsigned char c[3] = { (unsigned char)(pic[i] >> 16), (unsigned char)(pic[i] >> 8), (unsigned char)pic[i] }; fwrite(c, 1, 3, f); }
    fclose(f);
    return 0;
}

int mdrt_sdl_run(mdrt_config *cfg, const mdrt_sdl_options *o, mdrt_game_fn game, void *user)
{
    ob_profile profile = OB_PROFILE_DESKTOP;
    char why[600];
    memset(&F, 0, sizeof F);
    F.o = o; F.cfg = cfg; F.game = game; F.user = user;
    if (!ob_plat_init(&profile)) return 1;
    F.lock = SDL_CreateMutex();
    settings_load();
    if (!find_rom(why, sizeof why)) {
        char msg[1400];
        snprintf(msg, sizeof msg, "No NHL '94 cartridge found: %s.\n\nPut your NHL '94 Genesis ROM beside the program (any name; .gen, .md, .bin or .smd).",
                 why[0] ? why : o->rom_hint ? o->rom_hint : "");
        fprintf(stderr, "%s\n", msg);
        if (profile != OB_PROFILE_HANDHELD) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "NHL '94", msg, NULL);
        return 1;
    }
    ob_pads_init(&(ob_pads_cfg){ .players = MDRT_PADS, .n = BUTTONS, .b = buttons, .start = 7, .src_w = 320, .src_h = 224,
                                 .get = pads_get, .set = pads_set, .save = settings_write,
                                 .team_name = { "Away", "Home" }, .skin = &sides_skin, .mouse_btn = { 6, 7, 5 } });   /* the mouse: left B, right C, middle A */
    F.adaptor = ob_pads_extra() ? MDRT_ADAPTOR_EA4WAY : MDRT_ADAPTOR_NONE;
    options_from_settings();
    settings_write();
    ob_host_init(&host, OB_PORT_GENESIS, profile);
    ob_host_load_ini_beside(&host, "overbar.ini");   /* the Overbar settings stay set between runs */
    rules_widescreen(host.widescreen);
    options_ui_init(o, F.opt_values, F.set, user, settings_write);
    game_page_init();                                 /* (after overbar.ini: its game_ lines are kept until the rows are known) */
    {
        extern const bx_scene BOX_GEN94;              /* box_gen94.c: the box on the first launch (experimental) */
        ob_box_init(&host, &BOX_GEN94);
    }
    {
        ob_online_ops ops;
        memset(&ops, 0, sizeof ops);
        ops.max_pads = MDRT_PADS;                     /* (up to four players: 4 Way Play) */
        ops.max_delay = 10;
        ops.roles = 3;
        ops.start = online_start;
        ops.leave = online_leave;
        ops.status = online_status;
        ops.lan_address = ob_net_lan_address;
        ob_online_set_ops(&host, &ops);
    }
    {
        ob_shell shell;
        memset(&shell, 0, sizeof shell);
        shell.controllers = shell_controllers;
        shell.sides = shell_sides;
        host.sides_menu = 1;
        ob_shell_set(&shell);
    }
    {
        ob_plat_hooks hooks = { .title = o->title ? o->title : "NHL '94", .win_w = (host.widescreen ? 400 : 320) * (o->scale > 0 ? o->scale : 3), .win_h = 224 * (o->scale > 0 ? o->scale : 3),
                                .cls = OB_CLASS_LORES, .src_w = 320, .src_h = 224, .frame = hook_frame, .wide = hook_wide, .changed = hook_changed,
                                .action = hook_action, .captures = hook_captures, .raw_key = hook_raw_key, .raw_button = hook_raw_button,
                                .draw_over = hook_draw_over, .quit = hook_quit, .esc_opens = 1,
                                .game_name = "NHL 94", .game_platform = "For Sega Genesis" };
        host.widescreen_ok = 1;                       /* (the Widescreen setting: wide.c) */
        if (!ob_plat_open(&hooks, &host)) return 1;
    }
    {
        SDL_AudioSpec want = { .freq = cfg->audio_rate ? (int)cfg->audio_rate : 48000, .format = AUDIO_S16SYS, .channels = 2, .samples = 512 }, got;
        F.dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
        if (!F.dev) fprintf(stderr, "no sound (%s): the frames keep time by the clock\n", SDL_GetError());
        F.rate = F.dev ? (unsigned)got.freq : (unsigned)want.freq;
        cfg->audio_rate = F.rate;
    }
    cfg->platform = (mdrt_platform){ .user = NULL, .present = present, .audio = audio, .wait = frame_wait, .input = input, .idle = idle };
    if (!start_game()) { fprintf(stderr, "the game could not start\n"); return 1; }
    native_game_start_hook = game_starting;
    { extern uint8_t scout_b_back; scout_b_back = 1; }    /* (the scouting report: C the next matchup, B the one before) */
    test_aid_arm();
    while (!SDL_AtomicGet(&F.quit)) {
        test_aid_tick();
        if (native_net_window_poll()) restart_console(1);
        if (cd_restart) { cd_restart = 0; if (!F.session) restart_console(0); }   /* (the Game page's roster set) */
        ob_plat_pump();
        ob_pads_tick();
        { extern uint8_t setup_no_players; setup_no_players = (uint8_t)(!native_net_on() && !F.session && atoi(pads_get("sides.at_start", "1")));   /* (controller setup says who plays: no Players line) */ }
        if (SDL_AtomicGet(&sides_state) == 1) { ob_pads_sides_open(); SDL_AtomicSet(&sides_state, 2); }
        else if (SDL_AtomicGet(&sides_state) == 2 && !ob_pads_sides_active()) SDL_AtomicSet(&sides_state, 0);
        int in_play = 0;
        {   /* the mouse steers in play (the game's frames are running) and steps through the menus otherwise */
            static unsigned seen_frames;
            static Uint32 play_ms;
            if (native_play_frames != seen_frames) { seen_frames = native_play_frames; play_ms = SDL_GetTicks(); }
            in_play = play_ms && SDL_GetTicks() - play_ms < 250u;
            ob_pads_mouse_mode(play_ms && SDL_GetTicks() - play_ms < 250u);
            {   static unsigned seen_menu; static Uint32 menu_ms;       /* a game menu is being read: the mouse points at its rows */
                static ob_menu_rect mr[OB_MENU_ROWS]; int rect[OB_MENU_ROWS][4], n, sel, lr;
                ob_pad_menu pm = { mr, 0, 0, 0, MDRT_PAD_C, MDRT_PAD_B, 320, 0, 0, 0 };
                if (native_menu_calls != seen_menu) { seen_menu = native_menu_calls; menu_ms = SDL_GetTicks(); }
                if (((menu_ms && SDL_GetTicks() - menu_ms < 250u) || native_menu_waiting()) && !(play_ms && SDL_GetTicks() - play_ms < 250u) && native_menu_rows(rect, OB_MENU_ROWS, &n, &sel, &lr)) {
                    for (int i = 0; i < n; i++) { mr[i].x = rect[i][0] + (F.width > 320 ? (F.width - 320) / 2 : 0); mr[i].y = rect[i][1]; mr[i].w = rect[i][2]; mr[i].h = rect[i][3]; }
                    pm.n = n; pm.sel = sel; pm.lr = lr; pm.grid = native_menu_kind() == 3; pm.rleft = native_menu_kind() == 2;   /* (the setup screen: a right click steps an option back) */ if (native_menu_kind() >= 3) pm.back = MDRT_PAD_START;     /* the line editor and the playoff tree: right click is Start */ pm.w = F.width > 0 ? F.width : 320;
                    ob_pads_menu(&pm);
                } else ob_pads_menu(NULL);
            }
            {   int ax, ay, mp = ob_pads_mouse_player();
                if (seat_n) { int jj = -1; for (int q = 0; q < seat_n; q++) if (seat_pad[q] == mp) jj = q; mp = jj; }
                if (mp >= 0 && native_player_on_screen(mp, &ax, &ay)) ob_pads_mouse_anchor(ax, ay);
                else ob_pads_mouse_anchor(0, 0);
            }
        }
        int opening = segacd_opening();
        ob_pads_mouse_steps(!opening);                        /* (moving the mouse is not a press on the logos and the title) */
        {   /* (the clicks that leave the opening screens are not the menu's: the mouse presses nothing till a second after, its button up and
             * the clicking stopped a moment) */
            static Uint32 quiet_ms; static int held, was, quiet;
            Uint32 now = SDL_GetTicks();
            int ldown = (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK) != 0;
            if (opening) { quiet_ms = now + 1000; held = ldown; }
            else if (!ldown) held = 0;
            if (quiet && ldown && !was && quiet_ms - now < 700u) quiet_ms = now + 700;   /* (still clicking: not yet) */
            was = ldown;
            quiet = opening || held || (Sint32)(quiet_ms - now) > 0;
            ob_pads_mouse_quiet(quiet);
        }
        for (int p = 0; p < MDRT_PADS; p++) {
            int src = seat_n ? (p < seat_n ? seat_pad[p] : -1) : p;       /* (after the select-sides screen the pads are the seated players') */
            SDL_AtomicSet(&F.buttons[p], src >= 0 ? (int)ob_pads_read(src) : 0);
        }
        if (!in_play) {      /* in the menus any seated device (the keyboard too) drives the game's first pad, wherever the seating put it */
            int any = 0;
            for (int p = 0; p < 4 && p < MDRT_PADS; p++) if (ob_pads_seated(p) && p != ob_pads_mouse_player()) any |= (int)ob_pads_read(p);
            SDL_AtomicSet(&F.buttons[0], SDL_AtomicGet(&F.buttons[0]) | any);
        }
        {   /* a left click on the logos, the title and the license page leaves them, as a press of Start does (the movie takes its own) */
            static int was_down;
            static Uint32 click_until;
            int down = (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK) != 0;
            if (down && !was_down && opening && !host.overlay_open && !ob_screen_up()) click_until = SDL_GetTicks() + 60;
            was_down = down;
            if (click_until && SDL_GetTicks() < click_until) SDL_AtomicSet(&F.buttons[0], SDL_AtomicGet(&F.buttons[0]) | MDRT_PAD_START);
            else click_until = 0;
        }
        segacd_movie_pause(hold_state && !(segacd_movie_playing() && !hold_other));   /* (the movie is a full-window screen itself: only something over it pauses it) */
        intro_tick();
        segacd_career_tick(F.dev, F.rate, SDL_AtomicGet(&cd_dedither));
        cd_no_disc();
        pa_no_file();
        hats_no_file();
        pcspr_no_file();
        pcsfx_no_file();
        pcsong_no_file();
        pc_no_files();
        hold_other = ob_pads_setup_active() || (ob_pads_sides_active() && !SDL_AtomicGet(&sides_state)) || options_ui_active() || host.overlay_open;   /* (the sides screen a new game asked for: the game waits on it itself, its song playing on) */
        hold_state = hold_other || ob_screen_up();
        ob_plat_present();
        ob_plat_frame_done(SDL_AtomicSet(&F.fresh, 0));
        if (SDL_AtomicGet(&F.done)) break;
        SDL_Delay(2);
    }
    shutdown_game();
    ob_plat_close();
    SDL_Quit();
    return 0;
}

const char *mdrt_sdl_home(const char *app) { (void)app; return NULL; }
