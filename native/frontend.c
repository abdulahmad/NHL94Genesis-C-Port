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
#include "overbar/plat_pads.h"
#include "overbar/net.h"
#include "native.h"
#include "optionsui.h"

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
    uint32_t picture[320 * 240], shown[320 * 240];
    int width, height;
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
            "# controllers take slots 1-8 as they are plugged in). ` or F11 opens the Overbar overlay\n"
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
    if (o->options_changed) o->options_changed(-1, F.opt_values, F.user);   /* a preset agreeing with them */
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

static int hold_state;                                /* the overlay or the setup screen is open */

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

static void present(void *user, const uint16_t *px, int w, int h, int pitch)
{
    (void)user;
    SDL_LockMutex(F.lock);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) F.picture[y * 320 + x] = 0xFF000000u | mdrt_vdp_rgb(px[y * pitch + x], MDRT_RGB_GPGX);
    F.width = w;
    F.height = h;
    SDL_AtomicSet(&F.fresh, 1);
    SDL_UnlockMutex(F.lock);
}

static void audio(void *user, const int16_t *samples, int frames)
{
    (void)user;
    if (!F.dev || hold_state) return;
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
    {                                                   /* the picture's rows are 320 apart; give them packed */
        static uint32_t packed[320 * 240];
        for (int y = 0; y < *h; y++) memcpy(packed + y * *w, F.shown + y * 320, (size_t)*w * 4);
        return packed;
    }
}

static int hook_captures(void) { return ob_pads_setup_active() || options_ui_active(); }
static void hook_raw_key(int sc) { if (options_ui_active()) options_ui_key(sc); else ob_pads_setup_key(sc); }
static void hook_raw_button(int slot, int b) { if (options_ui_active()) options_ui_button(slot, b); else ob_pads_setup_button(slot, b); }
static int hook_action(int id) { if (id == OB_ID_CONTROLLERS) ob_pads_setup_open(-1); return 1; }
static void hook_draw_over(const SDL_Rect *dst) { ob_pads_setup_draw(dst); options_ui_draw(dst); }
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

/* the session's match began: the console powered on again for it (the game on its thread anew) */
static void restart_for_session(void)
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
    F.session = 1;
    if (F.dev) { SDL_ClearQueuedAudio(F.dev); SDL_PauseAudioDevice(F.dev, 1); F.queued = 0; }
    if (!start_game()) { fprintf(stderr, "the game could not start\n"); SDL_AtomicSet(&F.quit, 1); }
}

static ob_host host;

static int rules_open(void *ctx)
{
    ob_overlay_close(&host);                          /* the rules screen takes the place of the overlay */
    return options_ui_open(ctx);
}

static void shell_controllers(void) { ob_pads_setup_open(-1); }

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
                                 .get = pads_get, .set = pads_set, .save = settings_write });
    F.adaptor = ob_pads_extra() ? MDRT_ADAPTOR_EA4WAY : MDRT_ADAPTOR_NONE;
    options_from_settings();
    settings_write();
    ob_host_init(&host, OB_PORT_GENESIS, profile);
    options_ui_init(o, F.opt_values, F.set, user, settings_write);
    if (options_ui_pages()) ob_host_add_addon(&host, OB_ID_ADDON, "Rules", rules_open);
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
        ob_shell_set(&shell);
    }
    {
        ob_plat_hooks hooks = { .title = o->title ? o->title : "NHL '94", .win_w = 320 * (o->scale > 0 ? o->scale : 3), .win_h = 224 * (o->scale > 0 ? o->scale : 3),
                                .cls = OB_CLASS_LORES, .src_w = 320, .src_h = 224, .frame = hook_frame, .changed = hook_changed,
                                .action = hook_action, .captures = hook_captures, .raw_key = hook_raw_key, .raw_button = hook_raw_button,
                                .draw_over = hook_draw_over, .quit = hook_quit };
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
    test_aid_arm();
    while (!SDL_AtomicGet(&F.quit)) {
        test_aid_tick();
        if (native_net_window_poll()) restart_for_session();
        ob_plat_pump();
        ob_pads_tick();
        for (int p = 0; p < MDRT_PADS; p++) SDL_AtomicSet(&F.buttons[p], (int)ob_pads_read(p));
        hold_state = ob_pads_setup_active() || options_ui_active() || host.overlay_open;
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
