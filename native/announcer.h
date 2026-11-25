/* The rink announcer of NHL 95 PC beside the cartridge game (native/announcer.c). */
#pragma once
#include <stdint.h>

void announcer_start(void);                  /* look for NHL 95 PC's XBRUCE2.VIV beside the program (a thread) and hook the game's boxes */
void announcer_wait(void);                   /* (headless) until the search ends */
int announcer_searched(void);                /* 1 once the search ended */
int announcer_found(void);                   /* 1 when the clips were read (after the search) */
void announcer_set(int on);                  /* the Game page: 0 off (the cartridge), 1 on */
void announcer_mix(int16_t *out, int frames, unsigned rate);   /* the voice into a frame of sound */
int announcer_active(void);                  /* 1 when the voice is on and its clips were read (a game is on) */
void announcer_stop(void);                   /* quiet at once (pause, leaving the game) */

/* testing: a file of clips read in place of the search (the headless --announcer FILE); 1 when it was read */
int announcer_load_file(const char *path);
