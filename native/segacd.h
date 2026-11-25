/* The Sega CD disc's music and crowd bed beside the cartridge game (native/segacd.c). */
#pragma once
#include <stdint.h>
#include "overbar/cdimage.h"

void segacd_start(void);                 /* look for the disc beside the program (a thread) and hook the game's sound */
void segacd_wait(void);                  /* (headless) until the search ends */
ob_cd *segacd_disc(void);                /* the disc, NULL while looking or when there is none */
void segacd_set_music(int on);           /* the Game page: songs from the disc */
void segacd_set_crowd(int v);            /* 0 the cartridge's PSG crowd, 1 the disc's track 2, 2 both */
void segacd_set_voice(int v);            /* Ron Barr's scouting report: 0 off, 1 on (no song under it), 2 on over the music */
void segacd_set_screens(int v);          /* the opening screens: 0 the cartridge's, 1 the disc's */
void segacd_set_intro(int v);            /* the intro movie (HOCKEY.SEG) before the title: 0 off, 1 on */
extern void (*segacd_intro_hook)(void);  /* (the window's) plays it, the game's thread waiting; NULL: no movie */
int segacd_present(uint32_t *frame, int w, int h);   /* a w x h ARGB frame in a 320 x 224 buffer: the disc's opening picture over it, 320 x 224 (1), or left alone (0) */
void segacd_event(int ev, uint16_t n);   /* (cd_event_hook) */
uint8_t segacd_ym(int port, uint8_t v);  /* a YM2612 port write on its way to the chip: v, or the song's notes dropped */
uint8_t segacd_psg(uint8_t v);           /* a PSG write on its way to the chip */
void segacd_mix(int16_t *out, int frames, unsigned rate);   /* the disc's track into a frame of sound */

/* the disc's opening pictures (native/segacd_pics.c): 320 x 224 ARGB into out; 0 when it cannot */
enum { SEGACD_PIC_HSP, SEGACD_PIC_TITLE, SEGACD_PIC_LICENSE };
int segacd_picture(ob_cd *cd, int which, uint32_t *out);

/* the disc's movies (native/segacd_film.c): an SM picture over the tiles the frame before left; 0 when it is not one */
int segacd_sm_decode(const uint8_t *c, long n, uint8_t *tiles, int *w, int *h, uint16_t cram[64], uint8_t *line, int max_tiles);
void segacd_sm_indexes(const uint8_t *tiles, const uint8_t *line, int w, int h, uint8_t *out);
typedef struct segacd_film segacd_film;
segacd_film *segacd_film_open(ob_cd *cd, const char *name);     /* a .SEG on the disc, NULL when it is not there */
void segacd_film_close(segacd_film *f);
void segacd_film_size(const segacd_film *f, int *w, int *h);    /* the picture, in pixels */
long segacd_film_sound(const segacd_film *f, const int16_t **pcm);   /* all its sound: 16000 Hz mono */
double segacd_film_next(segacd_film *f, const uint8_t **idx, const uint32_t **argb);   /* the next picture and when (s); -1 at the end */
#define SEGACD_FILM_RATE 16000

/* the intro movie in the window (native/segacd_movie.c): an Overbar screen, its sound on dev (an SDL audio device, 0 none) */
int segacd_movie_start(ob_cd *cd, const char *name, uint32_t dev, unsigned rate);
int segacd_movie_tick(void);             /* (the window's loop) 0 once it has ended */
int segacd_movie_playing(void);
