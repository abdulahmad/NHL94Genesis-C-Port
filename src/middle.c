/* middle94.asm: shared helpers used all over the game. They fade palettes, make random
 * numbers and square roots, start sounds, read up to four joysticks and wait for buttons,
 * move and fill video memory and set up the screen.
 *
 * Every VDP access goes through the HAL (hal.h), one call per 68k port access, in the order the
 * 68k made them. Source addresses (DMA sources, strings, graphics) are 68k addresses: ROM or
 * work RAM, read through m8() and its kin. */
#include "game.h"
#include "hal.h"
#include "rom.h"
#include "symbols_gen.h"

/* the address word pair of a VDP write command for VRAM address a: high word 01aa aaaa
 * aaaa aaaa, low word 0000 0000 0000 00aa (asl.l #2 / lsr.w #2 / ori.w #$4000 / swap) */
static uint32_t vram_write_cmd(uint16_t a) { return (uint32_t)(0x4000 | (a & 0x3FFF)) << 16 | (a >> 14); }

/* nibblebuffer's word k (past its six as the 68000 goes on) */
static uint16_t nib(unsigned k) { return k < 6 ? g.nibblebuffer[k] : r16(rp_add(RAM(nibblebuffer), (int32_t)(2 * k)), 0); }
static void set_nib(unsigned k, uint16_t v)
{
    if (k < 6) g.nibblebuffer[k] = v;
    else w16(rp_add(RAM(nibblebuffer), (int32_t)(2 * k)), 0, v);
}

/* forceblack / forceblack2: fade to black over palcount frames, keeping palfadenew */
static void fade_black(uint16_t count)
{
    uint16_t keep[0x40];
    for (int i = 0; i < 0x40; i++) { keep[i] = g.palfadenew[i]; g.palfadenew[i] = 0; }
    g.palcount = count;
    forcefade();
    for (int i = 0; i < 0x40; i++) g.palfadenew[i] = keep[i];
}

/* the vblank interrupt advances Vcount; the 68k spins on it (.0 cmp.w (Vcount).w,d0 / beq.s .0) */
static void wait_vcount(void)
{
    uint16_t v = g.Vcount;
    while (g.Vcount == v) hal_wait_vblank();
}

/* ReadJoy1-4: read pad n -> d0 = direction and buttons, d1 = new presses, d2 = changed
 * buttons, d3 = held buttons; ljn keeps the held buttons for the next read */
static void read_pad(uint8_t pad, uint16_t *last, uint16_t *d0, uint16_t *d1, uint16_t *d2, uint16_t *d3)
{
    uint16_t held;
    ReadJoy(pad, d0, &held);
    *d2 = (uint16_t)(*last ^ held);
    *last = held;
    *d3 = held;
    *d1 = held & *d2;
}

/* DoDMA's .nd: one DMA transfer from 68k memory, with the Z80 off the bus. A transfer from
 * RAM writes its first word again by hand afterwards (the DMA bug the comment mentions). */
static void dma_68k(uint32_t src, uint16_t words, uint16_t vram)
{
    vdp_ctrl16(SYM_VDPR_MODE2 + 0x54);
    vdp_ctrl16(SYM_VDPR_AUTOINC + 0x02);
    vdp_ctrl16((uint16_t)(0x9300 | (words & 0xFF)));
    vdp_ctrl16((uint16_t)(0x9400 | (words >> 8)));
    uint32_t s = src >> 1;
    vdp_ctrl16((uint16_t)(0x9500 | (s & 0xFF)));
    vdp_ctrl16((uint16_t)(0x9600 | ((s >> 8) & 0xFF)));
    vdp_ctrl16((uint16_t)(SYM_VDPR_DMASRCH | ((s >> 16) & 0x7F)));
    uint32_t split = (uint32_t)(vram >> 14) << 16 | (vram & 0x3FFF);
    g.dmaram = split | 0x804000;
    uint16_t sr = cpu_get_sr();
    cpu_set_sr(0x2700);
    z80_busreq(0x100);
    while (z80_busack() & 1) {}
    vdp_ctrl16((uint16_t)g.dmaram);
    vdp_ctrl16((uint16_t)(g.dmaram >> 16));
    WaitDMA();
    vdp_ctrl16(SYM_VDPR_MODE2 + 0x64);
    z80_busreq(0);
    if ((int32_t)src < 0) {
        vdp_ctrl(vram_write_cmd(vram));
        vdp_data(m16(src));
    }
    cpu_set_sr(sr);
}

/* middle94.asm:UnpackNibbles. a0 = packed data, d0 = count: unpack count 4-bit values (high
 * nibble first) into words at nibblebuffer. */
void UnpackNibbles(uint32_t src, uint16_t count)
{
    uint32_t p = src;
    int lo = 0;
    for (unsigned k = 0; count--; k++) {
        uint8_t b = m8(p);
        if (lo) p++; else b >>= 4;
        lo ^= 1;
        set_nib(k, b & 0xF);
    }
}

/* middle94.asm:WeightedRandomSelect. d0 = number of word weights at nibblebuffer -> d0 = an
 * index picked with probability weight / total. The pick walks back from the last weight. */
uint16_t WeightedRandomSelect(uint16_t n)
{
    uint16_t k = 0, sum = 0;
    while (n--) sum += nib(k++);
    int16_t r = (int16_t)randomd0(sum);
    do r = (int16_t)(r - nib(--k)); while (r >= 0);
    return k;
}

/* middle94.asm:remap. a0 = tile words, a1 = 8-byte colour table, d0 = words, d1 = VRAM address:
 * write the tiles to VRAM with every pixel's colour index n replaced by nibble n of the table
 * (even n the high nibble of byte n/2, odd n the low one). Protected from the vblank. */
void remap(uint32_t src, uint32_t table, uint16_t words, uint16_t vram)
{
    uint16_t saved = g.disflags;
    g.disflags |= HI(dfng);
    uint32_t p = src, tab = table;
    Vmaddr(vram);
    do {
        uint16_t in = m16(p), out = 0;
        p += 2;
        for (int i = 0; i < 4; i++) {
            uint8_t c = m8(tab + ((in & 0xF) >> 1));
            if (!(in & 1)) c >>= 4;
            out = (uint16_t)(out | (c & 0xF));
            out = (uint16_t)(out >> 4 | out << 12);   /* ror.w #4 */
            in = (uint16_t)(in >> 4 | in << 12);
        }
        vdp_data(out);
    } while (--words != 0);
    g.disflags = saved;
}

/* middle94.asm:forceblack. Fade every colour to black (palcount $18), palfadenew untouched. */
void forceblack(void) { fade_black(0x18); }

/* middle94.asm:forceblack2. The same with palcount $64: CopyPaletteToCRAM sets black at once. */
void forceblack2(void) { fade_black(0x64); }

/* middle94.asm:forcefade. Run the fade to palfadenew whatever the interrupts and disflags are
 * doing: the fades-only vblank (vb2) steps it until palcount goes negative. */
void forcefade(void)
{
    uint16_t sr = cpu_get_sr();
    uint32_t VBint = g.VBint;
    uint16_t saved = g.disflags;
    g.disflags &= (uint16_t)~HI(dfng);
    g.VBint = SYM_vb2;
    cpu_set_sr(0x2500);
    while ((int16_t)g.palcount >= 0) hal_wait_vblank();
    g.disflags = saved;
    g.VBint = VBint;
    cpu_set_sr(sr);
}

/* middle94.asm:cramfade. Vblank code: step colour RAM toward palfadenew. palcount $64 copies the
 * palette at once; otherwise each frame moves one component (palcount mod 3: red, green, blue)
 * of every colour one step closer, while palcount counts down from $18. */
void cramfade(void)
{
    if ((int16_t)g.palcount < 0) return;
    if (g.palcount == 0x64) { CopyPaletteToCRAM(); return; }
    if ((int16_t)--g.palcount < 0) return;
    if ((int16_t)g.palcount > 0x18) return;
    unsigned shift = (g.palcount % 3) * 4;
    uint16_t step = (uint16_t)(2 << shift), mask = (uint16_t)(0xE << shift);
    for (uint16_t a = 0; a < 0x80; a += 2) {
        vdp_ctrl((uint32_t)a << 16 | 0x20);              /* CRAM read */
        uint16_t col = vdp_read();
        int16_t cur = (int16_t)(col & mask), want = (int16_t)(g.palfadenew[a >> 1] & mask);
        if (cur == want) continue;
        cur = (int16_t)(cur + (cur < want ? step : -step));
        col = (uint16_t)((col & ~mask) | cur);
        vdp_ctrl((uint32_t)(0xC000 | a) << 16);          /* CRAM write */
        vdp_data(col);
    }
}

/* middle94.asm:CopyPaletteToCRAM. Copy all 64 palfadenew colours to colour RAM, protected from
 * the vblank, and end the fade (palcount negative). */
void CopyPaletteToCRAM(void)
{
    uint16_t saved = g.disflags;
    g.disflags |= HI(dfng);
    vdp_ctrl(CRAM_W);
    for (int i = 0; i < 0x40; i += 2) vdp_data32((uint32_t)g.palfadenew[i] << 16 | g.palfadenew[i + 1]);
    ST_W(g.palcount);
    g.disflags = saved;
}

/* middle94.asm:randomd0s. d0 = range -> d0 = random number with -range <= d0 < range. */
uint16_t randomd0s(uint16_t range)
{
    return (uint16_t)(randomd0((uint16_t)(range << 1)) - range);
}

/* middle94.asm:randomd0. d0 = range -> d0 = random 0..range-1.
 * Steps the 32-bit seed with a 16x16 multiply spread over three products (the 68000 has no
 * 32x32 multiply): new = (lo*E62D) + ((lo*BB40 + hi*E62D) << 16) + 1, then scales
 * (new >> 8).w by range and keeps the high word of the product. */
uint16_t randomd0(uint16_t range)
{
    uint32_t lo = g.seed & 0xFFFF, hi = g.seed >> 16;
    uint32_t prod = lo * 0xE62Du;
    uint16_t hiw = (uint16_t)(prod >> 16) + (uint16_t)(lo * 0xBB40u) + (uint16_t)(hi * 0xE62Du);
    g.seed = (((uint32_t)hiw << 16) | (prod & 0xFFFF)) + 1;
    uint32_t scaled = (uint32_t)((int32_t)g.seed >> 8);        /* asr.l #8 */
    return (uint16_t)(((scaled & 0xFFFF) * range) >> 16);      /* mulu.w, swap */
}

/* middle94.asm:Sroot. d0.l -> d0 = its square root. Up to $640 by subtracting odd numbers, up to
 * $F00000 by Newton steps from v/256 (at most ten), above that by bisection. */
uint16_t Sroot(uint32_t v)
{
    if (v == 0) return 0;
    if (v <= 0x640) {
        uint16_t d0 = (uint16_t)v, d1 = 0xFFFF;
        int borrow;
        do { d1 += 2; borrow = d1 > d0; d0 -= d1; } while (!borrow);
        return d1 >> 1;
    }
    if (v > 0xF00000) {
        uint16_t lo = 0, hi = 0xFFFF;
        for (;;) {
            uint16_t mid = (uint16_t)(((uint32_t)lo + hi) >> 1);     /* add.w then roxr.w: 17-bit sum */
            if (mid == lo) return lo;
            if (v >= (uint32_t)mid * mid) lo = mid; else hi = mid;
        }
    }
    uint16_t d1 = (uint16_t)((v >> 8) + 2);
    for (int reps = 9; reps >= 0; reps--) {
        uint16_t d2 = d1;
        d1 = (uint16_t)m68k_divu(v, d2);                 /* overflow leaves v: its low word */
        d1 = (uint16_t)((uint16_t)(d1 + d2) >> 1);
        if (d1 == d2) break;
    }
    return d1;
}

/* middle94.asm:sfx. Word on the stack: start sound effect n (remembered in lastsfx), none if
 * negative. */
void sfx(int16_t n)
{
    if (n < 0) return;
    g.lastsfx = (uint16_t)n;
    play_sfx_or_music_track((uint16_t)n);
}

/* middle94.asm:song. Word on the stack: start song n, none if negative. */
void song(int16_t n)
{
    if (n < 0) return;
    play_sfx_or_music_track((uint16_t)n);
}

/* middle94.asm:waitx. d0 = frames: wait that many vblanks or until a button is pressed on any
 * pad (pads 3 and 4 in four way play). waitxpad = the buttons held. Returns the last ReadJoy
 * results (d0 = Vcount when the time ran out). */
void waitx(uint16_t frames, uint16_t *d0, uint16_t *d1, uint16_t *d2, uint16_t *d3)
{
    g.waitxpad = 0;
    g.Vcount = (uint16_t)-frames;
    for (;;) {
        ReadJoy1(d0, d1, d2, d3);
        g.waitxpad = *d3;
        if (*d1) return;
        ReadJoy2(d0, d1, d2, d3);
        g.waitxpad |= *d3;
        if (*d1) return;
        if (g.FourWayPlay) {
            ReadJoy3(d0, d1, d2, d3);
            g.waitxpad |= *d3;
            if (*d1) return;
            ReadJoy4(d0, d1, d2, d3);
            g.waitxpad |= *d3;
            if (*d1) return;
        }
        *d0 = g.Vcount;
        wait_vcount();
        if ((int16_t)*d0 >= 0) return;
    }
}

/* middle94.asm:waitxsr. waitx for the zamboni crossing: d0 = frames. Each frame moves the zamboni,
 * runs the crowd and arena animation and redraws; a button opens the menu (HandleMenuInput),
 * which either ends the wait (d1 bit 7 set) or resumes it with at least 120 frames left.
 * Returns d1. */
uint16_t waitxsr(uint16_t frames)
{
    int16_t keep_elapsed = elapsed;
    uint16_t d0, d1, d2, d3;
    g.Vcount = (uint16_t)-frames;
    for (;;) {
        if (g.Zamx <= 0x708) g.Zamx++;
        elapsed = 1;
        updatecrowdf();
        RunArenaAnim();
        g.pausepad = 0;
        g.sflags &= (uint16_t)~HI(sfpj);
        ReadJoy1(&d0, &d1, &d2, &d3);
        ProcessInputWithRepeat(&d1, d2, d3);
        int key = d1 != 0;
        if (!key) {
            g.pausepad = 0;
            g.sflags |= HI(sfpj);
            ReadJoy2(&d0, &d1, &d2, &d3);
            ProcessInputWithRepeat(&d1, d2, d3);
            key = d1 != 0;
        }
        if (!key && g.FourWayPlay) {
            g.sflags &= (uint16_t)~HI(sfpj);
            g.pausepad = 3;
            ReadJoy3(&d0, &d1, &d2, &d3);
            ProcessInputWithRepeat(&d1, d2, d3);
            key = d1 != 0;
            if (!key && g.FourWayPlay) {
                g.sflags |= HI(sfpj);
                g.pausepad = 4;
                ReadJoy4(&d0, &d1, &d2, &d3);
                ProcessInputWithRepeat(&d1, d2, d3);
                key = d1 != 0;
            }
        }
        if (key) {
            int16_t left = (int16_t)g.Vcount;
            if (left >= -0x78) left = -0x78;
            if (HandleMenuInput(&d1)) { d1 |= 0x80; break; }
            g.Vcount = (uint16_t)left;
        }
        setvideo();
        d0 = g.Vcount;
        wait_vcount();
        if ((int16_t)d0 >= 0) break;
    }
    elapsed = keep_elapsed;
    return d1;
}

/* middle94.asm:waitjoy. Wait a frame at a time until a button is pressed on any pad -> d1 = the
 * new presses. */
uint16_t waitjoy(void)
{
    uint16_t d1;
    do {
        wait_vcount();
        d1 = orjoy();
    } while (d1 == 0);
    return d1;
}

/* middle94.asm:orjoy. -> d1 = the new presses of pads 1 and 2 (all four in four way play). */
uint16_t orjoy(void)
{
    uint16_t d0, d1, d2, d3, any;
    if (!g.FourWayPlay) {
        ReadJoy1(&d0, &any, &d2, &d3);
        ReadJoy2(&d0, &d1, &d2, &d3);
        return d1 | any;
    }
    uint16_t p1, p2, p3;
    ReadJoy1(&d0, &p1, &d2, &d3);
    ReadJoy2(&d0, &p2, &d2, &d3);
    ReadJoy3(&d0, &p3, &d2, &d3);
    ReadJoy4(&d0, &d1, &d2, &d3);
    return d1 | p3 | p2 | p1;
}

/* middle94.asm:nodiag. d1 = new presses, d3 = held: when a diagonal is held, drop the new
 * direction presses (d1 bits 0-3). */
void nodiag(uint16_t *d1, uint16_t d3)
{
    uint16_t dir = d3 & 0xF;
    if (dir == 0) return;
    for (int b = 3; b >= 0; b--) if (dir == (1u << b)) return;
    *d1 &= 0xFFF0;
}

/* middle94.asm:ProcessInputWithRepeat. nodiag, then key repeat: holding the buttons d3 unchanged
 * (d2 = 0) for 16 frames, then every 5th frame, presses them again (d1 = d3). */
void ProcessInputWithRepeat(uint16_t *d1, uint16_t d2, uint16_t d3)
{
    nodiag(d1, d3);
    if (d3 == 0) return;
    if (d2 != 0) { g.repeatdelayframes = 0xF; return; }
    if ((int16_t)--g.repeatdelayframes >= 0) return;
    g.repeatdelayframes = 4;
    *d1 = d3;
}

/* middle94.asm:ReadJoy1. Pad 1 (PadRaw1). */
void ReadJoy1(uint16_t *d0, uint16_t *d1, uint16_t *d2, uint16_t *d3)
{
    uint16_t last = g.lj1; read_pad(g.PadRaw1, &last, d0, d1, d2, d3); g.lj1 = last;
}
/* middle94.asm:ReadJoy2. Pad 2 (PadRaw2). */
void ReadJoy2(uint16_t *d0, uint16_t *d1, uint16_t *d2, uint16_t *d3)
{
    uint16_t last = g.lj2; read_pad(g.PadRaw2, &last, d0, d1, d2, d3); g.lj2 = last;
}
/* middle94.asm:ReadJoy3. Four way play pad 3 (PadRaw3). */
void ReadJoy3(uint16_t *d0, uint16_t *d1, uint16_t *d2, uint16_t *d3)
{
    uint16_t last = g.lj3; read_pad(g.PadRaw3, &last, d0, d1, d2, d3); g.lj3 = last;
}
/* middle94.asm:ReadJoy4. Four way play pad 4 (PadRaw4). */
void ReadJoy4(uint16_t *d0, uint16_t *d1, uint16_t *d2, uint16_t *d3)
{
    uint16_t last = g.lj4; read_pad(g.PadRaw4, &last, d0, d1, d2, d3); g.lj4 = last;
}

/* middle94.asm:ReadJoy. d0 = pad byte (active low) -> d0 = direction 0-8 (jdtab) in bits 0-3 and
 * the buttons in bits 4-7, d1 = the buttons down (all 8 bits). */
void ReadJoy(uint8_t pad, uint16_t *d0, uint16_t *d1)
{
    uint8_t down = (uint8_t)~pad;
    *d0 = (uint16_t)((down & 0xF0) | rom_ptr(SYM_jdtab)[down & 0xF]);
    *d1 = down;
}

/* middle94.asm:DoDMApro. DoDMA protected from the vblank (dfng). */
void DoDMApro(uint32_t src, uint16_t words, uint16_t vram)
{
    uint16_t saved = g.disflags;
    g.disflags |= HI(dfng);
    DoDMA(src, words, vram);
    g.disflags = saved;
}

/* middle94.asm:DoDMA. a0 = source, d0 = words, d1 = VRAM address: DMA to VRAM. A source that
 * crosses a 64K boundary goes as two transfers. */
void DoDMA(uint32_t src, uint16_t words, uint16_t vram)
{
    uint32_t end = (uint32_t)(uint16_t)(words * 2) + (uint16_t)src;
    if (end > 0xFFFF && (uint16_t)end != 0) {
        uint16_t over = (uint16_t)((uint16_t)end >> 1);
        uint16_t first = (uint16_t)(words - over);
        dma_68k(src, first, vram);
        uint16_t bytes = (uint16_t)(first * 2);
        vram = (uint16_t)(vram + bytes);
        src += (uint32_t)(int32_t)(int16_t)bytes;
        words = over;
    }
    dma_68k(src, words, vram);
}

/* middle94.asm:DoDMA_nd2. d0 = words, d1 = VRAM destination, d2 = VRAM source: VRAM to VRAM
 * copy by DMA, protected from the vblank. */
void DoDMA_nd2(uint16_t words, uint16_t dst, uint16_t src)
{
    uint16_t saved = g.disflags;
    g.disflags |= HI(dfng);
    vdp_ctrl16(SYM_VDPR_MODE2 + 0x54);
    vdp_ctrl16(SYM_VDPR_AUTOINC + 0x01);
    vdp_ctrl16((uint16_t)(0x9300 | (words & 0xFF)));
    vdp_ctrl16((uint16_t)(0x9400 | (words >> 8)));
    vdp_ctrl16((uint16_t)(0x9500 | (src & 0xFF)));
    vdp_ctrl16((uint16_t)(0x9600 | (src >> 8)));
    vdp_ctrl16(SYM_VDPR_DMASRCH + 0xC0);
    g.dmaram = (uint32_t)(dst & 0x3FFF) << 16 | (dst >> 14) | 0xC0;
    z80_busreq(0x100);
    while (z80_busack() & 1) {}
    vdp_ctrl16((uint16_t)(g.dmaram >> 16));
    vdp_ctrl16((uint16_t)g.dmaram);
    WaitDMA();
    vdp_ctrl16(SYM_VDPR_MODE2 + 0x64);
    vdp_ctrl16(SYM_VDPR_AUTOINC + 0x02);
    z80_busreq(0);
    g.disflags = saved;
}

/* middle94.asm:DoFill. d0 = words, d1 = VRAM address, d2 = word: fill VRAM through the data
 * port, two words a pass (through dmaram). */
void DoFill(uint16_t words, uint16_t vram, uint16_t tile)
{
    vdp_ctrl(vram_write_cmd(vram));
    uint32_t pair = (uint32_t)tile << 16 | tile;
    uint16_t n = (uint16_t)((words >> 1) - 1);
    do {
        g.dmaram = pair;
        vdp_data((uint16_t)(g.dmaram >> 16));
        vdp_data((uint16_t)g.dmaram);
    } while (n-- != 0);
}

/* middle94.asm:WaitDMA. Spin while the VDP status says a DMA is running. */
void WaitDMA(void)
{
    while (vdp_status() & 2) {}
}

/* middle94.asm:setVram. d0 = colour: fade every colour to it, then setVram_0. */
void setVram(uint16_t colour)
{
    for (int i = 0; i < 0x40; i++) g.palfadenew[i] = colour;
    g.palcount = 0x18;
    forcefade();
    setVram_0();
}

/* middle94.asm:setVram_0. Clear VRAM and set the VDP registers from disflags (32 / 40 columns),
 * Map1col (64 or 128 wide), VmMAP1-3, VSPRITES and VSCRLPM; no window, whole-screen scroll. */
void setVram_0(void)
{
    uint16_t saved = g.disflags;
    g.disflags |= HI(dfng);
    vdp_ctrl16(SYM_VDPR_AUTOINC + 0x02);
    Vmaddr(0);
    for (int i = 0; i < 0x4000; i++) vdp_data32(0);
    uint16_t mode4 = 0x8C00;                          /* 8 for shadow mode */
    if (!(g.disflags & HI(df32c))) mode4 |= 0x81;
    vdp_ctrl16(mode4);                                 /* 40 column mode, no interlace */
    vdp_ctrl16(SYM_VDPR_MODE1 + 0x04);
    vdp_ctrl16(SYM_VDPR_MODE2 + 0x64);
    vdp_ctrl16(g.maps[0].colshift == 6 ? 0x9001 : 0x9003);     /* playfield 64x32 or 128x32 */
    vdp_ctrl16((uint16_t)(0x8200 | ((g.maps[0].vram >> 8 >> 2) & 0x38)));
    vdp_ctrl16((uint16_t)(0x8400 | (g.maps[1].vram >> 8 >> 5)));
    vdp_ctrl16((uint16_t)(0x8300 | ((g.maps[2].vram >> 8 >> 2) & 0x3E)));
    vdp_ctrl16((uint16_t)(0x8500 | (g.VSPRITES >> 8 >> 1)));
    vdp_ctrl16((uint16_t)(0x8D00 | (g.VSCRLPM >> 8 >> 2)));
    vdp_ctrl16(SYM_VDPR_WINX);                         /* no window plane */
    vdp_ctrl16(SYM_VDPR_WINY);
    vdp_ctrl16(SYM_VDPR_BGCOL);                        /* colour 0 is the border */
    vdp_ctrl16(SYM_VDPR_MODE3);                        /* whole-screen scroll */
    vdp_ctrl(VSRAM_W);
    vdp_data32(0);
    g.disflags = saved;
}

/* middle94.asm:Vmaddr. d0 = VRAM address: set the VDP to write there (a0 = the data port). */
void Vmaddr(uint16_t vram)
{
    vdp_ctrl(vram_write_cmd(vram));
}

/* ---- tile graphics ---------------------------------------------------------------------- */

/* middle94.asm:dobitmap. a0 = palettes, a1 = map, a2 = tiles, d0/d1 = start x/y in the map,
 * d2/d3 = width/height, d4 = first char, d5 = which of the four palettes to load (bits 0-3).
 * Copy the chosen palettes to palfadenew, the map rectangle to the screen at printx/printy
 * (char + d4, palette bits of printa), then the tiles unless sflags6 bit 0. -> d4 = next char. */
uint16_t dobitmap(uint32_t pal, uint32_t map, uint32_t tiles, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t start, uint16_t palbits)
{
    uint16_t keep_y = g.printy, saved = g.disflags;
    g.disflags |= HI(dfng);
    for (unsigned c = 0; palbits; palbits >>= 1, pal += 0x20, c += 0x10) {   /* the palettes of palbits */
        if (!(palbits & 1)) continue;
        if (c < 0x40) for (int i = 0xF; i >= 0; i--) g.palfadenew[c + i] = m16(pal + 2 * i);
        else for (int i = 0x1F; i >= 0; i--) w8(rp_add(RAM(palfadenew), (int32_t)(2 * c + i)), 0, m8(pal + i));   /* (past the four: on, as the 68000) */
    }
    uint16_t attr = g.printa & 0xF800;
    uint32_t m = map;
    uint16_t row = y, rows = h;
    do {
        xyVmMap();
        uint16_t off = (uint16_t)(((uint16_t)(row * m16(m)) + x) << 1);
        uint16_t cols = w;
        do {
            vdp_data((uint16_t)((m16(m + 4 + (int16_t)off) + start) ^ attr));
            off += 2;
        } while (--cols != 0);
        g.printy++;
        row++;
    } while (--rows != 0);
    if (!(g.sflags6 & HI(0))) start = DecompressGraphicsPlain(tiles, start);
    g.disflags = saved;
    g.printy = keep_y;
    return start;
}
