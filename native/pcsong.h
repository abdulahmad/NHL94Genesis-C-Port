/* The national anthems before the first face off: NHL 95 PC's FM songs or NHL 96 PC's recordings (native/pcsong.c). */
#pragma once
#include <stdint.h>

void pcsong_start(void);                     /* look for USA.KMS, CANADA.KMS and the FM timbres beside the program or on the disc image (a thread), render both anthems and hook the game */
void pcsong_wait(void);                      /* (headless) until the search ends */
int pcsong_searched(void);
int pcsong_found(void);                      /* 1 when either game's anthems are ready (after the search) */
int pcsong_flag_at(int frame);               /* the flag (0 USA, 1 Canada) of the anthem playing this many game frames after it began; -1 none */
void pcsong_set(int on);                     /* the Game page: the anthems before the game: 0 off, 1 NHL 95 PC's, 2 NHL 96 PC's (the other when the one asked for is missing) */
int pcsong_found95(void);                    /* NHL 95 PC's songs were found */
int pcsong_found96(void);                    /* NHL 96 PC's recordings (DATA/ANTHUS.ASF, ANTHCAN.ASF) were found */
void pcsong_stop(void);
void pcsong_rock_set(int on);                /* the Game page's PC crowd: NHL 95 PC's organ and rock cues at the whistles */
void pcsong_mix(int16_t *out, int frames, unsigned rate);   /* the anthem into a frame of sound */
int pcsong_cheer_frame(void);              /* the game frame of the anthem playing at which NHL 95 PC's crowd cheers (-1 none) */
int pcsong_write_wav(const char *path);      /* testing: both anthems as a wav */
