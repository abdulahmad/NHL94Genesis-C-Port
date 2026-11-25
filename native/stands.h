/* Widescreen's sides of the pause and Start Game screens (native/stands.c) */
#pragma once
#include <stdint.h>
#include "mdrt/vdp.h"
/* the stands of the horizontal rink (IceRinkHor) carried on into the M (32) pixels a side of out,
 * whose middle holds the game's picture -> 1 (0: not that screen) */
int stands_frame(const mdrt_vdp *v, int M, int h, uint32_t *out, int opitch);
