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
#include "segacd.h"
#include "wide.h"

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

static int hold_state;                                /* the overlay, the setup screen or a full-window screen (the box) is open */

static void sram_save(void)
{
    mdrt_sram *r = F.m ? mdrt_get_sram(F.m) : NULL;
    FILE *f;
    if (!r || !F.sram_file[0] || !r->size || !r->dirty) return;
    f = fopen(F.sram_file, "wb");
    if (!f) return;
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
    int cd = w <= 320 && h <= 224 && segacd_present(pic, w, h);   /* (the Sega CD's opening screens, when they are on) */
    if (cd) { w = 320; h = 224; }
    SDL_LockMutex(F.lock);
    F.wide = host.widescreen;
    rules_widescreen(host.widescreen);           /* (the wide rink from the next power on: rules.c) */
    if (F.wide) w = wide_frame(mdrt_get_vdp(F.m), pic, w, h, 320, F.picture, WIDE_MAX_W, cd);   /* (the sides: wide.c) */
    else if (!cd && native_net_on() && wide_rink_frame(w)) wide_crop_follow(), w = wide_crop(mdrt_get_vdp(F.m), pic, h, 320, F.picture, WIDE_MAX_W);   /* (a 16:9 player's wide rink, here 4:3) */
    else for (int y = 0; y < h; y++) memcpy(F.picture + y * WIDE_MAX_W, pic + y * 320, (size_t)w * 4);
    F.width = w;
    F.height = h;
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

static void input(void *user, uint16_t b[MDRT_PADS])
{
    (void)user;
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

static const uint32_t *hook_frame(int *w, int *h)
{
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
static const struct { int rule; const char *key; } cd_rows[] = {
    { RULE_CD_SKATING, "cd_skating" }, { RULE_CD_CORNERS, "cd_corners" }, { RULE_CD_PSBOX, "cd_penalty_shot_box" },
    { RULE_CD_PASSWORD, "cd_password" }, { RULE_CD_CLEARSHOT, "cd_clear_shots" }, { RULE_CD_COLOURS, "cd_colours" },
    { RULE_CD_SOUNDS, "cd_sounds" }, { RULE_CD_SONGS, "cd_songs" }, { RULE_CD_PERIOD_END, "cd_period_end" },
};
#define CD_ROWS ((int)(sizeof cd_rows / sizeof cd_rows[0]))
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
#define CD_PHOTOS (CD_ROWS + 11)
#define CD_CAREER (CD_ROWS + 12)
#define CD_OPTS (CD_ROWS + 13)
static ob_game_opt cd_opts[CD_OPTS];
static SDL_atomic_t cd_dedither;                          /* the movie's de-dither (Overbar's ob_postfx_mode) */
static SDL_atomic_t cd_smooth, cd_colour;                 /* the movie at 30 fps; each shot's colour */
static int cd_restart;                                    /* (the roster set changed: the console powers on again) */

/* the option of the rules page with this key, or -1 */
static int option_index(const char *key)
{
    for (int i = 0; i < F.o->noptions; i++) if (F.o->options[i].key && !strcmp(F.o->options[i].key, key)) return i;
    return -1;
}

static void cd_changed(void *ctx, int index, int value)
{
    char key[64];
    (void)ctx;
    if (index == CD_MUSIC) { segacd_set_music(value); return; }
    if (index == CD_CROWD) { segacd_set_crowd(value); return; }
    if (index == CD_VOICE) { segacd_set_voice(value); return; }
    if (index == CD_SCREENS) { segacd_set_screens(value); return; }
    if (index == CD_TITLE) { segacd_set_title(value); return; }
    if (index == CD_INTRO) { segacd_set_intro(value); return; }
    if (index == CD_DEDITHER) { SDL_AtomicSet(&cd_dedither, value); return; }
    if (index == CD_SMOOTH) { SDL_AtomicSet(&cd_smooth, value); return; }
    if (index == CD_COLOUR) { SDL_AtomicSet(&cd_colour, value); return; }
    if (index == CD_PHOTOS) { segacd_set_photos(value); return; }
    if (index == CD_CAREER) { segacd_set_career(value, ending); return; }
    if (index == CD_SFX) { segacd_set_sfx(value); return; }
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
    if (index < 0 || index >= CD_ROWS || !F.opt_values) return;
    snprintf(key, sizeof key, "rules.%s", cd_rows[index].key);
    int i = option_index(key);
    if (i < 0) return;
    if (F.opt_values[i] == value) return;
    F.opt_values[i] = value;
    mdrt_settings_set(F.set, key, cd_off_on[value ? 1 : 0]);
    settings_write();
    if (F.session) return;                              /* (a session plays by the host's rules) */
    if (F.o->options_use) F.o->options_use(F.opt_values, F.user);   /* (the next power on's) */
    rules[cd_rows[index].rule] = (uint8_t)(value ? 1 : 0);          /* (and this game's, from now on) */
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
                                                                        SDL_AtomicGet(&cd_smooth), SDL_AtomicGet(&cd_colour), intro_base,
                                                                        beside("rife-v4.6")) ? 2 : 0);
    else if (st == 2 && !segacd_movie_tick()) SDL_AtomicSet(&intro_state, 0);
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

static void game_page_init(void)
{
    static const char *const labels[CD_ROWS] = { "Sega CD skating", "Sega CD corner bounce", "Sega CD penalty shot box",
                                                  "Sega CD playoff password", "Sega CD shot data fix", "Sega CD uniforms",
                                                  "Sega CD sound choices", "Sega CD song timing", "Sega CD period end" };
    for (int k = 0; k < CD_ROWS; k++) {
        char key[64];
        snprintf(key, sizeof key, "rules.%s", cd_rows[k].key);
        int i = option_index(key);
        cd_opts[k] = (ob_game_opt){ cd_rows[k].key, labels[k], cd_off_on, 2, i >= 0 && F.opt_values ? (F.opt_values[i] ? 1 : 0) : 0, 1, 1 };
    }
    int r = option_index("rules.rosters");
    cd_opts[CD_ROSTERS] = (ob_game_opt){ "cd_rosters", "Sega CD rosters (restarts)", cd_off_on, 2, r >= 0 && F.opt_values && F.opt_values[r] == 1, 1, 2 };
    static const char *const music[2] = { "cartridge", "Sega CD" }, *const crowd[3] = { "cartridge", "Sega CD", "both" };
    cd_opts[CD_MUSIC] = (ob_game_opt){ "cd_music", "Music", music, 2, 0, 0, 3 };
    cd_opts[CD_CROWD] = (ob_game_opt){ "cd_crowd", "Crowd", crowd, 3, 0, 0, 3 };
    static const char *const voice[3] = { "off", "on", "over the music" };
    cd_opts[CD_VOICE] = (ob_game_opt){ "cd_voice", "Ron Barr scouting voice", voice, 3, 0, 0, 3 };
    cd_opts[CD_SCREENS] = (ob_game_opt){ "cd_screens", "Opening screens", music, 2, 0, 0, 3 };
    cd_opts[CD_TITLE] = (ob_game_opt){ "cd_title", "Title page", music, 2, 0, 0, 3 };
    cd_opts[CD_INTRO] = (ob_game_opt){ "cd_intro", "Intro movie", cd_off_on, 2, 0, 0, 3 };
    cd_opts[CD_DEDITHER] = (ob_game_opt){ "cd_dedither", "Movie de-dither", ob_postfx_names, OB_POSTFX_MODES, 0, 0, 3 };
    static const char *const smooth[2] = { "original", "30 fps" }, *const colour[2] = { "original", "enhanced" };
    cd_opts[CD_SMOOTH] = (ob_game_opt){ "cd_smooth", "Movie frame rate", smooth, 2, 0, 0, 3 };
    cd_opts[CD_COLOUR] = (ob_game_opt){ "cd_colour", "Movie colour", colour, 2, 0, 0, 3 };
    cd_opts[CD_PHOTOS] = (ob_game_opt){ "cd_photos", "Player photos", music, 2, 0, 0, 3 };
    static const char *const career[2] = { "off", "B on a player card" };
    cd_opts[CD_CAREER] = (ob_game_opt){ "cd_career", "Career stats", career, 2, 0, 0, 3 };
    cd_opts[CD_SFX] = (ob_game_opt){ "cd_sfx", "Sound effects", music, 2, 0, 0, 3 };
    segacd_start();
    segacd_intro_hook = intro_hook;
    ob_game_ops ops = { NULL, cd_opts, CD_OPTS, cd_changed, NULL };
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

/* (the game's thread, as a new game begins: lockstep.c) the select-sides screen first; the game waits for it. Not in a session, where the room
 * decides, and not with sides.at_start=0 in settings.ini. */
static void game_starting(void)
{
    if (native_net_on() || !F.o || SDL_AtomicGet(&F.quit) || !atoi(pads_get("sides.at_start", "1"))) return;
    sides_team[0] = native_game_teams[0];
    sides_team[1] = native_game_teams[1];
    SDL_AtomicSet(&sides_state, 1);
    while (SDL_AtomicGet(&sides_state) && !SDL_AtomicGet(&F.quit)) SDL_Delay(5);
}
static const ob_sides_skin sides_skin = { .background = sides_background, .text = sides_text, .text_width = sides_text_width, .frame = sides_frame,
                                        .team = sides_team_plate, .team_w = 88, .team_h = 16 };

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
    test_aid_arm();
    while (!SDL_AtomicGet(&F.quit)) {
        test_aid_tick();
        if (native_net_window_poll()) restart_console(1);
        if (cd_restart) { cd_restart = 0; if (!F.session) restart_console(0); }   /* (the Game page's roster set) */
        ob_plat_pump();
        ob_pads_tick();
        if (SDL_AtomicGet(&sides_state) == 1) { ob_pads_sides_open(); SDL_AtomicSet(&sides_state, 2); }
        else if (SDL_AtomicGet(&sides_state) == 2 && !ob_pads_sides_active()) SDL_AtomicSet(&sides_state, 0);
        {   /* the mouse steers in play (the game's frames are running) and steps through the menus otherwise */
            static unsigned seen_frames;
            static Uint32 play_ms;
            if (native_play_frames != seen_frames) { seen_frames = native_play_frames; play_ms = SDL_GetTicks(); }
            ob_pads_mouse_mode(play_ms && SDL_GetTicks() - play_ms < 250u);
            {   int ax, ay, mp = ob_pads_mouse_player();
                if (mp >= 0 && native_player_on_screen(mp, &ax, &ay)) ob_pads_mouse_anchor(ax, ay);
                else ob_pads_mouse_anchor(0, 0);
            }
        }
        for (int p = 0; p < MDRT_PADS; p++) SDL_AtomicSet(&F.buttons[p], (int)ob_pads_read(p));
        intro_tick();
        segacd_career_tick(F.dev, F.rate, SDL_AtomicGet(&cd_dedither));
        cd_no_disc();
        hold_state = ob_pads_setup_active() || ob_pads_sides_active() || options_ui_active() || host.overlay_open || ob_screen_up();
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
