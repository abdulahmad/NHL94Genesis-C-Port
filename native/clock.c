/* Original speed (opt-in): a 68k cost model. Smooth (the default) leaves every game-loop pass
 * at one vblank. With Original, each original-routine entry and each counted hardware access
 * charges master clocks; when a DoGameFrame pass exceeds one NTSC frame, extra vblanks fire
 * mid-logic so the next wait sees elapsed > 1, as on the cartridge. C helpers are not charged
 * (they are not 68k jsr's; charging them, or charging inside an extra vblank's handshake, loops).
 *
 * The per-event costs are a first 68k model, not a table fitted from traces. Lockstep stays on
 * Smooth and still resyncs lag (plan E6 checks lag frames only in Original). */
#include <stdio.h>
#include <stdlib.h>
#include "game.h"
#include "native.h"
#include "symbols_gen.h"

#define FRAME_NTSC (MDRT_LINES_NTSC * MDRT_LINE_MCLKS)   /* 896040 master clocks */
#define ROUTINE_MCLKS 7000u                             /* ~1000 68k cycles: an original routine's mean body */
#define PORT_MCLKS 112u                                 /* ~16 68k cycles: a VDP / pad / SRAM / PSG access */
#define BUS_MCLKS 56u                                   /* a 68k byte on the Z80's bus (hal_native.c) */

static int original_speed;              /* 0 Smooth (default), 1 Original */
static int cli = -1;                    /* -1: the menu / settings; else 0 or 1 from the command line */
static int in_dof, in_pass, pass_lagged, in_clock_vblank;
static uint32_t pass_mclks;
static long extra_vblanks, passes, lagged_passes;

void clock_cli(int on)
{
    cli = on ? 1 : 0;
    original_speed = cli;
}

void clock_set_original(int on)
{
    original_speed = cli >= 0 ? cli : (on ? 1 : 0);
}

int clock_original(void) { return original_speed; }

void clock_reset(void)
{
    in_dof = in_pass = pass_lagged = in_clock_vblank = 0;
    pass_mclks = 0;
    extra_vblanks = passes = lagged_passes = 0;
}

static int busy(void) { return !original_speed || !in_pass || in_clock_vblank || native_in_vblank(); }

static void charge(uint32_t mclks)
{
    uint32_t frame;
    int n = 0;
    if (busy()) return;
    pass_mclks += mclks;
    frame = native_m ? mdrt_frame_mclks(native_m) : FRAME_NTSC;
    if (!frame) frame = FRAME_NTSC;
    while (pass_mclks >= frame && n < 3) {
        pass_mclks -= frame;
        extra_vblanks++;
        n++;
        in_clock_vblank = 1;
        native_vblank_now();
        in_clock_vblank = 0;
    }
    if (n) pass_lagged = 1;
}

void clock_on_enter(uint32_t routine)
{
    if (!original_speed || in_clock_vblank || native_in_vblank()) return;
    if (routine == SYM_DoGameFrame) {
        in_dof = 1;
        in_pass = 0;
        pass_lagged = 0;
        pass_mclks = 0;
        return;
    }
    if (in_dof && !in_pass) {
        in_pass = 1;
        passes++;
    }
    /* C helpers (routine 0) are not 68k jsr's: charging them overcounts by tens of extras a pass */
    if (in_pass && routine) charge(ROUTINE_MCLKS);
}

void clock_on_exit(uint32_t routine)
{
    if (!original_speed || in_clock_vblank || native_in_vblank()) return;
    if (routine != SYM_DoGameFrame) return;
    if (pass_lagged) lagged_passes++;
    if (getenv("CLOCK_DEBUG"))
        fprintf(stderr, "clock: pass %ld, %u mclks left, elapsed %d, %ld extra vblanks so far%s\n",
                passes, pass_mclks, elapsed, extra_vblanks, pass_lagged ? ", lagged" : "");
    in_dof = in_pass = pass_lagged = 0;
    pass_mclks = 0;
}

void clock_on_port(void) { charge(PORT_MCLKS); }
void clock_on_bus(void) { charge(BUS_MCLKS); }

void clock_report(void)
{
    if (!original_speed) return;
    printf("original speed: %ld game-loop passes, %ld with extra vblanks, %ld extra vblanks in all\n",
           passes, lagged_passes, extra_vblanks);
}
