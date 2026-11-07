/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * Thin Overbar client for the Genesis C-port. Replaces PulseMan mdrt_view_place
 * (linked ahead of libmdrt.a) with L5 dest-rect: the pixel shape of a 4:3 screen of 240
 * lines, only the 224 real lines drawn (no 8+8 border).
 * The overlay is Overbar's menu (the same tree as the window bar). ` and F11
 * open it. Esc backs out of a page and does not open the overlay.
 * The window menu bar is the same settings without Resume and Quit, and is
 * attached when the window is shown. Record is not F11 (Capture page).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SDL_syswm.h>
#endif
#include "mdrt/look.h"
#include "mdrt/menu.h"
#include "mdrt/platform_sdl2.h"
#include "mdrt/upscale.h"
#include "mdrt/view.h"
#include "native.h"
#include "overbar/overbar.h"

static ob_host g_host;
static int g_menu_inited;

/* PulseMan is the Genesis host. Overbar owns the menu and calls these. */
static int genesis_prescale(const uint32_t *src, int sw, int sh, uint32_t **dst, int *dw, int *dh)
{
    return ob_mister_prescale(&g_host.lores, src, sw, sh, 320, dst, dw, dh);
}

static void shell_picture(const ob_host *h)
{
    const ob_look *look;
    int up = MDRT_UPSCALE_OFF;
    int scale = MDRT_SCALE_FIT;
    int scan = MDRT_SCANLINES_OFF;
    int mask = MDRT_MASK_OFF;
    int blend;
    if (!h) return;
    look = h->video_class == OB_CLASS_HIRES ? &h->hires : &h->lores;
    blend = h->blend;
    switch (look->mode) {
    case OB_MODE_SCALE2X: up = MDRT_UPSCALE_SCALE2X; break;
    case OB_MODE_SCALE3X: up = MDRT_UPSCALE_SCALE3X; break;
    case OB_MODE_MMPX2X: up = MDRT_UPSCALE_MMPX2X; break;
    case OB_MODE_MMPX4X: up = MDRT_UPSCALE_MMPX4X; break;
    case OB_MODE_XBR2X: up = MDRT_UPSCALE_XBR2X; break;
    case OB_MODE_XBR3X: up = MDRT_UPSCALE_XBR3X; break;
    case OB_MODE_XBR4X: up = MDRT_UPSCALE_XBR4X; break;
    case OB_MODE_CRT:
        if (look->crt == OB_CRT_LIGHT) scan = MDRT_SCANLINES_LIGHT;
        else if (look->crt == OB_CRT_STRONG) scan = MDRT_SCANLINES_HEAVY;
        else scan = MDRT_SCANLINES_MEDIUM;
        if (look->crt == OB_CRT_SOFT || look->crt == OB_CRT_SOFT_APERTURE) blend = MDRT_BLEND_SOFT;
        if (look->crt == OB_CRT_SOFT_APERTURE) mask = MDRT_MASK_APERTURE;
        break;
    case OB_MODE_RETROARCH: {
        static const int live[] = {
            MDRT_UPSCALE_SCALE2X, MDRT_UPSCALE_SCALE3X, MDRT_UPSCALE_MMPX2X, MDRT_UPSCALE_MMPX4X,
            MDRT_UPSCALE_XBR2X, MDRT_UPSCALE_XBR3X, MDRT_UPSCALE_XBR4X
        };
        int i = look->sharp;
        if (i < 0 || i > 6) i = 0;
        up = live[i];
        break;
    }
    default: break;
    }
    if (look->height == OB_HEIGHT_INTEGER || look->height == OB_HEIGHT_OVERSCAN)
        scale = MDRT_SCALE_INTEGER;
    else if (look->mode == OB_MODE_SHARP || look->mode == OB_MODE_MISTER || up != MDRT_UPSCALE_OFF)
        scale = MDRT_SCALE_SHARP;
    mdrt_sdl_host_upscale(up);
    mdrt_sdl_host_prescale(look->mode == OB_MODE_MISTER ? genesis_prescale : NULL);
    mdrt_sdl_host_hz(h->refresh == OB_REFRESH_70 ? 70 : h->refresh == OB_REFRESH_60 ? 60 : 0);
    mdrt_sdl_host_scale(scale);
    mdrt_sdl_host_scanlines(scan);
    mdrt_sdl_host_mask(mask);
    mdrt_sdl_host_blend(blend);
    mdrt_sdl_host_vsync(h->vsync);
    mdrt_sdl_host_fps(h->stats);
    mdrt_sdl_host_pointer(h->pointer);
    mdrt_sdl_host_fm(0, h->fm_core);
    mdrt_sdl_host_fm(1, h->fm_chip);
}

static void shell_capture(const ob_host *h, int id)
{
    if (!h) return;
    if (id == OB_ID_CAP_REPLAY) mdrt_sdl_host_capture(0, 0);
    else if (id == OB_ID_CAP_RECORD) mdrt_sdl_host_capture(1, 0);
    else if (id == OB_ID_CAP_SECONDS) mdrt_sdl_host_capture(2, h->replay_secs_index);
    else if (id == OB_ID_CAP_GIFSCALE) mdrt_sdl_host_capture(3, h->gif_scale);
}

static void shell_quit(void)
{
    SDL_Event q;
    memset(&q, 0, sizeof q);
    q.type = SDL_QUIT;
    SDL_PushEvent(&q);
}

static const ob_shell g_shell = {
    shell_picture, shell_capture, mdrt_sdl_host_hold, mdrt_sdl_host_redraw,
    mdrt_sdl_host_draw_now, mdrt_sdl_host_vsync, mdrt_sdl_host_fps, shell_quit
};

#ifdef _WIN32
static void *native_window(SDL_Window *w)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (w && SDL_GetWindowWMInfo(w, &info) && info.subsystem == SDL_SYSWM_WINDOWS)
        return info.info.win.window;
    return NULL;
}
#endif

static void filemenu_show_for_window(SDL_Window *w)
{
    Uint32 f;
    int full, handheld;
    if (!w) return;
    f = SDL_GetWindowFlags(w);
    full = !!(f & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP));
    handheld = g_host.profile == OB_PROFILE_HANDHELD;
    ob_filemenu_show(ob_filemenu_wanted(!full && !handheld, full, handheld));
}

static int watch(void *user, SDL_Event *e)
{
    (void)user;
    if (e->type == SDL_WINDOWEVENT &&
        (e->window.event == SDL_WINDOWEVENT_SHOWN
         || e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
        SDL_Window *w = SDL_GetWindowFromID(e->window.windowID);
        if (w && !g_menu_inited) {
#ifdef _WIN32
            ob_filemenu_init(native_window(w));
            SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
#else
            ob_filemenu_init(NULL);
#endif
            ob_filemenu_sync(&g_host);
            g_menu_inited = 1;
        }
        filemenu_show_for_window(w);
    }
    if (e->type == SDL_SYSWMEVENT && e->syswm.msg) {
#ifdef _WIN32
        SDL_SysWMmsg *m = (SDL_SysWMmsg *)e->syswm.msg;
        if (m->subsystem == SDL_SYSWM_WINDOWS) {
            ob_wm wm;
            wm.msg = m->msg.win.msg;
            wm.wparam = (uintptr_t)m->msg.win.wParam;
            ob_filemenu_event(&wm);
        }
#endif
    }
    return 1;
}

static int eq_i(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
    }
    return *a == 0 && *b == 0;
}

/* value of "key = value", trimmed. 0 when the line is not that key. */
static int setting_value(char *line, const char *key, char **value)
{
    char *v, *e;
    size_t n = strlen(key);
    if (strncmp(line, key, n)) return 0;
    if (line[n] != ' ' && line[n] != '\t' && line[n] != '=') return 0;
    v = strchr(line, '=');
    if (!v) return 0;
    for (v++; *v == ' ' || *v == '\t'; v++) {}
    e = v + strlen(v);
    while (e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) e--;
    *e = 0;
    *value = v;
    return 1;
}

/* ` opens the overlay. A missing file, or PulseMan's stock Escape / F11 record,
 * is rewritten so that is the default. A key the user already changed is kept. */
static void seed_keys(ob_profile profile)
{
    FILE *f = fopen("settings.ini", "rb");
    char *text = NULL;
    long n = 0;
    int has_menu = 0, changed = 0;
    if (!f) {
        f = fopen("settings.ini", "w");
        if (!f) return;
        fprintf(f, "key.menu = Grave\nkey.record =\n");
        if (profile == OB_PROFILE_HANDHELD) {
            fprintf(f, "video.scale = integer\nvideo.aspect = 4:3\nvideo.vsync = on\n");
            fprintf(f, "window.fullscreen = 1\n");
        }
        fclose(f);
        return;
    }
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    if (n > 0 && n < 1024 * 1024 && fseek(f, 0, SEEK_SET) == 0) {
        text = malloc((size_t)n + 1);
        if (text && fread(text, 1, (size_t)n, f) == (size_t)n) text[n] = 0;
        else { free(text); text = NULL; }
    }
    fclose(f);
    if (!text) return;
    {
        char *line = text;
        while (line && *line) {
            char *nl = strchr(line, '\n');
            char *value;
            if (nl) *nl = 0;
            if (setting_value(line, "key.menu", &value)) {
                has_menu = 1;
                if (eq_i(value, "Escape") || eq_i(value, "Esc")) {
                    memmove(value + 5, value + strlen(value), 1);
                    memcpy(value, "Grave", 5);
                    changed = 1;
                }
            } else if (setting_value(line, "key.record", &value) && eq_i(value, "F11")) {
                *value = 0;
                changed = 1;
            }
            if (!nl) break;
            *nl = '\n';
            line = nl + 1;
        }
    }
    if (!has_menu || changed) {
        f = fopen("settings.ini", "w");
        if (f) {
            fputs(text, f);
            if (!has_menu) fputs("key.menu = Grave\n", f);
            fclose(f);
        }
    }
    free(text);
}

static int host_is_open(void)
{
    return g_host.overlay_open;
}

static int host_quit(void)
{
    return g_host.request_quit;
}

static void host_after(int r)
{
    if (r == OB_ID_CONTROLLERS) {
        ob_overlay_close(&g_host);
        mdrt_sdl_host_open_players();
    }
    ob_picture_apply(&g_host);
    mdrt_sdl_host_redraw();
}

static void host_toggle(void)
{
    if (g_host.overlay_open) ob_overlay_input(&g_host, MDRT_MENU_BACK);
    else ob_overlay_open(&g_host);
    ob_picture_apply(&g_host);
    mdrt_sdl_host_redraw();
}

static int host_move(int scancode)
{
    if (scancode == SDL_SCANCODE_UP) return MDRT_MENU_UP;
    if (scancode == SDL_SCANCODE_DOWN) return MDRT_MENU_DOWN;
    if (scancode == SDL_SCANCODE_LEFT) return MDRT_MENU_LEFT;
    if (scancode == SDL_SCANCODE_RIGHT) return MDRT_MENU_RIGHT;
    if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_KP_ENTER || scancode == SDL_SCANCODE_SPACE)
        return MDRT_MENU_OK;
    if (scancode == SDL_SCANCODE_ESCAPE || scancode == SDL_SCANCODE_BACKSPACE
        || scancode == SDL_SCANCODE_GRAVE || scancode == SDL_SCANCODE_F11)
        return MDRT_MENU_BACK;
    return -1;
}

static void host_key(int scancode)
{
    int in = host_move(scancode);
    if (in < 0) return;
    host_after(ob_overlay_input(&g_host, in));
}

static void host_button(int button)
{
    int in = button == SDL_CONTROLLER_BUTTON_DPAD_UP ? MDRT_MENU_UP
           : button == SDL_CONTROLLER_BUTTON_DPAD_DOWN ? MDRT_MENU_DOWN
           : button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? MDRT_MENU_LEFT
           : button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ? MDRT_MENU_RIGHT
           : button == SDL_CONTROLLER_BUTTON_A || button == SDL_CONTROLLER_BUTTON_START ? MDRT_MENU_OK
           : button == SDL_CONTROLLER_BUTTON_B || button == SDL_CONTROLLER_BUTTON_BACK
             || button == SDL_CONTROLLER_BUTTON_GUIDE ? MDRT_MENU_BACK : -1;
    if (in < 0) return;
    host_after(ob_overlay_input(&g_host, in));
}

static void host_paint(uint32_t *px, int w, int h, int pitch)
{
    ob_overlay_draw(&g_host, px, w, h, pitch);
}

static void host_pointer(int x, int y, int w, int h, int what)
{
    int r = ob_overlay_pointer(&g_host, x, y, w, h, what);
    if (what == 0) {
        if (r) mdrt_sdl_host_redraw();
        return;
    }
    host_after(r);
}

static const mdrt_host_menu g_menu = {
    host_is_open, host_toggle, host_key, host_button, host_paint, host_quit, host_pointer
};

void overbar_install(void)
{
    ob_profile profile = ob_profile_from_env();
    ob_present_apply_hints(profile);
    seed_keys(profile);
    ob_host_init(&g_host, OB_PORT_GENESIS, profile);
    ob_shell_set(&g_shell);
    mdrt_sdl_set_host_menu(&g_menu);
    SDL_AddEventWatch(watch, NULL);
}

mdrt_view mdrt_view_place(int win_w, int win_h, int pic_w, int pic_h, int scale, int aspect)
{
    ob_view_in in;
    ob_view_out out;
    mdrt_view v = { 0, 0, 0, 0, 1 };
    (void)aspect;                       /* always L5; no square Native */
    if (win_w <= 0 || win_h <= 0 || pic_w <= 0 || pic_h <= 0) return v;
    {
        const ob_look *look = g_host.video_class == OB_CLASS_HIRES ? &g_host.hires : &g_host.lores;
        memset(&in, 0, sizeof in);
        in.src_w = pic_w;
        in.src_h = pic_h;
        in.dar_h = 240;
        in.window_w = win_w;
        in.window_h = win_h;
        in.height = look->height;
        in.line_double = g_host.video_class == OB_CLASS_LORES && look->khz31;
        ob_view(&in, &out);
        if (look->mode == OB_MODE_SHARP
            && (look->height == OB_HEIGHT_INTEGER || look->height == OB_HEIGHT_OVERSCAN)) {
            /* the Sharp step caps the rows per source line (3x: 3 rows for each of the 224) */
            int want = ob_sharp_mul(look->sharp);
            int unit = pic_h * (in.line_double ? 2 : 1);
            int k = unit > 0 ? out.blit_h / unit : 1;
            if (k > want) {
                int ch = want * unit;
                int cw = (int)((long)ch * 4 * in.dar_h / (3L * pic_h));
                out.canvas_w = out.blit_w = cw;
                out.canvas_h = out.blit_h = ch;
                out.canvas_x = out.blit_x = (win_w - cw) / 2;
                out.canvas_y = out.blit_y = (win_h - ch) / 2;
            }
        }
        v.x = out.blit_x;
        v.y = out.blit_y;
        v.w = out.blit_w;
        v.h = out.blit_h;
        if (scale == MDRT_SCALE_SHARP && pic_h > 0 && out.blit_h / pic_h >= 2) {
            int fit = out.blit_h / pic_h;
            int cap = look->mode == OB_MODE_SHARP ? ob_sharp_mul(look->sharp) : fit;
            if (cap < 2) cap = fit;
            v.prescale = cap < fit ? cap : fit;
        }
    }
    return v;
}
