/* The Sega CD disc's music and crowd bed beside the cartridge game (native/segacd.c). */
#pragma once
#include <stdint.h>
#include "overbar/cdimage.h"

void segacd_start(void);                 /* look for the disc beside the program (a thread) and hook the game's sound */
void segacd_wait(void);                  /* (headless) until the search ends */
ob_cd *segacd_disc(void);                /* the disc, NULL while looking or when there is none */
void segacd_set_music(int on);           /* the Game page: songs from the disc */
void segacd_set_crowd(int v);            /* 0 the cartridge's PSG crowd, 1 the disc's track 2, 2 both */
void segacd_event(int ev, uint16_t n);   /* (cd_sound_hook) */
uint8_t segacd_ym(int port, uint8_t v);  /* a YM2612 port write on its way to the chip: v, or the song's notes dropped */
uint8_t segacd_psg(uint8_t v);           /* a PSG write on its way to the chip */
void segacd_mix(int16_t *out, int frames, unsigned rate);   /* the disc's track into a frame of sound */
