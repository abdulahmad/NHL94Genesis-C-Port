/* shootoutpath94.asm: saved line data, win records, and the shootout skate paths. */
#include <string.h>
#include "game.h"
#include "rom.h"
#include "symbols_gen.h"
#include "ram.h"
#include "mem.h"

static inline int16_t abs16(int16_t v) { return v < 0 ? (int16_t)-v : v; }   /* sub / bpl / neg */

/* shootoutpath94.asm:ReadLineData. Read the $100 bytes at save RAM LineDataBase to databuffer;
 * sflags bit 4 cleared, lastsfx = none, recbpr = M68K_RAM. */
void ReadLineData(void)
{
    ReadSRAM(SYM_LineDataBase, 0x100, ram_addr(g.databuffer));
    g.sflags &= (uint16_t)~HI(sfwrap);
    g.lastsfx = 0xFFFF;
    g.recbpr = RAM_BASE;
}

/* shootoutpath94.asm:WriteLineData. databuffer back to save RAM at LineDataBase, the checksum; then as
 * ReadLineData after. */
void WriteLineData(void)
{
    WriteSRAM(SYM_LineDataBase, 0x100, ram_addr(g.databuffer));
    MakeSRAMChecksum();
    g.sflags &= (uint16_t)~HI(sfwrap);
    g.lastsfx = 0xFFFF;
    g.recbpr = RAM_BASE;
}

/* shootoutpath94.asm:ClearWinRecords. The win records (bytes 8-$B of the 8 records of $10 at ThreeStars)
 * cleared, the $80 bytes written back at TeamRecBase (WriteSRAM, MakeSRAMChecksum). */
void ClearWinRecords(void)
{
    for (int i = 0; i < 8; i++) memset(g.ThreeStars + i * 0x10 + 8, 0, 4);
    WriteSRAM(SYM_TeamRecBase, 0x80, ram_addr(g.ThreeStars));
    MakeSRAMChecksum();
}

/* shootoutpath94.asm:PSandSOpassdir. Penalty shot / shootout: passdir = the direction at the end of
 * the skate path, turned (passdirlist) for the bottom net. */
void PSandSOpassdir(struct SortObj *o)
{
    uint16_t d = g.sopathdir;
    if (!(o->pflags & 0x80)) d = rd16(rom_ptr(SYM_passdirlist) + (int16_t)(d << 1));
    g.passdir = d;
}

/* shootoutpath94.asm:StartShootoutPath. Pick one of the seven shootout skate paths at random and
 * start it (NextPathPoint, for skater a3). */
void StartShootoutPath(struct SortObj *o)
{
    uint16_t p;
    do p = randomd0(7); while (p > 6);
    g.sopath = p;
    g.sflags7 &= (uint8_t)~8;
    g.sopathpoint = 0xFFFF;
    g.sopathx = 0;
    NextPathPoint(o);
}

/* shootoutpath94.asm:NextPathPoint. Step the shootout path sopath to its next point (x turned for a
 * left-handed skater a3); at the end marker ($80) set sflags7 bit 3, sopathend and sopathdir. */
void NextPathPoint(struct SortObj *o)
{
    if ((g.sopathx >> 8) == 0x80) return;
    g.sopathpoint++;
    const uint8_t *path = rom_ptr(rd32(rom_ptr(SYM_ShootoutPaths + (int16_t)(g.sopath << 2))));
    const uint8_t *pt = path + (int16_t)(g.sopathpoint << 2);
    g.sopathx = rd16(pt);
    if (o->handed[0] & 1) g.sopathx = (uint16_t)-g.sopathx;
    g.sopathy = rd16(pt + 2);
    if (pt[4] == 0x80) {
        g.sflags7 |= 8;
        g.sopathend = pt[5];
        g.sopathdir = rd16(pt + 6);
    }
}

/* shootoutpath94.asm:SkatePath. Shootout skate path: d0 / d1 = the current point (sopathx / sopathy,
 * mirrored for the bottom net in the test); within $A of it (or $12 while standing) go on to
 * the next one. Unchanged once the path is over. */
void SkatePath(int16_t *x, int16_t *y, struct SortObj *o)
{
    if ((g.sopathx >> 8) == 0x80) return;
    int16_t dx = (int16_t)g.sopathx, dy = (int16_t)g.sopathy;
    if (!(o->pflags & 0x80)) { dx = (int16_t)-dx; dy = (int16_t)-dy; }
    int standing = o->Xvel == 0 && o->Yvel == 0;
    dx = abs16((int16_t)(dx - o->Xpos));
    if ((standing && dx <= 0x12) || dx <= 0xA) {
        dy = abs16((int16_t)(dy - o->Ypos));
        if ((standing && dy <= 0x12) || dy <= 0xA) NextPathPoint(o);
    }
    *x = (int16_t)g.sopathx;
    *y = (int16_t)g.sopathy;
}

/* shootoutpath94.asm:ShootoutShootCheck. Shootout skater a3 with the puck -> Z: hold the shot (before
 * shootoutclock 4, before the path starts, then until the path end is near or the puck has
 * stopped after $F). Z clear = shoot now; also when it is not the shootout carrier. */
int ShootoutShootCheck(struct SortObj *o)
{
    if (!(g.gmode2 & HI(1))) return 0;
    if (o->SCnum != (int16_t)puck_carrier()) return 0;
    if ((int16_t)g.shootoutclock <= 3) return 1;
    if ((g.sopathx >> 8) == 0x80) return 1;
    if (!(g.sflags7 & 8)) {
        if (g.puckvx != 0 || g.puckvy != 0) return 0;
        return (int16_t)g.shootoutclock < 0xF;
    }
    int16_t x = (int16_t)g.sopathx, y = (int16_t)g.sopathy;
    if (!(o->pflags & 0x80)) { x = (int16_t)-x; y = (int16_t)-y; }
    x = abs16((int16_t)(x - o->Xpos));
    y = abs16((int16_t)(y - o->Ypos));
    if (x > (int16_t)g.sopathend || y > (int16_t)g.sopathend) return 0;
    return 1;
}

