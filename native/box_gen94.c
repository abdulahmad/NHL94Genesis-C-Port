/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 abdulahmad
 *
 * The box (overbar/box.h, docs/BOX.md in Overbar) for NHL 94 Genesis: the clamshell case, the manual, the cartridge, then the console, TV and multitap (the mockup's gen3d.js, in C). */
#include "overbar/box.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { S_CLOSED, S_OPEN, S_DESK, S_PACK, S_POSTER, S_CARDS };
static int st, ready;
static int caseG, cart, tv, tvScreen, tvKnob, con, tap, cspine, ccover, cmanual, cseat, nslot, nsw, nled, nseat;
static float opn;
static struct { int cart, tv, sys, out, pads; } G;
static int tv_tex = -1, tv_mode = -1, led_mat = -1, tvPower = -1, tvRemote = -1;

/* the pack-ins: hidden in the case until "Pack-ins" lays them out in front of it; each opens front and back */
#define NPK 9
static int pk[NPK];
static float case_home[3];
/* everything in the case that can be laid out: the nine pack-ins, then the manual and the cartridge */
#define NIT (NPK + 2)
static int it_node[NIT], it_par[NIT], it_saved[NIT], laid;
static float it_l[NIT][9], it_w[NIT][9];         /* each one's pose in the case (in its parent, and in the world) */
/* the stage: dark ground with a pool of light, and the closed box's blurry reflection in it (after the game's TV commercial) */
static int nfloorA = -1, nfloorB = -1, nrefl = -1;
#define TV_DIST 58.0f                       /* the camera in front of the TV screen */
static float lk[3];                              /* where the camera looks on the desk (tweened) */
#define CASE_W 5.03f
#define CASE_H 7.02f
#define CASE_D 1.30f
#define DESK_Y (-8.2f)
static const char *pk_key[NPK] = { "pk4way", "pkFifa", "pkPoster", "pkHint", "pkReg", "pkWear", "pkOffer", "pkCard", "pkCap" };
static const char *pk_name[NPK] = { "EA 4 Way Play card", "FIFA International Soccer card", "EA SPORTS poster", "NHL '94 Hint Cards pack",
    "Registration card", "EA SPORTS-WEAR card", "Hint Cards order card", "Hint card: Brad Shaw", "EA SPORTS milk cap" };

/* ---- the TV's picture: 1280 x 960 on the glass, the MiSTer JVC D-Series shadow mask (mister/Shadow_Masks) over it,
 * the tube's turn-on (a bright line that opens to the full picture) and turn-off (the picture folds to a line, then a dot) ---- */
#define TVW 1280
#define TVH 960
static float mask_v[16 * 16][3];
static int mask_w, mask_h;
static float tv_on_t = -1, tv_off_t = -1;     /* 0..1 while the tube turns on or off, -1 otherwise */
static float snow_t;

static void mask_load(void)
{
    static const char *name = "mister/Shadow_Masks/JVC D-Series (~2000).txt";
    char path[1024], line[512];
    FILE *f = NULL;
    char *base = SDL_GetBasePath();
    if (base) { snprintf(path, sizeof path, "%s%s", base, name); SDL_free(base); f = fopen(path, "r"); }
    if (!f) f = fopen(name, "r");
    mask_w = mask_h = 0;
    if (!f) return;
    int want = 0, y = 0;
    while (fgets(line, sizeof line, f)) {
        char *q = line;
        while (*q == ' ' || *q == '\t') q++;
        if (*q == '#' || *q == '\n' || *q == '\r' || !*q) continue;
        if (!strncmp(q, "Resolution=", 11)) { if (want == 3) break; continue; }   /* the first pattern (for the lowest output size) */
        if (!strncmp(q, "v2", 2)) { want = 1; continue; }
        if (want == 1) {
            if (sscanf(q, "%d,%d", &mask_w, &mask_h) != 2 || mask_w < 1 || mask_h < 1 || mask_w > 16 || mask_h > 16) { mask_w = mask_h = 0; break; }
            want = 2; y = 0; continue;
        }
        if (want == 2) {
            for (int x = 0; x < mask_w; x++) {
                unsigned c = 0, on = 0, off = 0;
                if (sscanf(q, "%1x%1x%1x", &c, &on, &off) != 3) break;
                for (int ch = 0; ch < 3; ch++) mask_v[y * mask_w + x][ch] = (c & (4u >> ch)) ? 1.f + on / 16.f : off / 16.f;
                q = strchr(q, ',');
                if (!q) break;
                q++;
            }
            if (++y == mask_h) want = 3;
        }
    }
    fclose(f);
    if (want != 3) mask_w = mask_h = 0;
}

/* the picture the console puts out: the licence screen (genTmss in the art folder, 320 x 224), else the same words drawn here */
static uint32_t tmss[320 * 224];
static int tmss_ready;
static void tmss_make(void)
{
    if (tmss_ready) return;
    tmss_ready = 1;
    r3_tex *t = bx_art("genTmss");
    if (t && t->nlv && t->w > 0 && t->h > 0) {
        for (int y = 0; y < 224; y++) for (int x = 0; x < 320; x++) tmss[y * 320 + x] = t->lv[0][(y * t->h / 224) * t->w + x * t->w / 320] | 0xff000000u;
        return;
    }
    for (int i = 0; i < 320 * 224; i++) tmss[i] = 0xff000000u;
    const char *l[3] = { "PRODUCED BY OR", "UNDER LICENSE FROM", "SEGA ENTERPRISES LTD." };
    for (int i = 0; i < 3; i++) { float w = ob_vf_width(OB_VF_SEMIBOLD, 13, l[i], 0); ob_vf_draw(tmss, 320, 224, 320, 160 - w / 2, 92 + i * 16, OB_VF_SEMIBOLD, 13, 0xffffff, 255, l[i], 0, 0); }
    for (int i = 0; i < 320 * 224; i++) tmss[i] |= 0xff000000u;
}

static void tv_draw(int mode)
{
    tv_mode = mode;
    uint32_t *px = (uint32_t *)malloc((size_t)TVW * TVH * 4);
    if (!px) return;
    int lit = mode != 0 || tv_off_t >= 0;
    if (!lit) {                                          /* off: the dark grey-green of the bare tube */
        for (int i = 0; i < TVW * TVH; i++) px[i] = 0xff1c201d;
        r3_tex_set(&BX_S->texs[tv_tex], px, TVW, TVH);
        return;
    }
    if (mode == 2) tmss_make();
    float band = 1, wide = 1, boost = 1, flash = 0;
    if (tv_on_t >= 0) {
        float t = tv_on_t;
        band = t < .1f ? .004f : .004f + .996f * bx_ease((t - .1f) / .9f);
        boost = 1 + 1.6f * (1 - t); flash = t < .45f ? 1 - t / .45f : 0;
    }
    if (tv_off_t >= 0) {
        float t = tv_off_t;
        band = t < .55f ? 1 - .996f * bx_ease(t / .55f) : .004f;
        wide = t < .55f ? 1 : 1 - (t - .55f) / .45f;
        boost = 1 + 2 * t; flash = t;
    }
    static uint32_t r = 7;
    for (int y = 0; y < TVH; y++) {
        float dy = fabsf(y - TVH / 2.f) / (TVH / 2.f);
        int sy = (int)(((float)y / TVH - .05f) / .9f * 224);   /* the picture fills about 90% of the glass (overscan) */
        for (int x = 0; x < TVW; x++) {
            float dx = fabsf(x - TVW / 2.f) / (TVW / 2.f), c[3] = { 8, 9, 8 };
            if (dy <= band && dx <= wide) {
                if (mode == 1) {                                 /* no picture on the input: snow */
                    r = r * 1664525u + 1013904223u;
                    if ((x & 1) == 0) { float v = 20 + (r >> 24) * .55f; c[0] = c[1] = c[2] = v; }
                    else { uint32_t q = px[y * TVW + x - 1]; c[0] = c[1] = c[2] = (float)(q & 255); }
                } else if (mode == 2) {
                    float u = ((float)x / TVW - .04f) / .92f * 320 - .5f;
                    if (sy >= 0 && sy < 224 && u >= 0 && u < 319) {
                        int ix = (int)u; float f = u - ix;
                        uint32_t a = tmss[sy * 320 + ix], b = tmss[sy * 320 + ix + 1];
                        for (int ch = 0; ch < 3; ch++) { int sh = 16 - 8 * ch; c[ch] = ((a >> sh) & 255) * (1 - f) + ((b >> sh) & 255) * f; }
                    }
                }
                float e = band < 1 ? 1 - dy / band : 0;          /* the beam's edge glows while it opens */
                for (int ch = 0; ch < 3; ch++) {
                    float v = c[ch] * boost + 230 * flash * (band < .05f ? 1 : e * e);
                    if (mask_w) v *= mask_v[(y % mask_h) * mask_w + x % mask_w][ch];
                    c[ch] = v > 255 ? 255 : v;
                }
            }
            px[y * TVW + x] = 0xff000000u | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | (uint32_t)c[2];
        }
    }
    r3_tex_set(&BX_S->texs[tv_tex], px, TVW, TVH);
}


static int mesh_mat_of(int node) { for (int i = 0; i < BX_S->nmeshes; i++) if (BX_S->meshes[i].node == node) return BX_S->meshes[i].mat; return -1; }
static float lin(int c) { float v = c / 255.0f; return v <= .04045f ? v / 12.92f : powf((v + .055f) / 1.055f, 2.4f); }

/* the backdrop: near black, a touch of cold blue low down, darker corners */
static void stage_bg(uint32_t *px, int w, int h)
{
    for (int y = 0; y < h; y++) {
        float v = (float)y / (float)(h - 1);
        for (int x = 0; x < w; x++) {
            float u = (float)x / (float)(w - 1) - .5f, d = u * u * 1.6f + (v - .62f) * (v - .62f) * 1.2f;
            float k = 1 - d * 1.5f; if (k < 0) k = 0;
            int r = (int)(4 + 9 * k), g = (int)(5 + 10 * k), b = (int)(8 + 15 * k);
            px[(size_t)y * w + x] = 0xff000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
        }
    }
}
/* the ground's picture: a soft pool of cold light that fades out to nothing; shadow puts the box's contact shadow in the middle
   (size: the ground's side in inches) */
static int pool_tex(float bright, float spread, int shadow, float size)
{
    const int N = 512;
    uint32_t *px = (uint32_t *)malloc((size_t)N * N * 4);
    if (!px) return -1;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float u = (x + .5f) / N * 2 - 1, v = (y + .5f) / N * 2 - 1, r = sqrtf(u * u + v * v);
            float g = expf(-(r / spread) * (r / spread)) * bright;
            if (shadow) {
                float sx = u * size * .5f / (CASE_W * .62f), sy = v * size * .5f / (CASE_D * 1.5f);
                g *= 1 - .7f * expf(-(sx * sx + sy * sy));
            }
            float a = r < .7f ? 1 : r > 1 ? 0 : 1 - (r - .7f) / .3f;
            int R = (int)(6 + 190 * g), G2 = (int)(8 + 200 * g), B2 = (int)(12 + 236 * g);
            if (R > 255) R = 255;
            if (G2 > 255) G2 = 255;
            if (B2 > 255) B2 = 255;
            px[(size_t)y * N + x] = (uint32_t)(a * 255) << 24 | (uint32_t)R << 16 | (uint32_t)G2 << 8 | (uint32_t)B2;
        }
    return r3_tex_add(BX_S, px, N, N);
}
static int flat_mat(int tex, int side)
{
    r3_mat m; memset(&m, 0, sizeof m);
    m.type = 'B'; m.col[0] = m.col[1] = m.col[2] = 1; m.map = tex; m.opacity = 1; m.transparent = 1; m.side = side; m.depth_write = 0; m.visible = 1; m.bright = 1;
    return r3_mat_add(BX_S, &m);
}
static int ground(const char *name, float size, int tex, int ro)
{
    int n = r3_node_add(BX_S, -1, name);
    if (n < 0) return -1;
    int me = r3_mesh_quad(BX_S, n, flat_mat(tex, 2), -size / 2, size / 2, -size / 2, size / 2, 0, 1, 0);
    if (me >= 0) BX_S->meshes[me].ro = ro;
    BX_S->nodes[n].rot[0] = -PI_F / 2;
    return n;
}
/* a face of the box as the ground shows it: the scan, small and blurred, fading out away from the ground */
static int refl_tex(const char *key, float inset)
{
    const int W = 64, H = 96;
    r3_tex *t = key ? bx_art(key) : NULL;
    float *f = (float *)calloc((size_t)W * H * 3, sizeof(float)), *g = (float *)calloc((size_t)W * H * 3, sizeof(float));
    uint32_t *px = (uint32_t *)malloc((size_t)W * H * 4);
    if (!f || !g || !px) { free(f); free(g); free(px); return -1; }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float u = ((x + .5f) / W - inset) / (1 - 2 * inset), v = ((y + .5f) / H - inset * .7f) / (1 - 1.4f * inset);
            float c[3] = { 10, 10, 12 };
            if (t && t->w > 0 && u >= 0 && u < 1 && v >= 0 && v < 1) {
                /* the mean of a block of the scan */
                int x0 = (int)(u * t->w), y0 = (int)(v * t->h), bw = t->w / W + 1, bh = t->h / H + 1, n = 0; float a[3] = { 0, 0, 0 };
                for (int yy = y0; yy < y0 + bh && yy < t->h; yy += 2)
                    for (int xx = x0; xx < x0 + bw && xx < t->w; xx += 2) { uint32_t p = t->lv[0][(size_t)yy * t->w + xx]; a[0] += (p >> 16) & 255; a[1] += (p >> 8) & 255; a[2] += p & 255; n++; }
                if (n) for (int k = 0; k < 3; k++) c[k] = a[k] / n;
            }
            for (int k = 0; k < 3; k++) f[((size_t)y * W + x) * 3 + k] = c[k];
        }
    for (int pass = 0; pass < 3; pass++) {           /* blur */
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                for (int k = 0; k < 3; k++) {
                    float a = 0; int n = 0;
                    for (int dy = -2; dy <= 2; dy++)
                        for (int dx = -2; dx <= 2; dx++) { int xx = x + dx, yy = y + dy; if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue; a += f[((size_t)yy * W + xx) * 3 + k]; n++; }
                    g[((size_t)y * W + x) * 3 + k] = a / n;
                }
        float *sw = f; f = g; g = sw;
    }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float up = 1 - (y + .5f) / H;             /* 0 at the box's foot (the picture's last row), 1 at its top */
            float a = .6f * powf(1 - up, 2.6f);
            const float *c = f + ((size_t)y * W + x) * 3;
            px[(size_t)y * W + x] = (uint32_t)(a * 255) << 24 | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | (uint32_t)c[2];
        }
    free(f); free(g);
    return r3_tex_add(BX_S, px, W, H);
}
static float cb[6] = { -CASE_W / 2, CASE_W / 2, -CASE_H / 2, CASE_H / 2, -CASE_D / 2, CASE_D / 2 };   /* the closed case in its own frame: x0 x1 y0 y1 z0 z1 */
static void case_bounds(void)
{
    r3_update(BX_S);
    float inv[16], m[16], lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
    r3_m4_inv(inv, r3_world(BX_S, caseG));
    for (int i = 0; i < BX_S->nmeshes; i++) {
        const r3_mesh *me = &BX_S->meshes[i];
        if (!r3_is_under(BX_S, me->node, caseG) || r3_is_under(BX_S, me->node, cart) || !r3_visible(BX_S, me->node)) continue;
        r3_m4_mul(m, inv, r3_world(BX_S, me->node));
        for (int v = 0; v < me->nv; v++) {
            const float *p = me->v + v * 8;
            for (int k = 0; k < 3; k++) {
                float q = m[k * 4] * p[0] + m[k * 4 + 1] * p[1] + m[k * 4 + 2] * p[2] + m[k * 4 + 3];
                if (q < lo[k]) lo[k] = q;
                if (q > hi[k]) hi[k] = q;
            }
        }
    }
    if (hi[0] > lo[0]) for (int k = 0; k < 3; k++) { cb[k * 2] = lo[k]; cb[k * 2 + 1] = hi[k]; }
}
static void refl_face(int parent, const char *name, const char *key, float inset, float w, float ry, float px_, float pz)
{
    int n = r3_node_add(BX_S, parent, name);
    if (n < 0) return;
    int tx = refl_tex(key, inset);
    int me = r3_mesh_quad(BX_S, n, flat_mat(tx, 0), -w / 2, w / 2, cb[2], cb[3], 0, 1, 0);
    if (me >= 0) BX_S->meshes[me].ro = -1;
    r3_node *nd = &BX_S->nodes[n];
    nd->rot[1] = ry; nd->pos[0] = px_; nd->pos[2] = pz;
}
static void stage_make(void)
{
    case_bounds();
    nfloorA = ground("stageBox", 46, pool_tex(1.0f, .46f, 1, 46), -3);
    nfloorB = ground("stageDesk", 150, pool_tex(.5f, .5f, 0, 150), -3);
    nrefl = r3_node_add(BX_S, -1, "caseRefl");
    if (nrefl < 0) return;
    float w = cb[1] - cb[0], d = cb[5] - cb[4], mx = (cb[0] + cb[1]) / 2, mz = (cb[4] + cb[5]) / 2;
    refl_face(nrefl, "caseReflF", "genFront", .035f, w, 0, mx, cb[5]);
    refl_face(nrefl, "caseReflB", "genBack", .035f, w, PI_F, mx, cb[4]);
    refl_face(nrefl, "caseReflS", "genSpine", .03f, d, -PI_F / 2, cb[0], mz);
    refl_face(nrefl, "caseReflR", NULL, 0, d, PI_F / 2, cb[1], mz);
}
/* each frame: the ground under whatever stands on it, and the box's reflection opposite the box */
static void stage_frame(void)
{
    if (nfloorA < 0 || nfloorB < 0 || nrefl < 0) return;
    r3_node *fa = bx_n(nfloorA), *fb = bx_n(nfloorB), *rf = bx_n(nrefl), *c = bx_n(caseG);
    int desk = st == S_DESK;
    fa->vis = !desk; fb->vis = desk;
    fb->pos[0] = 62; fb->pos[1] = DESK_Y; fb->pos[2] = 2;
    /* the ground touches the lowest corner of the case, wherever the case is and however it is turned */
    r3_update(BX_S);
    const float *W = r3_world(BX_S, caseG);
    float rx = c->rot[0], fy = 1e9f, wp[3] = { W[3], W[7], W[11] };
    for (int i = 0; i < 8; i++) {
        float p[3] = { cb[i & 1], cb[2 + ((i >> 1) & 1)], cb[4 + ((i >> 2) & 1)] };
        float y = W[4] * p[0] + W[5] * p[1] + W[6] * p[2] + W[7];
        if (y < fy) fy = y;
    }
    fa->pos[0] = wp[0]; fa->pos[1] = fy; fa->pos[2] = wp[2];
    float k = (1 - opn * 3) * (1 - fabsf(rx) * 3);
    if (k < 0) k = 0;
    rf->vis = !desk && k > .01f && st < S_PACK;
    rf->opacity = k;
    rf->pos[0] = wp[0]; rf->pos[1] = 2 * fy - wp[1]; rf->pos[2] = wp[2];
    rf->rot[0] = -c->rot[0]; rf->rot[1] = c->rot[1]; rf->rot[2] = -c->rot[2];
    rf->scale[0] = c->scale[0]; rf->scale[1] = -c->scale[1]; rf->scale[2] = c->scale[2];
}

static void poster_make(void);
static void cards_make(void);
static void init(void)
{
    if (ready) return;
    caseG = bx_node("caseG"); cart = bx_node("cart"); tv = bx_node("tv"); tvScreen = bx_node("tvScreen"); tvKnob = bx_node("tvKnob");
    con = bx_node("con"); tap = bx_node("tap");
    cspine = bx_node("case.spine"); ccover = bx_node("case.cover"); cmanual = bx_node("case.manual"); cseat = bx_node("case.cartSeat");
    nslot = bx_node("con.slot"); nsw = bx_node("con.sw"); nled = bx_node("con.led"); nseat = bx_node("con.cartSeat");
    /* the TV's picture: a texture of our own on the screen's mesh */
    uint32_t *px = (uint32_t *)calloc((size_t)TVW * TVH, 4);
    tv_tex = r3_tex_add(BX_S, px, TVW, TVH);
    tvPower = bx_node("tv.power"); tvRemote = bx_node("tvRemote");
    for (int i = 0; i < NPK; i++) { char n[8]; snprintf(n, sizeof n, "pk%d", i); pk[i] = bx_node(n); }
    mask_load();
    stage_make();
    poster_make();
    cards_make();
    bx_bg_paint = stage_bg;
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

static int tv_want(void) { return G.tv ? (G.sys && G.cart ? 2 : 1) : 0; }
static void tv_on(void) { tv_draw(tv_want()); }
/* the TV's power: the door drops open, the button goes in, the tube turns on (or off), the door shuts */
static void door_shut(int a) { (void)a; bx_tw1(&bx_n(tvKnob)->rot[0], 0, 450, 0); }
static void button_out(int a) { (void)a; if (tvPower >= 0) bx_tw1(&bx_n(tvPower)->pos[2], -.08f, 120, 0); }
static void tv_switch(void)
{
    bx_tw1(&bx_n(tvKnob)->rot[0], 1.55f, 350, 0);
    if (tvPower >= 0) { bx_tw1(&bx_n(tvPower)->pos[2], -.16f, 120, 380); bx_after(560, button_out, 0); }
    if (G.tv) { tv_on_t = 0; tv_off_t = -1; } else { tv_off_t = 0; tv_on_t = -1; }
    bx_after(1500, door_shut, 0);
}
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
    if (st == S_POSTER) { snprintf(BX.title, sizeof BX.title, "The poster"); bx_say("<b>The poster, unfolded.</b> Drag to spin it, flip it over for the catalog, or pick it for a close look."); return; }
    if (st == S_CARDS) { snprintf(BX.title, sizeof BX.title, "Hint cards"); bx_say("<b>The hint cards from the pack.</b> Drag a card to spin it, pick it for a close look."); return; }
    if (st == S_PACK) { snprintf(BX.title, sizeof BX.title, "In the box"); bx_say(G.out ? "<b>Everything that came in the box.</b> Drag an item to spin it, pick it to look closer." : "<b>Everything that came in the box.</b> Drag an item to spin it. To play, pick the cartridge."); return; }
    snprintf(BX.title, sizeof BX.title, "%s", st == S_DESK ? "Set up the system" : "Box");
    if (st == S_CLOSED) bx_say("<b>The box.</b> Drag to spin it, pick it to open.");
    else if (st == S_OPEN) bx_say(G.out ? "<b>Your box.</b> Pick the bundle to lay everything out." : "<b>Manual and pack-ins on the left, cartridge on the right.</b> Pick anything in the box to lay it all out.");
    else bx_say("<b>Set up the system.</b> Put the cartridge in the console.");
}
static void open_case(int instant)
{
    st = S_OPEN;
    bx_n(cart)->vis = !G.out;
    float ms = instant ? 1 : 900;
    bx_tw3(bx_n(caseG)->rot, 0, -.05f, 0, ms, 0);
    bx_tw3(bx_n(caseG)->pos, 3.4f, 0, 0, ms, 0);
    bx_tw1(&opn, 1, instant ? 1 : 1400, 0);
    bx_cam_to(0, 1.2f, 23, ms);
    if (!instant) bx_sfx(SFX_CLICK, 0);
    caption();
}
static void open_now(int a) { (void)a; open_case(1); }

/* ---- the bundle: the pack-ins sit in the lid behind the manual, their edges showing round it; picking the bundle lays
 * everything out in two rows in front of the camera (a little smaller than life), the manual and the cartridge too ---- */
static void pose_get(int node, float o[9]) { r3_node *n = bx_n(node); memcpy(o, n->pos, 12); memcpy(o + 3, n->rot, 12); memcpy(o + 6, n->scale, 12); }
static void pose_set(int node, const float o[9]) { r3_node *n = bx_n(node); memcpy(n->pos, o, 12); memcpy(n->rot, o + 3, 12); memcpy(n->scale, o + 6, 12); }
/* the papers lie squared up between the lid and the manual, stepped so an edge of each shows round it (the lid leaves about
   0.3 in each side); the manual stands that much further out of the lid. The sealed pack of hint cards and the milk cap are too
   thick to go behind: they sit on the manual's lower corners. The sample hint card is in the pack. */
#define MAN_Z (-.105f)
#define MAN_OUT .05f
static void stack_pose(void)
{
    static const float off[NPK][3] = {       /* across, up, a quarter turn for the two wide cards */
        {  .42f,  .10f, 0 },      /* 4 Way Play */
        { -.42f,  .14f, 0 },      /* FIFA */
        {  .30f, -.20f, 0 },      /* poster */
        { -.62f, -.72f, 0 },      /* hint cards pack (on the manual) */
        { -.50f, -.12f, 1 },      /* registration card */
        {  .45f, -.26f, 1 },      /* sports wear */
        { 1.12f, -1.52f, 0 },     /* hint cards offer */
        { 0, 0, 0 },              /* the sample hint card: in the pack */
        { 1.02f, -2.0f, 0 },      /* milk cap (on the manual) */
    };
    static const int slot[NPK] = { 0, 1, 2, -1, 4, 5, 6, -1, -1 };
    bx_n(cmanual)->pos[2] = MAN_Z - MAN_OUT;
    const float *m = bx_n(cmanual)->pos;
    for (int i = 0; i < NPK; i++) {
        if (pk[i] < 0) continue;
        r3_node *n;
        if (i == 7) { bx_parent(pk[i], -1); bx_n(pk[i])->vis = 0; continue; }
        bx_parent(pk[i], bx_n(cmanual)->parent);
        n = bx_n(pk[i]);
        /* seen from inside the lid (its -z side): turned about y to face that way, so across runs the other way */
        float z = slot[i] >= 0 ? -.060f - .0065f * slot[i] : m[2] - .047f - (i == 3 ? .060f : .030f);
        bx_set3(n->pos, m[0] - off[i][0], m[1] + off[i][1], z);
        bx_set3(n->rot, 0, PI_F, off[i][2] > 0 ? PI_F / 2 : 0);
        bx_set3(n->scale, 1, 1, slot[i] >= 0 ? .3f : 1);
        n->vis = 1;
    }
}
/* back in the case at once (the desk's Back, a new opening) */
static void poster_away(void);
static void cards_away(void);
static void lay_restore(void)
{
    poster_away(); cards_away();
    for (int i = 0; i < NIT; i++) if (it_saved[i]) { bx_parent(it_node[i], it_par[i]); pose_set(it_node[i], it_l[i]); it_saved[i] = 0; }
    if (laid) bx_set3(bx_n(caseG)->pos, case_home[0], case_home[1], case_home[2]);
    laid = 0;
}
static void lay_restore_cb(int a) { (void)a; if (st == S_OPEN) lay_restore(); }
static const float it_wd[NIT] = { 3.9f, 3.9f, 4.13f, 2.7f, 5.75f, 5.75f, 2.5f, 2.5f, 1.84f, 4.2f, 3.86f };
#define LAY_Z 4.0f
static void lay_out(void)
{
    if (st != S_OPEN || laid) return;
    st = S_PACK; laid = 1; bx_sfx(SFX_SLIDE, 0);
    static const int row[2][6] = { { 9, 10, 0, 1, 2, 3 }, { 4, 5, 6, 8, -1, -1 } };
    const float sc = .6f, gap = .42f, ry[2] = { 1.85f, -2.8f };
    float *cm = BX_S->cam;
    r3_update(BX_S);
    it_node[NPK] = cmanual; it_node[NPK + 1] = cart;
    for (int i = 0; i < NPK; i++) it_node[i] = pk[i];
    int order = 0;
    for (int r = 0; r < 2; r++) {
        float tw = 0; int n = 0, use[6];
        for (int k = 0; k < 6 && row[r][k] >= 0; k++) {
            int i = row[r][k];
            if (it_node[i] < 0 || !r3_visible(BX_S, it_node[i])) continue;      /* the cartridge is in the system: it is not in the box */
            use[n++] = i; tw += it_wd[i] * sc;
        }
        tw += gap * (n - 1);
        float x = cm[0] - tw / 2;
        for (int k = 0; k < n; k++, order++) {
            int i = use[k], node = it_node[i];
            it_par[i] = bx_n(node)->parent; pose_get(node, it_l[i]); it_saved[i] = 1;
            bx_attach(node, -1); pose_get(node, it_w[i]);
            r3_node *p = bx_n(node);
            float w = it_wd[i] * sc, d = order * 70.f;
            bx_tw3(p->pos, x + w / 2, ry[r], LAY_Z, 800, d);
            /* the manual lies face down in the lid: it is laid out cover up (a half turn, however the angles spell it) */
            if (i == NPK && fabsf(p->rot[0]) > 1) bx_tw3(p->rot, p->rot[0] < 0 ? -PI_F : PI_F, 0, p->rot[2] < 0 ? -PI_F : PI_F, 800, d);
            else bx_tw3(p->rot, 0, i == NPK ? (p->rot[1] < 0 ? -PI_F : PI_F) : 0, i >= NPK - 1 ? 0 : (i & 1 ? .025f : -.025f), 800, d);
            bx_tw3(p->scale, sc, sc, sc, 800, d);
            x += w + gap;
        }
    }
    r3_node *cg = bx_n(caseG);   /* the empty case sinks out of the picture */
    case_home[0] = cg->pos[0]; case_home[1] = cg->pos[1]; case_home[2] = cg->pos[2];
    bx_tw3(cg->pos, cg->pos[0], cg->pos[1] - 14, cg->pos[2], 700, 250);
}
static void lay_in(void)
{
    st = S_OPEN; bx_sfx(SFX_SLIDE, 0);
    bx_tw3(bx_n(caseG)->pos, case_home[0], case_home[1], case_home[2], 600, 0);
    for (int i = 0; i < NIT; i++) if (it_saved[i]) {
        r3_node *p = bx_n(it_node[i]);
        bx_tw3(p->pos, it_w[i][0], it_w[i][1], it_w[i][2], 650, 150);
        bx_tw3(p->rot, it_w[i][3], it_w[i][4], it_w[i][5], 650, 150);
        bx_tw3(p->scale, it_w[i][6], it_w[i][7], it_w[i][8], 650, 150);
    }
    bx_after(830, lay_restore_cb, 0);
}

/* ---- the poster: twelve panels (four across, three down) hinged on its creases, folded to one panel in the box. Each panel's
 * picture is cut from the photos of the whole poster unfolded (pkPosterOpenF: "If it's in the game", pkPosterOpenB: the catalog)
 * at the points where the creases cross in each photo, so the photo is shown as it is and its creases lie on the hinges. The
 * sheet keeps the photo's proportions. Unfolded, the creases stay a little bent, as the paper does. ---- */
#define PW 4.13f
#define PH 6.04f                       /* the panel's height by the photo of the front (panel 0.684 wide for 1 high) */
#define PH_CARD 5.78f                  /* the folded panel as it is modelled in the box */
#define PT .015f
#define P_SC .47f
static int ppiv = -1, post = -1, pH[3], pP[3][4], post_tex[2] = { -1, -1 }, post_have, post_flip, post_busy;
static float post_home[9];
/* where the creases and edges cross in each photo, in its pixels: four rows of five, top left first */
static const float f_pt[4][5][2] = {
    { { 18, 86 }, { 359, 77 }, { 701, 61 }, { 1020, 43 }, { 1372, 14 } },
    { { 62, 530 }, { 387, 542 }, { 701, 534 }, { 1030, 528 }, { 1384, 522 } },
    { { 81, 1031 }, { 391, 1032 }, { 716, 1027 }, { 1054, 1041 }, { 1405, 1037 } },
    { { 81, 1527 }, { 391, 1532 }, { 731, 1552 }, { 1085, 1573 }, { 1418, 1558 } } };
static const float c_pt[4][5][2] = {
    { { 14, 22 }, { 372, 42 }, { 712, 70 }, { 1057, 45 }, { 1352, 68 } },
    { { 10, 535 }, { 373, 545 }, { 716, 560 }, { 1054, 575 }, { 1352, 585 } },
    { { 10, 1050 }, { 374, 1050 }, { 720, 1050 }, { 1050, 1050 }, { 1356, 1050 } },
    { { 13, 1572 }, { 375, 1568 }, { 722, 1556 }, { 1048, 1548 }, { 1353, 1548 } } };
static const float f_size[2] = { 1419, 1600 }, c_size[2] = { 1437, 1600 };
/* a place on the paper (0..1 across, 0..1 down) to its place in a photo */
static void crease_uv(const float pt[4][5][2], const float size[2], float X, float Y, float *u, float *v)
{
    float fx = X * 4, fy = Y * 3;
    int i = (int)fx, j = (int)fy;
    if (i > 3) i = 3;
    if (j > 2) j = 2;
    if (i < 0) i = 0;
    if (j < 0) j = 0;
    float a = fx - i, b = fy - j, o[2];
    for (int k = 0; k < 2; k++) {
        float t = pt[j][i][k] + (pt[j][i + 1][k] - pt[j][i][k]) * a, w = pt[j + 1][i][k] + (pt[j + 1][i + 1][k] - pt[j + 1][i][k]) * a;
        o[k] = (t + (w - t) * b) / size[k];
    }
    *u = o[0]; *v = o[1];
}
/* one side of panel (row r from the top, column c from the left, seen from the front): a 4 x 4 grid so the picture follows the creases */
static void poster_face(int node, int mat, float x0, float x1, int r, int c, int face)
{
    enum { N = 4 };
    int me = r3_mesh_quad(BX_S, node, mat, x0, x1, 0, PH, 0, face, 0);
    if (me < 0) return;
    float *v = (float *)malloc((N + 1) * (N + 1) * 8 * sizeof(float));
    uint32_t *ix = (uint32_t *)malloc(N * N * 6 * sizeof(uint32_t));
    if (!v || !ix) { free(v); free(ix); return; }
    for (int j = 0; j <= N; j++)
        for (int i = 0; i <= N; i++) {
            float a = (float)i / N, b = (float)j / N, *q = v + (j * (N + 1) + i) * 8, X = (c + a) / 4, Y = (r + 1 - b) / 3, u, t;
            q[0] = x0 + (x1 - x0) * a; q[1] = PH * b; q[2] = 0; q[3] = 0; q[4] = 0; q[5] = face < 0 ? -1.f : 1.f;
            if (face > 0) crease_uv(f_pt, f_size, X, Y, &u, &t); else crease_uv(c_pt, c_size, 1 - X, Y, &u, &t);
            q[6] = u; q[7] = 1 - t;
        }
    int k = 0;
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
            uint32_t p = (uint32_t)(j * (N + 1) + i), e[6] = { p, p + 1, p + N + 2, p, p + N + 2, p + N + 1 };
            if (face < 0) { e[1] = p + N + 2; e[2] = p + 1; e[4] = p + N + 1; e[5] = p + N + 2; }
            memcpy(ix + k, e, sizeof e); k += 6;
        }
    r3_mesh *q = &BX_S->meshes[me];
    free(q->v); free(q->idx);
    q->v = v; q->idx = ix; q->nv = (N + 1) * (N + 1); q->ni = N * N * 6;
}
static void poster_make(void)
{
    int mat[2];
    for (int k = 0; k < 2; k++) {
        uint32_t *px = (uint32_t *)malloc(4 * sizeof(uint32_t));
        if (px) for (int i = 0; i < 4; i++) px[i] = 0xff202020u;
        post_tex[k] = r3_tex_add(BX_S, px, 2, 2);
        r3_mat m; memset(&m, 0, sizeof m);
        m.type = 'B'; m.col[0] = m.col[1] = m.col[2] = 1; m.map = post_tex[k]; m.opacity = 1; m.depth_write = 1; m.visible = 1; m.bright = 1;
        mat[k] = r3_mat_add(BX_S, &m);
    }
    ppiv = r3_node_add(BX_S, -1, "posterPivot");     /* the middle of the open sheet: it turns about this */
    post = ppiv < 0 ? -1 : r3_node_add(BX_S, ppiv, "poster");
    if (post < 0) return;
    pH[2] = r3_node_add(BX_S, post, "posterRow2");
    pH[1] = r3_node_add(BX_S, pH[2], "posterRow1");
    pH[0] = r3_node_add(BX_S, pH[1], "posterRow0");
    for (int r = 0; r < 3; r++) {
        pP[r][2] = r3_node_add(BX_S, pH[r], "posterP2");
        pP[r][3] = r3_node_add(BX_S, pP[r][2], "posterP3");
        pP[r][1] = r3_node_add(BX_S, pP[r][2], "posterP1");
        pP[r][0] = r3_node_add(BX_S, pP[r][1], "posterP0");
    }
    for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) if (pH[r] < 0 || pP[r][c] < 0) { post = -1; return; }
    bx_n(pH[2])->pos[1] = -PH / 2; bx_n(pH[1])->pos[1] = PH; bx_n(pH[0])->pos[1] = PH;
    for (int r = 0; r < 3; r++) {
        bx_n(pP[r][3])->pos[0] = PW / 2; bx_n(pP[r][1])->pos[0] = -PW / 2; bx_n(pP[r][0])->pos[0] = -PW;
        static const float x0[4] = { -PW, -PW, -PW / 2, 0 };
        for (int c = 0; c < 4; c++) { poster_face(pP[r][c], mat[0], x0[c], x0[c] + PW, r, c, 1); poster_face(pP[r][c], mat[1], x0[c], x0[c] + PW, r, c, -1); }
    }
    bx_n(ppiv)->vis = 0;
}
static void pv(float *v, float to, float ms, float delay) { if (ms <= 0) *v = to; else bx_tw1(v, to, ms, delay); }
/* fold (the columns behind the third one, then the rows behind the bottom one) or open out, the creases left a little bent */
static void poster_cols(int fold, float ms, float delay)
{
    for (int r = 0; r < 3; r++) {
        pv(&bx_n(pP[r][3])->rot[1], fold ? PI_F : .06f, ms, delay);   pv(&bx_n(pP[r][3])->pos[2], fold ? -PT : 0, ms, delay);
        pv(&bx_n(pP[r][1])->rot[1], fold ? -PI_F : -.06f, ms, delay); pv(&bx_n(pP[r][1])->pos[2], fold ? -2 * PT : 0, ms, delay);
        pv(&bx_n(pP[r][0])->rot[1], fold ? PI_F : .08f, ms, delay);   pv(&bx_n(pP[r][0])->pos[2], fold ? PT : 0, ms, delay);
    }
}
static void poster_rows(int fold, float ms, float delay)
{
    pv(&bx_n(pH[1])->rot[0], fold ? -PI_F : -.05f, ms, delay); pv(&bx_n(pH[1])->pos[2], fold ? -7 * PT : 0, ms, delay);
    pv(&bx_n(pH[0])->rot[0], fold ? PI_F : .06f, ms, delay);   pv(&bx_n(pH[0])->pos[2], fold ? PT : 0, ms, delay);
}
static int poster_load(void)
{
    static const char *key[2] = { "pkPosterOpenF", "pkPosterOpenB" };
    if (post_have) return post_have > 0;
    post_have = -1;
    if (post < 0 || !bx_art_exists(key[0]) || !bx_art_exists(key[1])) return 0;
    for (int k = 0; k < 2; k++) {
        r3_tex *t = bx_art(key[k]);
        if (!t || !t->nlv || t->w < 1 || t->h < 1) return 0;
        uint32_t *px = (uint32_t *)malloc((size_t)t->w * t->h * 4);
        if (!px) return 0;
        memcpy(px, t->lv[0], (size_t)t->w * t->h * 4);
        r3_tex_set(&BX_S->texs[post_tex[k]], px, t->w, t->h);
    }
    post_have = 1;
    return 1;
}
static void caption(void);
static void pk_read(int i);
static void hide_items(int but) { for (int i = 0; i < NIT; i++) if (it_saved[i] && i != but) bx_n(it_node[i])->vis = 0; }
static void poster_ready(int a) { (void)a; post_busy = 0; }
static void poster_open(void)
{
    if (!poster_load()) { pk_read(2); return; }
    st = S_POSTER; post_flip = 0; post_busy = 1; bx_sfx(SFX_SLIDE, 0);
    hide_items(-1);
    pose_get(pk[2], post_home);
    if (post_home[2] > LAY_Z) post_home[2] = LAY_Z;
    post_home[7] *= PH_CARD / PH;                     /* folded, it is the size of the panel in the box */
    pose_set(ppiv, post_home);
    poster_cols(1, 0, 0); poster_rows(1, 0, 0);
    r3_node *p = bx_n(ppiv), *q = bx_n(post);
    bx_set3(q->pos, 0, 0, 0);
    p->vis = 1;
    bx_tw3(p->pos, BX_S->cam[0], -.45f, LAY_Z, 800, 0);
    bx_tw3(p->rot, 0, 0, 0, 800, 0);
    bx_tw3(p->scale, P_SC, P_SC, P_SC, 800, 0);
    bx_tw3(q->pos, PW / 2, -PH, 0, 800, 0);           /* the folded panel is the sheet's bottom row, third column */
    poster_rows(0, 800, 500); poster_cols(0, 900, 1200);
    bx_after(2100, poster_ready, 0);
    caption();
}
static void poster_away(void)
{
    if (ppiv >= 0) bx_n(ppiv)->vis = 0;
    for (int i = 0; i < NIT; i++) if (it_saved[i]) bx_n(it_node[i])->vis = 1;
    post_busy = 0;
}
static void poster_closed(int a) { (void)a; if (st != S_POSTER) return; poster_away(); st = S_PACK; caption(); }
static void poster_close(void)
{
    if (post_busy) return;
    post_busy = 1; bx_sfx(SFX_SLIDE, 0);
    r3_node *p = bx_n(ppiv), *q = bx_n(post);
    bx_tw3(p->rot, 0, 0, 0, 500, 0);
    poster_cols(1, 700, 0); poster_rows(1, 650, 600);
    bx_tw3(q->pos, 0, 0, 0, 550, 1100);
    bx_tw3(p->pos, post_home[0], post_home[1], post_home[2], 550, 1100);
    bx_tw3(p->scale, post_home[6], post_home[7], post_home[8], 550, 1100);
    bx_after(1700, poster_closed, 0);
}
static void poster_flip(int a)
{
    (void)a;
    if (st != S_POSTER || post_busy) return;
    post_flip = !post_flip; bx_sfx(SFX_SLIDE, 0);
    bx_tw3(bx_n(ppiv)->rot, 0, post_flip ? PI_F : 0, 0, 800, 0);
}
static void poster_look(void)
{
    static const char *k[2] = { "pkPosterOpenF", "pkPosterOpenB" };
    bx_view_open_keys("EA SPORTS poster", k, 2, 0, NULL);
}

/* ---- the hint cards: the sealed pack opens and the cards in it come out in a row beside it: the sample card that came in the
 * box (front and back), and the cards there are scans of (pkHc1F..: fronts only, so their backs are left plain) ---- */
#define NHC 4
static int hc[NHC] = { -1, -1, -1, -1 }, hc_tex[NHC] = { -1, -1, -1, -1 }, hc_on[NHC], hc_have, hc_busy;
static float pack_home[9];
static const char *hc_key[NHC] = { "pkCardF", "pkHc1F", "pkHc2F", "pkHc3F" };
static const char *hc_name[NHC] = { "Hint card: Brad Shaw", "Hint card: Mathieu Schneider", "Hint card: Eric Desjardins", "Hint card: Boston Bruins" };
static const float hc_w[NHC] = { 2.5f, 2.5f, 2.5f, 3.5f }, hc_h[NHC] = { 3.5f, 3.5f, 3.5f, 2.47f };
static void cards_make(void)
{
    hc[0] = pk[7];
    int src = -1;                                        /* a pack-in's printed face: the new cards are the same stock */
    for (int i = 0; i < BX_S->nmeshes && src < 0; i++) { int m = BX_S->meshes[i].mat; if (pk[7] >= 0 && r3_is_under(BX_S, BX_S->meshes[i].node, pk[7]) && m >= 0 && BX_S->mats[m].map >= 0) src = m; }
    if (src < 0) return;
    for (int k = 1; k < NHC; k++) {
        uint32_t *px = (uint32_t *)malloc(4 * sizeof(uint32_t));
        if (px) for (int i = 0; i < 4; i++) px[i] = 0xffd8d4ccu;
        hc_tex[k] = r3_tex_add(BX_S, px, 2, 2);
        r3_mat m = BX_S->mats[src]; m.map = hc_tex[k]; m.side = 0;
        int mf = r3_mat_add(BX_S, &m);
        m.map = -1; m.col[0] = m.col[1] = m.col[2] = .78f;
        int mb = r3_mat_add(BX_S, &m);
        hc[k] = r3_node_add(BX_S, -1, "hintCard");
        if (hc[k] < 0) continue;
        r3_mesh_quad(BX_S, hc[k], mf, -hc_w[k] / 2, hc_w[k] / 2, -hc_h[k] / 2, hc_h[k] / 2, .007f, 1, 0);
        r3_mesh_quad(BX_S, hc[k], mb, -hc_w[k] / 2, hc_w[k] / 2, -hc_h[k] / 2, hc_h[k] / 2, -.007f, -1, 0);
        bx_n(hc[k])->vis = 0;
    }
}
static void cards_load(void)
{
    if (hc_have) return;
    hc_have = 1;
    hc_on[0] = hc[0] >= 0;
    for (int k = 1; k < NHC; k++) {
        r3_tex *t = hc[k] >= 0 && bx_art_exists(hc_key[k]) ? bx_art(hc_key[k]) : NULL;
        if (!t || !t->nlv || t->w < 1 || t->h < 1) continue;
        uint32_t *px = (uint32_t *)malloc((size_t)t->w * t->h * 4);
        if (!px) continue;
        memcpy(px, t->lv[0], (size_t)t->w * t->h * 4);
        r3_tex_set(&BX_S->texs[hc_tex[k]], px, t->w, t->h);
        hc_on[k] = 1;
    }
}
static void cards_ready(int a) { (void)a; hc_busy = 0; }
static float hc_to[NHC][3];
static void card_place(int k)
{
    r3_node *c = bx_n(hc[k]);
    bx_tw3(c->pos, hc_to[k][0], hc_to[k][1], hc_to[k][2], 650, 0);
    bx_tw3(c->scale, 1, 1, 1, 650, 0);
}
static void pack_open(void)
{
    if (hc_busy) return;
    cards_load();
    st = S_CARDS; hc_busy = 1; bx_sfx(SFX_SLIDE, 0);
    hide_items(3);
    pose_get(pk[3], pack_home);
    if (pack_home[2] > LAY_Z) pack_home[2] = LAY_Z;
    const float gap = .45f, psc = .95f, y = -.5f;
    float tw = 2.7f * psc;
    int n = 0;
    for (int k = 0; k < NHC; k++) if (hc_on[k]) { tw += gap + hc_w[k]; n++; }
    float x = BX_S->cam[0] - tw / 2;
    r3_node *p = bx_n(pk[3]);
    float px = x + 2.7f * psc / 2;
    bx_tw3(p->pos, px, y, LAY_Z, 600, 0); bx_tw3(p->rot, 0, 0, 0, 600, 0); bx_tw3(p->scale, psc, psc, psc, 600, 0);
    x += 2.7f * psc + gap;
    n = 0;
    for (int k = 0; k < NHC; k++) {
        if (!hc_on[k]) continue;
        bx_parent(hc[k], -1);
        r3_node *c = bx_n(hc[k]);
        bx_set3(c->pos, pack_home[0], pack_home[1], pack_home[2] - .12f); bx_set3(c->rot, 0, 0, 0); bx_set3(c->scale, pack_home[6], pack_home[6], pack_home[6]);
        c->vis = 1;
        /* with the pack to its place, then out of it and along to its own */
        bx_tw3(c->pos, px, y, LAY_Z - .12f, 600, 0);
        bx_tw3(c->scale, .9f, .9f, .9f, 600, 0);
        bx_set3(hc_to[k], x + hc_w[k] / 2, y, LAY_Z);
        bx_after(650 + n * 140, card_place, k);
        x += hc_w[k] + gap; n++;
    }
    bx_after(650 + n * 140 + 700, cards_ready, 0);
    caption();
}
static void cards_away(void)
{
    for (int k = 0; k < NHC; k++) if (hc[k] >= 0) bx_n(hc[k])->vis = 0;
    hc_busy = 0;
}
static void pack_closed(int a)
{
    (void)a;
    if (st != S_CARDS) return;
    cards_away();
    for (int i = 0; i < NIT; i++) if (it_saved[i]) bx_n(it_node[i])->vis = 1;
    st = S_PACK; caption();
}
static void pack_close(void)
{
    if (hc_busy) return;
    hc_busy = 1; bx_sfx(SFX_SLIDE, 0);
    r3_node *p = bx_n(pk[3]);
    for (int k = 0; k < NHC; k++) if (hc_on[k]) {
        r3_node *c = bx_n(hc[k]);
        bx_tw3(c->pos, p->pos[0], p->pos[1], LAY_Z - .12f, 500, 0); bx_tw3(c->rot, 0, 0, 0, 500, 0); bx_tw3(c->scale, .9f, .9f, .9f, 500, 0);
    }
    bx_tw3(p->rot, 0, 0, 0, 400, 0);
    bx_tw3(p->pos, pack_home[0], pack_home[1], pack_home[2], 500, 450);
    bx_tw3(p->scale, pack_home[6], pack_home[7], pack_home[8], 500, 450);
    bx_after(980, pack_closed, 0);
}
static void card_read(int k)
{
    static const char *shaw[2] = { "pkCardF", "pkCardB" };
    static const char *one[1];
    if (k == 0) { bx_view_open_keys(hc_name[0], shaw, 1 + (bx_art_exists("pkCardB") ? 1 : 0), 0, NULL); return; }
    one[0] = hc_key[k];
    bx_view_open_keys(hc_name[k], one, 1, 0, NULL);
}
static void pk_read(int i)
{
    static char kf[24], kb[24]; const char *k[2] = { kf, kb };
    snprintf(kf, sizeof kf, "%sF", pk_key[i]); snprintf(kb, sizeof kb, "%sB", pk_key[i]);
    int n = bx_art_exists(kf) + (bx_art_exists(kb) ? 1 : 0);
    if (!bx_art_exists(kf)) { bx_say("<b>No scan of this pack-in.</b> Put it in the box art folder."); return; }
    bx_view_open_keys(pk_name[i], k, n, n > 1, NULL);
}

/* where the cartridge sits when it is pushed home: the commercial's seated cartridge shows about 2.2 in above the console
   (of its 3.53), so it goes this much further down than the slot's rim */
#define SEAT_DEEP .45f
static void seat_pos(float v[3]) { r3_world_pos(BX_S, nseat, v); v[1] -= SEAT_DEEP; }
#define CART_UP 1.0f                               /* how far the cartridge stands out of the slot before it is pushed in */
/* the desk opens on the commercial's shot: low, in front of the console, the cartridge standing in the slot not yet pushed home */
static void to_desk(int a)
{
    (void)a;
    st = S_DESK; G.cart = 0; caption();
    bx_attach(cart, -1);
    float v[3]; seat_pos(v);
    r3_node *c = bx_n(cart);
    bx_set3(c->pos, v[0], v[1] + CART_UP, v[2]); bx_set3(c->rot, 0, 0, 0); bx_set3(c->scale, 1, 1, 1);
    bx_cam(v[0] - 1.0f, v[1] + 1.95f, v[2] + 12.5f);
    bx_set3(lk, v[0], v[1] + 1.35f, v[2]);
}
static void to_case(int a)
{
    (void)a;
    st = S_OPEN;
    lay_restore();
    bx_cam_to(0, 1.2f, 23, 900);
    G.cart = 0;
    if (bx_n(cart)->parent != cseat) { bx_parent(cart, cseat); bx_set3(bx_n(cart)->pos, 0, 0, 0); bx_set3(bx_n(cart)->rot, 0, 0, 0); bx_set3(bx_n(cart)->scale, 1, 1, 1); }
    caption();
}
static void check_done(int a) { (void)a; bx_done(); }
static void check(void)
{
    if (G.cart && G.tv && G.sys) { bx_say("<b>Power on.</b>"); tv_on(); bx_after(3200, check_done, 0); }
}
static void sys_on(int a) { (void)a; G.sys = 1; bx_sfx(SFX_KEY, 0); set_power(1); tv_on(); check(); }
static void tv_turn_on(int a) { (void)a; G.tv = 1; bx_sfx(SFX_KEY, 0); tv_switch(); tv_on(); bx_after(1500, sys_on, 0); }
/* after the click: over to the TV, centred on its screen (the console below it, in the picture) */
static void cam_wide(int a)
{
    (void)a;
    float t[3]; r3_world_pos(BX_S, tvScreen, t);
    bx_cam_to(t[0], t[1], t[2] + TV_DIST, 1500); bx_tw3(lk, t[0], t[1], t[2], 1500, 0);
}
static void insert_cart(void)
{
    if (G.cart) return;
    G.cart = 1;
    bx_sfx(SFX_DISK_IN, 0);
    float v[3]; seat_pos(v);
    bx_tw3(bx_n(cart)->pos, v[0], v[1], v[2], 130, 0);                 /* pushed home: a short, hard click */
    bx_sfx(SFX_CLICK, 0);
    bx_say("<b>Cartridge in.</b> Turning on the TV and the system...");
    bx_after(650, cam_wide, 0);                                        /* then back to the whole desk */
    bx_after(2500, tv_turn_on, 0);
}
static void tv_power(void) { G.tv = !G.tv; bx_sfx(SFX_KEY, 0); tv_switch(); tv_on(); check(); }
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
    c->vis = 1; bx_set3(c->pos, 0, 0, 0); bx_set3(c->rot, 0, -.2f, 0);
    opn = 0; set_open(0);
    bx_n(cmanual)->vis = 1;
    bx_parent(cart, cseat); bx_n(cart)->vis = 0; bx_set3(bx_n(cart)->pos, 0, 0, 0); bx_set3(bx_n(cart)->rot, 0, 0, 0); bx_set3(bx_n(cart)->scale, 1, 1, 1);
    tv_on_t = tv_off_t = -1; bx_n(tvKnob)->rot[0] = 0;
    lay_restore(); memset(it_saved, 0, sizeof it_saved); laid = 0;
    stack_pose();
    tv_mode = -1; tv_on(); set_power(0);
    G.pads = count_pads();
    bx_cam(0, 2.4f, 21);
    st = S_CLOSED;
    caption();
    if (mode != BOX_SHOW_FIRST) bx_after(30, open_now, 0);
}

static void frame(float dt)
{
    set_open(opn);
    if (tv_on_t >= 0) { tv_on_t += dt / 750; if (tv_on_t >= 1) tv_on_t = -1; tv_on(); }
    else if (tv_off_t >= 0) { tv_off_t += dt / 420; if (tv_off_t >= 1) tv_off_t = -1; tv_on(); }
    else if (tv_mode == 1 && (snow_t += dt) > 70) { snow_t = 0; tv_on(); }
    G.pads = count_pads();
    bx_n(tap)->opacity = G.pads > 2 ? 1 : .28f;
    if (st == S_CLOSED || st == S_OPEN) {
        /* open, before the things are laid out: the case and everything in it are one selection (they sit under the case,
           so a pick anywhere on them lands here) */
        bx_hot_node(caseG, st == S_CLOSED ? "Open the box" : "Everything in the box: pick to lay it out");
    }
    if (st == S_PACK) {
        int hv = bx_hovered_node();
        if (it_saved[NPK]) bx_hot_node(cmanual, "Manual: pick to read");
        if (it_saved[NPK + 1]) bx_hot_node(cart, G.out ? "Cartridge" : "Cartridge: pick to put it in the system");
        for (int i = 0; i < NPK; i++) if (pk[i] >= 0) bx_hot_node(pk[i], pk_name[i]);
        for (int i = 0; i < NIT; i++) if (it_saved[i]) {           /* the one under the pointer lifts a little */
            float *z = &bx_n(it_node[i])->pos[2];
            if (it_node[i] == hv && *z >= LAY_Z - .01f) *z = LAY_Z + .5f;
            else if (*z > LAY_Z) *z = fmaxf(LAY_Z, *z - .04f * dt / 16.7f);
        }
    }
    if (st == S_POSTER && !post_busy) bx_hot_node(ppiv, "Poster: drag to spin, pick for a close look");
    if (st == S_CARDS && !hc_busy) {
        bx_hot_node(pk[3], "Hint Cards pack");
        for (int k = 0; k < NHC; k++) if (hc_on[k]) bx_hot_node(hc[k], hc_name[k]);
    }
    /* closed, nothing of the bundle shows (it is inside the case) */
    if (!laid && st <= S_OPEN) for (int i = 0; i < NPK; i++) if (pk[i] >= 0) bx_n(pk[i])->vis = i != 7 && opn > .04f;
    if (st == S_DESK) {
        if (!G.cart) { bx_hot_node(cart, "Insert the cartridge"); bx_hot_node(nslot, "Insert the cartridge"); }
        bx_hot_node(tvKnob, "TV power"); if (tvRemote >= 0) bx_hot_node(tvRemote, "TV power (remote)"); bx_hot_node(nsw, "System power");
        bx_hot_node(tap, G.pads > 2 ? "EA 4 Way Play: multitap enabled" : "Multitap: 2 or fewer input devices detected");
    }
    float *cm = BX_S->cam;
    if (st == S_DESK) { bx_look[0] = lk[0]; bx_look[1] = lk[1]; bx_look[2] = lk[2]; }
    else { bx_look[0] = cm[0]; bx_look[1] = cm[1] * .3f - 1.5f; bx_look[2] = 0; }
    stage_frame();
}
static void back(void)
{
    if (st == S_DESK) { bx_fade(to_case, 0); return; }
    if (st == S_POSTER) { poster_close(); return; }
    if (st == S_CARDS) { pack_close(); return; }
    if (st == S_PACK) { lay_in(); caption(); return; }
    if ((st == S_OPEN && G.out) || (st == S_CLOSED && !BX.first)) { bx_done(); return; }
}
static void back_btn(int a) { (void)a; back(); }
static void draw2d(void)
{
    float u = BX_C.u, top = BX_C.sy + 1.4f * u;
    if (!(st == S_CLOSED && BX.first) && !(st == S_OPEN && BX.first)) bx_button(BX_C.sx + 2 * u, top, st >= S_DESK ? "< Back" : "< Back to the game", back_btn, 0, -1);
    bx_text(BX_C.sx + BX_C.sw - 2 * u, top + 2.0f * u, 1.5f * u, 0xb5a58f, 255, BX.title, 0, 1);
    if (st == S_POSTER) bx_button(BX_C.sx + 11 * u, top, post_flip ? "Flip to the front" : "Flip over", poster_flip, 0, -1);
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
    if (node == caseG) { if (st == S_CLOSED) open_case(0); else if (st == S_OPEN && !laid) { lay_out(); caption(); } return; }
    if (st == S_OPEN) {
        int in = node == cmanual || node == cart;
        for (int i = 0; i < NPK; i++) if (node == pk[i]) in = 1;
        if (in) { lay_out(); caption(); return; }
    }
    if (node == cmanual) { open_manual(); return; }
    if (st == S_POSTER) { if (!post_busy && r3_is_under(BX_S, node, ppiv)) poster_look(); return; }
    if (st == S_CARDS) {
        if (hc_busy) return;
        if (node == pk[3]) { pk_read(3); return; }
        for (int k = 0; k < NHC; k++) if (node == hc[k]) { card_read(k); return; }
        return;
    }
    for (int i = 0; i < NPK; i++) if (st == S_PACK && node == pk[i]) { if (i == 2) poster_open(); else if (i == 3) pack_open(); else pk_read(i); return; }
    if (node == cart) {
        if (st == S_PACK && !G.out) bx_fade(to_desk, 0);
        else if (st == S_DESK) insert_cart();
        else bx_say("The cartridge is already in the system. Back returns to the game.");
        return;
    }
    if (node == nslot) { insert_cart(); return; }
    if (node == tvKnob || (tvRemote >= 0 && node == tvRemote)) { tv_power(); return; }
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
    if (st >= S_PACK) {                                     /* laid out: the item under the pointer turns in the hand */
        int ok = 0;
        if (node < 0) return;
        if (st == S_PACK) for (int i = 0; i < NIT; i++) if (it_saved[i] && it_node[i] == node) {
            ok = 1;
            r3_node *m = bx_n(node);
            if (i == NPK && fabsf(m->rot[0]) > 2) bx_set3(m->rot, 0, PI_F, 0);      /* the manual's half turn, spelled about y */
        }
        if (st == S_POSTER && node == ppiv && !post_busy) ok = 1;
        if (st == S_CARDS && !hc_busy) { if (node == pk[3]) ok = 1; for (int k = 0; k < NHC; k++) if (node == hc[k]) ok = 1; }
        if (!ok) return;
        r3_node *m = bx_n(node);
        m->rot[1] += dx * .012f; m->rot[0] += dy * .012f;
        if (m->rot[0] > 1.3f) m->rot[0] = 1.3f;
        if (m->rot[0] < -1.3f) m->rot[0] = -1.3f;
        return;
    }
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
