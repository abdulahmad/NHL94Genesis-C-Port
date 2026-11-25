/* The Sega CD's career pages (the Game page's "Career stats"): B on a player's card opens his career from the
 * disc, the bio and a row a season, regular season and playoffs, on an Overbar full-window screen while the game
 * waits. Any key, button or click closes it. The data is native/segacd_photos.c's. */
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include "overbar/screen.h"
#include "overbar/vfont.h"
#include "segacd.h"

#define BG 0xFF0A1A4Au                     /* (the card screen's blue, darker) */
#define INK 0xFFFFFF
#define DIM 0x9FB4E0
#define GOLD 0xFFD25A

static struct {
    segacd_career c;
    SDL_atomic_t state;                     /* 1 asked for, 2 up, 0 none */
    SDL_atomic_t close;
} C;

static void fill(uint32_t *px, int w, int h, int pitch, uint32_t c)
{
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) px[(long)y * pitch + x] = c;
}

/* where the playoffs' columns start: the column names' second GP */
static int playoffs_from(const segacd_career *c)
{
    if (!c->nrows) return 0;
    for (int i = 4; i < c->ncells[0]; i++) if (!strcmp(c->cell[0][i], "GP")) return i;
    return c->ncells[0];
}

static void draw(void *ud, uint32_t *px, int w, int h, int pitch)
{
    (void)ud;
    const segacd_career *c = &C.c;
    fill(px, w, h, pitch, BG);
    int cols = 0;
    for (int r = 0; r < c->nrows; r++) if (c->ncells[r] > cols) cols = c->ncells[r];
    /* the size: every line in the height, the table in the width (measured at 100 px, then scaled) */
    float colw[SEGACD_COLS] = { 0 }, gap = 0.9f, total = 0;
    for (int r = 0; r < c->nrows; r++)
        for (int i = 0; i < c->ncells[r]; i++) {
            float cw = ob_vf_width(r ? OB_VF_REGULAR : OB_VF_SEMIBOLD, 100, c->cell[r][i], 0) / 100;
            if (cw > colw[i]) colw[i] = cw;
        }
    for (int i = 0; i < cols; i++) total += colw[i] + gap;
    int lines = c->nbio + 3 + c->nrows;
    float size = h / (lines * 1.45f + 2.5f);
    if (total * size > w * 0.92f) size = w * 0.92f / total;
    float bio_size = size * 1.1f, line = size * 1.45f, x0 = (w - total * size) / 2, y = size * 2.2f;
    if (x0 < size) x0 = size;
    for (int i = 0; i < c->nbio; i++, y += line * (i == 1 ? 1.25f : 1))   /* the name, larger, then the rest */
        ob_vf_draw(px, w, h, pitch, x0, y, i ? OB_VF_REGULAR : OB_VF_SEMIBOLD, i ? bio_size : bio_size * 1.3f, i ? DIM : INK, 255, c->bio[i], 0, 0);
    y += line * 0.6f;
    float xs[SEGACD_COLS + 1];
    xs[0] = x0;
    for (int i = 0; i < cols; i++) xs[i + 1] = xs[i] + (colw[i] + gap) * size;
    int split = playoffs_from(c);
    if (c->heading[0][0] && split > 3) {                                   /* REGULAR SEASON over its columns, PLAYOFFS over theirs */
        float a = xs[3], b = xs[split] - gap * size;
        float tw = ob_vf_width(OB_VF_SEMIBOLD, size, c->heading[0], 0);
        ob_vf_draw(px, w, h, pitch, a + (b - a - tw) / 2, y, OB_VF_SEMIBOLD, size, GOLD, 255, c->heading[0], 0, 0);
        if (split < cols) {
            a = xs[split]; b = xs[cols] - gap * size;
            tw = ob_vf_width(OB_VF_SEMIBOLD, size, c->heading[1], 0);
            ob_vf_draw(px, w, h, pitch, a + (b - a - tw) / 2, y, OB_VF_SEMIBOLD, size, GOLD, 255, c->heading[1], 0, 0);
        }
        y += line;
    }
    for (int r = 0; r < c->nrows; r++, y += line) {
        for (int i = 0; i < c->ncells[r]; i++) {
            float tw = ob_vf_width(r ? OB_VF_REGULAR : OB_VF_SEMIBOLD, size, c->cell[r][i], 0);
            float x = i < 3 ? xs[i] : xs[i] + colw[i] * size - tw;            /* (season, club and league to the left; numbers to the right) */
            ob_vf_draw(px, w, h, pitch, x, y, r ? OB_VF_REGULAR : OB_VF_SEMIBOLD, size, r ? INK : GOLD, 255, c->cell[r][i], 0, 0);
        }
        if (!r) {                                                       /* a rule under the column names */
            int ry = (int)(y + size * 0.45f);
            for (int x = (int)x0; x < (int)xs[cols] && x < w; x++) if (ry >= 0 && ry < h) px[(long)ry * pitch + x] = 0xFF3A5AA0u;
            y += size * 0.3f;
        }
    }
}

static void input(void *ud, const ob_screen_ev *e)
{
    (void)ud;
    if (e->kind == OB_SCREEN_OK || e->kind == OB_SCREEN_BACK || e->kind == OB_SCREEN_PRESS) SDL_AtomicSet(&C.close, 1);
}

/* (the game's thread, B on a card) his career, the game waiting until it is closed; 0 when the disc has none */
int segacd_career_show(ob_cd *cd, const char *abbr, const char *name, int goalie, int (*ending)(void))
{
    if (SDL_AtomicGet(&C.state) || !segacd_career_get(cd, abbr, name, goalie, &C.c)) return 0;
    SDL_AtomicSet(&C.close, 0);
    SDL_AtomicSet(&C.state, 1);
    while (SDL_AtomicGet(&C.state) && !(ending && ending())) SDL_Delay(5);
    return 1;
}

/* (the window's loop) puts it up, and takes it down once closed */
void segacd_career_tick(void)
{
    int st = SDL_AtomicGet(&C.state);
    if (st == 1) {
        ob_screen s = { draw, input, NULL };
        ob_screen_show(&s);
        SDL_AtomicSet(&C.state, 2);
    } else if (st == 2 && SDL_AtomicGet(&C.close)) {
        ob_screen_show(NULL);
        SDL_AtomicSet(&C.state, 0);
    }
}
