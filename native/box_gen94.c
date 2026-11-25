/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 abdulahmad
 *
 * The box (overbar/box.h, docs/BOX.md in Overbar) for NHL 94 Genesis: the clamshell case, the manual, the cartridge, then the console, TV and multitap (the mockup's gen3d.js, in C). */
#include "overbar/box.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { S_CLOSED, S_OPEN, S_DESK };
static int st, ready;
static int caseG, cart, tv, tvScreen, tvKnob, con, tap, cspine, ccover, cmanual, cseat, nslot, nsw, nled, nseat;
static float opn;
static struct { int cart, tv, sys, out, pads; } G;
static int tv_tex = -1, tv_mode = -1, led_mat = -1;

static int mesh_mat_of(int node) { for (int i = 0; i < BX_S->nmeshes; i++) if (BX_S->meshes[i].node == node) return BX_S->meshes[i].mat; return -1; }
static float lin(int c) { float v = c / 255.0f; return v <= .04045f ? v / 12.92f : powf((v + .055f) / 1.055f, 2.4f); }

static void init(void)
{
    if (ready) return;
    caseG = bx_node("caseG"); cart = bx_node("cart"); tv = bx_node("tv"); tvScreen = bx_node("tvScreen"); tvKnob = bx_node("tvKnob");
    con = bx_node("con"); tap = bx_node("tap");
    cspine = bx_node("case.spine"); ccover = bx_node("case.cover"); cmanual = bx_node("case.manual"); cseat = bx_node("case.cartSeat");
    nslot = bx_node("con.slot"); nsw = bx_node("con.sw"); nled = bx_node("con.led"); nseat = bx_node("con.cartSeat");
    /* the TV's picture: a texture of our own on the screen's mesh */
    uint32_t *px = (uint32_t *)calloc(512 * 384, 4);
    tv_tex = r3_tex_add(BX_S, px, 512, 384);
    for (int i = 0; i < BX_S->nmeshes; i++) if (BX_S->meshes[i].node == tvScreen) {
        r3_mat m = BX_S->mats[BX_S->meshes[i].mat]; m.map = tv_tex; m.type = 'B'; m.col[0] = m.col[1] = m.col[2] = 1;
        BX_S->meshes[i].mat_override = r3_mat_add(BX_S, &m);
    }
    /* the LED gets a material of its own (its colour changes) */
    int lm = mesh_mat_of(nled);
    if (lm >= 0) { r3_mat m = BX_S->mats[lm]; led_mat = r3_mat_add(BX_S, &m); for (int i = 0; i < BX_S->nmeshes; i++) if (BX_S->meshes[i].node == nled) BX_S->meshes[i].mat_override = led_mat; }
    /* a label with no scan in the art folder (the cartridge's back and end labels) is left off, not drawn as a blank white sticker */
    int hid = -1;
    for (int i = 0; i < BX_S->nmeshes; i++) {
        int m = BX_S->meshes[i].mat, tx = m >= 0 ? BX_S->mats[m].map : -1;
        if (tx < 0 || BX_S->texs[tx].loaded || !BX_S->texs[tx].key[0] || strncmp(BX_S->texs[tx].key, "genCart", 7)) continue;
        if (hid < 0) { r3_mat h = BX_S->mats[m]; h.visible = 0; hid = r3_mat_add(BX_S, &h); }
        BX_S->meshes[i].mat_override = hid;
    }
    ready = 1;
}

static void tv_draw(int mode)
{
    if (mode == tv_mode && mode != 1) return;
    tv_mode = mode;
    r3_tex *t = &BX_S->texs[tv_tex];
    uint32_t *px = (uint32_t *)malloc(512 * 384 * 4);
    if (!px) return;
    uint32_t bg = mode == 0 ? 0xff030303 : mode == 1 ? 0xff0d2f5a : 0xff04163a;
    for (int i = 0; i < 512 * 384; i++) px[i] = bg;
    if (mode == 1) {
        static uint32_t r = 1;
        for (int i = 0; i < 1400; i++) {
            r = r * 1664525u + 1013904223u; int x = (r >> 8) % 510; r = r * 1664525u + 1013904223u; int y = (r >> 8) % 384;
            r = r * 1664525u + 1013904223u; int a = (int)((r >> 24) * .25f);
            for (int k = 0; k < 3; k++) { uint32_t d = px[y * 512 + x + k]; int c = ((d >> 16) & 255) + (((255 - (int)((d >> 16) & 255)) * a) >> 8); int g = ((d >> 8) & 255) + (((255 - (int)((d >> 8) & 255)) * a) >> 8); int b = (d & 255) + (((255 - (int)(d & 255)) * a) >> 8); px[y * 512 + x + k] = 0xff000000u | c << 16 | g << 8 | b; }
        }
    }
    if (mode == 2) {
        float w = ob_vf_width(OB_VF_SEMIBOLD, 88, "SEGA", 0);
        ob_vf_draw(px, 512, 384, 512, 256 - w / 2, 384 / 2 + 20, OB_VF_SEMIBOLD, 88, 0xffffff, 255, "SEGA", 0, 0);
        const char *l = "Produced by or under license from Sega Enterprises Ltd.";
        w = ob_vf_width(OB_VF_REGULAR, 17, l, 0);
        ob_vf_draw(px, 512, 384, 512, 256 - w / 2, 384 - 40, OB_VF_REGULAR, 17, 0xffffff, 255, l, 0, 0);
        for (int i = 0; i < 512 * 384; i++) px[i] |= 0xff000000u;
    }
    r3_tex_set(t, px, 512, 384);
}
static void tv_on(void) { tv_draw(G.tv ? (G.sys && G.cart ? 2 : 1) : 0); }
static void set_power(int on)
{
    r3_node *s = bx_n(nsw);
    s->pos[0] = on ? s->on_x : s->off_x;
    if (led_mat >= 0) { int c = on ? 0xff2a1a : 0x3a0606; BX_S->mats[led_mat].col[0] = lin(c >> 16); BX_S->mats[led_mat].col[1] = lin((c >> 8) & 255); BX_S->mats[led_mat].col[2] = lin(c & 255); }
}
static int count_pads(void)
{
    int n = 0;
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) n++;
    return n;
}
static void set_open(float t) { bx_n(cspine)->rot[1] = -PI_F / 2 * t; bx_n(ccover)->rot[1] = -PI_F / 2 * t; }

static void caption(void)
{
    snprintf(BX.title, sizeof BX.title, "%s", st == S_DESK ? "Set up the system" : "Box");
    if (st == S_CLOSED) bx_say("<b>The box.</b> Drag to spin it, pick it to open.");
    else if (st == S_OPEN) bx_say(G.out ? "<b>Your box.</b> Back returns to the game." : "<b>Manual on the left, cartridge on the right.</b>");
    else bx_say("<b>Set up the system.</b> Put the cartridge in the console.");
}
static void open_case(int instant)
{
    st = S_OPEN;
    bx_n(cart)->vis = !G.out;
    float ms = instant ? 1 : 900;
    bx_tw3(bx_n(caseG)->rot, .12f, -.05f, 0, ms, 0);
    bx_tw3(bx_n(caseG)->pos, 3.4f, 0, 0, ms, 0);
    bx_tw1(&opn, 1, instant ? 1 : 1400, 0);
    bx_cam_to(0, 0, 21, ms);
    if (!instant) bx_sfx(SFX_CLICK, 0);
    caption();
}
static void open_now(int a) { (void)a; open_case(1); }

static void seat_pos(float v[3]) { r3_world_pos(BX_S, nseat, v); }
static void desk2(int a)
{
    (void)a;
    bx_attach(cart, -1);
    float v[3]; seat_pos(v);
    bx_tw3(bx_n(cart)->pos, v[0], v[1] + 4.2f, v[2], 1000, 0);
    bx_tw3(bx_n(cart)->rot, 0, 0, 0, 1000, 0);
    bx_tw3(bx_n(cart)->scale, 1, 1, 1, 1000, 0);
    bx_cam_to(62, -1, 54, 1100);
}
static void to_desk(int a)
{
    (void)a;
    st = S_DESK; G.cart = 0; caption();
    r3_node *c = bx_n(cart);
    bx_tw1(&c->pos[2], c->pos[2] + 4, 500, 0);
    bx_after(520, desk2, 0);
}
static void to_case(int a)
{
    (void)a;
    st = S_OPEN;
    bx_cam_to(0, 0, 21, 900);
    G.cart = 0;
    if (bx_n(cart)->parent != cseat) { bx_parent(cart, cseat); bx_set3(bx_n(cart)->pos, 0, 0, 0); bx_set3(bx_n(cart)->rot, 0, 0, 0); bx_set3(bx_n(cart)->scale, 1, 1, 1); }
    caption();
}
static void check_done(int a) { (void)a; bx_done(); }
static void check(void)
{
    if (G.cart && G.tv && G.sys) { bx_say("<b>Power on.</b>"); tv_draw(2); bx_after(1300, check_done, 0); }
}
static void sys_on(int a) { (void)a; G.sys = 1; bx_sfx(SFX_KEY, 0); set_power(1); tv_on(); check(); }
static void tv_turn_on(int a) { (void)a; G.tv = 1; bx_sfx(SFX_KEY, 0); tv_on(); bx_after(600, sys_on, 0); }
static void insert_cart(void)
{
    if (G.cart) return;
    G.cart = 1;
    bx_sfx(SFX_DISK_IN, 0);
    float v[3]; seat_pos(v);
    bx_tw3(bx_n(cart)->pos, v[0], v[1], v[2], 650, 0);
    bx_say("<b>Cartridge in.</b> Turning on the TV and the system...");
    bx_after(700, tv_turn_on, 0);
}
static void tv_power(void) { G.tv = !G.tv; bx_sfx(SFX_KEY, 0); tv_on(); check(); }
static void sys_power(void)
{
    if (!G.cart) { bx_say("Put the cartridge in first."); return; }
    G.sys = !G.sys; bx_sfx(SFX_KEY, 0); set_power(G.sys); tv_on(); check();
}

/* ---- the manual: the front cover scan (genManual), then the pages genManual1, 2, ... (the back cover last), read from the
 * art folder as they are shown ---- */
static void open_manual(void)
{
    static char keys[200][24];
    static const char *k[200];
    int n = 0;
    if (bx_art_exists("genManual")) snprintf(keys[n++], sizeof keys[0], "genManual");
    for (int i = 1; n < 200; i++) {
        snprintf(keys[n], sizeof keys[0], "genManual%d", i);
        if (!bx_art_exists(keys[n])) break;
        n++;
    }
    if (!n) { bx_say("<b>No manual scans.</b> Put them in the box art folder."); if (BX.manual_only) bx_manual_only_closed(0); return; }
    for (int i = 0; i < n; i++) k[i] = keys[i];
    if (BX.manual_only) bx_view_open_keys("NHL '94 Genesis manual", k, n, 1, bx_manual_only_closed);
    else bx_book_open("NHL '94 Genesis manual", k, n, NULL);         /* read in 3D, in front of the open case */
}

static void enter(int mode)
{
    init();
    bx_time_clear();
    G.out = mode != BOX_SHOW_FIRST; G.cart = G.tv = G.sys = 0;
    r3_node *c = bx_n(caseG);
    c->vis = 1; bx_set3(c->pos, 0, 0, 0); bx_set3(c->rot, .18f, -.35f, 0);
    opn = 0; set_open(0);
    bx_n(cmanual)->vis = 1;
    bx_parent(cart, cseat); bx_n(cart)->vis = 0; bx_set3(bx_n(cart)->pos, 0, 0, 0); bx_set3(bx_n(cart)->rot, 0, 0, 0); bx_set3(bx_n(cart)->scale, 1, 1, 1);
    tv_mode = -1; tv_on(); set_power(0);
    G.pads = count_pads();
    bx_cam(0, 0, 19);
    st = S_CLOSED;
    caption();
    if (mode != BOX_SHOW_FIRST) bx_after(30, open_now, 0);
}

static void frame(float dt)
{
    (void)dt;
    set_open(opn);
    G.pads = count_pads();
    bx_n(tap)->opacity = G.pads > 2 ? 1 : .28f;
    if (st == S_CLOSED || st == S_OPEN) {
        if (st == S_OPEN) { bx_hot_node(cmanual, "Read the manual"); if (bx_n(cart)->vis && !G.cart) bx_hot_node(cart, "Take the cartridge"); }
        bx_hot_node(caseG, st == S_CLOSED ? "Open the box" : "");
    }
    if (st == S_DESK) {
        if (!G.cart) { bx_hot_node(cart, "Insert the cartridge"); bx_hot_node(nslot, "Insert the cartridge"); }
        bx_hot_node(tvKnob, "TV power"); bx_hot_node(nsw, "System power");
        bx_hot_node(tap, G.pads > 2 ? "EA 4 Way Play: multitap enabled" : "Multitap: 2 or fewer input devices detected");
    }
    float *cm = BX_S->cam;
    bx_look[0] = cm[0]; bx_look[1] = st == S_DESK ? -3 : cm[1] * .3f; bx_look[2] = 0;
}
static void back(void)
{
    if (st == S_DESK) { bx_fade(to_case, 0); return; }
    if ((st == S_OPEN && G.out) || (st == S_CLOSED && !BX.first)) { bx_done(); return; }
}
static void back_btn(int a) { (void)a; back(); }
static void draw2d(void)
{
    float u = BX_C.u, top = BX_C.sy + 1.4f * u;
    if (!(st == S_CLOSED && BX.first) && !(st == S_OPEN && BX.first)) bx_button(BX_C.sx + 2 * u, top, (st == S_DESK) ? "< Back" : "< Back to the game", back_btn, 0, -1);
    bx_text(BX_C.sx + BX_C.sw - 2 * u, top + 2.0f * u, 1.5f * u, 0xb5a58f, 255, BX.title, 0, 1);
    if (st == S_DESK) {
        const char *s[3] = { "Put the cartridge in the system", "Turn on the TV", "Turn on the system" };
        int d[3] = { G.cart, G.tv, G.sys };
        for (int i = 0; i < 3; i++) {
            float y = BX_C.sy + BX_C.sh * .93f - (2 - i) * 2.1f * u - 4 * u;
            uint32_t c = d[i] ? 0x5bbf7a : 0xf4ecdf;
            ob_vf_disc(BX_C.px, BX_C.w, BX_C.h, BX_C.pitch, BX_C.sx + 3.5f * u, y - .5f * u, .45f * u, d[i] ? 0xff000000u | c : 0, 0xff000000u | c, .15f * u);
            bx_text(BX_C.sx + 4.4f * u, y, 1.5f * u, c, 255, s[i], 0, -1);
        }
        const char *t = G.pads > 2 ? "Multitap enabled: more than 2 input devices" : "Multitap off: 2 or fewer input devices detected";
        float w = bx_text_w(1.4f * u, t, 0) + 2.8f * u;
        bx_rrect(BX_C.sx + BX_C.sw - 2 * u - w, BX_C.sy + 7 * u, w, 2.8f * u, .6f * u, G.pads > 2 ? 0xff143a22 : 0xff2a2a2e, G.pads > 2 ? 0xff2f7a4a : 0xff44444a, 1);
        bx_text(BX_C.sx + BX_C.sw - 2 * u - w / 2, BX_C.sy + 8.85f * u, 1.4f * u, G.pads > 2 ? 0xbff3cf : 0x8d8d95, 255, t, 0, 0);
    }
    if (BX.cap[0]) bx_rich(BX_C.sx + BX_C.sw / 2, BX_C.sy + BX_C.sh - 1.6f * u, 1.6f * u, BX.cap, 0xb5a58f, 0xf4ecdf);
}
static void activate(int node, float sx, float sy)
{
    (void)sx; (void)sy;
    if (node == caseG) { if (st == S_CLOSED) open_case(0); return; }
    if (node == cmanual) { open_manual(); return; }
    if (node == cart) {
        if (st == S_OPEN && !G.out) bx_fade(to_desk, 0);
        else if (st == S_DESK) insert_cart();
        else bx_say("The cartridge is already in the system. Back returns to the game.");
        return;
    }
    if (node == nslot) { insert_cart(); return; }
    if (node == tvKnob) { tv_power(); return; }
    if (node == nsw) { sys_power(); return; }
    if (node == tap) bx_say(G.pads > 2 ? "<b>Multitap enabled.</b> EA 4 Way Play is plugged into the second controller port." : "<b>Multitap off.</b> Connect more than 2 controllers to enable it.");
}
/* the middle of the spine in the world (case.spine's origin is its back edge; the spine runs 1.3 toward the cover) */
static void spine_mid(float o[3])
{
    const float *W = r3_world(BX_S, cspine);
    float p[3] = { 0, 0, .65f };
    for (int k = 0; k < 3; k++) o[k] = W[k * 4] * p[0] + W[k * 4 + 1] * p[1] + W[k * 4 + 2] * p[2] + W[k * 4 + 3];
}
static void drag(int node, float dx, float dy)
{
    if (st == S_DESK) return;
    int t = caseG;                                          /* the cartridge turns with its case: it is not taken out to be turned */
    r3_node *n = bx_n(t);
    float a[3], b[3];
    int pivot = t == caseG && st == S_OPEN;                 /* open, the case turns about the middle of its spine, so it stays in view */
    if (pivot) spine_mid(a);
    n->rot[1] += dx * .012f;
    n->rot[0] += dy * .012f;
    if (n->rot[0] > 1.2f) n->rot[0] = 1.2f;
    if (n->rot[0] < -1.2f) n->rot[0] = -1.2f;
    if (pivot) { spine_mid(b); for (int k = 0; k < 3; k++) n->pos[k] += a[k] - b[k]; }
}
static void manual_entry(void) { open_manual(); }

const bx_scene BOX_GEN94 = { "gen94", enter, frame, draw2d, activate, back, drag, manual_entry, NULL, NULL, NULL, NULL };
