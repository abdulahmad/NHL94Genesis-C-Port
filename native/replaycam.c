/* The mouse's camera moves in the instant replay (replaymouse.h): made on the game's thread at the replay's pad
 * read (getpzjoy, each pass of ReplayMode; lockstep.c's native_replay_pass_hook), with the state ReplayMode's own
 * d-pad sets. Never in an online session: these are not pad input, so the other machines would not make them.
 *
 *   follow   (a double click on a player) as the d-pad's .dir does when it finds one: sfscrl and sflags3 bit $20
 *            set, the camera's Yfrac the player's SCnum, setpads(4) (PadControlBits' top nibble), UpdateCameraPos
 *   free     (a double click on the ice: a glide there; the right drag) as its .scrl: sfscrl set, $20 clear, the
 *            camera's Zpos 0, PadControlBits' top nibble $E, the camera object at the place, UpdateCameraPos
 *
 * The click is in picture pixels from the picture's middle. The picture's middle is the view's (Hpos, Vpos): the
 * sprites are at Xpos - Hpos + 0x100 and 0xF0 - (Ypos - Vpos) (find3d), 128 and 112 on the screen; the 16:9
 * window's sides, or the wide view's view_shift, keep the 256 rink in the middle of the picture either way. */
#include <stdio.h>
#include <stdlib.h>
#include "game.h"
#include "native.h"
#include "replaymouse.h"

#define PICK_R 16           /* a double click this near a player (picture pixels) is on him */
#define BODY_UP 10          /* his middle: this far up from his place (his feet) */
#define GLIDE_DIV 6         /* a glide closes 1/6 of the way a pass */

static struct { int on, x, y; unsigned entries; } glide;

/* replay94.asm:UpdateCameraPos, here (the game's routines are instrumented: not called from the hook) */
static void camera_to(int16_t x, int16_t y, struct RinkObj *cam)
{
    if (g.sflags4 & HI(7)) { x = (int16_t)-x; y = (int16_t)-y; }
    cam->Xpos = x;
    cam->Ypos = y;
    if (!(x < 0x3C)) x = 0x3C;
    if (!(x > -0x3C)) x = -0x3C;
    g.Hpos = (uint16_t)x;
    if (!(y < 0x100)) y = 0x100;
    if (!(y > -0xC8)) y = -0xC8;
    g.Vpos = (uint16_t)y;
}

/* the free camera from what shows now */
static void go_free(struct RinkObj *cam)
{
    if (!(g.sflags & HI(sfscrl)) || (g.sflags3 & 0x20)) { cam->Xpos = (int16_t)g.Hpos; cam->Ypos = (int16_t)g.Vpos; }
    g.sflags |= HI(sfscrl);
    g.sflags3 &= (uint8_t)~0x20;
    cam->Zpos = 0;
    g.PadControlBits = (uint16_t)((g.PadControlBits & 0xFFF) | 0xE000);
}

/* where the free camera may be: as far as the view goes (no travel with nothing moving) */
static int16_t clampx(int v) { return (int16_t)(v > 0x3C ? 0x3C : v < -0x3C ? -0x3C : v); }
static int16_t clampy(int v) { return (int16_t)(v > 0x100 ? 0x100 : v < -0xC8 ? -0xC8 : v); }

void replaycam_pass(unsigned entries)
{
    struct RinkObj *cam = &g.pads[3];
    rm_cam c;
    if (entries != glide.entries) { glide.entries = entries; glide.on = 0; }
    int got = rm_cam_take(&c);
    if (native_net_on()) { glide.on = 0; return; }      /* (an online session: none, see above) */
    if (glide.on && (!(g.sflags & HI(sfscrl)) || (g.sflags3 & 0x20))) glide.on = 0;   /* (the d-pad, or B+A, took the camera) */
    if (got && c.pick) {
        int rx = (int16_t)g.Hpos + c.x, ry = (int16_t)g.Vpos - c.y, best = PICK_R * PICK_R + 1, at = -1;
        for (int i = 0; i < 12; i++) {                  /* the players, as the d-pad's search */
            const struct SortObj *o = sortobj(i);
            if ((int16_t)o->frame <= 0) continue;
            int dx = o->Xpos - rx, dy = o->Ypos + BODY_UP - ry, d = dx * dx + dy * dy;
            if (d < best) { best = d; at = i; }
        }
        glide.on = 0;
        if (at >= 0) {
            struct SortObj *o = sortobj(at);
            g.sflags |= HI(sfscrl);
            g.sflags3 |= 0x20;
            cam->Yfrac = (uint16_t)o->SCnum;
            g.PadControlBits = (uint16_t)((g.PadControlBits & 0x0FFF) | (uint16_t)((uint16_t)o->SCnum << 12));   /* setpads(4, o) */
            camera_to(o->Xpos, o->Ypos, cam);
        } else {
            go_free(cam);
            glide.on = 1; glide.x = clampx(rx); glide.y = clampy(ry);
        }
    }
    if (got && (c.pan_x || c.pan_y)) {                 /* the ice moves with the pointer: the view the other way */
        glide.on = 0;
        go_free(cam);
        camera_to(clampx(cam->Xpos - c.pan_x), clampy(cam->Ypos + c.pan_y), cam);
    }
    if (glide.on) {
        int dx = glide.x - cam->Xpos, dy = glide.y - cam->Ypos;
        int sx = dx / GLIDE_DIV, sy = dy / GLIDE_DIV;
        if (!sx && dx) sx = dx > 0 ? 1 : -1;
        if (!sy && dy) sy = dy > 0 ? 1 : -1;
        camera_to((int16_t)(cam->Xpos + sx), (int16_t)(cam->Ypos + sy), cam);
        if (!dx && !dy) glide.on = 0;
    }
}

/* (tests: NHL94_TEST_MOUSE) the camera, and which recorded frame shows: frames back from the record point, found by its
 * objects (-1: none matched, the reverse angle's included) */
int replaycam_state(char *line, int size)
{
    const uint32_t first = RAM_BASE & 0xFFFFFF, end = 0xFFAF54, step = 0x62;
    long back = -1;
    uint32_t now[16];
    for (int i = 0; i < 16; i++) {                     /* packed as updatereplay packs them */
        const struct SortObj *o = sortobj(i);
        uint32_t l = (uint32_t)(o->Xpos & 0x3FF);
        l |= (uint32_t)(uint16_t)((uint16_t)o->Ypos << 6) << 4;
        uint16_t hi = (uint16_t)(l >> 16);
        hi |= (uint16_t)(o->frame << 4);
        hi |= (uint16_t)((o->attribute & 0x1800) << 3);
        now[i] = ((uint32_t)hi << 16) | (l & 0xFFFF);
    }
    uint32_t rec = g.recbpr & 0xFFFFFF;
    long n = (long)((end - first) / step);
    long matches = 0;
    for (long k = 0; k < n; k++) {                    /* (nearest the record point first) */
        long idx = ((long)((rec - first) / step) - k + n) % n;
        const struct ReplayFrame *f = RAM_AT(struct ReplayFrame, ram_at(first + (uint32_t)idx * step));
        int same = 1;
        for (int i = 0; i < 16 && same; i++) same = f->obj[i] == now[i];
        if (same && back < 0) back = k;
        matches += same;
    }
    return snprintf(line, (size_t)size, "banner %d play %d follow %d target %d free %d cam %d,%d view %d,%d back %ld (of %ld alike) rev %d",
                    (g.sflags5 & 1) != 0, (g.sflags3 >> sf3rmplay) & 1, (g.sflags3 & 0x20) != 0, (int16_t)g.pads[3].Yfrac & 0xFF,
                    (g.sflags & HI(sfscrl)) != 0 && !(g.sflags3 & 0x20), g.pads[3].Xpos, g.pads[3].Ypos, (int16_t)g.Hpos, (int16_t)g.Vpos, back, matches,
                    (g.sflags4 & HI(4)) != 0);
}

/* (tests) player i's middle (where a double click picks him): picture pixels from the picture's middle */
void replaycam_player_at(int i, int *x, int *y)
{
    const struct SortObj *o = sortobj(i);
    *x = o->Xpos - (int16_t)g.Hpos;
    *y = -(o->Ypos + BODY_UP - (int16_t)g.Vpos);
}
