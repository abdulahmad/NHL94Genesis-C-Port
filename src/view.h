/* The wide view (RULE_VIEW): the rink 320 pixels wide (40 columns) instead of the cartridge's
 * 256. What the game places for 256 (plane A's overlays and scoreboard, the sprites) moves
 * VIEW_SHIFT to the right, to the middle; the rink, 384 pixels wide, shows all but 64 of them,
 * so the camera moves VIEW_HPOS either way instead of $3C; and players count as off screen
 * VIEW_SHIFT further out. The pause screen (the window plane, which cannot scroll) is 256 wide as
 * on the cartridge, as are the screens away from the rink. A rule, not a setting of the window:
 * the computer's players and a pad's goalie go by what is on screen. */
#pragma once
#include "game.h"
#include "rules.h"
#include "symbols_gen.h"

#define VIEW_SHIFT 32
#define VIEW_HPOS 0x20

static inline int view_wide(void) { return rules[RULE_VIEW] != 0; }
/* the extra room each side, for what counts as on screen */
static inline int16_t view_more(void) { return view_wide() ? VIEW_SHIFT : 0; }
/* the camera's reach either way (Hpos) */
static inline int16_t view_hpos(void) { return view_wide() ? VIEW_HPOS : 0x3C; }
/* the shift of what is placed for 256: while the rink is up and not paused */
static inline int16_t view_shift(void)
{
    return view_wide() && (g.disflags & HI(df32c)) && g.VBint == SYM_VBlank && !(g.sflags & HI(sfpz)) ? VIEW_SHIFT : 0;
}
/* the clock box and the score box (plane A's rows VIEW_BOX_ROW on, at the left and the right of the
 * 256 the game places them for) keep their gaps to the screen's edges in the wide view: VIEW_SHIFT
 * further out each. Those rows of plane A scroll by VIEW_BOX_SCROLL instead of VIEW_SHIFT (the
 * scroll is by 8 lines in the wide view), and column x of them (64 columns) is drawn at the
 * column this gives: the left third at the screen's left edge, the right third at its right edge,
 * between them as placed. No run of a row wraps. */
#define VIEW_BOX_ROW 22
#define VIEW_BOX_SCROLL 96
static inline int view_boxes(void) { return view_wide() && (g.disflags & HI(df32c)) && !(g.sflags & HI(sfhor)); }
static inline uint16_t view_box_col(uint16_t x, uint16_t y)
{
    if (!view_boxes() || y < VIEW_BOX_ROW || y > 27 || x >= 32) return x;
    if (x < 12) return (uint16_t)(x + 52);
    if (x >= 20) return (uint16_t)(x - 4);
    return (uint16_t)(x - 8);
}
void view_rows_scroll(void);    /* plane A's scroll of each 8 lines, for the rink as it is now */
/* the rink's screen mode and plane A's scroll, after a setVram or a screen over the rink */
void view_rink_mode(void);
void view_rink_resume(void);    /* (a snapshot's rink, set up in 32 columns) */
