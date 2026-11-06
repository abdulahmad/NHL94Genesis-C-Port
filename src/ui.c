/* ui94.asm: unpack, rink gfx, board fall, HiScore, played-by, and leftover print helpers. */
#include <string.h>
#include "game.h"
#include "hal.h"
#include "rom.h"
#include "symbols_gen.h"
#include "anim_gen.h"
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



/* ui94.asm:UnpackPicture. A picture a2 (a tile count, then 3 bytes a row of 8 pixels, 3 bits
 * each) to 4-bit tiles at picturebuf (the count first), each pixel + 5. */
void UnpackPicture(uint32_t pic)
{
    uint32_t in = pic;
    uint8_t *out = g.picturebuf;
    uint16_t n = m16(in);
    in += 2;
    wr16(out, n);
    out += 2;
    uint16_t rows = (uint16_t)((uint16_t)(n << 3) - 1);
    do {
        uint32_t b = (uint32_t)m8(in) << 24 | m8(in + 1) << 16 | m8(in + 2) << 8, v = 0;
        in += 3;
        g.picturebits = b;
        for (int px = 0; px < 8; px++)                    /* pixel px: bits 31-3px .. 29-3px to nibble 7-px */
            v |= ((b & (0xE0000000u >> (3 * px))) >> (px + 1)) + (5u << (28 - 4 * px));
        wr32(out, v);
        out += 4;
    } while (rows-- != 0);
}

/* ui94.asm:LoadHomeTeamGfx. d4 = char: load the home team's graphics (TeamGfxList) at
 * d4 - $18 -> d4 = the char after them. */
uint16_t LoadHomeTeamGfx(uint16_t start)
{
    uint32_t gfx = rosters_team(g.HomeTeam)->ice + 8;
    return DecompressGraphicsPlain(gfx, (uint16_t)(start - 0x18));
}

/* ui94.asm:PrintPlayerAssists. d0 = player, a2 = team: " (assists)", next row. */
void PrintPlayerAssists(uint16_t player, struct Team *tm)
{
    printz(str_PrintPlayerAssists_1, sizeof str_PrintPlayerAssists_1);
    PrintParenNumber(tm->passists, player);
}

/* ui94.asm:PrintPlayerGoals. d0 = player, a2 = team: " (goals)", next row. */
void PrintPlayerGoals(uint16_t player, struct Team *tm)
{
    printz(str_PrintPlayerGoals_1, sizeof str_PrintPlayerGoals_1);
    PrintParenNumber(tm->pgoals, player);
}

/* ui94.asm:PrintParenNumber. " (n)" with byte d0 of the team table, then the next
 * row at x $E */
void PrintParenNumber(const uint8_t *table, uint16_t player)
{
    int16_t n = (int8_t)table[(int16_t)player];
    uint16_t width = n > 0x63 ? 3 : n > 9 ? 2 : 1;
    print(PushNumberWidth((uint16_t)n, width));
    printz(str_PrintParenNumber_1, sizeof str_PrintParenNumber_1);
    g.printy++;
    g.printx = 0xE;
}

/* move a coordinate half way to a target (sub / asr.w #1 / add) */
static int16_t half_way(int16_t pos, int16_t target) { return (int16_t)(pos + ((int16_t)(target - pos) >> 1)); }

/* ui94.asm:PlaceBoardFall. Player a3 falling into the boards (or over the bench): move him
 * half way to the boards for the animation SPA. Front / back: y to +-$124 (+-$116 near the
 * corners, by FallXPos), only toward the boards. Sides: x to +-$82 (+-$88 over the bench), the
 * side by his flip (attribute bit 11). */
void PlaceBoardFall(struct SortObj *o)
{
    int flip = (o->attribute & 0x0800) != 0;
    int corner = (int16_t)g.FallXPos > 0x56 || (int16_t)g.FallXPos <= -0x56;
    switch (o->SPA) {
    case SPAbenchcheckl: o->Xpos = half_way(o->Xpos, flip ? 0x88 : -0x88); break;
    case SPAbenchcheckr: o->Xpos = half_way(o->Xpos, flip ? -0x88 : 0x88); break;
    case SPAboardleft:   o->Xpos = half_way(o->Xpos, flip ? 0x82 : -0x82); break;
    case SPAboardright:  o->Xpos = half_way(o->Xpos, flip ? -0x82 : 0x82); break;
    case SPAboardfront: {
        int16_t d = (int16_t)((corner ? 0x116 : 0x124) - o->Ypos);
        if (d >= 0) o->Ypos = (int16_t)(o->Ypos + (d >> 1));
        break;
    }
    case SPAboardback: {
        int16_t d = (int16_t)((corner ? -0x116 : -0x124) - o->Ypos);
        if (d < 0) o->Ypos = (int16_t)(o->Ypos + (d >> 1));
        break;
    }
    }
}

/* ui94.asm:GetLeagueCrowdRecord. d0 = the team (of NumTeams, the last first) with the loudest
 * crowd record (byte 8 of ReadCrowdRecord's buffer at ThreeStars; 0 counts as $50). */
uint16_t GetLeagueCrowdRecord(void)
{
    uint16_t best = 0, team = 0;
    for (int16_t t = (int16_t)(rosters.teams - 1); t >= 0; t--) {
        ReadCrowdRecord((uint32_t)t, ram_addr(g.ThreeStars));
        uint16_t db = g.ThreeStars[8];
        if (db == 0) db = 0x50;
        if ((int16_t)db > (int16_t)best) {
            best = db;
            team = (uint16_t)t;
        }
    }
    return team;
}

/* ui94.asm:getFgtbyte. -> d0 = the fighting byte of a2 / 4. */
uint16_t getFgtbyte(struct SortObj *a2, struct SortObj *a3) { return a2->fighting >> 2; }

/* ui94.asm:chkFgtBit1. -> Z: bit 1 of a2's fighting byte is clear. */
int chkFgtBit1(struct SortObj *a2, struct SortObj *a3) { return !(a2->fighting & 2); }

/* ui94.asm:RandomFaceoffAnim. -> d0 = a random faceoff animation (FaceoffAnims), 6 when
 * the faceoff spot is at fox <= -$40, foy <= $C0. */
uint16_t RandomFaceoffAnim(void)
{
    uint16_t a = rd16(rom_ptr(SYM_FaceoffAnims) + 2 * (randomd0(0x64) & 7));
    if ((int16_t)g.fox <= -0x40 && (int16_t)g.foy <= 0xC0) a = 6;
    return a;
}

/* ui94.asm:CheckScoreLeader. Compares the scores and tests a word of the leader, but the
 * branch on the result is a bne.w *+4: nothing happens. */
void CheckScoreLeader(void) {}

/* ui94.asm:DrawPlayoffSprite. The PlayoffSprite sprite at playoffspritex / playoffspritey in
 * Satt (an empty entry when it is off screen), the list ended there (Sattsize). */
void DrawPlayoffSprite(void)
{
    struct Vid v = { NULL, g.Satt, 1 };
    SetSframe(ART(PlayoffSprite), (int16_t)g.playoffspritex, (int16_t)g.playoffspritey, 1, (uint16_t)(g.energybarchars | 0x8000), &v);
    if (v.sat == g.Satt) { memset(v.sat, 0, 8); v.sat += 8; }
    v.sat[-5] = 0;                                        /* the last sprite's link */
    g.Sattsize = (uint16_t)((uint16_t)(v.sat - g.Satt) >> 1);
}

/* ui94.asm:HiScoreScreen. The attract mode's second screen (a bitmap and HiScoreImg) with
 * the fades-only vblank, for $50 x 4 frames or until a button. */
void HiScoreScreen(void)
{
    g.vbint = SYM_vb2;
    g.disflags &= (uint16_t)~HI(df32c);
    screen_layout();
    forceblack();
    memset(g.palfadenew, 0, sizeof g.palfadenew);
    setVram_0();
    uint8_t at[2][sizeof str_HiScoreScreen_1];            /* (str_HiScoreScreen_1 and _2 at the roster set's places) */
    for (int k = 0; k < 2; k++) {
        memcpy(at[k], k ? str_HiScoreScreen_2 : str_HiScoreScreen_1, sizeof at[k]);
        at[k][3] = rosters.hiscore_at[k][0];
        at[k][4] = rosters.hiscore_at[k][1];
    }
    printz(at[0], sizeof at[0]);
    struct Picture p = picture(ART(PlayoffSprite) + 0x12E0);
    uint16_t c = dobitmap(p.pal, p.map, p.tiles, 0, 0, p.w, p.h, 0, 0xF);
    printz(at[1], sizeof at[1]);
    p = picture(ART(HiScoreImg));
    dobitmap(p.pal, p.map, p.tiles, 0, 0, p.w, p.h, c, 0);
    g.palcount = 0x18;
    cpu_set_sr(0x2500);
    set_seed_hi(0x50);                                    /* (the seed's high word counts down) */
    do {
        uint16_t d0, d1, d2, d3;
        waitx(4, &d0, &d1, &d2, &d3);
        if (d1) break;
        set_seed_hi((uint16_t)(seed_hi() - 1));
    } while ((int16_t)seed_hi() >= 0);
    cpu_set_sr(0x2700);
}

/* ui94.asm:GetTeamStruct. a2 = the home team struct, or the visitors' when team d2 is not its
 * team ($28 of the struct). */
struct Team *GetTeamStruct(uint16_t team)
{
    struct Team *tm = team_struct(0);
    return tm->team == team ? tm : team_struct(1);
}

/* ui94.asm:CountButtonPress. Count a button press of pad d4 (homepresses / awaypresses), and
 * in the next long the presses with a direction other than 8 or a3's facing. Not in demos
 * (gmode bit 0). */
void CountButtonPress(int16_t pad, struct SortObj *o)
{
    if (g.gmode & 1) return;
    uint16_t dir = g.TempWord1;
    if (pad) g.awaypresses++; else g.homepresses++;
    if (dir == 8 || dir == (uint16_t)o->facedir) return;
    if (pad) g.awaydirpresses++; else g.homedirpresses++;
}

/* ui94.asm:TerminateLogName. Unreferenced. The GetLogName String at lognametext, ended with 0 at
 * namelength and its length word made even. */
void TerminateLogName(void)
{
    GetLogName(RAM_BASE + RAM_OFF_lognametext);
    g.lognametext[(int16_t)g.namelength] = 0;
    w16(RAM(lognametext), -2, (uint16_t)((g.namelength + 1) & 0xFFFE));
}

/* ui94.asm:PlayedByHome. The text player's '#' -> a1 = "(played by NAME)" for homeuser
 * (PlayedByText), or an empty String. */
uint32_t PlayedByHome(void) { return PlayedByText(g.homeuser); }

/* ui94.asm:PlayedByText. User d0's name (GetLogName from the name log, as a String at
 * nameentrybuf) in "(played by NAME)" at TempBuffer -> a1; user 0: PlayedByNone. */
uint32_t PlayedByText(uint16_t user)
{
    if (user == 0) return PlayedByNone();
    g.namelogsel = user;
    GetLogName(RAM_BASE + RAM_OFF_nameentrybuf + 2);
    g.nameentrybuf[2 + (int16_t)g.namelength] = 0;
    g.namelength = (uint16_t)(((g.namelength + 1) & 0xFFFE) + 2);
    wr16(g.nameentrybuf, g.namelength);
    CopyString(SYM_PlayedByTxt, ram_addr(g.TempBuffer));
    appstring(ram_addr(g.nameentrybuf), ram_addr(g.TempBuffer));
    appendz(str_PlayedByText_1, sizeof str_PlayedByText_1, ram_addr(g.TempBuffer));
    return PlayedByExit(ram_addr(g.TempBuffer));
}

/* ui94.asm:PlayedByExit. The common exit -> a1. */
uint32_t PlayedByExit(uint32_t a1) { return a1; }

/* ui94.asm:PlayedByNone. No user: the empty String. */
uint32_t PlayedByNone(void) { return PlayedByExit(SYM_PlayedByEmptyTxt); }

/* ui94.asm:PlayedByAway. The same for awayuser ('%'). */
uint32_t PlayedByAway(void) { return PlayedByText(g.awayuser); }

