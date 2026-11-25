/* Hat trick hats of NHL 94 SNES beside the cartridge game (native/hats.c). */
#pragma once
#include <stdint.h>

void hats_start(void);                       /* look for the player's NHL 94 SNES ROM beside the program (a thread) and hook the game */
void hats_wait(void);                        /* (headless) until the search ends */
int hats_searched(void);
int hats_found(void);                        /* 1 when the ROM was read (after the search) */
void hats_set(int on);                       /* the Game page: 0 off (the cartridge), 1 on */
void hats_present(uint32_t *pic, int w, int h, int pitch);   /* draw the hats over a finished picture */
int hats_load_file(const char *path);        /* testing: a ROM read in place of the search; 1 when it was read */
