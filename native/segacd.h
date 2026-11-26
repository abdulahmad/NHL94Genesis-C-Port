/* The Sega CD disc's music and crowd bed beside the cartridge game (native/segacd.c). */
#pragma once
#include <stdint.h>
#include "overbar/cdimage.h"

void segacd_start(void);                 /* look for the disc beside the program (a thread) and hook the game's sound */
void segacd_wait(void);                  /* (headless) until the search ends */
int segacd_searched(void);               /* 1 once the search ended (segacd_disc: what it found, NULL none) */
ob_cd *segacd_disc(void);                /* the disc, NULL while looking or when there is none */
void segacd_set_music(int on);           /* the Game page: songs from the disc */
void segacd_set_crowd(int v);            /* bit 0 the cartridge's PSG crowd, bit 1 the disc's track 2 */
void segacd_set_crowd_vol(int cart, int cd);   /* each bed's volume, percent */
void segacd_set_voice(int v);            /* Ron Barr's scouting report: 0 off, 1 on (no song under it), 2 on over the music */
void segacd_set_screens(int v);          /* the opening screens: 0 the cartridge's, 1 the disc's (EA SPORTS, High Score Productions, the license page) */
void segacd_set_title(int v);            /* the title page: 0 the cartridge's, 1 the disc's (its credits, then the license page) */
void segacd_set_photos(int v);           /* the player cards' photos: 0 the cartridge's, 1 the disc's where it has one */
void segacd_set_session(int on);
void segacd_set_career(int v, int (*ending)(void));   /* (the window's) B on a player's card: his career from the disc; on a team's card its team page; ending: give up waiting */         /* (a session's console: the photos stay the cartridge's) */
int segacd_opening(void);                /* the game is on a screen any press leaves: the logos, the title, the license page */
void segacd_set_intro(int v);            /* the intro movie (HOCKEY.SEG) before the title: 0 off, 1 on */
extern void (*segacd_intro_hook)(void);  /* (the window's) plays it, the game's thread waiting; NULL: no movie */
int segacd_present(uint32_t *frame, int w, int h);   /* a w x h ARGB frame in a 320 x 224 buffer: the disc's opening picture over it, 320 x 224 (1), or left alone (0) */
void segacd_event(int ev, uint16_t n);   /* (cd_event_hook) */
uint8_t segacd_ym(int port, uint8_t v);  /* a YM2612 port write on its way to the chip: v, or the song's notes dropped */
uint8_t segacd_psg(uint8_t v);           /* a PSG write on its way to the chip */
void segacd_mix(int16_t *out, int frames, unsigned rate);   /* the disc's track into a frame of sound */

/* the disc's sound effects (native/segacd_sfx.c, the Sound effects row): 0 the cartridge's, 1 the disc's (rendered in a thread
 * the first time); segacd_sfx_on: effect id is the disc's now (its cartridge notes are kept from the chips) */
void segacd_set_sfx(int v);
int segacd_sfx_on(int id);
void segacd_sfx_play(int id);
void segacd_sfx_stop(void);
void segacd_sfx_mix(int16_t *out, int frames, unsigned rate);
void segacd_sfx_wait(void);             /* (headless) until they are rendered */

/* the disc's opening pictures (native/segacd_pics.c): 320 x 224 ARGB into out; 0 when it cannot */
enum { SEGACD_PIC_HSP, SEGACD_PIC_TITLE, SEGACD_PIC_LICENSE };
int segacd_picture(ob_cd *cd, int which, uint32_t *out);
int segacd_ea_logo(ob_cd *cd, int pieces, uint32_t *out);   /* the disc's EA SPORTS logo with that many pieces up (320 x 224) */
int segacd_hsp(ob_cd *cd, int t, uint32_t *out);            /* its High Score Productions page t frames in (its sparkles) */
int segacd_title_begin(ob_cd *cd);                          /* its title page with the credits, from the first page: 0 not on this disc */
int segacd_title_step(void);                                /* a frame of the credits: 0 at their end */
void segacd_title_draw(uint32_t *out);                      /* the page now (320 x 224) */

/* the disc's movies (native/segacd_film.c): an SM picture over the tiles the frame before left; 0 when it is not one */
int segacd_sm_decode(const uint8_t *c, long n, uint8_t *tiles, int *w, int *h, uint16_t cram[64], uint8_t *line, int max_tiles);
void segacd_sm_indexes(const uint8_t *tiles, const uint8_t *line, int w, int h, uint8_t *out);
typedef struct segacd_film segacd_film;
segacd_film *segacd_film_open(ob_cd *cd, const char *name);     /* a .SEG on the disc, NULL when it is not there */
void segacd_film_close(segacd_film *f);
void segacd_film_size(const segacd_film *f, int *w, int *h);    /* the picture, in pixels */
long segacd_film_sound(const segacd_film *f, const int16_t **pcm);   /* all its sound: 16000 Hz mono */
double segacd_film_next(segacd_film *f, const uint8_t **idx, const uint32_t **argb);   /* the next picture and when (s); -1 at the end */
segacd_film *segacd_film_dup(const segacd_film *f);              /* the same movie from its start, for another thread */
uint32_t segacd_film_id(const segacd_film *f);                   /* a hash of the file (the de-dither cache's source) */
#define SEGACD_FILM_RATE 16000

/* the intro movie in the window (native/segacd_movie.c): an Overbar screen, its sound on dev (an SDL audio device, 0 none) */
/* dedither: ob_postfx_mode; smooth: 30 fps; colour: each shot's own; split: the 50/50 demo (the left half the disc's own picture
 * at its own rate, the right half cached de-dither, colour and 30 fps, whatever the other three say); base: the made files' path less its
 * extension (<base>.dedither, .smooth, .colour; "" none made); model: the frame generation model's folder */
int segacd_movie_start(ob_cd *cd, const char *name, uint32_t dev, unsigned rate, int dedither, int smooth, int colour, int split, const char *base,
                       const char *model);
/* dedither: an Overbar ob_postfx_mode (off, live, cached); cache_path: where the cached pass is kept (made on the first play) */
/* the made passes the options want, made from now on (the start of the opening, once the disc is found), so the movie
 * need not wait for them; the same arguments as segacd_movie_start's */
void segacd_movie_prepare(ob_cd *cd, const char *name, int dedither, int smooth, int colour, int split, const char *base, const char *model);
void segacd_movie_pause(int on);            /* the overlay is up: the intro waits */
int segacd_movie_tick(void);             /* (the window's loop) 0 once it has ended */
int segacd_movie_playing(void);

/* the disc's player photos (native/segacd_photos.c): team abbr's player name's, as UnpackPicture leaves a picture
 * (the tile count word, 36 tiles) into buf and its 16 colours (big-endian words) into colours; 0 when the disc has none */
int segacd_photo(ob_cd *cd, const char *abbr, const char *name, uint8_t *buf, uint8_t colours[32]);
int segacd_photo_count(ob_cd *cd);
const char *segacd_photo_name(int i, const char **abbr);

/* a player's career from the disc (native/segacd_photos.c): his name, position and size, birthplace, birthday, draft;
 * the table's two headings (the regular season's, the playoffs'), its column names (row 0) and a row a season.
 * (The plain team page is one too: its name, and a row a line with no column names, noheader.) */
#define SEGACD_BIO 8
#define SEGACD_ROWS 40
#define SEGACD_COLS 20
#define SEGACD_CELL 72
typedef struct {
    int nbio;
    char bio[SEGACD_BIO][SEGACD_CELL];
    char heading[2][32];
    int nrows, ncells[SEGACD_ROWS], noheader;
    char cell[SEGACD_ROWS][SEGACD_COLS][SEGACD_CELL];
} segacd_career;
int segacd_career_get(ob_cd *cd, const char *abbr, const char *name, int goalie, segacd_career *out);
/* the disc's other pieces (native/segacd_photos.c): a GTABLE.BIN record of GRAPH2.BIN, PLAYER.BIN, and a team's
 * number on the disc (its order in the cartridge, 0-27; -1 a team the disc doesn't have) */
const uint8_t *segacd_graph2_record(ob_cd *cd, int rec, long *len);
const uint8_t *segacd_player_bin(ob_cd *cd, long *len);
int segacd_team_index(ob_cd *cd, const char *abbr);

/* the disc's player and team cards (native/segacd_career.c, PLAYER.BIN's screen): B on a card. A team the disc
 * has gets the disc's own screen; another (a roster set's) a plain page with the same words */
typedef struct {
    char abbr[8], city[24], nick[24], arena[32];
    int adv[6];                             /* the team card's six numbers, as the disc reads its nibbles */
    int player;                             /* 1: a player's card (else the team's) */
    char name[40];
    uint8_t number;                         /* BCD */
    int goalie;
    segacd_career career;
} segacd_card;
int segacd_card_show(ob_cd *cd, const segacd_card *card, int (*ending)(void));
void segacd_career_tick(uint32_t dev, unsigned rate, int dedither);   /* (the window's loop: its audio device and rate, the movie de-dither) */
