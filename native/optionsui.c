/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * The game's rules pages (rosters, game rules, speed) as a screen over the picture, opened from
 * Overbar's menu ("Rules"). Arrow keys or the d-pad choose and change; Esc, B or Back saves and
 * closes. Lifted from the options pages of PulseMan's window (mdrt_sdl_options.options), which this front end replaced.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "mdrt/menu.h"
#include "mdrt/platform_sdl2.h"
#include "mdrt/settings.h"
#include "overbar/plat.h"
#include "optionsui.h"

enum { CW = MDRT_MENU_CELL_W, CH = MDRT_MENU_CELL_H };
#define SHADE 0xD0000000u
#define WHITE 0xFFFFFF
#define DIM 0x9090A0
#define GOLD 0xFFD84A

static const mdrt_sdl_options *O;
static int *V;
static mdrt_settings *S;
static void *U;
static void (*save_file)(void);
static int is_open, page, sel, changed_any;
static int src_w = 320, src_h = 224;
static SDL_Texture *tex;
static uint32_t *buf;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void options_ui_init(const mdrt_sdl_options *o, int *values, mdrt_settings *set, void *user, void (*save)(void))
{
    O = o; V = values; S = set; U = user; save_file = save;
}

int options_ui_active(void) { return is_open; }
int options_ui_pages(void) { return O && V && O->noptions > 0; }

int options_ui_open(void *ctx)
{
    (void)ctx;
    if (!options_ui_pages()) return 0;
    is_open = 1;
    page = 0;
    sel = 0;
    changed_any = 0;
    return 1;
}

static int npages(void) { return O->npages > 0 ? O->npages : 1; }

static mdrt_sdl_page page_of(int p)
{
    if (O->npages <= 0) return (mdrt_sdl_page){ O->options_title ? O->options_title : "Rules", O->options_note, 0, O->noptions };
    mdrt_sdl_page cp = O->pages[clampi(p, 0, O->npages - 1)];
    cp.first = clampi(cp.first, 0, O->noptions);
    cp.count = clampi(cp.count, 0, O->noptions - cp.first);
    return cp;
}

/* the choice the value stands for (-1: a number between the named ones) */
static int choice_of(const mdrt_sdl_option *op, int value)
{
    if (op->numbers) {
        for (int k = 0; k < op->count; k++) if (op->numbers[k] == value) return k;
        return -1;
    }
    return clampi(value, 0, op->count - 1);
}

static void value_text(int i, char *out, size_t size)
{
    const mdrt_sdl_option *op = &O->options[i];
    if (op->count > 0) {
        int k = choice_of(op, V[i]);
        if (k >= 0) snprintf(out, size, "%s", op->labels ? op->labels[k] : op->names[k]);
        else snprintf(out, size, "%d%s", V[i], op->unit ? op->unit : "");
    } else snprintf(out, size, "%d%s", V[i], op->unit ? op->unit : "");
}

static void persist(int i)
{
    const mdrt_sdl_option *op = &O->options[i];
    char v[24];
    if (op->count > 0) {
        int k = choice_of(op, V[i]);
        if (k >= 0) mdrt_settings_set(S, op->key, op->names[k]);
        else { snprintf(v, sizeof v, "%d", V[i]); mdrt_settings_set(S, op->key, v); }
    } else mdrt_settings_set_int(S, op->key, V[i]);
}

static void change(int i, int dir)
{
    const mdrt_sdl_option *op = &O->options[i];
    int before[64];
    int n = O->noptions < 64 ? O->noptions : 64;
    memcpy(before, V, (size_t)n * sizeof *V);
    if (op->count > 0) {
        int k = choice_of(op, V[i]);
        if (k < 0) k = dir > 0 ? -1 : 0;
        k = (k + dir + op->count) % op->count;
        V[i] = op->numbers ? op->numbers[k] : k;
    } else V[i] = clampi(V[i] + dir * (op->step > 0 ? op->step : 1), op->min, op->max);
    if (O->options_changed) O->options_changed(i, V, U);
    for (int j = 0; j < n; j++) if (j == i || before[j] != V[j]) persist(j);
    changed_any = 1;
}

static void close_ui(void)
{
    is_open = 0;
    if (changed_any && save_file) save_file();
}

void options_ui_key(int k)
{
    mdrt_sdl_page pg = page_of(page);
    if (k == SDL_SCANCODE_ESCAPE || k == SDL_SCANCODE_RETURN) { close_ui(); return; }
    if (k == SDL_SCANCODE_UP && pg.count) sel = (sel + pg.count - 1) % pg.count;
    else if (k == SDL_SCANCODE_DOWN && pg.count) sel = (sel + 1) % pg.count;
    else if (k == SDL_SCANCODE_LEFT && pg.count) change(pg.first + sel, -1);
    else if (k == SDL_SCANCODE_RIGHT && pg.count) change(pg.first + sel, 1);
    else if (k == SDL_SCANCODE_TAB || k == SDL_SCANCODE_PAGEDOWN) { page = (page + 1) % npages(); sel = 0; }
    else if (k == SDL_SCANCODE_PAGEUP) { page = (page + npages() - 1) % npages(); sel = 0; }
}

void options_ui_button(int slot, int b)
{
    (void)slot;
    if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) options_ui_key(SDL_SCANCODE_UP);
    else if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) options_ui_key(SDL_SCANCODE_DOWN);
    else if (b == SDL_CONTROLLER_BUTTON_DPAD_LEFT) options_ui_key(SDL_SCANCODE_LEFT);
    else if (b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT || b == SDL_CONTROLLER_BUTTON_A) options_ui_key(SDL_SCANCODE_RIGHT);
    else if (b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) options_ui_key(SDL_SCANCODE_PAGEUP);
    else if (b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) options_ui_key(SDL_SCANCODE_PAGEDOWN);
    else if (b == SDL_CONTROLLER_BUTTON_B || b == SDL_CONTROLLER_BUTTON_START || b == SDL_CONTROLLER_BUTTON_BACK) close_ui();
}

/* the font has plain ASCII only: any other character (a UTF-8 bullet) is drawn as a bar */
static void text(int col, int row, const char *s, uint32_t rgb)
{
    char t[256];
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && n < sizeof t - 1; p++) {
        if (*p < 0x80) t[n++] = (char)*p;
        else if (*p >= 0xC0) t[n++] = '|';
    }
    t[n] = 0;
    mdrt_menu_text(buf, src_w, src_h, src_w, 4 + col * CW, 4 + row * CH, t, rgb);
}

void options_ui_draw(const SDL_Rect *dst)
{
    SDL_Renderer *ren = ob_plat_renderer();
    mdrt_sdl_page pg;
    char line[120], val[64], extra[200];
    int row = 0, cols = (src_w - 8) / CW - 2;
    if (!is_open || !ren) return;
    if (!buf && !(buf = malloc((size_t)src_w * (size_t)src_h * 4))) return;
    if (!tex) {
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, src_w, src_h);
        if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }
    if (!tex) return;
    pg = page_of(page);
    for (int i = 0; i < src_w * src_h; i++) buf[i] = SHADE;
    snprintf(line, sizeof line, "%s  (%d/%d)", pg.title ? pg.title : "Rules", page + 1, npages());
    text(0, row++, line, GOLD);
    if (pg.note) { snprintf(line, sizeof line, "%.*s", cols, pg.note); text(0, row, line, DIM); }
    row += 2;
    for (int k = 0; k < pg.count; k++) {
        const mdrt_sdl_option *op = &O->options[pg.first + k];
        value_text(pg.first + k, val, sizeof val);
        snprintf(line, sizeof line, "%c %-22.22s %.*s", k == sel ? 0x7F : ' ', op->label, cols - 25, val);
        text(0, row++, line, k == sel ? GOLD : WHITE);
    }
    extra[0] = 0;
    if (O->options_text) O->options_text(page, V, extra, sizeof extra, U);
    if (extra[0]) { row++; snprintf(line, sizeof line, "%.*s", cols, extra); text(0, row++, line, DIM); }
    if (pg.count && O->options_detail) {
        extra[0] = 0;
        O->options_detail(pg.first + sel, V[pg.first + sel], extra, sizeof extra, U);
        if (extra[0]) { snprintf(line, sizeof line, "%.*s", cols, extra); text(0, row++, line, DIM); }
    }
    text(0, (src_h - 8) / CH - 2, "Up/Down: choose  Left/Right: change  Tab: page", DIM);
    text(0, (src_h - 8) / CH - 1, "Esc: save, close (next game start)", DIM);
    for (int i = 0; i < src_w * src_h; i++) if (!(buf[i] >> 24)) buf[i] |= 0xFF000000u;
    SDL_UpdateTexture(tex, NULL, buf, src_w * 4);
    SDL_RenderCopy(ren, tex, NULL, dst);
}
