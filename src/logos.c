/* logos94.asm: team logo palettes (data in the ROM), LeadSong, ClearPenalties, HotColdIcon. */
#include "game.h"
#include "rom.h"
#include "symbols_gen.h"
#include "ram.h"
#include "mem.h"

/* logos94.asm:LeadSong. Outside a shootout, when one team leads: choose the lead song (SongIndex
 * 1 when the home team leads, 4 when the away team does) and flag it in sflags8 bit 6. The
 * sflags2 bits it tests on the way are put back on exit. */
void LeadSong(void)
{
    uint16_t saved = g.sflags2;
    if (g.gmode2 & HI(1)) { LeadSongExit(saved); return; }
    struct Team *leader = team_struct(1);
    int16_t diff = (int16_t)(team_struct(0)->ap - team_struct(1)->ap);
    if (diff == 0) { LeadSongExit(saved); return; }
    if (diff < 0) {
        if (!(g.sflags2 & HI(6))) { ClearLeadSong(); g.sflags2 |= HI(6); }
    } else {
        leader = team_struct(0);
        if (g.sflags2 & HI(6)) { ClearLeadSong(); g.sflags2 &= (uint16_t)~HI(6); }
    }
    int was = (g.sflags2 & HI(5)) != 0;
    g.sflags2 |= HI(5);
    if (was) { LeadSongExit(saved); return; }
    g.HmTeam = g.HomeTeam;
    g.SongIndex = leader == team_struct(0) ? 1 : 4;
    ChooseSong();
    g.sflags8 |= 0x40;
    LeadSongExit(saved);
}

/* logos94.asm:ClearLeadSong. */
void ClearLeadSong(void) { g.sflags2 &= (uint16_t)~HI(5); }

/* logos94.asm:LeadSongExit. LeadSong's exit: sflags2 as it was on entry. */
void LeadSongExit(uint16_t sflags2) { g.sflags2 = sflags2; }

/* logos94.asm:ClearPenalties. No penalties: the counters, PenBuf and both teams' slots. */
void ClearPenalties(void)
{
    g.PBnum = 0;
    g.Penaltytimer = 0;
    g.Pencntdwn = 0;
    ram_fill(RAM(PenBuf), 0, 0x44);                       /* 17 longs: PenBuf and the word after */
    clrTmPdst(team_struct(0));
    clrTmPdst(team_struct(1));
}

/* logos94.asm:clrTmPdst. Team a0: all 26 roster slots on the bench (-2), then the end word -1. */
void clrTmPdst(struct Team *tm)
{
    for (int i = 0; i < 26; i++) tm->pdst[i] = 0xFFFE;
    tm->penbox[0] = tm->penbox[1] = 0xFF;                 /* (the end word -1 after them: the penalty box empty) */
}

/* logos94.asm:HotColdIcon. Team a2: the icon by the name of iconplayer (x 2 away, $20 home,
 * y $19): the hot icon for the team's hot player, the cold one for its cold player, else clear
 * it. */
void HotColdIcon(struct Team *tm)
{
    int home = tm == team_struct(0);
    g.iconx = home ? 0x20 : 2;
    uint32_t icon;
    uint16_t chars;
    if ((home ? g.homehotplayer : g.awayhotplayer) == g.iconplayer) {
        chars = g.hoticonchars; icon = ART(HotIconMap);
    } else if ((home ? g.homecoldplayer : g.awaycoldplayer) == g.iconplayer) {
        chars = g.coldiconchars; icon = ART(ColdIconMap);
    } else {
        g.printx = g.iconx;
        g.printy = 0x19;
        eraser(8, 3, 0x7FF);
        return;
    }
    g.printx = g.iconx;
    g.printy = 0x19;
    const uint8_t *hdr = rom_ptr(icon);
    uint32_t pal = icon + rd32(hdr), map = icon + rd32(hdr + 4);
    const uint8_t *m = rom_ptr(map);
    dobitmap(pal, map, SYM_NoTiles, 0, 0, rd16(m), rd16(m + 2), chars, 0);
}

