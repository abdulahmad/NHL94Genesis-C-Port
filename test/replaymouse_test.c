/* make replaymousetest: the instant replay's mouse gestures (native/replaymouse.c) without a window. A made-up replay
 * reads the pad once a frame and makes a pass every frame (or none, to show a tap waits for one); each gesture's
 * buttons and camera moves are checked. */
#include <stdio.h>
#include <stdlib.h>
#include "replaymouse.h"

enum { A = 0x40, B = 0x10, C = 0x20, START = 0x80 };
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static unsigned entries = 1, passes;
static uint32_t ms;

/* n frames of the made-up replay: the pad read, then a pass that reads it. Counts the passes that saw each button */
static void frames(int n, int pass, int seen[256])
{
    for (int i = 0; i < n; i++) {
        uint16_t b = rm_pad(entries, passes);
        ms += 16;
        rm_tick(ms, 0);
        if (pass) { passes++; for (int k = 0; k < 8; k++) if (b >> k & 1) seen[1 << k]++; }
    }
}
static void clear(int s[256]) { for (int i = 0; i < 256; i++) s[i] = 0; }

int main(void)
{
    int seen[256];
    rm_cam c;
    rm_init((rm_bits){ .a = A, .b = B, .c = C, .start = START });

    /* moving the mouse alone: nothing */
    clear(seen);
    for (int i = 0; i < 50; i++) { rm_motion(i * 3 - 60, i - 20, ms, 0, 1); frames(1, 1, seen); }
    CHECK(!seen[A] && !seen[B] && !seen[C] && !seen[START], "a move pressed something");
    CHECK(!rm_cam_take(&c), "a move moved the camera");

    /* a left click: one pass of C, after the double click time */
    clear(seen);
    rm_down(RM_LEFT, 0, 0, ms, 0, 1); frames(3, 1, seen); rm_up(RM_LEFT, 0, 0, ms);
    frames(RM_DOUBLE_MS / 16 - 2, 1, seen);
    CHECK(!seen[C], "C before the double click time was over");
    frames(20, 1, seen);
    CHECK(seen[C] == 1, "a click: %d passes saw C, not 1", seen[C]);

    /* a tap waits for a pass: none for 10 frames, then exactly one */
    clear(seen);
    rm_wheel(-1, 0);
    frames(10, 0, seen);
    frames(10, 1, seen);
    CHECK(seen[B] == 1, "a wheel notch down after a wait: %d passes saw B, not 1", seen[B]);

    /* the wheel: down one B pass a notch, up one A pass a notch */
    clear(seen);
    rm_wheel(-3, 0); frames(30, 1, seen);
    CHECK(seen[B] == 3 && !seen[A], "3 notches down: B %d A %d", seen[B], seen[A]);
    clear(seen);
    rm_wheel(2, 0); frames(30, 1, seen);
    CHECK(seen[A] == 2 && !seen[B], "2 notches up: A %d B %d", seen[A], seen[B]);

    /* a double click: no C, a pick there */
    clear(seen);
    rm_down(RM_LEFT, 30, -40, ms, 0, 1); rm_up(RM_LEFT, 30, -40, ms); frames(6, 1, seen);
    rm_down(RM_LEFT, 31, -40, ms, 0, 1); rm_up(RM_LEFT, 31, -40, ms); frames(40, 1, seen);
    CHECK(!seen[C], "a double click pressed C %d times", seen[C]);
    CHECK(rm_cam_take(&c) && c.pick && c.x == 31 && c.y == -40, "a double click: pick %d at %d,%d", c.pick, c.x, c.y);
    CHECK(!rm_cam_take(&c), "the pick twice");
    /* the same online (no camera moves): no pick, no C */
    rm_down(RM_LEFT, 0, 0, ms, 0, 0); rm_up(RM_LEFT, 0, 0, ms); frames(6, 1, seen);
    rm_down(RM_LEFT, 0, 0, ms, 0, 0); rm_up(RM_LEFT, 0, 0, ms); frames(40, 1, seen);
    CHECK(!seen[C] && !rm_cam_take(&c), "a double click online: C %d, camera", seen[C]);
    /* two clicks too slow for a double click: two Cs */
    clear(seen);
    rm_down(RM_LEFT, 0, 0, ms, 0, 1); rm_up(RM_LEFT, 0, 0, ms); frames(30, 1, seen);
    rm_down(RM_LEFT, 0, 0, ms, 0, 1); rm_up(RM_LEFT, 0, 0, ms); frames(30, 1, seen);
    CHECK(seen[C] == 2, "two slow clicks: C %d", seen[C]);

    /* a left drag scrubs: the further, the more passes; past RM_SCRUB_FULL every pass; no C after */
    int rate[4], dist[4] = { RM_SCRUB_DEAD + 12, RM_SCRUB_DEAD + 48, RM_SCRUB_DEAD + RM_SCRUB_FULL, -(RM_SCRUB_DEAD + 48) };
    for (int k = 0; k < 4; k++) {
        clear(seen);
        rm_down(RM_LEFT, 0, 0, ms, 0, 1);
        rm_motion(dist[k], 3, ms, 0, 1);
        CHECK(rm_scrub() == dist[k], "the scrub %d, not %d", rm_scrub(), dist[k]);
        frames(120, 1, seen);
        rm_up(RM_LEFT, dist[k], 3, ms);
        CHECK(rm_scrub() == 0, "the scrub after the button went up");
        rate[k] = dist[k] > 0 ? seen[B] : seen[A];
        frames(40, 1, seen);
        CHECK(!seen[C], "a drag pressed C");
        printf("scrub %+d px: %d of 120 passes\n", dist[k], rate[k]);
    }
    CHECK(rate[0] > 0 && rate[0] < rate[1] && rate[1] < rate[2] && rate[2] == 120, "scrub rates %d %d %d", rate[0], rate[1], rate[2]);
    CHECK(rate[3] == rate[1], "back scrub %d, forward %d", rate[3], rate[1]);
    /* a small wobble is a click, not a drag */
    clear(seen);
    rm_down(RM_LEFT, 0, 0, ms, 0, 1); rm_motion(RM_DRAG_PX, 2, ms, 0, 1); rm_up(RM_LEFT, RM_DRAG_PX, 2, ms); frames(30, 1, seen);
    CHECK(seen[C] == 1 && !seen[B], "a wobbly click: C %d B %d", seen[C], seen[B]);

    /* a right drag: the travel, for the camera; online none */
    rm_down(RM_RIGHT, 10, 10, ms, 0, 1);
    rm_motion(20, 15, ms, 0, 1); rm_motion(35, 5, ms, 0, 1);
    rm_up(RM_RIGHT, 35, 5, ms);
    rm_motion(80, 80, ms, 0, 1);
    CHECK(rm_cam_take(&c) && !c.pick && c.pan_x == 25 && c.pan_y == -5, "a right drag: %d,%d", c.pan_x, c.pan_y);
    rm_down(RM_RIGHT, 10, 10, ms, 0, 0); rm_motion(50, 50, ms, 0, 0); rm_up(RM_RIGHT, 50, 50, ms);
    CHECK(!rm_cam_take(&c), "a right drag online moved the camera");
    clear(seen); frames(30, 1, seen);
    CHECK(!seen[A] && !seen[B] && !seen[C], "a right drag pressed something");

    /* the fourth button: Start. On the banner: Start, right B, left C, at once; the wheel and drags nothing */
    clear(seen);
    rm_down(RM_X1, 0, 0, ms, 0, 1); rm_up(RM_X1, 0, 0, ms); frames(10, 1, seen);
    CHECK(seen[START] == 1, "button 4: Start %d", seen[START]);
    clear(seen);
    rm_down(RM_LEFT, 0, 0, ms, 1, 1); rm_up(RM_LEFT, 0, 0, ms); frames(5, 1, seen);
    CHECK(seen[C] == 1, "the banner's left click: C %d", seen[C]);
    rm_down(RM_RIGHT, 0, 0, ms, 1, 1); rm_up(RM_RIGHT, 0, 0, ms); frames(5, 1, seen);
    CHECK(seen[B] == 1, "the banner's right click: B %d", seen[B]);
    rm_down(RM_X1, 0, 0, ms, 1, 1); rm_up(RM_X1, 0, 0, ms); frames(5, 1, seen);
    CHECK(seen[START] == 1, "the banner's button 4: Start %d", seen[START]);
    clear(seen);
    rm_wheel(-2, 1); rm_down(RM_LEFT, 0, 0, ms, 1, 1); rm_motion(200, 0, ms, 1, 1); frames(30, 1, seen);
    CHECK(seen[C] == 1 && !seen[B] && !seen[A], "the banner's wheel and drag: C %d B %d A %d", seen[C], seen[B], seen[A]);
    rm_up(RM_LEFT, 200, 0, ms);

    /* taps between C and Start are a new press each: a pass without the button between two */
    {
        rm_wheel(-2, 0);
        int prev = 0, edges = 0;
        for (int i = 0; i < 20; i++) { int b = (rm_pad(entries, passes) & B) != 0; edges += b && !prev; prev = b; passes++; }
        CHECK(edges == 2, "two notches: %d presses", edges);
    }

    /* a new replay drops what was left */
    rm_wheel(-5, 0);
    entries++;
    clear(seen); frames(30, 1, seen);
    CHECK(!seen[B], "a new replay got the last one's taps: %d", seen[B]);

    printf(fails ? "replaymouse: %d FAILED\n" : "replaymouse: PASS\n", fails);
    return fails != 0;
}
