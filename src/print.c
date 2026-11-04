/* print94.asm: text printing. VRAM address from printx / printy, small and big font
 * strings, frames, numbers and times, and the font tile uploads. */
#include "game.h"
#include "hal.h"
#include "rom.h"
#include "symbols_gen.h"

/* ---- printing --------------------------------------------------------------------------- */

/* the map selected by printm (a byte offset from VmMap1: 0, 4 or 8, though the 68000 takes any):
 * its VRAM address and row shift */
static struct Plane print_map(void)
{
    int16_t k = (int16_t)g.printm;
    if (k == 0 || k == 4 || k == 8) return g.maps[k / 4];
    rp m = rp_add(RAM(VmMap1), k);                        /* (another offset: the words there) */
    return (struct Plane){ r16(m, 0), r16(m, 2) };
}
/* asl.w Dn: the count is taken mod 64 and 16 or more clears the word */
static uint16_t asl_w(uint16_t v, uint16_t n) { n &= 63; return n >= 16 ? 0 : (uint16_t)(v << n); }

/* Framer's .tbline: one row of the frame from the three-entry map row at framer word index
 * col: left, w-2 middles, right */
static void frame_row(const uint8_t *map, uint16_t *col, uint16_t base, uint16_t w)
{
    xyVmMap();
    g.printy++;
    vdp_data((uint16_t)(rd16(map + *col) + base));
    *col += 2;
    uint16_t n = (uint16_t)(w - 3);
    do vdp_data((uint16_t)(rd16(map + *col) + base)); while (n-- != 0);
    *col += 2;
    vdp_data((uint16_t)(rd16(map + *col) + base));
    *col += 2;
}

/* the font map a printed character comes from: setup font when sflags6 bit 3 */
static uint16_t font_char(uint8_t c)
{
    const uint8_t *map = rom_ptr(g.sflags6 & HI(3) ? ART(PrintFont2Map) : ART(SmallFontMap));
    map += rd32(map + 4);
    return rd16(map + 4 + 2 * c);
}

/* the address past a String as the game holds it: RAM's from $FFFF0000, the cartridge's 24 bits */
static uint32_t past(struct Text t) { uint32_t a = t.at & 0xFFFFFF; return is_ram(a) ? RAM_BASE + (a & 0xFFFF) : a; }

/* the String length word and padding that end PushTime / PushNumber: the text runs from p to
 * end; a pad byte keeps the String even. -> its address */
static uint32_t push_string(uint8_t *p, const uint8_t *end)
{
    uint16_t len = (uint16_t)(end - p + 2);
    if (len & 1) { *--p = 0; len++; }
    p -= 2;
    wr16(p, len);
    return ram_addr(p);
}

/* print94.asm:xyVmMap. Set the VDP to write at printx/printy on the map printm. */
void xyVmMap(void)
{
    struct Plane m = print_map();
    uint16_t row = asl_w(g.printy, m.colshift);
    Vmaddr((uint16_t)(((uint16_t)(g.printx + row) << 1) + m.vram));
}

/* print94.asm:eraser. d0/d1 = width/height, d2 = map word: fill the rectangle at
 * printx/printy (printy advances, wrapping at 32 rows). */
void eraser(uint16_t w, uint16_t h, uint16_t attr)
{
    uint16_t saved = g.disflags;
    g.disflags |= HI(dfng);
    do {
        xyVmMap();
        uint16_t n = w;
        do vdp_data(attr); while (--n != 0);
        g.printy = (uint16_t)((g.printy + 1) & 0x1F);
    } while (--h != 0);
    g.disflags = saved;
}

/* print94.asm:Framer. d0/d1 = width/height: draw a frame filled in (framer.map, its tiles at
 * framercset) with the top left corner at printx/printy. */
void Framer(uint16_t w, uint16_t h)
{
    uint16_t saved = g.disflags;
    g.disflags |= HI(dfng);
    uint16_t base = (uint16_t)(g.printa + g.framercset);
    const uint8_t *map = rom_ptr(ART(FramerMap));
    map += rd32(map + 4) + 4;
    uint16_t col = 0;
    frame_row(map, &col, base, w);
    int16_t rows = (int16_t)(h - 3);
    do {
        frame_row(map, &col, base, w);
        col -= 6;
    } while (--rows >= 0);
    col += 6;
    frame_row(map, &col, base, w);
    g.disflags = saved;
}

/* print94.asm:printsmallz. printsmall of the String that follows the call. */
void printsmallz(const uint8_t *s, unsigned n) { (void)n; printsmall_p((struct Text){ s, 0 }); }

uint32_t printsmall(uint32_t str) { return past(printsmall_p((struct Text){ NULL, str })); }

/* print94.asm:ControlCode_SetMapAndPosition. Code -8: attribute, map, x, y. */
void ControlCode_SetMapAndPosition(struct PrintState *st)
{
    ControlCode_SetAttribute(st);
    ControlCode_SetMap(st);
    ControlCode_SetX(st);
    ControlCode_SetY(st);
}

/* print94.asm:ControlCode_SetMap. Code -1: the next byte is the map (1-3) -> printm. */
void ControlCode_SetMap(struct PrintState *st)
{
    uint8_t m = text_next(&st->p);
    st->count--;
    g.printm = (uint16_t)(((m & 3) << 2) - 4);
    xyVmMap();
}

/* print94.asm:ControlCode_SetAttribute. Code -2: the next byte is palette / priority -> printa. */
void ControlCode_SetAttribute(struct PrintState *st)
{
    st->attr = (uint16_t)((text_next(&st->p) & 7) << 13);
    st->count--;
    g.printa = st->attr;
}

/* print94.asm:ControlCode_SetX. Code -3: printx = the next byte. */
void ControlCode_SetX(struct PrintState *st) { g.printx = text_next(&st->p); st->count--; xyVmMap(); }
/* print94.asm:ControlCode_SetY. Code -4: printy = the next byte. */
void ControlCode_SetY(struct PrintState *st) { g.printy = text_next(&st->p); st->count--; xyVmMap(); }
/* print94.asm:ControlCode_AddX. Code -5: printx += the next (signed) byte. */
void ControlCode_AddX(struct PrintState *st) { g.printx = (uint16_t)(g.printx + (int8_t)text_next(&st->p)); st->count--; xyVmMap(); }
/* print94.asm:ControlCode_AddY. Code -6: printy += the next (signed) byte. */
void ControlCode_AddY(struct PrintState *st) { g.printy = (uint16_t)(g.printy + (int8_t)text_next(&st->p)); st->count--; xyVmMap(); }
/* print94.asm:ControlCode_SetFont. Code -7: printfontset = the next byte (char set index). */
void ControlCode_SetFont(struct PrintState *st) { g.printfontset = (uint16_t)(text_next(&st->p) << 1); st->count--; }

/* print94.asm:printz. print the String that follows the call. */
void printz(const uint8_t *s, unsigned n) { (void)n; print_p((struct Text){ s, 0 }); }

uint32_t print(uint32_t str) { return past(print_p((struct Text){ NULL, str })); }

/* print94.asm:FormatAndPrintTime. d0 = period (bits 14-15) and seconds (bits 0-13): print the
 * period label (" 1", " 2", " 3", "OT"), two columns on, the time as m:ss. */
void FormatAndPrintTime(uint16_t v)
{
    Fprintsmall(SYM_PeriodLabelTable, (uint16_t)(v >> 14));
    g.printx += 2;
    print(PushTime(v & 0x3FFF));
}

/* print94.asm:PushTime. d0 = seconds -> a1 = a String " m:ss" (or "mm:ss") in mesarea. */
uint32_t PushTime(uint16_t t)
{
    uint8_t *end = g.mesarea + 30, *p = end;
    uint32_t d0 = m68k_divu((uint32_t)(int32_t)(int16_t)t, 10);
    *--p = (uint8_t)((d0 >> 16) + '0');
    d0 = m68k_divu((uint32_t)(int32_t)(int16_t)d0, 6);
    *--p = (uint8_t)((d0 >> 16) + '0');
    *--p = ':';
    d0 = m68k_divu((uint32_t)(int32_t)(int16_t)d0, 10);
    *--p = (uint8_t)((d0 >> 16) + '0');
    *--p = ' ';
    if ((uint16_t)d0) *p = (uint8_t)(d0 + '0');
    return push_string(p, end);
}

/* print94.asm:PushNumber. d0 -> a1 = a String of it in decimal, no leading zeros. */
uint32_t PushNumber(uint16_t n)
{
    uint8_t *end = g.PushWidthBuf + sizeof g.PushWidthBuf, *p = end;   /* (PushNumberBuf: the text ends there) */
    uint32_t d0 = n;
    do {
        d0 = m68k_divu((uint32_t)(int32_t)(int16_t)d0, 10);
        *--p = (uint8_t)((d0 >> 16) + '0');
    } while ((uint16_t)d0);
    return push_string(p, end);
}

/* print94.asm:PushNumberWidth. d0 = number, d1 = width -> a1 = a String of it right-justified
 * in width columns (spaces before the first digit) at PushWidthBuf. */
uint32_t PushNumberWidth(uint16_t n, uint16_t width)
{
    uint8_t *buf = ram_bytes(RAM(PushWidthBuf), (uint16_t)(width + 3)), *p = buf + 2;
    uint32_t div = 1;
    for (uint16_t i = (uint16_t)(width - 1); i; i--) div = (uint16_t)div * 10u;
    uint8_t fill = ' ';
    uint32_t d0 = n;
    do {
        d0 = m68k_divu((uint32_t)(int32_t)(int16_t)d0, (uint16_t)div);
        if ((uint16_t)d0 == 0 && (uint16_t)div != 1) d0 = (d0 & 0xFFFF0000) | fill;
        else { fill = '0'; d0 = (d0 & 0xFFFF0000) | (uint16_t)(d0 + '0'); }
        *p++ = (uint8_t)d0;
        d0 = d0 >> 16 | d0 << 16;                    /* swap: the remainder goes on */
        div = m68k_divu(div, 10);                    /* divu.w #10 of the whole long */
    } while ((uint16_t)div);
    uint16_t len = (uint16_t)(p - buf);
    if (len & 1) { *p++ = 0; len++; }
    wr16(buf, len);
    return ram_addr(buf);
}

/* print94.asm:appendz. appstring of the String that follows the call onto a3. */
void appendz(const uint8_t *s, unsigned n, uint32_t buf) { (void)n; appstring_p((struct Text){ s, 0 }, buf); }

void appstring(uint32_t src, uint32_t buf) { appstring_p((struct Text){ NULL, src }, buf); }

/* print94.asm:printbigz. printbig of the String that follows the call. */
void printbigz(const uint8_t *s, unsigned n) { (void)n; printbig_p((struct Text){ s, 0 }); }

void printbig(uint32_t str) { printbig_p((struct Text){ NULL, str }); }

/* print94.asm:PrintBigChar. d0 = ASCII: put its big font tiles (bfasciicon: map column, negative
 * for a one-column char) at d4/d5, top and bottom, and advance d4. */
void PrintBigChar(uint8_t c, struct BigPen *pen)
{
    int8_t col = (int8_t)rom_ptr(SYM_bfasciicon)[(int16_t)(c - 0x20)];
    int cols = 2;
    if (col < 0) { col = (int8_t)-col; cols = 1; }
    uint16_t off = (uint16_t)(col << 1);
    const uint8_t *map = rom_ptr(ART(BigFontMap));
    map += rd32(map + 4);
    while (cols--) {
        PutBigTile(rd16(map + 4 + off), pen);
        uint16_t row = (uint16_t)(rd16(map) << 1);
        pen->y++;
        PutBigTile(rd16(map + 4 + (uint16_t)(off + row)), pen);
        pen->y--;
        pen->x++;
        off += 2;
    }
}

/* print94.asm:PutBigTile. d3 = tile: write it plus the pen attribute at d4/d5 on printm. */
void PutBigTile(uint16_t tile, const struct BigPen *pen)
{
    struct Plane m = print_map();
    uint16_t row = asl_w(pen->y, m.colshift);
    Vmaddr((uint16_t)(((uint16_t)(row + pen->x) << 1) + m.vram));
    vdp_data((uint16_t)(tile + pen->attr));
}

/* print94.asm:AddSmallFont. d4 = first char: load the small font tiles there -> d4 = next. */
uint16_t AddSmallFont(uint16_t start)
{
    g.smallfontchars = start;
    return DecompressGraphicsPlain(ART(SmallFontMap) + 8, start);
}

/* print94.asm:AddFramer. d4 = first char: load the framer tiles there -> d4 = next. */
uint16_t AddFramer(uint16_t start)
{
    g.framercset = start;
    return DecompressGraphicsPlain(ART(FramerMap) + 8, start);
}

/* print94.asm:AddTeamBlock. Load the team block tiles at char 2 -> d4 = next char (the roster set's:
 * with banners of its own after the cartridge's). */
uint16_t AddTeamBlock(void)
{
    return DecompressGraphicsPlain(RS(Teamblocksmap) + 8, 2);
}

/* print94.asm:printsmall. Print a String with control codes (negative bytes,
 * ControlCodeJumpTable) at printx/printy/printm with printa; the char set is printfontset
 * (smallfontchars, or setupfontchars when sflags6 bit 3). -> a1 = past the string. */
struct Text printsmall_p(struct Text s)
{
    static void (*const codes[9])(struct PrintState *) = {
        NULL, ControlCode_SetMap, ControlCode_SetAttribute, ControlCode_SetX, ControlCode_SetY,
        ControlCode_AddX, ControlCode_AddY, ControlCode_SetFont, ControlCode_SetMapAndPosition,
    };
    uint16_t saved = g.disflags;
    g.disflags |= HI(dfng);
    uint16_t chars = g.sflags6 & HI(3) ? RAM_OFF_setupfontchars : RAM_OFF_smallfontchars;
    xyVmMap();
    uint16_t len = text_word(&s);
    struct PrintState st = { s, g.printa, (uint16_t)(len - 2) };
    while (st.count-- != 0) {
        int8_t c = (int8_t)text_next(&st.p);
        if (c <= 0) {
            if (codes[-c]) codes[-c](&st);
            continue;
        }
        uint16_t tile;
        if (c == 0x40) tile = 0x7FF;
        else if (c == 0x5E) { g.printx++; xyVmMap(); continue; }
        else tile = (uint16_t)(font_char((uint8_t)c) + m16(RAM_BASE + (chars + g.printfontset)));
        vdp_data((uint16_t)(tile + st.attr));
        g.printx++;
    }
    g.disflags = saved;
    return st.p;
}

/* print94.asm:print. Print a String at printx/printy/printm with printa. A negative byte -$ab
 * starts a new position: map a, palette / priority b, then x and y bytes; $40 is a blank tile,
 * $5E skips a column, 0 is padding. -> a1 = past the string. */
struct Text print_p(struct Text s)
{
    uint16_t saved = g.disflags;
    g.disflags |= HI(dfng);
    xyVmMap();
    uint16_t attr = g.printa;
    uint16_t n = (uint16_t)(text_word(&s) - 2);
    while (n-- != 0) {
        int8_t c = (int8_t)text_next(&s);
        if (c == 0) continue;
        if (c < 0) {
            uint16_t code = (uint16_t)-c;
            attr = (uint16_t)((code << 9) & 0xF800);
            g.printa = attr;
            g.printm = (uint16_t)(((code & 3) << 2) - 4);
            g.printx = (uint16_t)(int8_t)text_next(&s);
            g.printy = (uint16_t)(int8_t)text_next(&s);
            xyVmMap();
            n -= 2;
            continue;
        }
        uint16_t tile;
        if (c == 0x40) tile = 0x7FF;
        else if (c == 0x5E) { g.printx++; xyVmMap(); continue; }
        else tile = (uint16_t)(font_char((uint8_t)c) + (g.sflags6 & HI(3) ? g.setupfontchars : g.smallfontchars));
        vdp_data((uint16_t)(tile + attr));
        g.printx++;
    }
    g.disflags = saved;
    return s;
}

/* print94.asm:appstring. a1 = String, a3 = String in RAM: append the first to the second (the
 * text of a3 ends at its first zero byte after the first character). */
void appstring_p(struct Text src, uint32_t buf)
{
    uint16_t have = m16(buf), more = text_word(&src);
    uint8_t *dst = ram_bytes(ram_at(buf), (uint16_t)((have > 2 ? have : 2) + more)), *a0 = dst + 2;
    int16_t n = (int16_t)(have - 3);
    if (n >= 0) {
        do a0++; while (*a0 != 0 && n-- != 0);
    }
    n = (int16_t)(more - 3);
    if (n < 0) return;
    do {
        uint8_t b = text_next(&src);
        *a0++ = b;
        if (b == 0) a0--;
    } while (n-- != 0);
    if (ram_addr(a0) & 1) *a0++ = 0;
    wr16(dst, (uint16_t)(a0 - dst));
}

/* print94.asm:printbig. Print a String in the big font (two rows high, chars one or two
 * columns wide) at printx/printy; a negative byte starts a new position as in print. printx /
 * printy end past the text. */
struct Text printbig_p(struct Text s)
{
    uint16_t saved = g.disflags;
    g.disflags |= HI(dfng);
    struct BigPen pen = { g.printx, g.printy, (uint16_t)(g.printa + g.BigFontChars) };
    uint16_t n = (uint16_t)(text_word(&s) - 2);
    while (n-- != 0) {
        int8_t c = (int8_t)text_next(&s);
        if (c == 0) continue;
        if (c < 0) {
            uint16_t code = (uint16_t)-c;
            g.printa = (uint16_t)((code << 9) & 0xF800);
            pen.attr = (uint16_t)(g.printa + g.BigFontChars);
            g.printm = (uint16_t)(((code & 3) << 2) - 4);
            g.printx = pen.x = (uint16_t)(int8_t)text_next(&s);
            g.printy = pen.y = (uint16_t)(int8_t)text_next(&s);
            n -= 2;
            continue;
        }
        if (c >= 'a' && c <= 'z') c -= 0x20;
        PrintBigChar((uint8_t)c, &pen);
    }
    g.printx = pen.x;
    g.printy = pen.y;
    g.disflags = saved;
    return s;
}
