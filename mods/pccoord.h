/* NHL 95 PC's rink on the cartridge's. The PC's is 0A0h across to the boards and 108h long to the end boards, with its goal line
 * at 0E8h; the cartridge's 88h, 12Ah and 108h. A PC x is put on the cartridge's rink times 88h / 0A0h; a PC y times 108h / 0E8h
 * up to the goal line and beyond it the end's 22h / 20h, so the PC's goal line, blue line (4Eh: the cartridge's 58h) and end
 * boards land on the cartridge's own. Both ways, signed (the rink is the same either side of the middle). Integers only: the
 * game's state depends on these */
#ifndef PCCOORD_H
#define PCCOORD_H

#define PC_SIDE_X 0xA0                                  /* (the PC's side boards) */
#define PC_GOAL_Y 0xE8                                  /* (its goal line) */
#define PC_END_Y 0x108                                  /* (its end boards) */
#define CART_SIDE_X 0x88
#define CART_GOAL_Y 0x108
#define CART_END_Y 0x12A

/* a PC x / y on the cartridge's rink */
static inline int pc_x(int v) { return v * CART_SIDE_X / PC_SIDE_X; }
static inline int pc_y(int v)
{
    int a = v < 0 ? -v : v;
    a = a <= PC_GOAL_Y ? a * CART_GOAL_Y / PC_GOAL_Y : CART_GOAL_Y + (a - PC_GOAL_Y) * (CART_END_Y - CART_GOAL_Y) / (PC_END_Y - PC_GOAL_Y);
    return v < 0 ? -a : a;
}
/* a cartridge x / y on the PC's rink */
static inline int cart_pc_x(int v) { return v * PC_SIDE_X / CART_SIDE_X; }
static inline int cart_pc_y(int v)
{
    int a = v < 0 ? -v : v;
    a = a <= CART_GOAL_Y ? a * PC_GOAL_Y / CART_GOAL_Y : PC_GOAL_Y + (a - CART_GOAL_Y) * (PC_END_Y - PC_GOAL_Y) / (CART_END_Y - CART_GOAL_Y);
    return v < 0 ? -a : a;
}

#endif
