/* title94.asm: the title screen and scrolling credits. */
#include <string.h>
#include "game.h"
#include "hal.h"
#include "rom.h"
#include "symbols_gen.h"
#include "title_strings_gen.h"

struct Picture { uint32_t pal, map, tiles; uint16_t w, h; };
static struct Picture picture(uint32_t img)
{
    struct Picture p = { img + rd32(rom_ptr(img)), img + rd32(rom_ptr(img + 4)), img + 8, 0, 0 };
    p.w = m16(p.map);
    p.h = m16(p.map + 2);
    return p;
}

/* the screens' VRAM layout: planes at $C000 / $A000, window $F000, sprites $F800, hscroll $FC00 */
static void screen_layout(void)
{
    g.maps[2].colshift = 5;
    g.maps[1].vram = 0xA000;
    g.maps[1].colshift = 7;
    g.maps[0].vram = 0xC000;
    g.maps[0].colshift = 7;
    g.maps[2].vram = 0xF000;
    g.VSPRITES = 0xF800;
    g.VSCRLPM = 0xFC00;
}

/* title94.asm:TitleScreen. The title screen (TitleScreenImg, the NHL shield, the NHLPA logo,
 * the title) with its own vblank (VBlank_TitleScreen: a line scroll), the title song, then the
 * credits (Credits, then CreditsList) scrolling up a row at a time until they end or Start. */
void TitleScreen(void)
{
    static const uint8_t font[8] = { 0x0D, 0x10, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
    cpu_set_sr(0x2700);
    uint16_t hi = vdp_hv16();                             /* the seed from the HV counter: its high word first */
    g.seed = (uint32_t)hi << 16 | vdp_hv16();
    g.vbint = SYM_VBlank_TitleScreen;
    g.disflags |= HI(df32c);
    screen_layout();
    setVram(0);
    vdp_ctrl16(SYM_VDPR_WINX);
    vdp_ctrl16(SYM_VDPR_WINY + 0x17);
    vdp_ctrl16(SYM_VDPR_MODE3 + 3);                       /* line hscroll */
    g.Vscroll = 0;
    printz(str_TitleScreen_1, sizeof str_TitleScreen_1);
    eraser(0x80, 0x20, 0x7FF);
    printz(str_TitleScreen_2, sizeof str_TitleScreen_2);
    eraser(0x80, 0x20, 0x7FF);
    printz(str_TitleScreen_3, sizeof str_TitleScreen_3);
    struct Picture p = picture(ART(TitleScreenImg));
    g.TempWord1 = dobitmap(p.pal, p.map, p.tiles, 0, 0, p.w, 0x17, 0, 0xF);
    printz(str_TitleScreen_4, sizeof str_TitleScreen_4);   /* its last 5 rows on plane B */
    g.sflags6 |= HI(0);
    dobitmap(p.pal, p.map, p.tiles, 0, 0x17, p.w, 5, 0, 0);
    g.sflags6 &= (uint16_t)~HI(0);
    printz(str_TitleScreen_5, sizeof str_TitleScreen_5);
    p = picture(ART(NHLShieldImg));
    uint16_t c = dobitmap(p.pal, p.map, p.tiles, 0, 0, p.w, p.h, g.TempWord1, 8);
    printz(str_TitleScreen_6, sizeof str_TitleScreen_6);
    p = picture(ART(PAlogoImg));
    c = dobitmap(p.pal, p.map, p.tiles, 0, 0, p.w, p.h, c, 0);
    printz(str_TitleScreen_7, sizeof str_TitleScreen_7);
    p = picture(ART(TitleImg));
    c = dobitmap(p.pal, p.map, p.tiles, 0, 0, p.w, p.h, c, 8);
    g.palfadenew[0] = 0;
    g.fomen[0].frame = 0;                                 /* (clr.l: and its attribute) */
    g.fomen[0].attribute = 0;
    g.fomen[1].frame = 0;
    g.smallfontchars = c;
    DecompressGraphicsWithRemap(ART(SmallFontMap) + 8, c, rom_find(SYM_TitleScreen, SYM_CreditsPrintRow, font, 8));
    printz(str_TitleScreen_8, sizeof str_TitleScreen_8);
    CreditsPrintRow(SYM_Credits);
    g.Vscroll += 0x20;
    g.clampcounter = 0x104;
    g.playoffspritex = 0x120;
    g.playoffspritey = 0xD0;
    g.asv = 0;
    g.DispAttribCtr = 0xFFCE;
    g.palcount = 0x20;
    song(SYM_SngTitle);
    CreditsLineScroll();
    cpu_set_sr(0x2500);
    do if (CreditsWait()) return; while (g.clampcounter != 0);
    for (int i = 0; i <= 0x3C; i++) if (CreditsWait()) return;
    printz(str_TitleScreen_9, sizeof str_TitleScreen_9);
    uint32_t a1 = SYM_CreditsList;
    do {
        a1 = CreditsPrintRow(a1);
        a1 += (uint32_t)(int16_t)rd16(rom_ptr(a1));       /* past the row's end */
        for (int i = 0; i <= 0x27; i++) {
            if (CreditsWait() || CreditsWait()) return;
            g.Vscroll++;
        }
        for (int i = 0; i <= 0x78; i++) if (CreditsWait()) return;
    } while ((int16_t)rd16(rom_ptr(a1 + 2)) >= 0);
}

/* title94.asm:CreditsPrintRow. Clear the six rows below the screen (Vscroll / 8 + $1C) and print
 * the Strings from a1 there, one a row, each centred, until one that starts with a control
 * code -> a1 at that one. */
uint32_t CreditsPrintRow(uint32_t a1)
{
    uint16_t row = (uint16_t)((((int16_t)g.Vscroll >> 3) + 0x1C) & 0x1F);
    g.printy = row;
    g.printx = 0;
    eraser(0x20, 6, 0x7FF);
    g.printy = row;
    do {
        g.printx = (uint16_t)(0x11 - ((int16_t)m16(a1) >> 1));
        a1 = print(a1);
        g.printy = (g.printy + 1) & 0x1F;
    } while ((int16_t)m16(a1 + 2) >= 0);
    return a1;
}

/* title94.asm:CreditsWait. Wait for the vblank, count clampcounter down by 2 (to 0) and read
 * the pads -> Start pressed (the 68k then returns from its caller too). */
int CreditsWait(void)
{
    uint16_t v = g.vcount;
    while (g.vcount == v) hal_wait_vblank();
    g.clampcounter -= 2;
    if ((int16_t)g.clampcounter < 0) g.clampcounter = 0;
    return (orjoy() & 0x80) != 0;
}

/* title94.asm:VBlank_TitleScreen. The title's vblank: unless dfng, the line scroll table (at
 * SortCords) and Vscroll, the sprites, the fade; then vcount, CreditsScrollStep and the music. */
void VBlank_TitleScreen(void)
{
    if (!(g.disflags & HI(dfng))) {
        DoDMA(RAM_BASE + RAM_OFF_SortCords, 0x1C0, g.VSCRLPM);
        vdp_ctrl(VSRAM_W);
        vdp_data(g.Vscroll);
        if (g.Sattsize) {
            uint16_t n = g.Sattsize;
            g.Sattsize = 0;
            DoDMA(ram_addr(g.Satt), n, g.VSPRITES);
        }
        cramfade();
    }
    g.vcount++;
    CreditsScrollStep();
    p_music_vblank();
}

/* title94.asm:CreditsLineScroll. The title's line scroll table at SortCords: $51 lines of
 * plane B from $FD80 (one less every other line), then $61 from $100 (4 more a line); the
 * step every $50 frames by $14. */
void CreditsLineScroll(void)
{
    g.Hscroll = 0x14;
    g.TempWord2 = 0x50;
    rp a0 = RAM(SortCords);
    uint32_t d1 = 0xFD80;
    for (int16_t d0 = 0x50; d0 >= 0; d0--, a0 = rp_add(a0, 4)) {
        w32(a0, 0, d1);
        if (!(d0 & 1)) d1 = (d1 & 0xFFFF0000) | (uint16_t)(d1 - 1);
    }
    d1 = 0x100;
    for (int i = 0; i <= 0x60; i++, a0 = rp_add(a0, 4), d1 += 4) w32(a0, 0, d1);
}

/* title94.asm:CreditsScrollStep. Every TempWord2 frames (then every frame) the lines of the
 * line scroll table move: plane B's of the first $51 by + Hscroll until they wrap to 0, the
 * next $66 by - Hscroll down to 0. */
void CreditsScrollStep(void)
{
    if ((int16_t)--g.TempWord2 >= 0) return;
    g.TempWord2 = 0;
    rp a0 = RAM(SortCords);
    for (int16_t d0 = 0xDF; d0 >= 0; d0--) {
        a0 = rp_add(a0, 4);
        if (d0 <= 0x28) continue;
        rp b = rp_add(a0, -2);
        if (d0 >= 0x8F) {
            uint16_t v = r16(b, 0);
            if (v == 0) continue;
            v = (uint16_t)(v + g.Hscroll);
            w16(b, 0, v);
            if ((v & 0xFC00) != 0xFC00) w16(b, 0, 0);
        } else {
            uint16_t v = (uint16_t)(r16(b, 0) - g.Hscroll);
            w16(b, 0, (int16_t)v < 0 ? 0 : v);
        }
    }
}

