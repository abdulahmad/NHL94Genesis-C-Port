/* setup94.asm: rink and team setup, the EA Sports attract screen, and the menu and playoff
 * data. This file lays down the ice, sets up the sprites and the teams, runs the flow between
 * periods, and packs playoff results into the password. */
#include <string.h>
#include "game.h"
#include "hal.h"
#include "rom.h"
#include "symbols_gen.h"
#include "view.h"
#include "anim_gen.h"
#include "setup_tables_gen.h"
#include "setup_strings_gen.h"

/* the remap table that follows a DecompressGraphicsWithRemap call in routine [from, to) */
static uint32_t inline_table(uint32_t from, uint32_t to, const uint8_t table[8])
{
    return rom_find(from, to, table, 8);
}

/* resetplstuff's .top: team a2's skaters lose their flags and glide again */
static void reset_team(struct Team *tm)
{
    tm->tmflags &= (uint8_t)~0x10;                                    /* team offside bit */
    struct SortObj *o = sortobj_at(tm->tmsort);
    for (int i = 0; i < 6; i++, o++) {
        o->pflags2 = 0;
        if (o->position < 0) continue;
        SetSPA(o, SPAglide);
        o->impact = 0;
        o->nopuck[0] = 0;
        o->pflags &= 0xC2;                               /* keep pfteam, pfgoal, pfna */
    }
}

/* a team's block for setupTeamBlocksMap: its row of the cartridge's (CopyTeamBlockMapData), or a
 * banner of the roster set's own, its 22 tiles straight from the roster set to vram (native/banner.c) */
static uint8_t *CopyTeamBlock(uint32_t map, uint16_t team, uint16_t vram, uint8_t *dst)
{
    if (team >= ROSTERS_MAX || !rosters.block_tiles[team]) return CopyTeamBlockMapData(map, rosters_block(team), vram, dst);
    uint32_t own = RS(Teamblocksmap) + rd32(rom_ptr(RS(Teamblocksmap) + 4)) + 4 + (uint32_t)rosters_block(team) * 0x2C;
    DoDMApro(rosters.block_tiles[team], 0x16 * 16, vram);
    for (int i = 0; i < 0x16; i++, dst += 2) wr16(dst, (uint16_t)((rd16(rom_ptr(own + 2u * (uint32_t)i)) & 0xF800) | ((vram >> 5) + i) | 0x8000));
    return dst;
}

#include "attract_strings_gen.h"
#include "data_strings_gen.h"

#define PRINTZ(s) printz(s, sizeof s)

/* the goal entry of the last goal: ChkCnt + ScoreSumbytes (byte 2 bit 7 away, 3 scorer, 4 / 5
 * assists, negative when none) */
static const uint8_t *last_goal(void) { return ram_bytes(rp_add(RAM(ScoreSum), (int16_t)(g.ScoreSumbytes - 6)), 6); }

/* the star scores: a long per roster slot of both teams from ThreeStars (52, past it into
 * teamrecbuf), big-endian as the 68000 keeps them in that scratch buffer */
static uint8_t *star_scores(void) { return ram_bytes(RAM(ThreeStars), 0x34 * 4); }

/* the number of the player whose name that is (the byte after it, on the cartridge) */
static uint8_t name_number(const struct RosterText *name) { return ((const struct RosterPlayer *)(const void *)name)->number; }

/* FormatPlayerName / FormatPlayerNumInitial: "NN " then the name from after the first space
 * (the first name), optionally after the first initial and ". " */
static uint32_t format_name(uint16_t player, struct Team *tm, int initial)
{
    const struct RosterText *name = GetPlayerNamePointer(player, tm);
    const uint8_t *p = name->s;
    uint8_t *out = text_buffer();
    out = ConvertByteToDigits(name_number(name), out);    /* his jersey number */
    *out++ = ' ';
    const uint8_t *end = p + rd16(p);
    p += 2;
    if (initial) { *out++ = *p++; *out++ = '.'; *out++ = ' '; }
    while (*p++ != ' ') {}
    do *out++ = *p++; while (p != end);
    return FinalizeTextBuffer(out);
}

/* the player of TempPlOffset (bit 15 set: the away team) */
static struct Team *temp_player(uint16_t *player)
{
    uint16_t p = g.TempPlOffset;
    if ((int16_t)p >= 0) { *player = p; return team_struct(0); }
    *player = p & 0xFF;
    return team_struct(1);
}

/* the playoff stat bit stream runs down in words from outputbuffer + $DE: the long holding bit
 * pos of it */
static uint8_t *stat_long(uint16_t pos) { return g.outputbuffer + 0xDE - (uint16_t)((pos >> 4) << 1); }

/* ---- the playoff password (after a playoff game) --------------------------------------------- */

/* the long at p (two words of a 5-word number) plus v, the carry run up through at most n words
 * above it (dbcc: n counts down to -1) */
static void add_long_carry(uint8_t *p, uint32_t v, int16_t n)
{
    uint32_t old = rd32(p), sum = old + v;
    wr32(p, sum);
    int carry = sum < old;
    while (carry && --n != -1) {
        p -= 2;
        uint16_t w = (uint16_t)(rd16(p) + 1);
        wr16(p, w);
        carry = w == 0;
    }
}

/* setup94.asm:setupice. Set up the rink: the VDP layout (32 columns, maps at $C000 / $E000 /
 * $F000, sprites at $DC00, scroll at $FC00), clear VRAM, then load the rink, team, EASN, energy
 * bar, crowd, sprite, framer, font and team block tiles, the default objects and palettes, flip
 * the goals for the second period's ends, the pad and logo sprites, and the game vblank. */
void setupice(void)
{
    static const uint8_t small[8] = { 0x43, 0x43, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
    static const uint8_t big[8] = { 0x71, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
    g.disflags |= HI(df32c);
    g.maps[1].vram = 0xC000; g.maps[1].colshift = 6;
    g.VSPRITES = 0xDC00;
    g.maps[0].vram = 0xE000; g.maps[0].colshift = 6;
    g.maps[2].vram = 0xF000; g.maps[2].colshift = 5;
    g.VSCRLPM = 0xFC00;
    setVram(0);
    view_rink_mode();                                     /* (the wide view's 40 columns) */
    g.sflags &= (uint16_t)~HI(sfpz);
    uint16_t saved = g.disflags;
    g.disflags |= HI(dfng);
    g.Hpos = 0;
    g.Vpos = 0;
    g.Oldrow = 0x7D0;
    ST_W(g.Zamx);
    DoFill(0x800, g.maps[0].vram, 0x7FF);
    g.rinkvrcset = 0;
    uint16_t c = DecompressGraphicsPlain(g.sflags4 & HI(4) ? ART(RevRinkTiles) : ART(Rinktiles), 0);
    c = LoadHomeTeamGfx(c);
    g.EASNcset = c;
    c = setupEASportsMap();
    g.energybarchars = c;
    c = DecompressGraphicsPlain(ART(EnergyBarMap) + 8, c);
    g.crowdvrcset = c;
    c = DecompressGraphicsPlain(ART(CrowdFrameList) + 8, c);
    g.arenaanimchars = c;
    c += 0x32;
    g.spritechars = c;
    c = defaultsprites(c);
    setupIceRinkPal();
    c = AddFramer(c);
    g.smallfontchars = c;
    c = DecompressGraphicsWithRemap(ART(SmallFontMap) + 8, c, inline_table(SYM_setupice, SYM_setupice + 0x200, small));
    g.BigFontchars = c;
    c = DecompressGraphicsWithRemap(ART(BigFontMap) + 8, c, inline_table(SYM_setupice + 1, SYM_setupice + 0x200, big));
    c = setupTeamBlocksMap(c);
    if (view_wide()) DoFill(0x80, g.maps[0].vram, 0x7FF);       /* (its unpacking ran over plane A's first two rows: the wide view shows their other half) */
    g.ExtraChars = c;
    if (g.gmode & (1 << gmdir))
        for (int i = 0; i < 12; i++) sortobj(i)->pflags ^= 1 << pfgoal;
    setplayercolors();
    g.PadControlBits = 0xFFFF;
    g.PadControlBits34 = 0xFFFF;
    memset(g.padcont, 0, sizeof g.padcont);
    ST_W(g.c1playernum);
    ST_W(g.c2playernum);
    struct Vid v = { g.DMAList, g.Satt, 1 };
    for (int i = 0; i < 6; i++) addframe2(0, 0, &g.pads[i].head, &v);                     /* pads, logo, gloves */
    g.DMAlistend = ram_addr(v.dma);
    dodmalist();
    g.disflags = saved;
    g.VBint = SYM_VBlank;
    g.disflags &= (uint16_t)~(HI(dfok) | HI(dfng));
    cpu_set_sr(0x2300);
}

/* setup94.asm:setupice_highlight. Rebuild the rink objects for a highlight without reloading
 * the tiles (the sprite chars setupice kept in spritechars), then the game vblank and a fade
 * in. */
void setupice_highlight(void)
{
    forceblack();
    g.sflags &= (uint16_t)~HI(sfpz);
    uint16_t saved = g.disflags;
    g.disflags |= HI(dfng);
    g.Hpos = 0;
    g.Vpos = 0;
    g.Oldrow = 0x7D0;
    ST_W(g.Zamx);
    defaultsprites(g.spritechars);
    setupIceRinkPal();
    if (g.gmode & (1 << gmdir))
        for (int i = 0; i < 12; i++) sortobj(i)->pflags ^= 1 << pfgoal;
    setplayercolors();
    g.PadControlBits = 0xFFFF;
    g.PadControlBits34 = 0xFFFF;
    memset(g.padcont, 0, sizeof g.padcont);
    ST_W(g.c1playernum);
    ST_W(g.c2playernum);
    setupEASportsMap();
    struct Vid v = { g.DMAList, g.Satt, 1 };
    for (int i = 0; i < 6; i++) addframe2(0, 0, &g.pads[i].head, &v);
    g.DMAlistend = ram_addr(v.dma);
    dodmalist();
    g.palcount = 0x1C;
    g.disflags = saved;
    g.VBint = SYM_VBlank;
    g.disflags &= (uint16_t)~(HI(dfok) | HI(dfng));
    cpu_set_sr(0x2300);
}

/* setup94.asm:setupIceRinkPal. The ice rink palettes 0 and 1 (Rinktilelist) to palfadenew. */
void setupIceRinkPal(void)
{
    uint32_t pal = ART(Rinktilelist) + rd32(rom_ptr(ART(Rinktilelist)));
    pal_load(0, rom_ptr(pal), 0x20);
}

/* setup94.asm:setupEASportsMap. Load the EASN logo tiles at EASNcset, colours remapped -> d4. */
uint16_t setupEASportsMap(void)
{
    static const uint8_t table[8] = { 0x71, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
    return DecompressGraphicsWithRemap(ART(EASNmap) + 8, g.EASNcset, inline_table(SYM_setupEASportsMap, SYM_setupEASportsMap + 0x20, table));
}

/* setup94.asm:ReloadEnergyBarTiles. -> d4 = the char after them */
uint16_t ReloadEnergyBarTiles(void) { return DecompressGraphicsPlain(ART(EnergyBarMap) + 8, g.energybarchars); }

/* setup94.asm:ReloadCrowdTiles. -> d4 = the char after them */
uint16_t ReloadCrowdTiles(void) { return DecompressGraphicsPlain(ART(CrowdFrameList) + 8, g.crowdvrcset); }

/* setup94.asm:ReloadRefHorTiles. The horizontal referee tiles at ExtraChars -> d4. */
uint16_t ReloadRefHorTiles(void) { return DecompressGraphicsPlain(ART(RefMap2) + 8, g.ExtraChars); }

/* setup94.asm:ReloadRefTiles. The referee tiles (RefsMap) at ExtraChars -> d4. */
uint16_t ReloadRefTiles(void) { return DecompressGraphicsPlain(ART(RefsMap) + 8, g.ExtraChars); }
/* setup94.asm:ReloadFaceOffTiles. The faceoff sprite tiles at faceoffvrcset -> d4. */
uint16_t ReloadFaceOffTiles(void) { return DecompressGraphicsPlain(ART(FaceOffSprites) + 8, g.faceoffvrcset); }
/* setup94.asm:ReloadFaceOffMap. The faceoff map tiles at ExtraChars -> d4. */
uint16_t ReloadFaceOffMap(void) { return DecompressGraphicsPlain(ART(FaceOffMap) + 8, g.ExtraChars); }

/* setup94.asm:setupTeamBlocksMap. d4 = first char: the TeamBlocks tiles at d4 + $2C, then the
 * home and visitor blocks copied to basetileoffset and basetileoffset + $16 -> d4 =
 * basetileoffset + $2C. */
uint16_t setupTeamBlocksMap(uint16_t chars)
{
    g.basetileoffset = chars;
    uint8_t *buf = g.TeamBlockMap;
    DecompressGraphicsPlain(SYM_Teamblocksmap + 8, (uint16_t)(chars + 0x2C));
    uint32_t map = SYM_Teamblocksmap + rd32(rom_ptr(SYM_Teamblocksmap + 4));
    buf = CopyTeamBlock(map, g.HomeTeam, (uint16_t)(g.basetileoffset << 5), buf);   /* (the roster set's: its art's, or its own) */
    CopyTeamBlock(map, g.VisTeam, (uint16_t)((g.basetileoffset + 0x16) << 5), buf);
    return (uint16_t)(g.basetileoffset + 0x2C);
}

/* the wide view (view.h): the rink in 40 columns, plane A's overlays VIEW_SHIFT to the right */
void view_rink_mode(void)
{
    if (!view_wide()) return;
    uint16_t saved = g.disflags;
    g.disflags |= HI(dfng);
    vdp_ctrl16(SYM_VDPR_MODE4 + 0x81);
    Vmaddr(g.VSCRLPM);
    vdp_data(VIEW_SHIFT);
    g.disflags = saved;
}

/* the wide view on a rink set up in 32 columns (a snapshot's): 40 columns, the camera within the
 * wide rink's reach, and plane A's first two rows' right halves, which setupTeamBlocksMap's
 * unpacking ran over, cleared */
void view_rink_resume(void)
{
    if (!view_wide()) return;
    view_rink_mode();
    if ((int16_t)g.Hpos > VIEW_HPOS) g.Hpos = VIEW_HPOS;  /* (and the camera within the wide rink) */
    if ((int16_t)g.Hpos < -VIEW_HPOS) g.Hpos = (uint16_t)-VIEW_HPOS;
    DoFill(0x20, (uint16_t)(g.maps[0].vram + 0x40), 0x7FF);
    DoFill(0x20, (uint16_t)(g.maps[0].vram + 0xC0), 0x7FF);
}

/* setup94.asm:CopyTeamBlockMapData. a0 = the TeamBlocks map, d0 = team, d1 = VRAM address,
 * a1 = map word buffer: copy the team's 22 block chars from the loaded tiles to d1 on (VRAM to
 * VRAM DMA) and store their map words (priority, palette of the map word, the char) -> a1. */
uint8_t *CopyTeamBlockMapData(uint32_t map, uint16_t team, uint16_t vram, uint8_t *dst)
{
    const uint8_t *src = rom_ptr(map + 4 + (uint16_t)(team * 0x2C));
    for (int i = 0; i < 0x16; i++, src += 2, dst += 2) {
        dst[0] = src[0];
        dst[1] = 0;
        wr16(dst, (uint16_t)((rd16(dst) & 0xF800) | (vram >> 5) | 0x8000));
        uint16_t from = (uint16_t)((((rd16(src) & 0x7FF) + g.basetileoffset + 0x2C)) << 5);
        DoDMA_nd2(0x20, vram, from);
        vram += 0x20;
    }
    return dst;
}

/* setup94.asm:defaultsprites. d4 = first char: the 4 screen objects (sso, 0x14 bytes) and the 7
 * objects tied to the rink scrolling (pads, 0x1C bytes) from their tables, each given its
 * chars, then defaultsprites_0 -> d4. */
uint16_t defaultsprites(uint16_t chars)
{
    const uint16_t *t = listsso;
    for (int i = 0; i < 4; i++) {
        struct SpriteHead *o = &g.sso[i];
        o->oldframe = 0xFFFF;                             /* not locked to the rink scroll */
        o->Xpos = (int16_t)*t++;
        o->Xfrac = *t++;                                  /* (an arrow's y) */
        o->frame = *t++;
        o->attribute = *t++;
        o->VRchar = chars;
        chars += *t++;
    }
    t = listffo;
    for (int i = 0; i < 7; i++) {
        struct RinkObj *o = &g.pads[i];
        ST_W(o->oldframe);                                /* tied to the rink scroll */
        o->Xpos = (int16_t)*t++;
        o->Ypos = (int16_t)*t++;
        o->Zpos = (int16_t)*t++;
        o->frame = *t++;
        o->attribute = *t++;
        o->VRchar = chars;
        chars += *t++;
    }
    return defaultsprites_0(chars);
}

/* setup94.asm:defaultsprites_0. The 16 sort objects (players, nets, puck, shadow) cleared and set
 * from the table (position, frame, attribute, chars, radius, first assignment, flags), with
 * the sort order lists, then SprSort -> d4. */
uint16_t defaultsprites_0(uint16_t chars)
{
    const uint16_t *t = dslist;
    for (uint16_t n = 0; n < 0x10; n++) {
        struct SortObj *o = sortobj(n);
        memset(o, 0, sizeof *o);
        o->SCnum = (int16_t)n;
        ST_W(o->oldframe);
        o->pnum = 0xFF;
        o->Xpos = (int16_t)*t++;
        o->Ypos = (int16_t)*t++;
        o->Zpos = (int16_t)*t++;
        o->frame = *t++;
        o->attribute = *t++;
        o->VRchar = chars;
        chars += *t++;
        o->radiusx = (int16_t)*t++;
        o->radiusy = (int16_t)*t++;
        o->asslist[0] = (uint8_t)*t++;
        o->pflags = (uint8_t)*t++;
        g.OOlist[n] = (uint8_t)(n << 1);
        g.OOlistpos[n] = n;
    }
    SprSort();
    return chars;
}

/* setup94.asm:SprSort. Sort the sort objects by y (x in horizontal mode): Ylist = the
 * coordinates, OOlist = the order (byte offsets into Ylist), OOlistpos = each one's place.
 * A bubble sort over the 16 entries until nothing moves. */
void SprSort(void)
{
    for (int i = 0; i < 0x10; i++) {
        struct SortObj *o = sortobj(i);
        g.Ylist[i] = (uint16_t)(g.sflags & HI(sfhor) ? o->Xpos : o->Ypos);
    }
    int moved;
    do {
        moved = 0;
        for (int i = 0; i < 0xF; i++) {
            uint8_t a = g.OOlist[i], b = g.OOlist[i + 1];    /* (byte offsets: the object * 2) */
            if ((int16_t)g.Ylist[a / 2] <= (int16_t)g.Ylist[b / 2]) continue;
            g.OOlist[i + 1] = a;
            g.OOlist[i] = b;
            g.OOlistpos[a / 2] = (uint16_t)(i + 1);
            g.OOlistpos[b / 2] = (uint16_t)i;
            moved = 1;
        }
    } while (moved);
}

/* setup94.asm:resetplstuff. Reset the team variables and the players of both teams. */
void resetplstuff(void)
{
    reset_team(team_struct(0));
    reset_team(team_struct(1));
}

/* setup94.asm:clearTeamStats. Clear both team structs, keeping the first $1A0 bytes of each
 * hot / cold table (+$1A2, through homehotcoldsave / awayhotcoldsave) and the goalie words; the
 * last roster slot's place is -1. Falls into setteams. */
void clearTeamStats(void)
{
    struct Team *home = team_struct(0), *away = team_struct(1);
    uint16_t hg = home->tmgoalie, ag = away->tmgoalie;
    memcpy(g.homehotcoldsave, home->tmHotCold, 0x1A0);
    memcpy(g.awayhotcoldsave, away->tmHotCold, 0x1A0);
    memset(home, 0, sizeof *home);
    memset(away, 0, sizeof *away);
    rules_clear_stats();                                  /* (the rules' own, beside the RAM) */
    memcpy(home->tmHotCold, g.homehotcoldsave, 0x1A0);
    memcpy(away->tmHotCold, g.awayhotcoldsave, 0x1A0);
    away->tmgoalie = ag;
    home->tmgoalie = hg;
    home->tmboxlist[0] = 0xFF;                               /* (st tmpdst+$34: the penalty box empty) */
    away->tmboxlist[0] = 0xFF;
    setteams();
}

/* setup94.asm:setteams. Both team structs from HomeTeam / VisTeam, with their sort objects. */
void setteams(void)
{
    team_struct(0)->tmsort = RAM_OFF_SortCords;
    InitTeamStructure(g.HomeTeam, team_struct(0));
    team_struct(1)->tmsort = RAM_OFF_SortCords + 6 * 0x80;
    InitTeamStructure(g.VisTeam, team_struct(1));
}

/* setup94.asm:InitTeamStructure. Team struct a2 for team d0: the team number ($28), its data
 * (tmdata, from TeamList) and its line sets at $16A (the layout by OptLine). */
void InitTeamStructure(uint16_t team, struct Team *tm)
{
    tm->tmteam = team;
    tm->tmdata = rosters_team(team)->data;
    InitTeamLines(tm);
}

/* setup94.asm:InitTeamStructure+$10. Team struct a2's line sets at $16A from its data's (the
 * layout by OptLine); the line editor's "Set Original lines". */
void InitTeamLines(struct Team *tm)
{
    const uint8_t *src = rosters_team(tm->tmteam)->lines[0];
    uint8_t *dst = tm->tmlines[0];                          /* (the 7 line sets, row after row) */
    if (g.OptLine != 0) {
        memcpy(dst, src, 8);
        memcpy(dst + 8, src + 16, 12 * 4);
    } else {
        memcpy(dst, src + 8, 14 * 4);
    }
}

/* setup94.asm:setplayercolors. Each team's colours (team data + its palette offset) to
 * palfadenew palettes 2 (home) and 3 (visitors). */
void setplayercolors(void)
{
    for (int away = 0; away < 2; away++) {
        pal_load_w(0x20 + 0x10 * away, rosters_team(team_struct(away)->tmteam)->colours[away], 0x10);
    }
}

/* setup94.asm:PeriodOver. The period is over: count it (gsp) and flip the ends; after the third
 * period a tied game goes to overtime, after overtime only a tied playoff game goes on; else
 * the game is over (gsp 4). Fade out and fall into PeriodOver_sp, which does not return. */
void PeriodOver(void)
{
    g.gsp++;
    g.gmode ^= 1 << gmdir;
    if (g.gsp >= 3) {
        if (g.gsp > 3) {
            g.gsp = 3;
            if (g.OptPlayMode == 0) g.gsp = 4;
        }
        if (team_struct(0)->tmscore != team_struct(1)->tmscore) g.gsp = 4;
    }
    forceblack();
    PeriodOver_sp();
}

/* setup94.asm:PeriodOver_sp. Between periods: reset the clock, two vblanks with the sound
 * off between, the end of period song, the ticker scores (and the playoff team stats at the end
 * of the game), the intermission, then the next period or the game over. Does not return. */
void PeriodOver_sp(void)
{
    ResetClock();
    uint16_t v = g.Vcount;
    while (g.Vcount == v) hal_wait_vblank();
    p_turnoff();
    v = g.Vcount;
    while (g.Vcount == v) hal_wait_vblank();
    song(SYM_SngEOG);
    UpdateScores();
    if (g.gsp == 4) AddPlayoffTeamStats();
    Intermission();
    if (g.gsp == 4) GameOver();
    else StartPer();
}

/* setup94.asm:GameOver. After the game: the playoff password (EncodePW), in the playoffs their
 * stats and the playoff screen; then back to the title (ExitToOpening). */
void GameOver(void)
{
    EncodePW();
    if (g.OptPlayMode != 0) {
        g.sflags &= (uint16_t)~HI(sfpj);
        PlayoffStatsScreen();
    }
    PlayoffScreen();
    ExitToOpening();
}

/* setup94.asm:ExitToOpening. The title song, then the options again (Opening2, with a new
 * stack). From the game over, the end of a demo and the end of a shootout. */
void ExitToOpening(void)
{
    song(SYM_SngTitle);
    restart_at(Opening2);
}

/* setup94.asm:Opening. The title screen, then into Opening2. From Begin. */
void Opening(void)
{
    KillCrowd();
    TitleScreen();
    Opening2();
}

/* setup94.asm:Opening2. With a new stack: clear the game's variables ($B000-$D03D), the
 * options (GameSetUp), the name entry, then Opening3. Does not return. */
void Opening2(void)
{
    KillCrowd();
    cpu_set_sr(0x2700);
    ram_fill(RAM(VSCRLPM), 0, 0xD03E - RAM_OFF_VSCRLPM);
    GameSetUp();
    UserNameEntry();
    Opening3();
}

/* setup94.asm:Opening3. The playoff screen (in the playoffs), the scouting report (not in a
 * shootout), then the game (StartGame). Does not return. */
void Opening3(void)
{
    PlayoffScreen();
    if (!(g.gmode2 & HI(0))) ScoutingReport();
    StartGame();
}

/* ---- the playoff screen ---------------------------------------------------------------------- */

/* setup94.asm:PlayoffScreen. In the playoffs: the tree (PlayoffTreeSetup by gamelevel: the team
 * blocks, the arrows, the series scores), the round's title and the playoff sprite, with its own
 * vblank (VBlank_Playoff); left / right scroll it a page ($70 pixels) at a time, Start leaves. */
void PlayoffScreen(void)
{
    if (g.OptPlayMode == 0) return;
    g.VBint = SYM_VBlank_Playoff;
    g.disflags &= (uint16_t)~HI(df32c);
    g.VSCRLPM = 0;
    g.VSPRITES = 0xBC00;
    g.maps[2].vram = 0xB000;
    g.maps[2].colshift = 6;
    g.maps[1].vram = 0xC000;
    g.maps[1].colshift = 7;
    g.maps[0].vram = 0xE000;
    g.maps[0].colshift = 7;
    setVram(0);
    printz(str_PlayoffScreen_1, sizeof str_PlayoffScreen_1);
    eraser(0x80, 0x1C, 0x7FF);
    printz(str_PlayoffScreen_2, sizeof str_PlayoffScreen_2);
    eraser(0x28, 2, 0x7FF);
    uint16_t c = AddSmallFont(AddTeamBlock());
    g.ExtraChars = c;
    c = DecompressGraphicsPlain(ART(Arrowsmap) + 8, c);
    printz(str_PlayoffScreen_3, sizeof str_PlayoffScreen_3);
    uint32_t img = ART(ScoutMap), map = img + rd32(rom_ptr(img + 4));
    c = dobitmap(img + rd32(rom_ptr(img)), map, img + 8, 0, 0, rd16(rom_ptr(map)), rd16(rom_ptr(map) + 2), c, 3);
    const uint8_t *sp = rom_ptr(ART(PlayoffSprite) + rd32(rom_ptr(ART(PlayoffSprite))));   /* its palette */
    for (unsigned k = 0x20; k < 0x30; k++, sp += 2) {              /* palette 0 to 3, the sprite's to 0, 1 to 2 */
        g.palfadenew[k + 0x10] = g.palfadenew[k - 0x20];
        g.palfadenew[k - 0x20] = rd16(sp);
        g.palfadenew[k] = g.palfadenew[k - 0x10];
    }
    g.energybarchars = c;
    DecompressGraphicsPlain(ART(PlayoffSprite) + 8, c);
    g.clampcounter = 0x104;
    g.playoffspritey = 0xA0;
    vdp_ctrl16(SYM_VDPR_WINY + 2);
    uint8_t *tree = g.potree;
    const uint8_t *a0 = rom_ptr(SYM_PlayoffTreeSetup + (int16_t)rd16(rom_ptr(SYM_PlayoffTreeSetup + (uint16_t)(g.gamelevel << 1))));
    for (int16_t n = *a0++; n >= 0; n--) {                /* the team blocks */
        printz(str_PlayoffScreen_4, sizeof str_PlayoffScreen_4);
        g.printx = (uint16_t)((g.printx & 0xFF00) | *a0++);
        g.printy = (uint16_t)((g.printy & 0xFF00) | *a0++);
        PlayoffScreen_doteam((uint16_t)(*tree++ << 1));
    }
    for (int16_t n = *a0++; n >= 0; n--) {                /* the arrows */
        printz(str_PlayoffScreen_5, sizeof str_PlayoffScreen_5);
        g.printx = (uint16_t)((g.printx & 0xFF00) | *a0++);
        PlayoffScreen_doarrow(*a0++);
    }
    printz(str_PlayoffScreen_6, sizeof str_PlayoffScreen_6);
    if (g.bosgames != 7 && !(*a0 & 0x80)) {               /* the series scores */
        const struct Matchup *gs = g.matchups;
        for (int16_t n = *a0++; n >= 0; n--, gs++) {
            g.printx = (uint16_t)((g.printx & 0xFF00) | *a0++);
            g.printy = (uint16_t)((g.printy & 0xFF00) | *a0++);
            PlayoffScreen_doscores(gs);
        }
    }
    printsmallz(str_PlayoffScreen_7, sizeof str_PlayoffScreen_7);
    uint32_t title = AdvanceStringPtr(SYM_PlayoffScreenText, g.gamelevel);   /* the round, centred */
    g.printx -= (uint16_t)(rd16(rom_ptr(title)) >> 1);
    printsmall(title);
    if (g.gamelevel != 0) printz(str_PlayoffScreen_8, sizeof str_PlayoffScreen_8);
    g.palcount = 0x18;
    song(SYM_SngEOG);
    g.PlayerScrollCtr = 0xFFFF;
    g.DispAttribCtr = 1;
    PlayoffScreen_scrup();
    for (;;) {
        waitxc1();
        DrawPlayoffSprite();
        if (PlayoffScreen_jin()) return;                 /* PlayoffScreen_exit */
    }
}

/* setup94.asm:PlayoffScreen_jin. Both pads: Start leaves the playoff screen (-> 1, the 68k's
 * PlayoffScreen_exit), right / left set the scroll step; then PlayoffScreen_scrup. */
int PlayoffScreen_jin(void)
{
    uint16_t d0, d1, d2, held, held2;
    ReadJoy1(&d0, &d1, &d2, &held);
    ReadJoy2(&d0, &d1, &d2, &held2);
    held |= held2;
    if (held & 0x80) { PlayoffScreen_exit(); return 1; }
    if (held & 8) g.PlayerScrollCtr = 0xFFFE;
    if (held & 4) g.PlayerScrollCtr = 2;
    PlayoffScreen_scrup();
    return 0;
}

/* setup94.asm:PlayoffScreen_scrup. The tree one step (PlayerScrollCtr) within its pages (up to
 * gamelevel x $70 either way), stopping on a page boundary; the sprite's x while it shows. */
void PlayoffScreen_scrup(void)
{
    int16_t d0 = (int16_t)g.PlayerScrollCtr;
    if (d0 == 0) return;
    d0 = (int16_t)(d0 + g.DispAttribCtr);
    uint16_t lim = g.gamelevel > 3 ? 3 : g.gamelevel;
    int16_t d1 = (int16_t)(uint16_t)(lim * 0x70);
    if (d0 > d1 || d0 < -d1) return;
    g.DispAttribCtr = (uint16_t)d0;
    g.playoffspritex = 0;
    int16_t x = (int16_t)(d0 + 0x100);
    if (x >= 0x40 && x <= 0x200) g.playoffspritex = (uint16_t)x;
    if ((int16_t)(m68k_divs(d0, 0x70) >> 16) == 0) g.PlayerScrollCtr = 0;
}

/* setup94.asm:PlayoffScreen_exit. Drop PlayoffScreen_jin's return: PlayoffScreen returns (the C
 * caller does). */
void PlayoffScreen_exit(void) {}

/* setup94.asm:waitxc1. When the fade has run out, colour $21 flashes ($EE eor)
 * and the fade starts again; then wait for the vblank. */
void waitxc1(void)
{
    if ((int16_t)g.palcount < 0) {
        g.palfadenew[0x21] ^= 0xEE;
        g.palcount = 0x18;
    }
    while (g.Vcount == g.OldVcount) hal_wait_vblank();
    g.OldVcount = g.Vcount;
}

/* setup94.asm:PlayoffScreen_doscores. The best-of-7 wins "t-b" of game struct a2 at printx / printy. */
void PlayoffScreen_doscores(const struct Matchup *gs)
{
    uint8_t *m = g.mesarea;
    wr16(m, 6);
    m[2] = (uint8_t)(gs->gspotwins + '0');
    m[3] = '-';
    m[4] = (uint8_t)(gs->gspobwins + '0');
    m[5] = 0;
    print(ram_addr(g.mesarea));
}

/* setup94.asm:PlayoffScreen_doarrow. Column d0 of the arrows map (Arrowsmap, 2 x $17) at printx /
 * printy. */
void PlayoffScreen_doarrow(uint16_t col)
{
    uint32_t img = ART(Arrowsmap);
    dobitmap(img + rd32(rom_ptr(img)), img + rd32(rom_ptr(img + 4)), SYM_NoTiles, col, 0, 2, 0x17, g.ExtraChars, 0);
}

/* setup94.asm:PlayoffScreen_doteam. Team block d1 (team x 2: its row of Teamblocksmap) at printx /
 * printy, highlighted (printa $6000) for the user's playoff team. */
void PlayoffScreen_doteam(uint16_t row)
{
    uint8_t user = (uint8_t)(g.potree[g.potreeteam] << 1);
    if (user == (uint8_t)row) g.printa = 0x6000;
    uint32_t map = RS(Teamblocksmap) + rd32(rom_ptr(RS(Teamblocksmap) + 4));
    dobitmap(0, map, SYM_NoTiles, 0, (uint16_t)(rosters_block((uint16_t)(row >> 1)) << 1), rd16(rom_ptr(map)), 2, 2, 0);   /* (its art's, or its own) */
}

/* setup94.asm:VBlank_Playoff. The playoff screen's vblank: unless dfng, the sprites (and the tree's
 * scroll, $FEA0 + DispAttribCtr) and the fade; then Vcount and the music. */
void VBlank_Playoff(void)
{
    if (!(g.disflags & HI(dfng))) {
        if (g.Sattsize) {
            uint16_t n = g.Sattsize;
            g.Sattsize = 0;
            DoDMA(ram_addr(g.Satt), n, g.VSPRITES);
            Vmaddr(g.VSCRLPM);
            vdp_data((uint16_t)(0xFEA0 + g.DispAttribCtr));
        }
        cramfade();
    }
    g.Vcount++;
    p_music_vblank();
}

/* ---- the scouting report's text player ------------------------------------------------------- */

/* setup94.asm:InitScoutingDisplay. Start the text player: no line yet, a new paragraph next ->
 * a0 = the paragraph list (TestList). */
uint32_t InitScoutingDisplay(void)
{
    g.VertLineScrolling = 0xFFFF;
    ST_W(g.PlayerScrollCtr);
    g.SelectedPlayerIdx = 0;
    g.ScoutingReportTimer = 0;
    return ram_addr(g.TestList);
}

/* setup94.asm:UpdateScoutingDisplay. The text player: each time its delay (ScoutingReportTimer; longer
 * after a comma or a full stop; none with asv set) runs out, the next word of the paragraph
 * (PlayerScrollCtr: its offset in ScoutTextScript; negative: the next paragraph of TestList) to
 * the text box, a line at a time (ScrollDisplayUp); the special characters put in the team data:
 * the city of the team in form ($), the goalies ([ ]), the hot / cold players (< > | \), the
 * crowd record (@), the users' names (# %), nicknames (= *), arenas (^ ;) and the rosters' ends
 * (_ + { }). */
void UpdateScoutingDisplay(void)
{
    if ((int16_t)g.ScoutingReportTimer > 0x1000) return;
    if ((int16_t)g.asv >= 0 && (int16_t)--g.ScoutingReportTimer >= 0) return;
    const uint32_t a1 = SYM_ScoutTextScript;
    int16_t d0 = (int16_t)g.PlayerScrollCtr;
    if (d0 < 0) {                                         /* the next paragraph */
        g.DispAttribCtr = 0;
        uint16_t i = g.SelectedPlayerIdx;
        g.SelectedPlayerIdx += 2;
        d0 = (int16_t)g.TestList[i / 2];                  /* (SelectedPlayerIdx: its byte offset) */
        if (d0 < 0) {                                     /* the list's end */
            g.ScoutingReportTimer = 0x7FFF;
            g.screentimer = 0x1E0;
            return;
        }
        ScrollDisplayUp();
        const uint8_t *p = rom_ptr(a1);
        for (uint16_t n = (uint16_t)d0; n-- != 0;) while (*p++ != 0xD) {}
        d0 = (int16_t)(uint16_t)(p - rom_ptr(a1));
    }
    const uint8_t *a2 = rom_ptr(a1) + d0;
    g.ScoutingReportTimer = 0xA;
    d0 = (int16_t)g.DispAttribCtr;
    struct Text str;                                      /* a String the special character puts in */
    struct Team *tm;
    uint16_t d1;
    switch (*a2) {
    case '_': case '{': tm = team_struct(0); goto city;
    case '+': case '}': tm = team_struct(1); goto city;
    case '$': tm = team_struct(g.awayhotter != 0); goto city;
    case '[': tm = team_struct(0); d1 = tm->tmgoalie; goto roster;
    case ']': tm = team_struct(1); d1 = tm->tmgoalie; goto roster;
    case '<': d1 = NextHomeHotPlayer(&tm); goto roster;
    case '>': d1 = NextAwayHotPlayer(&tm); goto roster;
    case '|': d1 = NextHomeColdPlayer(&tm); goto roster;
    case '\\': d1 = NextAwayColdPlayer(&tm); goto roster;
    case '@': str = (struct Text){ NULL, ScoutCrowdRecord() }; goto append;
    case '#': str = (struct Text){ NULL, PlayedByHome() }; goto append;
    case '%': str = (struct Text){ NULL, PlayedByAway() }; goto append;
    case '=': str = GetTeamNickname(team_struct(0)); goto append;
    case '*': str = GetTeamNickname(team_struct(1)); goto append;
    case '^': str = GetTeamArena(team_struct(0)); goto append;
    case ';': str = GetTeamArena(team_struct(1)); goto append;
    default: break;
    }
    {                                                     /* a word, and the spaces after it */
        ST_W(g.PlayerScrollCtr);
        uint8_t *a0 = text_buffer();
        d1 = 0;
        while (*a2 != 0xD) {
            d0++;
            int more;
            do {
                *a0++ = *a2;
                d1++;
                if (*a2 == ',' || *a2 == '.') g.ScoutingReportTimer += 0x28;
                more = *a2++ == ' ' && *a2 == ' ';
            } while (more);
            if (a2[-1] == ' ') {
                d0--;
                g.PlayerScrollCtr = (uint16_t)(a2 - rom_ptr(a1));
                break;
            }
        }
        wr16(g.mesarea, (uint16_t)(d1 + 2));
        if (d1 & 1) { *a0 = 0; wr16(g.mesarea, (uint16_t)(rd16(g.mesarea) + 1)); }
        str = (struct Text){ NULL, ram_addr(g.mesarea) };
        goto put;
    }
city:                                                     /* the team's city (the String at word 4 of its data) */
    str = roster_text(&rosters_team(tm->tmteam)->city);
    goto append;
roster:                                                   /* player d1's name in team a1's roster */
    str = roster_text(&rosters_team(tm->tmteam)->player[d1].name);
append:
    g.PlayerScrollCtr++;
    d0 = (int16_t)(d0 + text_len(str) - 1);
    d1 = (uint16_t)(text_len(str) - 2);
    if (text_byte(str, 1u + (uint32_t)(int16_t)d1) == 0) d1--;   /* (not its pad byte) */
put:
    if (d0 > 0x1D) ScrollDisplayUp();                     /* not on this line: the next */
    printz(str_UpdateScoutingDisplay_1, sizeof str_UpdateScoutingDisplay_1);
    g.printx += g.DispAttribCtr;
    g.printy += g.VertLineScrolling;
    print_p(str);
    g.DispAttribCtr += d1;
}

/* setup94.asm:ScrollDisplayUp. The next line of the text box; past its seventh, the box's 8 rows
 * one row up (VRAM to VRAM DMA). */
void ScrollDisplayUp(void)
{
    g.DispAttribCtr = 0;
    g.VertLineScrolling++;
    if ((int16_t)g.VertLineScrolling < 7) return;
    g.VertLineScrolling--;
    uint16_t d1 = (uint16_t)(g.maps[0].vram + 0x212);
    for (int i = 0; i < 8; i++) {
        uint16_t d2 = (uint16_t)(d1 + 0x80);
        DoDMA_nd2(0x3A, d1, d2);
        d1 = d2;
    }
}

/* setup94.asm:EASportsScreen. The EA Sports screen, faded in with the fades-only vblank,
 * until a button or $50 x 4 frames (about 5 s). */
void EASportsScreen(void)
{
    g.VBint = SYM_vb2;
    g.disflags &= (uint16_t)~HI(df32c);
    g.maps[2].colshift = 5;
    g.maps[1].vram = 0xA000;
    g.maps[1].colshift = 7;
    g.maps[0].vram = 0xC000;
    g.maps[0].colshift = 7;
    g.maps[2].vram = 0xF000;
    g.VSPRITES = 0xF800;
    g.VSCRLPM = 0xFC00;
    memset(g.palfadenew, 0, sizeof g.palfadenew);        /* all 64 colours black */
    CopyPaletteToCRAM();
    setVram_0();
    printz(str_EASportsScreen_1, sizeof str_EASportsScreen_1);
    uint32_t img = ART(EASportsMap), map = img + rd32(rom_ptr(img + 4));
    dobitmap(img + rd32(rom_ptr(img)), map, img + 8, 0, 0, rd16(rom_ptr(map)), rd16(rom_ptr(map) + 2), 0, 0xF);
    g.palfadenew[0] = 0xEEE;
    g.palfadenew[7] = 0;
    g.palcount = 0x18;                                    /* fade in */
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

/* setup94.asm:UpdatePlayoffLevel. In the new playoffs: pojoy = 7 (or $B with FourWayPlay) -
 * OptNOP. */
void UpdatePlayoffLevel(void)
{
    if ((int16_t)g.OptPlayMode < 2 || g.OptPlayMode == 4) return;
    g.pojoy = (uint16_t)((g.FourWayPlay ? 0xB : 7) - g.OptNOP);
}

/* setup94.asm:TeamIcons. The visitors' and the home team's bitmaps (DrawTeamBitmap)
 * with their colours from TeamPalettes (the menu's teams in Regular Season and Shootout), then
 * the team structs (setteams) and the palette at once. */
void TeamIcons(void)
{
    if (g.OptPlayMode == 4 || g.OptPlayMode == 0) {
        g.HomeTeam = g.Opt1team;
        g.VisTeam = g.Opt2team;
    }
    printz(str_TeamIcons_1, sizeof str_TeamIcons_1);
    uint16_t t = g.VisTeam;
    pal_load_w(0x13, &rosters_team(t)->matchup[1][3], 2);          /* (TeamPalettes: colours 3 and 4 of its second) */
    DrawTeamBitmap(t, g.teambitmapchars);
    printz(str_TeamIcons_2, sizeof str_TeamIcons_2);
    t = g.HomeTeam;
    pal_load_w(0x11, &rosters_team(t)->matchup[0][1], 2);          /* (colours 1 and 2 of its first) */
    g.palfadenew[0x15] = 0xEEE;
    DrawTeamBitmap(t, 2);
    setteams();
    g.palcount = 0x64;
}

/* setup94.asm:DrawTeamBitmap. Rows d1 * 2 and the next of TeamBitmaps' map (no tiles) at
 * printx / printy from char d4 -> d4. A roster set with banners of its own draws each from its 22
 * tiles, put into the first 22 chars of the set (the home team's at char 2, as loaded; the
 * visitors' remapped, as LoadSetupTiles loads theirs): one banner of a set shows at a time. */
uint16_t DrawTeamBitmap(uint16_t team, uint16_t chars)
{
    uint32_t img = ART(TeamBitmaps), map = img + rd32(rom_ptr(img + 4));
    if (team < ROSTERS_MAX && rosters.banner_tiles[team]) {
        if (chars == 2) DoDMApro(rosters.banner_tiles[team], 0x16 * 16, (uint16_t)(chars << 5));
        else remap(rosters.banner_tiles[team], SetupTilesRemap(), 0x16 * 16, (uint16_t)(chars << 5));
        return dobitmap(img + rd32(rom_ptr(img)), RS(BannerSlotMap), SYM_NoTiles, 0, 0, 0xB, 2, chars, 0);
    }
    return dobitmap(img + rd32(rom_ptr(img)), map, SYM_NoTiles, 0, (uint16_t)(rosters_art(team) << 1), rd16(rom_ptr(map)), 2, chars, 0);   /* (its art) */
}

/* setup94.asm:VBlank_SetOptions. The setup screen's vblank: unless dfng, the sprites (when
 * dfok) and the fade; then Vcount and the music. */
void VBlank_SetOptions(void)
{
    if (!(g.disflags & HI(dfng))) {
        if (g.disflags & HI(dfok)) {
            g.disflags &= (uint16_t)~HI(dfok);
            DumpSprites2();
        }
        cramfade();
    }
    g.Vcount++;
    p_music_vblank();
}

/* setup94.asm:DefaultMenus. The options at power on (defmenuoptions: regular season, one
 * player, Montreal at Los Angeles, 10 minute periods ...); no demo has run yet. */
void DefaultMenus(void)
{
    ST_W(g.demoflag);
    const uint8_t *t = rom_ptr(SYM_defmenuoptions);
    g.OptPlayMode = rd16(t);
    g.OptNOP = rd16(t + 2);
    g.Opt1team = rd16(t + 4);
    g.Opt2team = rd16(t + 6);
    g.Optperlen = rd16(t + 8);
    g.OptGoalie = rd16(t + 0xA);
    g.OptUserRec = rd16(t + 0xC);
    g.OptPen = rd16(t + 0xE);
    g.OptLine = rd16(t + 0x10);
}

/* setup94.asm:ContinuePlayoffs. The playoff state saved at passbits (ReadPassBits); when a
 * game has been played, the tree (MakeTree) and OptNOP from pojoy (7 or $B - pojoy, pojoy
 * moved to the pad set-ups). */
void ContinuePlayoffs(void)
{
    ReadPassBits(pass_bits(RAM(passbits)));
    if ((g.gamelevel | g.bosgames) == 0) return;          /* nothing played yet */
    MakeTree();
    uint16_t d0 = g.FourWayPlay ? 0xB : 7;
    if (!g.FourWayPlay) {
        if ((int16_t)g.pojoy >= 2) g.pojoy -= 2;
    } else if (g.pojoy == 1) {
        g.pojoy = 3;
    } else if (g.pojoy == 2) {
        g.pojoy = 4;
    }
    g.OptNOP = (uint16_t)(d0 - g.pojoy);
}

/* setup94.asm:NewPO. New playoffs: a random tree row (from the one after a random one) that holds
 * Team 1 (the all-stars count as the last playoff team), series wins cleared unless
 * best-of-one; falls into MakeTree. */
void NewPO(void)
{
    uint16_t row = randomd0(rosters.playoff_rows), d1;
    uint16_t team = g.Opt1team > rosters.playoff_teams - 1 ? rosters.playoff_teams - 1 : g.Opt1team;
    for (;;) {
        row = (row + 1) & (rosters.playoff_rows - 1);
        const uint8_t *a0 = roster_seats[row & 31];
        for (d1 = SYM_PlayoffRowTeams - 1; *a0++ != (uint8_t)team; )   /* dbeq */
            if (d1-- == 0) break;
        if (d1 != 0xFFFF) break;
    }
    g.potreeteam = d1 ^ (SYM_PlayoffRowTeams - 1);
    g.postarts = row;
    g.gamelevel = 0;
    g.bosgames = 7;
    if (g.OptPlayMode != 2) {
        g.bosgames = 0;
        for (int n = 0; n < 8; n++) {
            g.matchups[n].gspotwins = 0;
            g.matchups[n].gspobwins = 0;
        }
    }
    MakeTree();
}

/* setup94.asm:MakeTree. The playoff tree (potree): the first round's 16 teams from playoffseats
 * row postarts, each later round's winners by WinBits; then this round's games: scores
 * cleared, their teams (in tree order for series games 2, 3 and 5, else flipped), FigureJoy. */
void MakeTree(void)
{
    uint8_t *tree = g.potree;
    memcpy(tree, roster_seats[g.postarts & 31], 16);
    uint8_t *a0 = tree, *a1 = tree + 16;
    uint16_t bits = g.WinBits;
    for (int i = 0; i < 15; i++, a0 += 2, bits >>= 1) *a1++ = a0[bits & 1];
    uint16_t first;
    int16_t n = (int16_t)GetShifter(&first);
    if (n < 0) return;                                    /* the playoffs are over */
    a0 = tree + (int16_t)(first * 2);
    for (struct Matchup *gs = g.matchups; n >= 0; n--, gs++) {
        gs->gss1 = 0;
        gs->gss2 = 0;
        gs->gsper = 0;
        gs->gsflags &= (uint8_t)~MU_HIGHLIGHT;
        if (g.bosgames == 2 || g.bosgames == 3 || g.bosgames == 5) {
            gs->gsflags &= (uint8_t)~MU_FLIPPED;
            gs->gst1 = (uint16_t)((gs->gst1 & 0xFF00) | *a0++);
            gs->gst2 = (uint16_t)((gs->gst2 & 0xFF00) | *a0++);
        } else {
            gs->gsflags |= MU_FLIPPED;
            gs->gst2 = (uint16_t)((gs->gst2 & 0xFF00) | *a0++);
            gs->gst1 = (uint16_t)((gs->gst1 & 0xFF00) | *a0++);
        }
    }
    FigureJoy();
}

/* setup94.asm:FigureJoy. The teams of the pads (cont1team ... cont4team). In the playoffs (not
 * a shootout) find this round's game of the playoff team and take its teams and the pads from
 * .pojoylist (.pojoylist2 with FourWayPlay) by pojoy; otherwise from .noplist by OptNOP, and
 * random matchups for the other games (InitializeGameStructures), as after a demo. */
void FigureJoy(void)
{
    static const uint8_t pojoylist[8] = { 0, 1, 0, 0, 0, 1, 0, 1 };      /* its first two pairs */
    g.cont1team = g.cont2team = g.cont3team = g.cont4team = 0;
    if (g.demoflag != 0) {
        if (g.OptPlayMode != 0 && !(g.gmode2 & HI(0))) {
            uint16_t first, n = GetShifter(&first);       /* (games this round - 1) */
            uint16_t team = g.potree[g.potreeteam];
            struct Matchup *gs = &g.matchups[n];
            uint16_t d0;
            ST_W(g.gamenum);
            for (;;) {
                if (gs->gst1 == team) {
                    d0 = 0;
                    g.Opt2team = gs->gst2;
                    g.Opt1team = gs->gst1;
                    break;
                }
                if (gs->gst2 == team) {
                    d0 = g.FourWayPlay ? 5 : 4;
                    g.Opt2team = gs->gst1;
                    g.Opt1team = gs->gst2;
                    break;
                }
                if (n-- == 0) return;                     /* the playoff team is out */
                gs = &g.matchups[n];
            }
            d0 = (uint16_t)((d0 + g.pojoy) << 2);
            g.gamenum = n;
            g.HomeTeam = gs->gst1;
            g.VisTeam = gs->gst2;
            uint32_t list = rom_find(SYM_FigureJoy, SYM_InitializeGameStructures, pojoylist, 8);
            if (g.FourWayPlay) list += 0x20;              /* .pojoylist2 follows */
            g.cont1team = rd16(rom_ptr(list + d0));
            g.cont2team = rd16(rom_ptr(list + d0 + 2));
            if (g.FourWayPlay) {
                if (d0 == 4 || d0 == 0x18) {
                    g.cont3team = g.cont1team;
                    g.cont4team = g.cont2team;
                } else if (d0 == 8 || d0 == 0x1C) {
                    g.cont3team = g.cont1team;
                    g.cont4team = 0;
                }
            }
            return;
        }
        uint16_t d0 = (uint16_t)(g.OptNOP << 2);          /* (past OptNOP 6: the code after the table) */
        uint32_t list = SYM_InitializeGameStructures - 7 * 4;   /* .noplist, just before it */
        g.cont1team = rd16(rom_ptr(list + d0));
        g.cont2team = rd16(rom_ptr(list + d0 + 2));
        if (d0 == 0x14) {                                 /* Three */
            g.cont3team = g.cont1team;
            g.cont4team = 0;
        } else if (d0 == 0x18) {                          /* Four */
            g.cont3team = g.cont1team;
            g.cont4team = g.cont2team;
        }
    }
    InitializeGameStructures();
}

/* setup94.asm:InitializeGameStructures. Random team pairs for the 8 gstruct games, none of
 * them HomeTeam or VisTeam. */
void InitializeGameStructures(void)
{
    ST_W(g.gamenum);
    uint32_t used = 1u << (g.HomeTeam & 31);
    used |= 1u << (g.VisTeam & 31);
    for (int n = 0; n < 8; n++) {
        g.matchups[n].gst1 = GetRandomUnusedTeam(&used);
        g.matchups[n].gst2 = GetRandomUnusedTeam(&used);
    }
}

/* setup94.asm:GetRandomUnusedTeam. -> d0 = a random playoff team whose bit in d3 is clear; set it. */
uint16_t GetRandomUnusedTeam(uint32_t *used)
{
    for (;;) {
        uint16_t t = randomd0(rosters.playoff_teams);
        uint32_t bit = 1u << (t & 31);
        int was = (*used & bit) != 0;
        *used |= bit;
        if (!was) return t;
    }
}

/* setup94.asm:ReadPassBits. The playoff state from the 5 saved words at a3 (kept): each game's
 * series wins (bit 2 of its flags: a series won), WinBits, pojoy (1 / 2 become 4 without
 * FourWayPlay), potreeteam, gamelevel, bosgames, postarts. */
void ReadPassBits(uint8_t *bits)
{
    uint16_t saved[5];
    for (int i = 0; i < 5; i++) saved[i] = rd16(bits + 2 * i);
    for (int n = 7; n >= 0; n--) {
        struct Matchup *gs = &g.matchups[n];
        gs->gsflags &= (uint8_t)~MU_SERIES_OVER;
        uint16_t w = SuperDiv(bits, 5);
        gs->gspobwins = w;
        if (w == 4) gs->gsflags |= MU_SERIES_OVER;
        w = SuperDiv(bits, 5);
        gs->gspotwins = w;
        if (w == 4) gs->gsflags |= MU_SERIES_OVER;
    }
    g.WinBits = SuperDiv(bits, 0x4000);
    g.pojoy = SuperDiv(bits, 8);
    if (!g.FourWayPlay && (g.pojoy == 1 || g.pojoy == 2)) g.pojoy = 4;
    g.potreeteam = SuperDiv(bits, 0x10);
    g.gamelevel = SuperDiv(bits, 4);
    g.bosgames = SuperDiv(bits, 8);
    g.postarts = SuperDiv(bits, 0x20);
    for (int i = 0; i < 5; i++) wr16(bits + 2 * i, saved[i]);
}

/* setup94.asm:EncodePW. After a playoff game (not Demo, Regular Season or a shootout): the results
 * (ResolveGames), the tree, and the state saved (WritePassBits to passbits, WriteLineData);
 * the playoffs over (or the user's team out): cleared, and the mode back to new playoffs. */
void EncodePW(void)
{
    if (g.OptNOP == 0 || g.OptPlayMode == 0 || g.OptPlayMode == 4) return;
    g.OptPlayMode = 1;                                    /* continue playoffs */
    g.TempOptPlayMode = 1;
    ResolveGames();
    MakeTree();
    if (g.gamelevel != 4 && (int16_t)g.gamenum >= 0) {
        WritePassBits(pass_bits(RAM(passbits)));
        WriteLineData();
        if (!(g.sflags3 & (1 << sf3alttree))) return;
        ReadPassBits(pass_bits(RAM(tpassbits)));                          /* the bits before the round was settled */
        MakeTree();
        return;
    }
    ClrPassBits(pass_bits(RAM(passbits)));
    WriteLineData();
    g.OptPlayMode = 2;                                    /* new playoffs */
    g.TempOptPlayMode = 2;
    if (g.bosgames == 7) return;
    g.OptPlayMode = 3;                                    /* new playoffs, best of 7 */
    g.TempOptPlayMode = 3;
}

/* setup94.asm:WritePassBits. The playoff state into the 5 words at a3 (as ReadPassBits reads it):
 * postarts, bosgames, gamelevel, potreeteam, pojoy, WinBits, then each game's series wins. */
void WritePassBits(uint8_t *bits)
{
    ClrPassBits(bits);
    PushBits(bits, g.postarts, 0x20);
    PushBits(bits, g.bosgames, 8);
    PushBits(bits, g.gamelevel, 4);
    PushBits(bits, g.potreeteam, 0x10);
    PushBits(bits, g.pojoy, 8);
    PushBits(bits, g.WinBits, 0x4000);
    for (int n = 0; n < 8; n++) {
        PushBits(bits, g.matchups[n].gspotwins, 5);
        PushBits(bits, g.matchups[n].gspobwins, 5);
    }
}

/* setup94.asm:PushBits. bits = bits x d1 + d0 (d1 the value's range, 2-$4000). */
void PushBits(uint8_t *bits, uint16_t v, uint16_t range)
{
    SuperMult(bits, range);
    SuperAdd(bits, v);
}

/* setup94.asm:ClrPassBits. Clear the 5 words at a3. */
void ClrPassBits(uint8_t *bits)
{
    for (int i = 0; i < 10; i++) bits[i] = 0;
}

/* setup94.asm:SuperAdd. The 5-word number at a3 plus d0 (a long). */
void SuperAdd(uint8_t *bits, uint32_t v)
{
    add_long_carry(bits + 6, v, 3);
}

/* setup94.asm:SuperMult. The 5-word number at a3 times d0 (a word): each word's product added in
 * from the lowest (the highest's spills into the word below a3, which the game's numbers never
 * reach). */
void SuperMult(uint8_t *bits, uint16_t m)
{
    uint16_t w[5];
    for (int i = 0; i < 5; i++) { w[i] = rd16(bits + 2 * i); wr16(bits + 2 * i, 0); }
    for (int16_t i = 4; i >= 0; i--) add_long_carry(bits + 2 * i - 2, (uint32_t)m * w[i], i);
}

/* ---- the playoff tree (game setup) ---------------------------------------------------------- */


/* setup94.asm:SuperDiv. Divide the 5-word number at a3 by d0 in place -> the remainder. */
uint16_t SuperDiv(uint8_t *bits, uint16_t div)
{
    uint32_t d2 = 0;
    for (int i = 0; i < 5; i++) {
        d2 = m68k_divu((d2 & 0xFFFF0000) | rd16(bits + 2 * i), div);     /* the remainder rides in the high word */
        wr16(bits + 2 * i, (uint16_t)d2);
    }
    return (uint16_t)(d2 >> 16);
}

/* setup94.asm:GetShifter. -> d1 = games in a playoff round - 1, d2 = its first bit of WinBits
 * (by gamelevel). */
uint16_t GetShifter(uint16_t *first_bit)
{
    uint16_t d1 = 0x10, d2 = (uint16_t)-0x10;
    uint16_t n = g.gamelevel;
    do { d2 += d1; d1 >>= 1; } while (n-- != 0);        /* dbf: gamelevel + 1 times */
    if (first_bit) *first_bit = d2;
    return (uint16_t)(d1 - 1);
}

/* setup94.asm:AddPlayoffTeamStats. In the playoffs: add the playoff team's game stats (the $68
 * bytes from team struct + $B4) to its unpacked totals and pack them back, each clamped to its
 * bit width. */
void AddPlayoffTeamStats(void)
{
    if ((int16_t)g.OptPlayMode < 1) return;
    ReadTeamStats();
    uint16_t team = g.potree[g.potreeteam];
    struct Team *tm = team_struct(0);
    if (tm->tmteam != team) tm = team_struct(1);
    const uint8_t *st = (const uint8_t *)tm + offsetof(struct Team, tmgoals);   /* (goals, assists, shots, penalty minutes: four tables of 26) */
    uint8_t *tot = g.statsbuffer;
    for (int i = 0; i < 0x68; i++) wr16(tot + 2 * i, (uint16_t)(rd16(tot + 2 * i) + st[i]));
    const uint8_t *widths = rom_ptr(SYM_BitWidthTable);
    uint16_t pos = 0;
    for (int16_t n = 0x67; n >= 0; n--, tot += 2) {
        uint32_t v = rd16(tot);
        uint8_t w = widths[n & 3];
        uint32_t max = (uint16_t)((1u << (w & 31)) - 1);
        if ((int16_t)v > (int16_t)max) v = max;
        uint32_t keep = ~max;
        unsigned r = pos & 0xF;
        keep = r ? (keep << r | keep >> (32 - r)) : keep;   /* rol.l */
        v = r ? (v << r | v >> (32 - r)) : v;
        uint8_t *p = stat_long(pos);
        wr32(p, (rd32(p) & keep) | v);
        pos += w;
    }
}

/* setup94.asm:ReadTeamStats. Unpack the $68 playoff stat totals of the bit stream (widths from
 * BitWidthTable by stat & 3) into statsbuffer words. */
void ReadTeamStats(void)
{
    uint8_t *out = g.statsbuffer;
    const uint8_t *widths = rom_ptr(SYM_BitWidthTable);
    uint16_t pos = 0;
    for (int16_t n = 0x67; n >= 0; n--, out += 2) {
        uint32_t v = rd32(stat_long(pos)) >> (pos & 0xF);
        uint8_t w = widths[n & 3];
        pos += w;
        wr16(out, (uint16_t)(v & (uint16_t)((1u << (w & 31)) - 1)));
    }
}

/* setup94.asm:ResolveGames. After a playoff game: its score into its game struct, then this
 * round's results. Best of seven: a win to each unfinished series' winner; past game seven, or
 * when the user's series is over (the rest of the round decided at random, the bits before kept
 * at tpassbits, sf3alttree), the next round: WinBits from the series won, gamelevel + 1. Best of
 * one: WinBits from the scores (by the games' flipped flags). */
void ResolveGames(void)
{
    struct Matchup *gs = &g.matchups[g.gamenum];
    gs->gss1 = g.teams[0].tmscore;
    gs->gss2 = g.teams[1].tmscore;
    uint16_t first;
    int16_t n = (int16_t)GetShifter(&first);
    uint16_t won = 0;
    if (g.bosgames == 7) {                                /* best of one */
        for (; n >= 0; n--) {
            const struct Matchup *a0 = &g.matchups[n];
            if (!(a0->gss1 > a0->gss2)) won |= (uint16_t)(1u << n);
            if (a0->gsflags & MU_FLIPPED) won ^= (uint16_t)(1u << n);
        }
        goto winbits;
    }
    for (; n >= 0; n--) {                                 /* the series' wins */
        struct Matchup *a0 = &g.matchups[n];
        if (a0->gspotwins == 4 || a0->gspobwins == 4) continue;
        int bottom = (a0->gsflags & MU_FLIPPED) != 0;          /* (team1 the tree's bottom team) */
        if ((int16_t)(a0->gss1 - a0->gss2) < 0) bottom = !bottom;
        if (bottom) a0->gspobwins++; else a0->gspotwins++;
    }
    if (++g.bosgames != 7) {
        gs = &g.matchups[g.gamenum];
        if (gs->gspotwins != 4 && gs->gspobwins != 4) return;
        if ((int16_t)g.gamelevel < 3) {                   /* the user's series is over: the round's others */
            n = (int16_t)GetShifter(&first);
            for (; n >= 0; n--) {
                struct Matchup *a0 = &g.matchups[n];
                if ((uint16_t)n == g.gamenum) continue;
                while (a0->gspotwins != 4 && a0->gspobwins != 4) {
                    a0->gspotwins++;
                    if (randomd0(0xC8) & 1) {
                        a0->gspotwins--;
                        a0->gspobwins++;
                    }
                }
            }
            WritePassBits(pass_bits(RAM(tpassbits)));
            g.sflags3 |= 1 << sf3alttree;
        }
    }
    g.bosgames = 0;                                       /* the next round */
    n = (int16_t)GetShifter(&first);
    for (; n >= 0; n--) {
        struct Matchup *a0 = &g.matchups[n];
        if (a0->gspotwins != 4) won |= (uint16_t)(1u << n);
        a0->gspotwins = 0;
        a0->gspobwins = 0;
    }
winbits:
    g.WinBits &= (uint16_t)((uint16_t)(1u << first) - 1);
    g.WinBits |= (uint16_t)(won << first);
    g.gamelevel++;
}

/* setup94.asm:ShowThreeStars. Game over, once the referee animation is far enough (RefCnt <= $40)
 * and once only (gmode bit 7): the "Stars of the Game" box with the three best star scores, and
 * the home win song. */
void ShowThreeStars(void)
{
    if ((int16_t)g.RefCnt > 0x40) return;
    if (g.gmode & 0x80) return;
    g.gmode |= 0x80;
    g.disflags |= HI(dfclock);
    uint16_t v = g.Vcount;
    while (g.Vcount == v) hal_wait_vblank();
    PRINTZ(str_ShowThreeStars_1);
    Framer(0x1C, 9);
    PRINTZ(str_ShowThreeStars_2);
    uint32_t map = ART(Rinktilelist) + rd32(rom_ptr(ART(Rinktilelist) + 4));
    dobitmap(0, map, SYM_NoTiles, 0xD, 0x5B, 6, 4, g.rinkvrcset, 0);
    CalcStarScores(6);                                   /* d2 still 6 from dobitmap */
    g.printy = 0x12;
    for (int i = 0; i < 3; i++) {
        struct Team *tm;
        uint16_t player = TakeTopStarScore(&tm);
        g.printx = 0x1A;
        g.printy++;
        print_p(roster_text(&rosters_team(tm->tmteam)->abbr));
        uint32_t name = getname(player, tm);
        g.printx = 3;
        print(name);
    }
    if ((int16_t)(team_struct(0)->tmscore - team_struct(1)->tmscore) > 0) song(SYM_SFXhomewin);
}

/* setup94.asm:TakeTopStarScore. Take the highest of the 52 star scores (and clear it) -> d0 = the
 * player slot, a2 = his team. (When no score is above 0 the 68k clears through a1 as the caller
 * left it; the scores always include time on ice or shots, so the C takes slot 0.) */
uint16_t TakeTopStarScore(struct Team **tm)
{
    uint8_t *s0 = star_scores(), *s = s0, *best = s;
    int32_t top = 0;
    for (int i = 0; i < 0x34; i++, s += 4)
        if (!(top >= (int32_t)rd32(s))) { top = (int32_t)rd32(s); best = s; }
    wr32(best, 0);
    uint16_t n = (uint16_t)((best - s0) >> 2);
    *tm = team_struct(0);
    if (n >= SYM_RosterSlots) { n -= SYM_RosterSlots; *tm = team_struct(1); }
    return n;
}

/* setup94.asm:CalcStarScores. The star score of every roster slot of both teams to ThreeStars
 * (26 longs each, home first); goalies need d5 = period time + d2 on the ice. In overtime the
 * scorer of the winning goal is first star. */
void CalcStarScores(uint16_t d2)
{
    uint16_t mintime = (uint16_t)(GetPeriodTime() + d2);
    uint8_t *out = star_scores();
    CalcTeamStarScores(team_struct(0), team_struct(1), &out, mintime);
    int16_t diff = CalcTeamStarScores(team_struct(1), team_struct(0), &out, mintime);
    if (g.gsp != 3 || diff == 0) return;
    const uint8_t *e = last_goal();
    uint8_t slot = (uint8_t)((e[2] & 0x80 ? SYM_RosterSlots : 0) + e[3]);
    wr32(star_scores() + (uint16_t)(slot << 2), 0x7FFFFFFF);
}

/* setup94.asm:CalcTeamStarScores. Team a2 against a3: a long per roster slot to (a4)+, each from
 * the goal difference. Skaters: goals * 11000 + assists * 10100 + shots * 10, in a tied game
 * shots * 1000 + time on ice. Goalies on the ice at least d5 who faced shots: +32000 for at most
 * 4 goals per 100 shots, +75000 more for a shutout. -> d3 = the goal difference. */
int16_t CalcTeamStarScores(struct Team *tm, struct Team *other, uint8_t **out, uint16_t mintime)
{
    int16_t diff = (int16_t)(tm->tmscore - other->tmscore);
    uint16_t goalies = (uint16_t)(0x19 - GetGoalieCount(tm));
    for (int16_t slot = 0x19, k = 0; slot >= 0; slot--, k++, *out += 4) {                /* (k: the roster slot) */
        uint32_t score = (uint32_t)(int32_t)diff;
        if ((uint16_t)slot > goalies) {                  /* goalie slot */
            if (mintime > tm->tmpice[k]) { wr32(*out, score); continue; }
            uint16_t shots = tm->tmpshots[k];
            if (shots == 0) { wr32(*out, score); continue; }
            uint16_t pct = (uint16_t)m68k_divu((uint32_t)tm->tmgoals[k] * 100, shots);
            if (pct <= 4) {
                score += 32000;
                if (pct == 0) score += 75000;
            }
            wr32(*out, score);
            continue;
        }
        score += tm->tmgoals[k] * 11000u + tm->tmassists[k] * 10100u;
        uint32_t shots = tm->tmpshots[k] * 10u;
        if (diff == 0) score += (uint16_t)shots * 100u + tm->tmpice[k];
        else score += shots;
        wr32(*out, score);
    }
    return diff;
}

/* setup94.asm:ShowInjuryBox. "Injury to:" player TempPlOffset, "Out for period" ("Out for game"
 * with sflags7 bit 5, "the game" after a fight). */
void ShowInjuryBox(void)
{
    PRINTZ(str_ShowInjuryBox_1);
    Framer(0x12, 5);
    if (g.sflags7 & 0x20) PRINTZ(str_ShowInjuryBox_3);
    else PRINTZ(str_ShowInjuryBox_2);
    print(GetPlayerNumInitial());
    if (sortobj(puckscnum)->pflags2 & (1 << pf2fight)) PRINTZ(str_ShowInjuryBox_4);
}

/* setup94.asm:PenaltyShotBox. Penalty shot box (gmode2 bit 7): "PENALTY SHOT!", the shooter, the
 * penalty and who committed it. In a shootout the shootout box instead. */
void PenaltyShotBox(void)
{
    g.gmode2 |= HI(7);
    if (g.gmode2 & HI(0)) { ShootoutModeBox(); return; }
    prefmes(-1);
    PRINTZ(str_PenaltyShotBox_1);
    Framer(0x1B, 8);
    printbig(SYM_PenShotBigTxt);
    uint16_t shooter = sortobj((int16_t)g.BA_Sktr_SCnum)->pnum;
    print(FormatPlayerNumInitial(shooter, team_struct(g.BA_Team != 0)));
    PRINTZ(str_PenaltyShotBox_2);
    const uint8_t *list = rom_ptr(SYM_Penaltylist);
    print(SYM_Penaltylist + (int16_t)rd16(list + (int16_t)g.pspenalty) + 2);
    PRINTZ(str_PenaltyShotBox_3);
    print(FormatPlayerNumInitial(g.BA_Checker_Offset, team_struct(g.BA_Team == 0)));
}

/* setup94.asm:ShowGoalBox. The goal box: close both line change boxes, frame it (more rows per
 * assist), "GOAL!" ("PP GOAL!" after a delayed penalty, "HAT TRICK!" on a skater's third goal,
 * which starts the home crowd animation for the home team), the scorer with his goals and the
 * assists with theirs. */
void ShowGoalBox(void)
{
    if (g.BA_PS_flags & HI(2)) g.sflags2 |= HI(sf2drec);
    lcfound_close(team_struct(0));
    lcfound_close(team_struct(1));
    const uint8_t *e = last_goal();
    PRINTZ(str_ShowGoalBox_1);
    uint16_t rows = 5;
    if (!(e[4] & 0x80)) {
        rows += 2;
        if (!(e[5] & 0x80)) rows++;
    }
    Framer(0x13, rows);
    CountGoalies();
    struct Team *tm = team_struct(0);
    g.scorergoalies = g.homegoalies;
    if (e[2] & 0x80) { tm = team_struct(1); g.scorergoalies = g.awaygoalies; }
    uint32_t text = SYM_GoalBigTxt;
    if (g.DelayedPen & 1) { g.DelayedPen &= (uint8_t)~1; text = SYM_PPGoalBigTxt; }
    if (tm->tmgoals[e[3]] == 3 && e[3] >= g.scorergoalies) {          /* third goal of a skater */
        text += rd16(rom_ptr(text));                               /* HAT TRICK! */
        if (tm->tmteam == g.HomeTeam) StartArenaAnim(0);
    }
    printbig(text);
    uint16_t scorer = e[3];
    g.printx = 0xC;
    print(FormatPlayerName(scorer, tm));
    if (scorer >= g.scorergoalies) PrintPlayerGoals(scorer, tm);
    if (!(e[4] & 0x80)) {
        int none = (g.sflags4 & HI(5)) != 0;
        g.sflags4 &= (uint16_t)~HI(5);
        if (!none) {
            PRINTZ(str_ShowGoalBox_2);
            print(FormatPlayerName(e[4], tm));
            PrintPlayerAssists(e[4], tm);
            if (!(e[5] & 0x80)) {
                PRINTZ(str_ShowGoalBox_3);
                print(FormatPlayerName(e[5], tm));
                PrintPlayerAssists(e[5], tm);
            }
        }
    }
    g.sflags4 &= (uint16_t)~HI(5);
}

/* setup94.asm:box. Clear a $13 x 8 rectangle at x $B, y 2 of map 1. */
void box(void)
{
    PRINTZ(str_box_1);
    eraser(0x13, 8, 0x7FF);
}

/* setup94.asm:GetPlayerName. -> a1 = mesarea "NN First Last" for player TempPlOffset. */
uint32_t GetPlayerName(void)
{
    uint16_t player;
    struct Team *tm = temp_player(&player);
    return getname(player, tm);
}

/* setup94.asm:getname. d0 = roster slot, a2 = team -> a1 = mesarea "NN First Last" (the
 * number, then appendz / appstring). */
uint32_t getname(uint16_t player, struct Team *tm)
{
    const struct RosterText *name = GetPlayerNamePointer(player, tm);
    uint8_t *mes = g.mesarea;
    uint32_t mes_addr = ram_addr(mes);
    wr16(mes, 4);
    ConvertByteToDigits(name_number(name), mes + 2);
    appendz(str_getname_1, sizeof str_getname_1, mes_addr);
    appstring_p(roster_text(name), mes_addr);
    return mes_addr;
}

/* setup94.asm:GetPlayerNumInitial. -> a1 = mesarea "NN F. Last" for player TempPlOffset. */
uint32_t GetPlayerNumInitial(void)
{
    uint16_t player;
    struct Team *tm = temp_player(&player);
    return FormatPlayerNumInitial(player, tm);
}

/* setup94.asm:FormatPlayerNumInitial. d0 = roster slot, a2 = team -> a1 = mesarea "NN F. Last". */
uint32_t FormatPlayerNumInitial(uint16_t player, struct Team *tm) { return format_name(player, tm, 1); }

/* setup94.asm:FormatPlayerName. d0 = roster slot, a2 = team -> a1 = mesarea "NN Last". */
uint32_t FormatPlayerName(uint16_t player, struct Team *tm) { return format_name(player, tm, 0); }

/* setup94.asm:FormatPlayerNameLast. d0 = roster slot, a2 = team -> a1 = mesarea "Last". */
uint32_t FormatPlayerNameLast(uint16_t player, struct Team *tm)
{
    return FormatLastName(GetPlayerNamePointer(player, tm), text_buffer());
}

/* setup94.asm:FormatPlayerNameShort. d0 = roster slot, a2 = team -> a1 = mesarea " Last". */
uint32_t FormatPlayerNameShort(uint16_t player, struct Team *tm)
{
    uint8_t *out = text_buffer();
    *out++ = ' ';
    return FormatLastName(GetPlayerNamePointer(player, tm), out);
}

/* setup94.asm:FormatLastName. Name string a0 (a length word String): from after the first space
 * (the first name) to its end or a 0 byte, to a1, space padded to TextBuffer + 12 -> a1 =
 * mesarea (FinalizeTextBuffer). */
uint32_t FormatLastName(const struct RosterText *name, uint8_t *out)
{
    const uint8_t *p = name->s;
    const uint8_t *end = p + rd16(p);
    p += 2;
    while (*p++ != ' ') {}
    do *out++ = *p++; while (p != end && *p != 0);
    for (const uint8_t *lim = text_buffer() + 12; out < lim;) *out++ = ' ';
    return FinalizeTextBuffer(out);
}

/* setup94.asm:FinalizeTextBuffer. a1 = the end of the text after mesarea's length word: set the
 * length (with a pad byte when odd) -> a1 = mesarea. */
uint32_t FinalizeTextBuffer(uint8_t *end)
{
    uint8_t *mes = g.mesarea;
    uint16_t len = (uint16_t)(end - mes);
    if (len & 1) { *end = 0; len++; }
    wr16(mes, len);
    return ram_addr(mes);
}

/* setup94.asm:GetPlayerNamePointer. d0 = roster slot, a2 = team -> a0 = the player's name. */
const struct RosterText *GetPlayerNamePointer(uint16_t player, struct Team *tm) { return &rosters_team(tm->tmteam)->player[player].name; }

/* setup94.asm:ConvertByteToDigits. d0 = BCD byte: two ASCII digits to (a1)+, a leading 0 as a
 * space. -> a1 */
uint8_t *ConvertByteToDigits(uint16_t bcd, uint8_t *dst)
{
    uint8_t hi = (uint8_t)((uint8_t)bcd >> 4);
    if (hi == 0) hi = 0xF0;
    *dst++ = (uint8_t)(hi + '0');
    *dst++ = (uint8_t)((bcd & 0xF) + '0');
    return dst;
}
