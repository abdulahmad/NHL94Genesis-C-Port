/* NHL 95 PC's goal siren and end of period horn beside the cartridge game (native/pcsfx.c). */
#pragma once
#include <stdint.h>

void pcsfx_start(void);                      /* look for PCFF001.DIG beside the program (a thread) and hook the game's siren and horn */
void pcsfx_wait(void);                       /* (headless) until the search ends */
int pcsfx_searched(void);
int pcsfx_found(void);                       /* 1 when the samples were read (after the search) */
void pcsfx_set_goal(int on);                 /* the Game page: the goal siren, 0 the cartridge's, 1 NHL 95 PC's */
void pcsfx_set_horn(int on);                 /* the same for the end of the period */
void pcsfx_mix(int16_t *out, int frames, unsigned rate);   /* the sounds into a frame of sound */
void pcsfx_stop(void);
int pcsfx_load_file(const char *path);       /* testing: a PCFF001.DIG read in place of the search (--pc-sfx FILE) */
