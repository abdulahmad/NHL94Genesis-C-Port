/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * Native file-menu shell. L3: menu bar only in windowed desktop. Overlay always
 * available. Handheld / KMSDRM: overlay only. Win32 CreateMenu from the same
 * item tree as the overlay; GTK/Cocoa bars are separate files.
 *
 * Opening the Win32 bar runs Windows' own loop in the main thread, so the game
 * is paused until the menu closes.
 */
#include "overbar/overbar.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SDL.h>
#include <SDL_syswm.h>
#endif

static void *g_window;
static int g_shown;
static ob_host *g_host;

#ifdef _WIN32
#define CMD0 0x1100
#define NCMDS 160

static HWND g_hwnd;
static HMENU g_bar;
static struct { int id, value; } cmds[NCMDS];
static int ncmds;

static UINT add_cmd(int id, int value)
{
    UINT c;
    if (ncmds >= NCMDS) return 0;
    c = (UINT)(CMD0 + ncmds);
    cmds[ncmds].id = id;
    cmds[ncmds].value = value;
    ncmds++;
    return c;
}

static int page_of(int id)
{
    switch (id) {
    case OB_ID_VIDEO: return OB_PAGE_VIDEO;
    case OB_ID_SOUND: return OB_PAGE_SOUND;
    case OB_ID_INPUT: return OB_PAGE_INPUT;
    case OB_ID_CAPTURE: return OB_PAGE_CAPTURE;
    case OB_ID_ONLINE: return OB_PAGE_ONLINE;
    case OB_ID_KEYS: return OB_PAGE_KEYS;
    default: return -1;
    }
}

static void append_items(HMENU m, const ob_item *items, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        const ob_item *it = &items[i];
        char buf[160];
        if (it->id == OB_ID_BACK) continue;
        if (it->kind == MDRT_ITEM_CHOICE && it->nchoices > 0 && it->choices) {
            HMENU sm = CreatePopupMenu();
            int j;
            for (j = 0; j < it->nchoices; j++)
                AppendMenuA(sm, MF_STRING, add_cmd(it->id, j), it->choices[j] ? it->choices[j] : "");
            AppendMenuA(m, MF_POPUP, (UINT_PTR)sm, it->label ? it->label : "");
        } else if (it->kind == MDRT_ITEM_TEXT) {
            snprintf(buf, sizeof buf, "%s%s%s", it->label ? it->label : "",
                     it->detail[0] ? " — " : "", it->detail);
            AppendMenuA(m, MF_STRING | MF_GRAYED, 0, buf);
        } else if (it->kind == MDRT_ITEM_NUMBER) {
            snprintf(buf, sizeof buf, "%s  %d", it->label ? it->label : "", it->value);
            AppendMenuA(m, MF_STRING | MF_GRAYED, 0, buf);
        } else {
            snprintf(buf, sizeof buf, "%s%s%s", it->label ? it->label : "",
                     it->detail[0] ? "\t" : "", it->detail);
            AppendMenuA(m, MF_STRING, add_cmd(it->id, it->value), buf);
        }
    }
}

static void apply_marks(void)
{
    static const int pages[] = {
        OB_PAGE_VIDEO, OB_PAGE_SOUND, OB_PAGE_INPUT,
        OB_PAGE_CAPTURE, OB_PAGE_ONLINE, OB_PAGE_KEYS
    };
    ob_item items[OB_ITEMS_MAX];
    int p, n, i, c;
    if (!g_bar || !g_host) return;
    for (c = 0; c < ncmds; c++) {
        int cur = -1, found = 0;
        for (p = 0; p < (int)(sizeof pages / sizeof *pages); p++) {
            n = ob_page_items(g_host, pages[p], items, OB_ITEMS_MAX);
            for (i = 0; i < n; i++) {
                if (items[i].id == cmds[c].id && items[i].kind == MDRT_ITEM_CHOICE) {
                    cur = items[i].value;
                    found = 1;
                    break;
                }
            }
            if (found) break;
        }
        if (found)
            CheckMenuItem(g_bar, (UINT)(CMD0 + c),
                          MF_BYCOMMAND | (cmds[c].value == cur ? MF_CHECKED : MF_UNCHECKED));
    }
}

static void build_bar(void)
{
    ob_item main_items[OB_ITEMS_MAX];
    int n, i;
    if (!g_hwnd || !g_host) return;
    ncmds = 0;
    if (g_bar) {
        SetMenu(g_hwnd, NULL);
        DestroyMenu(g_bar);
        g_bar = NULL;
    }
    g_bar = CreateMenu();
    n = ob_main_items(g_host, main_items, OB_ITEMS_MAX);
    for (i = 0; i < n; i++) {
        const ob_item *it = &main_items[i];
        int page = page_of(it->id);
        if (page >= 0) {
            ob_item sub[OB_ITEMS_MAX];
            int ns = ob_page_items(g_host, page, sub, OB_ITEMS_MAX);
            HMENU sm = CreatePopupMenu();
            append_items(sm, sub, ns);
            AppendMenuA(g_bar, MF_POPUP, (UINT_PTR)sm, it->label ? it->label : "");
        } else {
            AppendMenuA(g_bar, MF_STRING, add_cmd(it->id, 0), it->label ? it->label : "");
        }
    }
    apply_marks();
}

static void apply_cmd(int id, int value)
{
    int i;
    if (!g_host) return;
    if (id == OB_ID_RESUME) {
        ob_overlay_close(g_host);
        return;
    }
    if (id == OB_ID_QUIT) {
        SDL_Event q;
        g_host->request_quit = 1;
        ob_overlay_close(g_host);
        memset(&q, 0, sizeof q);
        q.type = SDL_QUIT;
        SDL_PushEvent(&q);
        return;
    }
    if (id == OB_ID_CONTROLLERS) {
        if (!g_host->overlay_open) ob_overlay_open(g_host);
        g_host->overlay_page = OB_PAGE_INPUT;
        ob_overlay_build(g_host);
        return;
    }
    for (i = 0; i < g_host->naddons; i++) {
        if (g_host->addons[i].id == id && g_host->addons[i].open) {
            g_host->addons[i].open(g_host->addon_ctx);
            break;
        }
    }
    ob_host_apply(g_host, g_host->video_class, id, value);
}
#endif

int ob_filemenu_wanted(int windowed, int fullscreen, int handheld)
{
    return windowed && !fullscreen && !handheld;
}

int ob_filemenu_items(const ob_host *h, ob_item *out, int cap)
{
    /* Same ids as the overlay Video page (plus main actions a bar would list). */
    ob_item tmp[OB_ITEMS_MAX];
    int n = 0, i, v;
    ob_item *it;
    if (cap < 1) return 0;
    it = &out[n++];
    memset(it, 0, sizeof *it);
    it->id = OB_ID_RESUME;
    it->kind = MDRT_ITEM_ACTION;
    it->label = "Resume";
    v = ob_video_items(h, h->has_hires ? h->video_class : OB_CLASS_LORES, tmp, OB_ITEMS_MAX);
    for (i = 0; i < v && n < cap; i++) {
        if (tmp[i].id == OB_ID_BACK) continue;   /* a bar has no Back */
        out[n++] = tmp[i];
    }
    return n;
}

void ob_filemenu_init(void *sdl_window)
{
    g_window = sdl_window;
    g_shown = 0;
#ifdef _WIN32
    {
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        g_hwnd = NULL;
        g_bar = NULL;
        ncmds = 0;
        if (sdl_window && SDL_GetWindowWMInfo((SDL_Window *)sdl_window, &info)
            && info.subsystem == SDL_SYSWM_WINDOWS)
            g_hwnd = info.info.win.window;
        SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
        (void)g_window;
    }
#else
    (void)sdl_window;
#endif
}

void ob_filemenu_sync(const ob_host *h)
{
    g_host = (ob_host *)h;
#ifdef _WIN32
    if (!g_hwnd || !g_host) return;
    build_bar();
    SetMenu(g_hwnd, g_shown ? g_bar : NULL);
    DrawMenuBar(g_hwnd);
#endif
}

void ob_filemenu_show(int on)
{
    g_shown = on;
#ifdef _WIN32
    if (!g_hwnd) return;
    SetMenu(g_hwnd, on && g_bar ? g_bar : NULL);
    DrawMenuBar(g_hwnd);
#else
    (void)g_window;
#endif
}

void ob_filemenu_event(void *syswm_msg)
{
#ifdef _WIN32
    SDL_SysWMmsg *m = (SDL_SysWMmsg *)syswm_msg;
    if (!m || m->subsystem != SDL_SYSWM_WINDOWS) return;
    if (m->msg.win.msg == WM_COMMAND && HIWORD(m->msg.win.wParam) == 0) {
        int idx = LOWORD(m->msg.win.wParam) - CMD0;
        if (idx >= 0 && idx < ncmds) {
            apply_cmd(cmds[idx].id, cmds[idx].value);
            ob_filemenu_sync(g_host);
        }
    }
#else
    (void)syswm_msg;
#endif
}

int ob_filemenu_client_size(int *w, int *h)
{
#ifdef _WIN32
    RECT r;
    if (!g_hwnd || !GetClientRect(g_hwnd, &r) || r.right <= 0 || r.bottom <= 0) return 0;
    if (w) *w = r.right;
    if (h) *h = r.bottom;
    return 1;
#else
    (void)w; (void)h;
    return 0;
#endif
}
