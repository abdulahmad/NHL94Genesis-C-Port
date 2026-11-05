/* arena94.asm: arena / faceoff animations and the organ song pick (ChooseSong). */
#include "game.h"
#include "rom.h"
#include "symbols_gen.h"
#include "rosters.h"

/* arena94.asm:RunArenaAnim. Run the arena animation arenaanim (negative: none): the first
 * time load its frame list and graphics from ArenaAnims, then count the frame time down by
 * the elapsed frames and step. */
void RunArenaAnim(void)
{
    if ((int16_t)g.arenaanim < 0) return;
    if (g.arenaspritelist == 0) {
        const uint8_t *e = rom_ptr(SYM_ArenaAnims) + g.arenaanim * 8;
        g.arenaframelist = rd32(e);
        g.arenaspritelist = rd32(e + 4);
        DecompressGraphicsPlain(rd32(e + 4) + 8, g.arenaanimchars);
        g.arenaframeidx = 0;
        NextArenaFrame();
        return;
    }
    g.arenaframetime = (uint16_t)(g.arenaframetime - elapsed);
    if ((int16_t)g.arenaframetime >= 0) return;
    g.arenaframeidx++;
    NextArenaFrame();
}

/* arena94.asm:NextArenaFrame. Read frame arenaframeidx of the list: arenaframe = frame,
 * arenaframetime = time; frame $FF loops to the start, $FE ends the animation. */
void NextArenaFrame(void)
{
    for (;;) {
        const uint8_t *a0 = rom_ptr(g.arenaframelist) + g.arenaframeidx * 2;
        g.arenaframe = (uint16_t)(int8_t)a0[0];
        g.arenaframetime = (uint16_t)(int8_t)a0[1];
        if (g.arenaframe != 0xFFFF) break;
        g.arenaframeidx = 0;
    }
    if (g.arenaframe == 0xFFFE) EndArenaAnim();
}

/* arena94.asm:SetCameraTop. Unreferenced. */
void SetCameraTop(void)
{
    g.sflags |= HI(sfslock);
    g.xc1 = 0;
    g.yc1 = 0x160;
}

/* arena94.asm:StartArenaAnim. Start arena animation d0; RunArenaAnim loads it. */
void StartArenaAnim(uint16_t n)
{
    g.arenaanim = n;
    g.arenaframelist = 0;
    g.arenaspritelist = 0;
    g.arenaframe = 1;
    g.arenaframeidx = 0;
    ST_W(g.faceoffanim[0]);
}

/* arena94.asm:SetFaceoffAnim. sflags7 bit 0, faceoffanim = d0. */
void SetFaceoffAnim(uint16_t n)
{
    g.sflags7 |= 1;
    g.faceoffanim[0] = n;
}

/* arena94.asm:EndArenaAnim. arenaanim = -1, sflags7 bit 0 cleared. */
void EndArenaAnim(void)
{
    g.arenaanim = 0xFFFF;
    g.sflags7 &= (uint8_t)~1;
}

/* arena94.asm:ChooseSong. SongNum = song SongIndex of the six of team HmTeam (TeamSongs), or one
 * of the eight RandomSongs when that is 0; none ($FFFF) with gmode bit 4. */
void ChooseSong(void)
{
    uint16_t n;
    if (g.gmode & 0x10) {
        n = 0xFFFF;
    } else {
        g.sflags8 &= (uint8_t)~0x40;
        n = rosters_team(g.HmTeam)->songs[g.SongIndex];
        if (n == 0) n = rom_ptr(SYM_RandomSongs)[randomd0(8) & 7];
    }
    g.SongNum = n;
}

