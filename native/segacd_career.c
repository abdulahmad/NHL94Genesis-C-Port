/* The Sega CD's player and team cards (the Game page's "Career stats"): B on a card opens the disc's own card screen,
 * PLAYER.BIN's, on an Overbar full-window screen while the game waits. The player's card: the team's page with the
 * empty photo frame (GRAPH2.BIN's TeamDetailsNoPhoto<T>, GTABLE record 68 + T, its second piece), his photo in it
 * (or the disc's stock one), his name, size, birthplace and birthday, his position and number in the disc's big
 * letters at the foot (PlUnpackNibbles), and his career, 7 seasons and 3 columns at a time with "Regular" or
 * "Playoff" over them (PlDrawCareerStats). The team's card: its page with the arena photo, its abbreviation, name and
 * arena, and its six advantage lines (PlShowPicture). The video card: the team's frame (TeamVideoFrame<T>, the
 * third piece) with the team's clips playing in it, PLAYER.BIN's list of them (PlRecordTable: 13 sectors a team, the
 * disc finds a clip by where it starts) one after another as asked for (PlDecodePicture), their sound on the window's
 * audio device and the movie de-dither's live pass when it is on. Everything at the disc's places on its 40 x 28 screen, in its
 * 8 x 8 font (GTABLE record 64) and its colours (PlSelectPicture), scaled to the window like the intro movie.
 * Up and down scroll the seasons, left and right the columns (the disc's C + left / right); on the video card left
 * and right pick the clip before or after (the disc's B) and up plays it again (its C). Enter, Space or A turns to the
 * next card (the disc keeps them side by side: player, team, video); Esc, Backspace, B or a click closes it.
 * A team the disc doesn't have (a roster set's) gets a plain page with the same words. The data is
 * native/segacd_photos.c's. */
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include "overbar/cdimage.h"
#include "overbar/postfx.h"
#include "overbar/screen.h"
#include "overbar/vfont.h"
#include "segacd.h"

#define SW 320
#define SH 224

static struct {
    segacd_card k;
    ob_cd *cd;
    int disc;                               /* 1 the disc's look (else the plain page) */
    int team;                               /* the disc's team (GTABLE 68 + it) */
    int side;                               /* 0 the player's card, 1 the team's, 2 the video card */
    int row, col;                           /* the first season shown, the stat columns' place in the disc's list */
    uint32_t fb[SW * SH];                   /* the disc's screen, made when something changes */
    SDL_atomic_t dirty;
    segacd_career plain;                    /* the plain page's words */
    SDL_atomic_t state;                     /* 1 asked for, 2 up, 0 none */
    SDL_atomic_t close;
    uint32_t dev;                           /* (the window's audio device, its rate, the movie de-dither: tick's) */
    unsigned rate;
    int dedither;
} C;

/* the video card's clip, played like the intro movie: its pictures kept in time with its sound */
#define CLIP_W 160
#define CLIP_H 112
#define CLIP_AHEAD_MS 120
static struct {
    segacd_film *film;
    int clip;                               /* its place in the team's list */
    const int16_t *pcm;
    long pcm_len;
    double pcm_at;
    uint64_t out;
    uint32_t t0;
    const uint8_t *nidx;
    const uint32_t *ncol;
    double ntime;
    int index;
    uint32_t pic[CLIP_W * CLIP_H];          /* the picture now (black before the first) */
    ob_postfx *pp;
} V;

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)be16(p) << 16 | be16(p + 2); }

static uint32_t rgb(unsigned c)
{
    static const uint8_t l[8] = { 0, 36, 73, 109, 146, 182, 219, 255 };
    return 0xFF000000u | (uint32_t)l[c >> 1 & 7] << 16 | (uint32_t)l[c >> 5 & 7] << 8 | l[c >> 9 & 7];
}

/* ---- the disc's screen: a 40 x 28 grid of 8 x 8 cells, colour 0 of a cell see-through */

static void cell(int cx, int cy, const uint8_t *tile, const uint32_t pal[16], int hf, int vf)
{
    if (cx < 0 || cy < 0 || cx >= SW / 8 || cy >= SH / 8 || !tile) return;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int sx = hf ? 7 - x : x, sy = vf ? 7 - y : y;
            int v = tile[sy * 4 + sx / 2] >> (sx & 1 ? 0 : 4) & 15;
            if (v) C.fb[(cy * 8 + y) * SW + cx * 8 + x] = pal[v];
        }
}

static void blank(int cx, int cy, uint32_t c)
{
    if (cx < 0 || cy < 0 || cx >= SW / 8 || cy >= SH / 8) return;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) C.fb[(cy * 8 + y) * SW + cx * 8 + x] = c;
}

/* a picture of GRAPH2.BIN's ($0808, its tile count, the tiles, 64 colours, width, height, the cells) */
typedef struct { const uint8_t *tiles, *map; unsigned ntiles, w, h; uint16_t cram[64]; } picture;

static int picture_at(const uint8_t *b, long len, picture *p)
{
    if (len < 4 || be16(b) != 0x0808) return 0;
    p->ntiles = be16(b + 2);
    long at = 4 + 32L * p->ntiles;
    if (at + 132 > len) return 0;
    p->tiles = b + 4;
    for (int i = 0; i < 64; i++) p->cram[i] = (uint16_t)be16(b + at + 2 * i);
    p->w = be16(b + at + 128);
    p->h = be16(b + at + 130);
    p->map = b + at + 132;
    return at + 132 + 2L * (long)p->w * (long)p->h <= len;
}

/* the team's page k (0 the arena photo, 1 the empty photo frame) */
static int team_picture(int k, picture *p)
{
    long len;
    const uint8_t *r = segacd_graph2_record(C.cd, 68 + C.team, &len);
    if (!r || len < 12) return 0;
    long off = (long)be32(r + 4 * k);
    return off < len && picture_at(r + off, len - off, p);
}

/* (PrintTextLine) a string at a cell in the disc's font, white (palette 2's colour 1) */
static void text(int cx, int cy, const char *s)
{
    long len;
    const uint8_t *f = segacd_graph2_record(C.cd, 64, &len);
    if (!f || len < 4) return;
    unsigned n = be16(f + 2);
    uint32_t pal[16];
    for (int i = 0; i < 16; i++) pal[i] = rgb(0xEEE);
    for (; *s && cx < SW / 8; s++, cx++) {
        unsigned c = (unsigned char)*s;
        if (c < 0x20 || c - 0x20 >= n || 4 + 32L * (long)(c - 0x20 + 1) > len) continue;
        cell(cx, cy, f + 4 + 32 * (c - 0x20), pal, 0, 0);
    }
}

/* ---- the player's card */

/* PLAYER.BIN's stock photos (PicTilesStillA, a goalie's; PicTilesStillB, a skater's) and its big letters */
#define PIC_STILL_GOALIE 0x40C4
#define PIC_STILL_SKATER 0x68E4
#define PIC_BIG 0x82A4
#define PHOTO_BYTES 0x504

static const char *const skater_heads[] = { "Gp", "G", "A", "Tp", "Pim", "Gp", "G", "A", "Tp", "Pim" };
static const char *const goalie_heads[] = { "Gp", "W", "L", "T", "Mins", "Ga", "So", "Avg", "Gp", "W", "L", "Mins", "Ga", "So", "Avg" };
static const int skater_widths[] = { 3, 3, 3, 3, 4, 3, 3, 3, 3, 4 };                       /* (StatColumnWidthsA) */
static const int goalie_widths[] = { 3, 3, 3, 3, 4, 3, 2, 4, 3, 3, 3, 5, 3, 3, 4 };       /* (StatColumnWidthsB) */
static const int skater_cols[] = { 0, 1, 2, 5, 6, 7 };                                     /* (StatColumnIdsSkaterA) */
static const int goalie_cols[] = { 0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12 };                   /* (StatColumnIdsSkaterB) */

static int ncols(void) { return C.k.goalie ? (int)(sizeof goalie_cols / sizeof *goalie_cols) : (int)(sizeof skater_cols / sizeof *skater_cols); }

/* a field of w + 1 cells (PrintTextLine pads with spaces, which are see-through) */
static int field(int cx, int cy, const char *s, int w)
{
    text(cx, cy, s);
    int n = (int)strlen(s);
    return cx + (n > w + 1 ? n : w + 1);
}

static void player_card(void)
{
    const segacd_career *c = &C.k.career;
    const uint8_t *pb;
    long plen;
    /* palette 2: the photo's colours, then PlSelectPicture's: 0 the backdrop grey, 1 and 2 white, 3 grey, 13 red */
    uint32_t pal2[16];
    uint8_t photo[2 + 36 * 32], colours[32];
    int have = segacd_photo(C.cd, C.k.abbr, C.k.name, photo, colours);
    pb = segacd_player_bin(C.cd, &plen);
    if (!have && pb) {
        long at = C.k.goalie ? PIC_STILL_GOALIE : PIC_STILL_SKATER;
        if (at + PHOTO_BYTES <= plen) {
            memcpy(photo, pb + at + 2, sizeof photo);
            memcpy(colours, pb + at + 4 + 36 * 32 + 32, 32);
            have = 1;
        }
    }
    for (int i = 0; i < 16; i++) pal2[i] = rgb(have ? be16(colours + 2 * i) : 0x444);
    pal2[0] = pal2[3] = rgb(0x444);
    pal2[1] = pal2[2] = rgb(0xEEE);
    pal2[13] = rgb(0x02C);
    if (have) for (int i = 0; i < 36; i++) cell(3 + i % 6, 5 + i / 6, photo + 2 + 32 * i, pal2, 0, 0);
    /* his position and number at the foot, in the disc's big letters (PlUnpackNibbles: cells of the background) */
    if (pb && PIC_BIG + 4 <= plen) {
        picture big;
        unsigned n = be16(pb + PIC_BIG + 2);
        big.tiles = pb + PIC_BIG + 4;
        big.map = pb + PIC_BIG + 4 + 32 * n + 4;
        if (big.map + 2 * 40 * 6 - pb <= plen) {
            char pos = c->nbio > 1 ? c->bio[1][0] : 'R';
            int at = pos == 'G' ? 0 : pos == 'C' ? 8 : pos == 'L' ? 16 : pos == 'D' ? 24 : 32;
            struct { int x, w, map; } part[3] = { { 1, 8, 3 * 40 + at }, { 32, 3, 3 * (C.k.number >> 4 & 15) }, { 34, 3, 3 * (C.k.number & 15) } };
            for (int k = 0; k < 3; k++)
                for (int y = 0; y < 3; y++)
                    for (int x = 0; x < part[k].w; x++) {
                        unsigned e = be16(big.map + 2 * (part[k].map + y * 40 + x)), t = e & 0x7FF;
                        blank(part[k].x + x, 24 + y, rgb(0x444));
                        if (t < n) cell(part[k].x + x, 24 + y, big.tiles + 32 * t, pal2, e >> 11 & 1, e >> 12 & 1);
                    }
        }
    }
    /* his name and bio (PlDrawPicture, PlDrawCareerStats): the size line without its position, the birthday's month
     * cut to three letters; the draft line isn't shown */
    text(11, 5, C.k.name);
    if (c->nbio > 1) text(11, 8, c->bio[1][0] && c->bio[1][1] == ' ' ? c->bio[1] + 2 : c->bio[1]);
    if (c->nbio > 2) text(11, 11, c->bio[2]);
    if (c->nbio > 3) {
        char d[SEGACD_CELL];
        const char *sp = strchr(c->bio[3], ' ');
        snprintf(d, sizeof d, "%.3s %s", c->bio[3], sp ? sp + 1 : "");
        text(11, 12, d);
    }
    if (c->nrows < 2) return;
    /* the table: Season (8 cells), Club (the longest, at least 10), Lea (6), then 3 stat columns */
    int clubw = 10;
    for (int r = 1; r < c->nrows; r++) if (c->ncells[r] > 1 && (int)strlen(c->cell[r][1]) > clubw) clubw = (int)strlen(c->cell[r][1]);
    const int *cols = C.k.goalie ? goalie_cols : skater_cols, *widths = C.k.goalie ? goalie_widths : skater_widths;
    const char *const *heads = C.k.goalie ? goalie_heads : skater_heads;
    int nheads = C.k.goalie ? 15 : 10, first = cols[C.col], split = C.k.goalie ? 5 : 2;
    text(15 + clubw, 13, first <= split ? "Regular" : "Playoff");
    int x = field(1, 14, "Season", 7);
    x = field(x, 14, "Club", clubw - 1);
    x = field(x, 14, "Lea", 5);
    for (int i = first; i < first + 3 && i < nheads; i++) x = field(x, 14, heads[i], widths[i]);
    for (int r = 0; r < 7 && 1 + C.row + r < c->nrows; r++) {
        const int q = 1 + C.row + r, y = 16 + r;
        x = field(1, y, c->cell[q][0], 7);
        x = field(x, y, c->ncells[q] > 1 ? c->cell[q][1] : "", clubw - 1);
        x = field(x, y, c->ncells[q] > 2 ? c->cell[q][2] : "", 5);
        for (int i = first; i < first + 3 && i < nheads; i++) x = field(x, y, 3 + i < c->ncells[q] ? c->cell[q][3 + i] : "", widths[i]);
    }
}

/* ---- the team's card (PlShowPicture with $54 0) */

static void team_card(void)
{
    static const char *const line[6] = { "Power play advantage", "Penalty killing disadvantage", "Home team advantage",
                                         "Road team disadvantage", "Number of forwards", "Number of defensemen" };
    text(14, 5, C.k.abbr);
    text(14, 8, C.k.nick);
    text(14, 11, C.k.arena);
    for (int i = 0; i < 6; i++) {
        char s[64];
        int v = C.k.adv[i];
        if (i < 4) snprintf(s, sizeof s, "%s %d%%", line[i], v * 100 / 2);     /* (BinToBcdCarry) */
        else snprintf(s, sizeof s, "%s %d", line[i], v);                         /* (BinToBcd) */
        text(1, 13 + i, s);
    }
}

static void compose(void)
{
    for (int i = 0; i < SW * SH; i++) C.fb[i] = rgb(0x444);            /* (the backdrop: colour 0 of palette 0) */
    picture p;
    if (C.side == 2)                                                      /* (the clip under the frame's open middle) */
        for (int y = 0; y < CLIP_H; y++) memcpy(C.fb + (72 + y) * SW + 80, V.pic + y * CLIP_W, CLIP_W * 4);
    if (team_picture(C.side == 2 ? 2 : C.side ? 0 : 1, &p)) {
        uint16_t cram[64];
        memcpy(cram, p.cram, sizeof cram);
        cram[32] = cram[35] = 0x444;                                       /* (PlSelectPicture's palette 2) */
        cram[33] = cram[34] = 0xEEE;
        cram[45] = 0x02C;
        uint32_t pal[4][16];
        for (int i = 0; i < 64; i++) pal[i / 16][i % 16] = rgb(cram[i]);
        for (unsigned y = 0; y < p.h && y < SH / 8; y++)
            for (unsigned x = 0; x < p.w && x < SW / 8; x++) {
                unsigned e = be16(p.map + 2 * (y * p.w + x)), t = e & 0x7FF;
                if (t < p.ntiles) cell((int)x, (int)y, p.tiles + 32 * t, pal[e >> 13 & 3], e >> 11 & 1, e >> 12 & 1);
            }
    }
    if (C.side == 1 || (!C.side && !C.k.player)) team_card();
    else if (!C.side) player_card();
}

/* ---- the plain page (a team the disc doesn't have) */

#define BG 0xFF0A1A4Au
#define INK 0xFFFFFF
#define DIM 0x9FB4E0
#define GOLD 0xFFD25A

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

static void draw_plain(uint32_t *px, int w, int h, int pitch)
{
    const segacd_career *c = &C.plain;
    fill(px, w, h, pitch, BG);
    int cols = 0;
    for (int r = 0; r < c->nrows; r++) if (c->ncells[r] > cols) cols = c->ncells[r];
    /* the size: every line in the height, the table in the width (measured at 100 px, then scaled) */
    float colw[SEGACD_COLS] = { 0 }, gap = 0.9f, total = 0;
    for (int r = 0; r < c->nrows; r++)
        for (int i = 0; i < c->ncells[r]; i++) {
            float cw = ob_vf_width(r || c->noheader ? OB_VF_REGULAR : OB_VF_SEMIBOLD, 100, c->cell[r][i], 0) / 100;
            if (cw > colw[i]) colw[i] = cw;
        }
    for (int i = 0; i < cols; i++) total += colw[i] + gap;
    int lines = c->nbio + 3 + c->nrows;
    float size = h / (lines * 1.45f + 2.5f);
    if (size > h / 26.0f) size = h / 26.0f;                              /* (a short page, the team's, not huge) */
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
    if (c->heading[0][0] && split > 3 && !c->noheader) {                   /* REGULAR SEASON over its columns, PLAYOFFS over theirs */
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
            int body = r || c->noheader, left = c->noheader ? i == 0 : i < 3;
            float tw = ob_vf_width(body ? OB_VF_REGULAR : OB_VF_SEMIBOLD, size, c->cell[r][i], 0);
            float x = left ? xs[i] : xs[i] + colw[i] * size - tw;             /* (season, club and league to the left; numbers to the right) */
            ob_vf_draw(px, w, h, pitch, x, y, body ? OB_VF_REGULAR : OB_VF_SEMIBOLD, size, body ? INK : GOLD, 255, c->cell[r][i], 0, 0);
        }
        if (!r && !c->noheader) {                                       /* a rule under the column names */
            int ry = (int)(y + size * 0.45f);
            for (int x = (int)x0; x < (int)xs[cols] && x < w; x++) if (ry >= 0 && ry < h) px[(long)ry * pitch + x] = 0xFF3A5AA0u;
            y += size * 0.3f;
        }
    }
}

/* the plain page's words: the player's name and bio and his whole table, or the team's name and lines */
static void plain_words(void)
{
    segacd_career *pg = &C.plain;
    if (C.k.player) { *pg = C.k.career; return; }
    static const char *const line[6] = { "Power play advantage", "Penalty killing disadvantage", "Home team advantage",
                                         "Road team disadvantage", "Number of forwards", "Number of defensemen" };
    memset(pg, 0, sizeof *pg);
    snprintf(pg->bio[0], sizeof pg->bio[0], "%.30s %.30s", C.k.city, C.k.nick);
    pg->nbio = 1;
    pg->noheader = 1;
    for (int i = 0; i < 6; i++) {
        snprintf(pg->cell[i][0], SEGACD_CELL, "%s", line[i]);
        if (i < 4) snprintf(pg->cell[i][1], SEGACD_CELL, "%d%%", C.k.adv[i] * 100 / 2);
        else snprintf(pg->cell[i][1], SEGACD_CELL, "%d", C.k.adv[i]);
        pg->ncells[i] = 2;
    }
    pg->nrows = 6;
}

/* ---- the video card's clips */

#define PL_CLIPS 31988                      /* (PLAYER.BIN's PlRecordTable: 28 teams of 13 longs, -1 past the last) */

static uint32_t clip_lba(int k)
{
    long len;
    const uint8_t *pb = segacd_player_bin(C.cd, &len);
    if (!pb || k < 0 || k >= 13 || PL_CLIPS + 52L * (C.team + 1) > len) return 0xFFFFFFFFu;
    return be32(pb + PL_CLIPS + 52 * C.team + 4 * k);
}

static int clips(void)
{
    int n = 0;
    while (n < 13 && clip_lba(n) != 0xFFFFFFFFu) n++;
    return n;
}

static void clip_stop(void)
{
    if (V.film) {
        segacd_film_close(V.film);
        V.film = NULL;
        if (C.dev) SDL_ClearQueuedAudio(C.dev);
    }
    ob_postfx_free(V.pp);
    V.pp = NULL;
}

/* (the window's thread) clip k of the team's list, from its start */
static void clip_start(int k)
{
    clip_stop();
    int n = clips();
    if (!n) return;
    V.clip = (k % n + n) % n;
    const char *name = ob_cd_file_at(C.cd, (long)clip_lba(V.clip));
    if (!name || !(V.film = segacd_film_open(C.cd, name))) return;
    int w, h;
    segacd_film_size(V.film, &w, &h);
    if (w != CLIP_W || h != CLIP_H) { segacd_film_close(V.film); V.film = NULL; return; }
    if (C.dedither != OB_POSTFX_OFF) V.pp = ob_postfx_new(w, h, NULL, segacd_film_id(V.film));
    V.pcm_len = segacd_film_sound(V.film, &V.pcm);
    V.pcm_at = 0;
    V.out = 0;
    V.index = -1;
    for (int i = 0; i < CLIP_W * CLIP_H; i++) V.pic[i] = 0xFF000000u;
    V.ntime = segacd_film_next(V.film, &V.nidx, &V.ncol);
    if (C.dev) { SDL_ClearQueuedAudio(C.dev); SDL_PauseAudioDevice(C.dev, 0); }
    V.t0 = SDL_GetTicks();
    SDL_AtomicSet(&C.dirty, 1);
}

/* (the window's loop) its sound topped up and its picture brought to its time; at its end the last picture stays */
static void clip_tick(void)
{
    if (!V.film) return;
    double now;
    unsigned rate = C.rate ? C.rate : 48000;
    if (C.dev) {
        static int16_t buf[2 * 1024];
        double step = (double)SEGACD_FILM_RATE / rate;
        while (V.pcm_at < V.pcm_len && SDL_GetQueuedAudioSize(C.dev) < rate * 4 * CLIP_AHEAD_MS / 1000) {
            int k = 0;
            for (; k < 1024; k++, V.pcm_at += step) {
                long i = (long)V.pcm_at;
                int v = 0;
                if (i + 1 < V.pcm_len) { double f = V.pcm_at - i; v = (int)(V.pcm[i] * (1 - f) + V.pcm[i + 1] * f); }
                buf[2 * k] = buf[2 * k + 1] = (int16_t)v;
            }
            SDL_QueueAudio(C.dev, buf, (Uint32)k * 4);
            V.out += (uint64_t)k;
        }
        now = (double)(V.out - SDL_GetQueuedAudioSize(C.dev) / 4) / rate;
    } else now = (SDL_GetTicks() - V.t0) / 1000.0;
    int fresh = 0;
    static uint32_t raw[CLIP_W * CLIP_H];
    while (V.ntime >= 0 && V.ntime <= now) {
        for (int i = 0; i < CLIP_W * CLIP_H; i++) raw[i] = V.ncol[V.nidx[i]];
        V.index++;
        V.ntime = segacd_film_next(V.film, &V.nidx, &V.ncol);
        fresh = 1;
    }
    if (fresh) {
        int w = CLIP_W, h = CLIP_H;
        const uint32_t *p = V.pp ? ob_postfx_run(V.pp, OB_POSTFX_LIVE, V.index, raw, &w, &h) : raw;
        memcpy(V.pic, w == CLIP_W && h == CLIP_H ? p : raw, sizeof V.pic);
        SDL_AtomicSet(&C.dirty, 1);
    }
    if (V.ntime < 0 && V.pcm_at >= V.pcm_len) {                      /* (played out: its picture stays, its sound ends) */
        segacd_film_close(V.film);
        V.film = NULL;
    }
}

/* the cards in turn: a player's player, team, video; a team's team, video */
static void turn(void)
{
    int next = C.side == 2 ? (C.k.player ? 0 : 1) : C.side + 1;
    if (C.side == 2) clip_stop();
    C.side = next;
    if (C.side == 2) clip_start(V.clip);
}

/* ---- the screen */

static void draw(void *ud, uint32_t *px, int w, int h, int pitch)
{
    (void)ud;
    if (!C.disc) { draw_plain(px, w, h, pitch); return; }
    if (SDL_AtomicSet(&C.dirty, 0)) compose();
    int bw = w, bh = w * 3 / 4;             /* (the Genesis picture is 4:3 whatever its width) */
    if (bh > h) { bh = h; bw = h * 4 / 3; }
    int x0 = (w - bw) / 2, y0 = (h - bh) / 2;
    for (int y = 0; y < h; y++) {
        uint32_t *row = px + (long)y * pitch;
        int sy = (y - y0) * SH / (bh ? bh : 1);
        for (int x = 0; x < w; x++) {
            int sx = (x - x0) * SW / (bw ? bw : 1);
            row[x] = y < y0 || y >= y0 + bh || x < x0 || x >= x0 + bw ? 0xFF000000u : C.fb[sy * SW + sx];
        }
    }
}

static void input(void *ud, const ob_screen_ev *e)
{
    (void)ud;
    int seasons = C.k.career.nrows - 1;
    switch (e->kind) {
    case OB_SCREEN_BACK: case OB_SCREEN_PRESS: SDL_AtomicSet(&C.close, 1); return;
    case OB_SCREEN_OK: if (C.disc) turn(); else SDL_AtomicSet(&C.close, 1); break;
    case OB_SCREEN_UP: if (C.side == 2) clip_start(V.clip); else if (C.row > 0) C.row--; break;
    case OB_SCREEN_DOWN: if (C.side != 2 && C.row + 7 < seasons) C.row++; break;
    case OB_SCREEN_LEFT: if (C.side == 2) clip_start(V.clip - 1); else if (C.col > 0) C.col--; break;
    case OB_SCREEN_RIGHT: if (C.side == 2) clip_start(V.clip + 1); else if (C.col + 1 < ncols()) C.col++; break;
    case OB_SCREEN_WHEEL: if (e->y > 0 && C.row > 0) C.row--; else if (e->y < 0 && C.row + 7 < seasons) C.row++; break;
    default: return;
    }
    SDL_AtomicSet(&C.dirty, 1);
}

/* (the game's thread, B on a card) the card, the game waiting until it is closed; 0 when there is none */
int segacd_card_show(ob_cd *cd, const segacd_card *card, int (*ending)(void))
{
    if (SDL_AtomicGet(&C.state) || (card->player && !card->career.nbio)) return 0;
    C.k = *card;
    C.cd = cd;
    C.team = segacd_team_index(cd, card->abbr);
    C.disc = C.team >= 0 && segacd_graph2_record(cd, 64, NULL) && segacd_graph2_record(cd, 68 + C.team, NULL);
    C.side = card->player ? 0 : 1;
    C.row = C.col = 0;
    V.clip = 0;
    plain_words();
    SDL_AtomicSet(&C.dirty, 1);
    SDL_AtomicSet(&C.close, 0);
    SDL_AtomicSet(&C.state, 1);
    while (SDL_AtomicGet(&C.state) && !(ending && ending())) SDL_Delay(5);
    return 1;
}

/* (the window's loop) puts it up, plays the video card's clip, and takes it down once closed */
void segacd_career_tick(uint32_t dev, unsigned rate, int dedither)
{
    int st = SDL_AtomicGet(&C.state);
    C.dev = dev;
    C.rate = rate;
    C.dedither = dedither;
    if (st == 2 && C.side == 2) clip_tick();
    if (st == 1) {
        ob_screen s = { draw, input, NULL };
        ob_screen_show(&s);
        SDL_AtomicSet(&C.state, 2);
    } else if (st == 2 && SDL_AtomicGet(&C.close)) {
        clip_stop();
        ob_screen_show(NULL);
        SDL_AtomicSet(&C.state, 0);
    }
}

#ifdef SEGACD_CARD_TEST
/* (a test's) the card's screen as it would be drawn, 320 x 224 */
const uint32_t *segacd_card_test(ob_cd *cd, const segacd_card *card, int side, int row, int col)
{
    C.k = *card;
    C.cd = cd;
    C.team = segacd_team_index(cd, card->abbr);
    C.side = side;
    C.row = row;
    C.col = col;
    if (side == 2) {                                                      /* (row: the clip, col: how many pictures in) */
        clip_start(row);
        for (int i = 0; V.film && i <= col; i++) { V.ntime = 0; V.pcm_at = V.pcm_len; clip_tick(); }
    }
    compose();
    return C.fb;
}
#endif
