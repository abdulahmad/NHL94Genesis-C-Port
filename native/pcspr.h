#ifndef PCSPR_H
#define PCSPR_H
#include <stdint.h>

void pcspr_start(void);                    /* look for NHL 95 PC's sprite banks (000_049.PPV ...) beside the program or on its disc (a thread) and hook the game */
void pcspr_wait(void);                     /* (headless) until the search ends */
int pcspr_searched(void);
int pcspr_found(void);                     /* 1 when the sprites were read (after the search) */
void pcspr_ref_set(int on);               /* the Game page: the referee on the ice (NHL 95 PC's frames) */
void pcspr_ref_phys_set(int on);              /* the Game page: the referee bumped by skaters (NHL 95 PC's checkcx), only in the picture */
void pcspr_logo_set(int on);              /* the Game page: NHL 95 PC's centre ice logos in place of the cartridge's */
int pcspr_logos(void);                    /* 1 when the disc had them (after the search) */
void pcspr_set(int on);                    /* the Game page: 0 off (the cartridge's sprites), 1 on (the PC's, the cartridge's where the PC has none) */
struct mdrt_vdp;
void pcspr_present(const struct mdrt_vdp *vdp, const uint16_t *px, int pitch_px, uint32_t *pic, int w, int h, int pitch);   /* draw the PC's sprites over a finished picture (px: its VDP colours) */
/* pcspr_present and then white ice (whiteice_present), through a fade as without one, faded as the picture is (the window and the shots: in place of the two) */
void pclayers_present(const struct mdrt_vdp *vdp, const uint16_t *px, int pitch_px, uint32_t *pic, int w, int h, int pitch);
uint32_t pclayers_rgb(const struct mdrt_vdp *vdp, int i);   /* colour RAM entry i as the picture shows it (white ice, the fade): the 16:9 sides */
#endif
