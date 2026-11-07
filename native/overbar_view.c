/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * Thin Overbar client for the Genesis C-port. Replaces PulseMan mdrt_view_place
 * (linked ahead of libmdrt.a) with L5 dest-rect: 4:3 as if 240 high, source 224.
 * Overlay keys ` and F11 are remapped to PulseMan's menu key; first launch sets
 * key.menu to Grave so Esc does not open the overlay (Esc still backs out of it).
 * Opaque overlay pixels come from the vendored PulseMan menu.c (linked ahead of
 * libmdrt). Win32 file menu is attached when the window is shown.
 */
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "mdrt/view.h"
#include "native.h"
#include "overbar/overbar.h"

static ob_host g_host;
static int g_menu_inited;

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
    if ((e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) && !e->key.repeat) {
        if (e->key.keysym.scancode == SDL_SCANCODE_F11)
            e->key.keysym.scancode = SDL_SCANCODE_GRAVE;
    }
    if (e->type == SDL_WINDOWEVENT &&
        (e->window.event == SDL_WINDOWEVENT_SHOWN
         || e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
        SDL_Window *w = SDL_GetWindowFromID(e->window.windowID);
        if (w && !g_menu_inited) {
            ob_filemenu_init(w);
            ob_filemenu_sync(&g_host);
            g_menu_inited = 1;
        }
        filemenu_show_for_window(w);
    }
    if (e->type == SDL_SYSWMEVENT && e->syswm.msg)
        ob_filemenu_event(e->syswm.msg);
    return 1;
}

static void seed_keys(ob_profile profile)
{
    FILE *f = fopen("settings.ini", "r");
    if (f) { fclose(f); return; }
    f = fopen("settings.ini", "w");
    if (!f) return;
    /* PulseMan fills the rest of the file on first run. Grave opens overlay; F11
     * is remapped to Grave. Record is not F11 (Capture page). Handheld: fullscreen
     * 4:3 integer, vsync on, 60 Hz panel (PulseMan has no VRR). */
    fprintf(f, "key.menu = Grave\nkey.record =\n");
    if (profile == OB_PROFILE_HANDHELD) {
        fprintf(f, "video.scale = integer\nvideo.aspect = 4:3\nvideo.vsync = on\n");
        fprintf(f, "window.fullscreen = 1\n");
    }
    fclose(f);
}

void overbar_install(void)
{
    ob_profile profile = ob_profile_from_env();
    ob_present_apply_hints(profile);
    seed_keys(profile);
    ob_host_init(&g_host, OB_PORT_GENESIS, profile);
    SDL_AddEventWatch(watch, NULL);
}

mdrt_view mdrt_view_place(int win_w, int win_h, int pic_w, int pic_h, int scale, int aspect)
{
    ob_view_in in;
    ob_view_out out;
    mdrt_view v = { 0, 0, 0, 0, 1 };
    (void)aspect;                       /* always L5; no square Native */
    if (win_w <= 0 || win_h <= 0 || pic_w <= 0 || pic_h <= 0) return v;
    memset(&in, 0, sizeof in);
    in.src_w = pic_w;
    in.src_h = pic_h;
    in.dar_h = 240;
    in.window_w = win_w;
    in.window_h = win_h;
    in.height = (scale == MDRT_SCALE_INTEGER) ? OB_HEIGHT_INTEGER : OB_HEIGHT_FIT;
    ob_view(&in, &out);
    v.x = out.blit_x;
    v.y = out.blit_y;
    v.w = out.blit_w;
    v.h = out.blit_h;
    if (scale == MDRT_SCALE_SHARP && pic_h > 0 && out.blit_h / pic_h >= 2)
        v.prescale = out.blit_h / pic_h;
    return v;
}
