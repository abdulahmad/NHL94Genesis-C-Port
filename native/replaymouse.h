/* The mouse in the instant replay (ReplayMode, src/replay.c): its gestures turned into the pausing pad's
 * buttons, and into camera moves for the game's thread (native/replaycam.c).
 *
 *   left click            play / stop (C)
 *   left drag             scrub: right holds B (forward), left A (back), faster the further the drag
 *   wheel                 down: one recorded frame on (a B pass); up: one pass of A (the cartridge's A goes
 *                         back 4 recorded frames a pass: the closest the pad comes to one)
 *   double click          on a player: the camera follows him; on the ice: the camera glides there and stays
 *   right drag            the camera moves with the ice, as if grabbed
 *   fourth button         Start: the banner. On the banner: the fourth button again resumes the game (Start),
 *                         a right click leaves the replay (B), a left click turns the angle round (C)
 *   moving it alone       nothing
 *
 * The buttons reach the game as the mouse's pad (the window's input, an online session's too: the frames'
 * recorded input). Camera moves are not pad input: the game's thread makes them at the replay's pad read
 * (native_replay_pass_hook), so never in an online session (replaymouse_direct 0).
 *
 * The gestures (rm_down ... rm_tick) run on the window's thread; rm_pad and rm_cam_take on the game's.
 * Pure C: test/replaymouse_test.c drives it without a window. */
#pragma once
#include <stdint.h>

enum { RM_LEFT = 1, RM_MIDDLE = 2, RM_RIGHT = 3, RM_X1 = 4 };              /* (SDL's button numbers) */
enum { RM_TAP_C = 1, RM_TAP_B, RM_TAP_A, RM_TAP_START };                  /* the taps the queue holds */
#define RM_DOUBLE_MS 250u        /* a second click within this is a double click; a single click waits this long for it */
#define RM_DRAG_PX 4             /* picture pixels a held button moves before it is a drag */
#define RM_SCRUB_DEAD 6          /* a scrub's dead zone (picture pixels from where it started) */
#define RM_SCRUB_FULL 96         /* a scrub this far past the dead zone holds its button every pass */

/* the port's pad bits (mdrt's) */
typedef struct { uint16_t a, b, c, start; } rm_bits;
void rm_init(rm_bits bits);

/* the window's thread. x, y: picture pixels from the picture's middle (x right, y down). banner: the replay's
 * banner is up. direct: camera moves allowed (no online session) */
void rm_reset(void);                                   /* (the replay began or ended: no gesture in progress) */
void rm_down(int button, int x, int y, uint32_t ms, int banner, int direct);
void rm_up(int button, int x, int y, uint32_t ms);
void rm_motion(int x, int y, uint32_t ms, int banner, int direct);
void rm_wheel(int notches, int banner);                /* notches: SDL's wheel y (up / away from the player is plus) */
void rm_tick(uint32_t ms, int banner);                 /* each frame of the window: a single click's C once its double click time is over */

/* the game's thread, once a frame at the read of the mouse's pad while the replay runs: the buttons to add.
 * entries / passes: ReplayMode's calls and its passes (its getpzjoy calls). A tap is held until a pass has
 * read it, then let go until a pass has read that (so each tap is exactly one pass, and C and Start are a new
 * press each); a scrub below full speed is taps at its rate, at full speed its button held */
uint16_t rm_pad(unsigned entries, unsigned passes);

/* camera moves for the game's thread (replaycam.c): a double click there (1), and the right drag's travel */
typedef struct { int pick, x, y, pan_x, pan_y; } rm_cam;
int rm_cam_take(rm_cam *c);                             /* 1 when there is something (pick and / or a pan) */

/* (tests) the scrub now: picture pixels, plus right */
int rm_scrub(void);
