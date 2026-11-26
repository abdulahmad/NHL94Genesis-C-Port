/* NHL 93's fight frames from the player's own NHLPA Hockey 93 cartridge (native/fight93.c) */
#pragma once
#include <stdint.h>

#define FIGHT93_BUS 0xB00000u     /* where NHL 93's Sprites.anim is on the 68k bus for the VDP's DMA (nothing else is there) */

void fight93_start(void);                     /* find the cartridge and take its fight frames (once) */
int fight93_have(uint16_t frame);             /* frame (ours) is drawn with NHL 93's art */
int fight93_bus(uint32_t addr, uint8_t *v);   /* a byte of the bus window: 1 when addr is in it */
