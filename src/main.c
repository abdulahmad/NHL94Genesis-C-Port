/* main94.asm:Start. Power on: SegaInit, the checksum, then the game. */
#include <string.h>
#include "game.h"
#include "hal.h"
#include "rom.h"
#include "symbols_gen.h"

/* main94.asm:Start. Power on: SegaInit, the checksum, then the game. */
void Start(void)
{
    SegaInit();
    ValidationRoutine();
    Begin();
}
