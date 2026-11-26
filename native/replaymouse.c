/* The mouse in the instant replay: gestures to the pad's buttons and to camera moves (replaymouse.h). */
#include <stdatomic.h>
#include <stdlib.h>
#include "replaymouse.h"

static rm_bits bits;
void rm_init(rm_bits b) { bits = b; }

/* ---- shared between the threads: the taps (one writer, one reader), the scrub, the camera moves ---- */
#define RING 32
static uint8_t ring[RING];
static atomic_uint head, tail;                         /* head: the window's thread writes; tail: the game's */
static atomic_int scrub;                               /* picture pixels the left drag is from its start (0: none) */
static atomic_int pan_x, pan_y;                        /* the right drag's travel not yet taken */
static atomic_uint pick_seq;                           /* a new double click */
static atomic_int pick_x, pick_y;

static void tap(int code)
{
    unsigned h = atomic_load(&head);
    if (h - atomic_load(&tail) >= RING) return;        /* (full: a wheel spun far ahead of the replay) */
    ring[h % RING] = (uint8_t)code;
    atomic_store(&head, h + 1);
}

/* ---- the window's thread: the gestures ---- */
static struct {
    int ldown, ldouble, dragging, x0, y0;              /* the left button: held, the second press of a double click, a scrub */
    int rdown, rx, ry;                                 /* the right button: held, the pointer when last seen */
    uint32_t pending;                                  /* a single click's C waits till then (0: none) */
    int cx, cy;                                        /* where that click was */
} G;

void rm_reset(void)
{
    G.ldown = G.ldouble = G.dragging = G.rdown = 0;
    G.pending = 0;
    atomic_store(&scrub, 0);
    atomic_store(&pan_x, 0);
    atomic_store(&pan_y, 0);
}

void rm_down(int button, int x, int y, uint32_t ms, int banner, int direct)
{
    if (button == RM_X1) { tap(RM_TAP_START); return; }   /* the banner, and on it: resume the game */
    if (banner) {
        G.pending = 0;
        if (button == RM_LEFT) tap(RM_TAP_C);          /* the reverse angle */
        else if (button == RM_RIGHT) tap(RM_TAP_B);    /* leave the replay */
        return;
    }
    if (button == RM_LEFT) {
        if (G.pending && abs(x - G.cx) <= RM_DRAG_PX * 2 && abs(y - G.cy) <= RM_DRAG_PX * 2) {
            G.pending = 0;                             /* a double click: no C, the camera instead */
            G.ldouble = 1;
            if (direct) {
                atomic_store(&pick_x, x);
                atomic_store(&pick_y, y);
                atomic_fetch_add(&pick_seq, 1);
            }
        } else {
            if (G.pending) { G.pending = 0; tap(RM_TAP_C); }   /* (the click before, elsewhere: its own) */
            G.ldouble = 0;
        }
        G.ldown = 1; G.dragging = 0; G.x0 = x; G.y0 = y;
        (void)ms;
    } else if (button == RM_RIGHT) {
        G.rdown = direct; G.rx = x; G.ry = y;
    }
}

void rm_up(int button, int x, int y, uint32_t ms)
{
    if (button == RM_LEFT && G.ldown) {
        G.ldown = 0;
        if (G.dragging) { G.dragging = 0; atomic_store(&scrub, 0); }
        else if (!G.ldouble) { G.pending = ms + RM_DOUBLE_MS; if (!G.pending) G.pending = 1; G.cx = x; G.cy = y; }
        G.ldouble = 0;
    } else if (button == RM_RIGHT) G.rdown = 0;
}

void rm_motion(int x, int y, uint32_t ms, int banner, int direct)
{
    (void)ms;
    if (banner) { G.dragging = 0; G.rdown = 0; atomic_store(&scrub, 0); return; }
    if (G.ldown && !G.ldouble) {
        int dx = x - G.x0;
        if (!G.dragging && abs(dx) > RM_DRAG_PX) G.dragging = 1;
        if (G.dragging) atomic_store(&scrub, dx);
    }
    if (G.rdown && direct) {                           /* the ice moves with the pointer */
        atomic_fetch_add(&pan_x, x - G.rx);
        atomic_fetch_add(&pan_y, y - G.ry);
    }
    G.rx = x; G.ry = y;
}

void rm_wheel(int notches, int banner)
{
    if (banner) return;
    for (int i = 0; i < abs(notches) && i < RING; i++) tap(notches < 0 ? RM_TAP_B : RM_TAP_A);   /* down: on; up: back */
}

void rm_tick(uint32_t ms, int banner)
{
    if (G.pending && (int32_t)(ms - G.pending) >= 0) {
        G.pending = 0;
        if (!banner) tap(RM_TAP_C);
    }
}

int rm_scrub(void) { return atomic_load(&scrub); }

/* ---- the game's thread ---- */
static struct {
    unsigned entries;                                  /* the replay these are for */
    int code, gap;                                     /* the tap being pressed (0: none); its release after */
    unsigned since;                                    /* the passes when it began */
    int acc;                                           /* the scrub's taps: 1/256s of a tap */
} P;

static uint16_t tap_bits(int code)
{
    switch (code) {
    case RM_TAP_C: return bits.c;
    case RM_TAP_B: return bits.b;
    case RM_TAP_A: return bits.a;
    case RM_TAP_START: return bits.start;
    default: return 0;
    }
}

uint16_t rm_pad(unsigned entries, unsigned passes)
{
    if (entries != P.entries) {                        /* a new replay: what was left of the last one is dropped */
        P.entries = entries;
        P.code = P.gap = 0;
        P.acc = 0;
        atomic_store(&tail, atomic_load(&head));
    }
    if (P.code) {
        if (passes == P.since) return P.gap ? 0 : tap_bits(P.code);   /* (no pass has read it yet) */
        if (!P.gap) { P.gap = 1; P.since = passes; return 0; }
        P.code = P.gap = 0;                            /* let go, and a pass saw that */
    }
    int s = atomic_load(&scrub), d = abs(s) - RM_SCRUB_DEAD;
    uint16_t sb = s > 0 ? bits.b : bits.a;
    if (d > 0) {
        int rate = d >= RM_SCRUB_FULL ? 256 : 256 * d / RM_SCRUB_FULL;
        if (rate >= 256) return sb;                    /* full speed: held */
        P.acc += rate;
        if (P.acc >= 256) { P.acc -= 256; P.code = s > 0 ? RM_TAP_B : RM_TAP_A; P.gap = 0; P.since = passes; return sb; }
        return 0;
    }
    P.acc = 0;
    unsigned t = atomic_load(&tail);
    if (t != atomic_load(&head)) {
        P.code = ring[t % RING];
        atomic_store(&tail, t + 1);
        P.gap = 0;
        P.since = passes;
        return tap_bits(P.code);
    }
    return 0;
}

int rm_cam_take(rm_cam *c)
{
    static unsigned seen;
    unsigned s = atomic_load(&pick_seq);
    c->pick = s != seen;
    seen = s;
    c->x = atomic_load(&pick_x);
    c->y = atomic_load(&pick_y);
    c->pan_x = atomic_exchange(&pan_x, 0);
    c->pan_y = atomic_exchange(&pan_y, 0);
    return c->pick || c->pan_x || c->pan_y;
}
