/* Widescreen (the Widescreen setting, 16:9): each frame of the game made wider, never stretched.
 * The rink in the wide view (rules.view) is already 320; the screens drawn in 256 columns get 32
 * pixels a side (320), those in 320 get 40 a side (400). The middle is the game's own picture; the
 * sides come from the VDP's planes and sprites past the screen's edges, with the screens'
 * backdrops carried on (native/wide.c). A frame is 5 / 4 as wide as the 4:3 picture whatever its
 * width (ob_plat_hooks.wide). Nothing here writes to the game or the VDP. */
#pragma once
#include <stdint.h>
#include "mdrt/vdp.h"

#define WIDE_MAX_W 400

/* one line M pixels a side past the screen, as colour indices (0 the backdrop): everything, plane B
 * alone, or plane B and the sprites; and an index's colour */
enum { WIDE_ALL, WIDE_B, WIDE_BS, WIDE_A, WIDE_S };   /* (WIDE_A: plane A and the window alone, WIDE_S the sprites alone: colour 0 where they show nothing) */
void wide_line(const mdrt_vdp *v, int line, int M, int what, uint8_t *idx);
uint32_t wide_rgb(const mdrt_vdp *v, uint8_t c);
/* one line of the screen as wide_line (WIDE_ALL, no sides), and where each pixel came from: its plane
 * (WIDE_FROM_*, 0 a sprite or the backdrop) and its place in it */
enum { WIDE_FROM_A = 1, WIDE_FROM_B, WIDE_FROM_WINDOW };
#define WIDE_AT(plane, x, y) ((uint32_t)(plane) << 20 | (uint32_t)((y) & 1023) << 10 | (uint32_t)((x) & 1023))
#define WIDE_AT_PLANE(a) ((int)((a) >> 20))
#define WIDE_AT_X(a) ((int)((a) & 1023))
#define WIDE_AT_Y(a) ((int)(((a) >> 10) & 1023))
void wide_line_at(const mdrt_vdp *v, int line, uint8_t *idx, uint32_t *at);

/* the frame pic (w x h, ARGB, rows pitch apart) made wide into out (rows opitch apart) -> its width.
 * sega_cd: pic is a Sega CD screen drawn by the port (segacd_present), not the VDP's */
int wide_frame(const mdrt_vdp *v, const uint32_t *pic, int w, int h, int pitch, uint32_t *out, int opitch, int sega_cd);

/* a w-wide frame is the wide-view rink (rules.view) */
int wide_rink_frame(int w);
int wide_hor_frame(int w);       /* the wide view's horizontal rink (the penalty screen): 256 in the middle of 320 */
/* the wide rink's picture (320 x h) for a 4:3 screen: its middle 256, panning with the puck, plane A's
 * overlays and boxes where the cartridge has them -> 256 */
void wide_crop_follow(void);   /* (each frame the rink is up: the crop's camera) */
int wide_crop(const mdrt_vdp *v, const uint32_t *pic, int h, int pitch, uint32_t *out, int opitch);

/* the 16:9 rink's overlay windows (the face off window, the referee's, the goal box...) each 32 pixels out to the side of
 * the 256 it is nearer, keeping its 4:3 gap: the VDP to draw the picture from (v itself when nothing moves), and the lines
 * from <= y < to that differ from v's. Only the picture: the game is not changed */
const mdrt_vdp *wide_windows(const mdrt_vdp *v, int *from, int *to);
