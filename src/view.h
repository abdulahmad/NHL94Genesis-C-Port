/* The wide view (RULE_VIEW): the rink 320 pixels wide (40 columns) instead of the cartridge's
 * 256. What the game places for 256 (plane A's overlays and scoreboard, the sprites) moves
 * VIEW_SHIFT to the right, to the middle; the rink, 384 pixels wide, shows all but 64 of them,
 * so the picture's camera (view_cam) goes VIEW_HPOS either way instead of $3C. The pause screen
 * (the window plane, which cannot scroll) is 256 wide as on the cartridge, as are the screens away
 * from the rink.
 *
 * RULE_VIEW 1 (what 16:9 Widescreen gives) is the picture alone: the game plays by the cartridge's
 * 256, its camera (Hpos) going $3C either way and its players off screen where the cartridge's
 * are (a pad's goalie taken as on the cartridge); only the picture is wide. RULE_VIEW 2 ("wide
 * play", an option of its own) also plays by the 320: the camera goes VIEW_HPOS, and players
 * count as off screen VIEW_SHIFT further out. */
#pragma once
#include "game.h"
#include "rules.h"
#include "symbols_gen.h"

#define VIEW_SHIFT 32
#define VIEW_HPOS 0x20

/* the picture is the wide view's (RULE_VIEW 1 or 2) */
static inline int view_wide(void) { return RULE(RULE_VIEW) != 0; }
/* the game plays by the wide view (RULE_VIEW 2): its camera's reach and what counts as on screen */
static inline int view_play_wide(void) { return RULE(RULE_VIEW) == 2; }
/* the picture's extra room each side, for what is drawn (sprites, the crowd, the arrows) */
static inline int16_t view_more(void) { return view_wide() ? VIEW_SHIFT : 0; }
/* the game's extra room each side, for what counts as on screen in play (a pad's goalie) */
static inline int16_t view_play_more(void) { return view_play_wide() ? VIEW_SHIFT : 0; }
/* the game's camera's reach either way (Hpos): the cartridge's $3C unless the game plays by the wide view */
static inline int16_t view_hpos(void) { return view_play_wide() ? VIEW_HPOS : 0x3C; }
/* the picture's camera: the game's Hpos, kept within the wide picture's reach (VIEW_HPOS) in the wide view.
 * What is drawn on the rink goes by this; what is played, by Hpos */
static inline int16_t view_cam(void)
{
    int16_t h = (int16_t)g.Hpos;
    if (!view_wide()) return h;
    return h > VIEW_HPOS ? VIEW_HPOS : h < -VIEW_HPOS ? (int16_t)-VIEW_HPOS : h;
}
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
/* the zamboni's crossing (in quarter pixels) VIEW_SHIFT further out each way in the wide view, so that a
 * 16:9 picture's sides (native/wide.c: the pause screen's 32 pixels a side) see it come in and go out whole */
static inline uint16_t view_zam_more(void) { return view_wide() ? VIEW_SHIFT * 4 : 0; }
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
/* Widescreen (native/wide.c): the full-width pictures dobitmap drew (the screens' backdrops), by
 * screen row, so the sides can carry them on. Not game state: only the picture reads it */
struct view_row { uint32_t map; uint16_t maprow, start, attr, vram, colshift; };
extern struct view_row view_rows[32];
void view_note_rows(uint32_t map, uint16_t maprow, uint16_t rows, uint16_t start, uint16_t attr);    /* plane A's scroll of each 8 lines, for the rink as it is now */
/* the rink's screen mode and plane A's scroll, after a setVram or a screen over the rink */
void view_rink_mode(void);
void view_rink_resume(void);    /* (a snapshot's rink, set up in 32 columns) */
