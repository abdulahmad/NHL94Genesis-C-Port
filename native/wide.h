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
enum { WIDE_ALL, WIDE_B, WIDE_BS };
void wide_line(const mdrt_vdp *v, int line, int M, int what, uint8_t *idx);
uint32_t wide_rgb(const mdrt_vdp *v, uint8_t c);

/* the frame pic (w x h, ARGB, rows pitch apart) made wide into out (rows opitch apart) -> its width.
 * sega_cd: pic is a Sega CD screen drawn by the port (segacd_present), not the VDP's */
int wide_frame(const mdrt_vdp *v, const uint32_t *pic, int w, int h, int pitch, uint32_t *out, int opitch, int sega_cd);
