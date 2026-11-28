/* White ice: the cartridge's pale cyan rink drawn white, as on the PC games (native/whiteice.c). */
#pragma once
#include <stdint.h>

void whiteice_set(int on);                 /* the Game page: 0 the cartridge's ice, 1 white */
int whiteice_on(void);
uint32_t whiteice_rgb(uint32_t argb);      /* one pixel of the rink's picture (anything not ice stays as it is) */
void whiteice_fade_rink(int on);   /* (pcspr.c, each frame) a fade of a game's colours runs: the rink is up though the game draws no frames */
void whiteice_present(uint32_t *pic, int w, int h, int pitch);   /* a whole picture (only while a game's rink is up) */
