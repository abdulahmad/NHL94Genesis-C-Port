/* replay94.asm: the replay recorder (updatereplay) and the instant replay (ReplayMode)
 * with its navigation helpers. */
#include <string.h>
#include "game.h"
#include "symbols_gen.h"
#include "view.h"
#include "hal.h"
#include "rom.h"
#include "replay_strings_gen.h"

#define REPLAY_FRAME 0x62                 /* bytes per recorded frame (sizeof (struct ReplayFrame)) */
#define REPLAYEND 0xFFFFAF54u             /* the replay buffer is work RAM from M68K_RAM up to here */
#define M68K_RAM RAM_BASE

/* the replay frame at a4 (the game keeps the frames' 68000 addresses: recbpr) */
static struct ReplayFrame *replay_frame(uint32_t a4) { return RAM_AT(struct ReplayFrame, ram_at(a4)); }

/* ReplayMode's .dirtab: the camera step per d-pad direction 0-7 (x, y) */
static const int16_t replay_dirtab[16] = { 0, 2, 2, 2, 2, 0, 2, -2, 0, -2, -2, -2, -2, 0, -2, 2 };

/* replay94.asm:ReplayMode. The instant replay, from the pause menu. Rewind to the oldest
 * frame, then each pass (at most one recorded frame per vblank, d7 = elapsed): the d-pad moves
 * the camera or picks the nearest object in that direction to follow, A rewinds, B steps or
 * fast-forwards, C plays / stops; start shows the banner, where start again resumes the game,
 * B leaves, C reverses the angle. Leaving restores the frame at the record point and the rink. */
void ReplayMode(void)
{
    g.sflags5 &= (uint8_t)~1;
    forceblack();
    uint16_t saved = g.disflags;
    g.sflags2 |= HI(sf2replay);
    g.sflags4 &= (uint16_t)~HI(4);
    ClrHor();
    DoFill(0x400, g.maps[2].vram, 0x7FF);
    DecompressGraphicsPlain(ART(ReplayMap) + 8, g.ExtraChars);
    ShowReplayIcon();
    g.sflags &= (uint16_t)~HI(sfscrl);
    g.sflags3 &= (uint8_t)~0x20;
    g.pads[3].Yfrac |= 0xFF00;                            /* st replayactive: the camera's Yfrac, its high byte */
    struct RinkObj *cam = &g.pads[3];                     /* a5, the camera (ReplayStarStruct): Xpos, Ypos its place, Yfrac the
                                                           * object it follows, Zpos < 0 none (not read while sfscrl is clear) */
    uint32_t a4 = g.recbpr;
    do elapsed = (int16_t)suba4(&a4, cam); while (elapsed != 0);
    SprSort();
    setvideo();
    g.sflags3 &= (uint8_t)~2;
    memset(g.padcont, 0, sizeof g.padcont);
    g.palcount = 0x18;
    elapsed = 1;
    for (;;) {                                            /* .top */
        while ((uint16_t)elapsed > (uint16_t)(g.vcount - g.oldvcount)) hal_wait_vblank();
        g.oldvcount = g.vcount;
        if ((int16_t)g.replayicontimer >= 0) {
            g.replayicontimer = (uint16_t)(g.replayicontimer - elapsed);
            if ((int16_t)g.replayicontimer < 0) { g.replayicontimer = 0; EraseReplayIcon(); }
        }
        sortobj(12)->oldframe = 1;
        sortobj(13)->oldframe = 1;
        elapsed = 1;
        uint16_t d0, d1, d2, d3;
        getpzjoy(&d0, &d1, &d2, &d3);
        if (g.sflags5 & 1) {                              /* the banner is up: no d-pad */
            d0 = (uint16_t)((d0 & 0xFFF0) | 8);
            d1 &= 0xFFF0;
            d3 &= 0xFFF0;
        }
        int manual = !(d0 & 8);
        if ((g.sflags3 & 0x20) && (d1 & 0xF) == 0) manual = 0;
        if (manual) {                                     /* .dir: move or retarget the camera */
            if (cam->Xpos <= -0xA0) cam->Xpos = 0;
            if (cam->Ypos <= -0x130) cam->Ypos = 0;
            int scrolling = (g.sflags & HI(sfscrl)) != 0;
            g.sflags |= HI(sfscrl);
            if (!scrolling) { cam->Xpos = (int16_t)g.Hpos; cam->Ypos = (int16_t)g.Vpos; }
            uint16_t dir = (uint16_t)((d0 & 7) ^ 4);
            uint16_t skip = 0xFF;                         /* st d3 */
            int tracking = (g.sflags3 & 0x20) != 0;
            g.sflags3 &= (uint8_t)~0x20;
            if (tracking) skip = cam->Yfrac;
            int16_t cx = cam->Xpos, cy = cam->Ypos;
            uint32_t best = 0x100;
            for (int i = 0; i < 12; i++) {
                const struct SortObj *o = sortobj(i);
                if ((uint16_t)o->SCnum == skip) continue;
                int16_t dx = (int16_t)(cx - o->Xpos), dy = (int16_t)(cy - o->Ypos);
                uint16_t v = vtoa(dx, dy);
                if (v & 8) continue;
                if (((uint16_t)(v - dir + 1) & 7) > 2) continue;
                uint32_t dist = (uint32_t)((int32_t)dx * dx) + (uint32_t)((int32_t)dy * dy);
                if (dist <= 4 || dist > best) continue;
                best = dist;
                cam->Yfrac = (uint16_t)o->SCnum;
                g.sflags3 |= 0x20;
            }
            if (g.sflags3 & 0x20) {                       /* follow the object found */
                struct SortObj *o = sortobj((int16_t)cam->Yfrac);
                setpads(4, o);
                UpdateCameraPos(&o->rink, cam);
                setvideo();
                continue;
            }
            g.sflags |= HI(sfscrl);                       /* .scrl: move the camera */
            dir ^= 4;
            int16_t nx = (int16_t)(replay_dirtab[2 * dir] + cam->Xpos);
            if (nx <= 0x88 && nx >= -0x88) cam->Xpos = nx;
            int16_t ny = (int16_t)(replay_dirtab[2 * dir + 1] + cam->Ypos);
            if (ny <= 0x126 && ny >= -0x126) cam->Ypos = ny;
            cam->Zpos = 0;
            g.PadControlBits = (uint16_t)((g.PadControlBits & 0xFFF) | 0xE000);
            UpdateCameraPos(cam, cam);
            setvideo();
            continue;
        }
        cam->Zpos = (int16_t)(cam->Zpos | 0xFF00);        /* .nomans (st: Zpos's high byte) */
        if ((d1 & 0x20) && !(g.sflags5 & 1)) g.sflags3 ^= 1 << sf3rmplay;
        if ((d3 & 0x40) && !(g.sflags5 & 1)) {            /* A: rewind */
            for (int i = 0; i < 4; i++) elapsed = (int16_t)suba4(&a4, cam);
            SprSort();
            setvideo();
            elapsed = 1;
            g.sflags3 &= (uint8_t)~2;
        }
        if (!(g.sflags5 & 1) && (d3 & 0x10)) {            /* B */
            if (d3 & 0x40) {                              /* with A: stop tracking */
                g.sflags3 &= (uint8_t)~0x20;
                g.sflags &= (uint16_t)~HI(sfscrl);
                g.PadControlBits = (uint16_t)((g.PadControlBits & 0xFFF) | 0xE000);
                continue;
            }
            elapsed = (int16_t)adda4(&a4, cam);           /* step / fast forward */
            SprSort();
            setvideo();
            elapsed = (int16_t)(elapsed << 1);
            g.sflags3 &= (uint8_t)~2;
        }
        if (g.sflags3 & (1 << sf3rmplay)) {               /* play */
            ST_W(g.lastsfx);
            elapsed = (int16_t)adda4(&a4, cam);
            sfx((int16_t)g.lastsfx);
            SprSort();
        }
        setvideo();
        if (!(g.sflags5 & 1)) {
            if (!(d1 & 0x80)) continue;
            ShowReplayBanner();
            g.sflags3 &= (uint8_t)~2;
            int up = g.sflags5 & 1;
            g.sflags5 |= 1;
            if (!up) continue;
        }
        if (d1 & 0x80) {                                  /* start: resume the game */
            g.menu.item = 0;
            g.sflags5 |= 2;
            break;
        }
        if (d1 & 0x40) continue;
        if (d1 & 0x10) break;                             /* B: leave */
        if (!(d1 & 0x20)) continue;
        g.sflags4 |= HI(6);                               /* C: reverse angle */
        g.sflags5 &= (uint8_t)~1;
        EraseReplayBanner();
        elapsed = (int16_t)adda4(&a4, cam);
        SprSort();
    }
    forceblack();
    g.PadControlBits |= 0xF000;
    g.sflags &= (uint16_t)~HI(sfscrl);
    a4 = g.recbpr;
    g.sflags4 &= (uint16_t)~HI(4);
    RestoreReplayFrame(a4, cam);
    SprSort();
    g.sflags2 &= (uint16_t)~HI(sf2replay);
    g.disflags = saved;
    ReloadRinkGraphics();
    setupEASportsMap();
    ReloadEnergyBarTiles();
    ReloadCrowdTiles();
    ReloadRefTiles();
    if (g.disflags & HI(4)) {
        ReloadFaceOffMap();
        ReloadFaceOffTiles();
    }
    SetHor();
    setvideo();
    g.palcount = 0x18;
}

/* replay94.asm:getpzjoy. Read the joystick of the pad that paused: pads 3 and 4 on the four way
 * play (pausepad 3 or 4), else pad 2 when sfpj is set, pad 1 otherwise. */
void getpzjoy(uint16_t *d0, uint16_t *d1, uint16_t *d2, uint16_t *d3)
{
    if (g.FourWayPlay != 0 && g.pausepad != 0) {
        if (g.pausepad == 3) ReadJoy3(d0, d1, d2, d3);
        else ReadJoy4(d0, d1, d2, d3);
        return;
    }
    if (g.sflags & HI(sfpj)) ReadJoy2(d0, d1, d2, d3);
    else ReadJoy1(d0, d1, d2, d3);
}

/* replay94.asm:ShowReplayIcon. Show the replay icon (8 x 4 from the rink tiles) and start the $F0
 * frame replayicontimer. */
void ShowReplayIcon(void)
{
    g.replayicontimer = 0xF0;
    printz(str_ShowReplayIcon_1, sizeof str_ShowReplayIcon_1);
    uint32_t map = (g.sflags4 & HI(4)) ? ART(RevRinkTilelist) : ART(Rinktilelist);
    map += rd32(rom_ptr(map) + 4);
    dobitmap(0, map, SYM_NoTiles, 0x20, 0x55, 8, 4, g.rinkvrcset, 0);
}

/* replay94.asm:EraseReplayIcon. Erase the 8 x 4 replay icon. */
void EraseReplayIcon(void)
{
    printz(str_EraseReplayIcon_1, sizeof str_EraseReplayIcon_1);
    eraser(8, 4, 0x7FF);
}

/* replay94.asm:ShowReplayBanner. Show ReplayMap at 0,0 (16 x 11) and stop the icon timer. */
void ShowReplayBanner(void)
{
    ST_W(g.replayicontimer);
    printz(str_ShowReplayBanner_1, sizeof str_ShowReplayBanner_1);
    dobitmap(0, ART(ReplayMap) + rd32(rom_ptr(ART(ReplayMap)) + 4), SYM_NoTiles, 0, 0, 0x10, 0xB, g.ExtraChars, 0);
}

/* replay94.asm:EraseReplayBanner. Erase the 16 x 11 area ShowReplayBanner drew. */
void EraseReplayBanner(void)
{
    printz(str_EraseReplayBanner_1, sizeof str_EraseReplayBanner_1);
    eraser(0x10, 0xB, 0x7FF);
}

/* replay94.asm:suba4. a4 = the current replay frame. Back up one frame (wrapping from M68K_RAM to
 * the end when the buffer has wrapped) and show it. Returns d7: the frame's delay, or 0 at the
 * start of the replay. */
uint16_t suba4(uint32_t *frame, struct RinkObj *cam)
{
    uint32_t a4 = *frame;
    if (a4 == M68K_RAM) {
        if (!(g.sflags & HI(sfwrap))) goto end;
        a4 = REPLAYEND;
    }
    a4 -= REPLAY_FRAME;
    if (a4 != g.recbpr) { *frame = a4; return RestoreReplayFrame(a4, cam); }
    a4 += REPLAY_FRAME;
end:
    *frame = a4;
    RestoreReplayFrame(a4, cam);
    return 0;
}

/* replay94.asm:adda4. Step forward one frame. First handles a reverse angle switch (sflags4 bit
 * 6): fade out, reload the rink for the other angle, redraw, and show the icon again. With
 * sf2drec (recording stopped) it stops one frame before the record point. */
uint16_t adda4(uint32_t *frame, struct RinkObj *cam)
{
    if (g.sflags4 & HI(6)) {
        g.sflags4 &= (uint16_t)~HI(6);
        g.sflags4 ^= HI(4);
        uint16_t hpos = g.Hpos, vpos = g.Vpos;
        forceblack2();
        MenuWaitVblank();
        MenuWaitVblank();
        ReloadRinkGraphics();
        g.Hpos = 0;
        g.Vpos = 0;
        g.Oldrow = 0x7D0;
        for (int i = 0; i < 2; i++) { setvideo(); MenuWaitVblank(); }
        g.Vpos = vpos;
        g.Hpos = hpos;
        setvideo();
        for (int i = 0; i < 2; i++) MenuWaitVblank();
        sortobj(12)->oldframe = 1;                            /* the nets resend their tiles */
        sortobj(13)->oldframe = 1;
        RevReplayAdj(*frame, cam);
        ShowReplayIcon();
        g.palcount = 0x64;
    }
    if (g.sflags2 & HI(sf2drec)) {
        uint32_t next = *frame;
        adda43(&next);
        if (next == g.recbpr) return 0;
    }
    return adda42(frame, cam);
}

/* replay94.asm:adda42. adda4 without the look-ahead: stop at the record point (d7 = 0), else show
 * the frame and step past it. */
uint16_t adda42(uint32_t *frame, struct RinkObj *cam)
{
    if (*frame == g.recbpr) return 0;
    uint16_t d7 = RestoreReplayFrame(*frame, cam);
    adda43(frame);
    return d7;
}

/* replay94.asm:adda43. a4 += $62, wrapping from replayend to M68K_RAM. */
void adda43(uint32_t *frame)
{
    *frame += REPLAY_FRAME;
    if (*frame == REPLAYEND) *frame = M68K_RAM;
}

/* replay94.asm:UpdateCameraPos. Camera object a5 = object a3's x / y (negated for the reverse
 * angle), Hpos / Vpos clamped to the rink. */
void UpdateCameraPos(const struct RinkObj *o, struct RinkObj *cam)
{
    int16_t d0 = o->Xpos, d1 = o->Ypos;
    if (g.sflags4 & HI(7)) d0 = (int16_t)-d0;
    cam->Xpos = d0;
    if (g.sflags4 & HI(7)) d1 = (int16_t)-d1;
    cam->Ypos = d1;
    if (!(d0 < 0x3C)) d0 = 0x3C;
    if (!(d0 > -0x3C)) d0 = -0x3C;
    g.Hpos = (uint16_t)d0;
    if (!(d1 < 0x100)) d1 = 0x100;
    if (!(d1 > -0xC8)) d1 = -0xC8;
    g.Vpos = (uint16_t)d1;
}

/* replay94.asm:RevReplayAdj. Flag a reverse angle frame conversion (sflags4 bit 7), then
 * RestoreReplayFrame. */
uint16_t RevReplayAdj(uint32_t frame, struct RinkObj *cam)
{
    if (g.sflags4 & HI(4)) g.sflags4 |= HI(7);
    return RestoreReplayFrame(frame, cam);
}

/* replay94.asm:RestoreReplayFrame. Unpack replay frame a4 into the 16 objects and the other
 * replay variables (the inverse of updatereplay). Flips x / y and frames for the reverse angle.
 * Returns d7 = the frame's elapsed count. */
uint16_t RestoreReplayFrame(uint32_t frame, struct RinkObj *cam)
{
    const struct ReplayFrame *f = replay_frame(frame);
    const uint8_t *revtbl = rom_ptr(SYM_revframetbl);
    int rev = (g.sflags4 & HI(4)) != 0;
    for (int d1 = 0xF; d1 >= 0; d1--) {
        struct SortObj *o = sortobj(0xF - d1);
        uint32_t l = f->obj[0xF - d1];
        uint16_t hi = (uint16_t)(l >> 16);
        int16_t d2 = (int16_t)(l & 0x3FF);
        if (d2 & 0x200) d2 = (int16_t)(d2 | 0xFC00);
        if (rev) d2 = (int16_t)-d2;
        o->Xpos = d2;
        d2 = (int16_t)((int16_t)(uint16_t)(l >> 4) >> 6);
        if (d2 & 0x200) d2 = (int16_t)(d2 | 0xFC00);
        if (rev) {
            d2 = (int16_t)-d2;
            if (d1 == 0) d2 = (int16_t)(d2 + 2);                 /* the puck shadow */
        }
        o->Ypos = d2;
        int16_t raw = (int16_t)(((int16_t)hi >> 4) & 0x3FF);
        d2 = raw;
        if (rev) {
            d2 = (int16_t)rd16(revtbl + d2 * 2);
            if (o->SCnum == 0xF && d2 != (int16_t)SYM_SPFpuck) o->Ypos = (int16_t)0xFC00;
            if (d2 < 1 || d2 >= 0x34E) d2 = raw;
        }
        o->frame = (uint16_t)d2;
        if (d2 >= (int16_t)SYM_SPFbglass && d2 < (int16_t)(SYM_SPFbglass + 5) && rev) o->Ypos = 0x190;
        uint16_t attr = (uint16_t)(((int16_t)hi >> 3) & 0x1800);
        o->attribute = (uint16_t)((o->attribute & 0xE7FF) | attr);
        if (rev && (int16_t)o->frame >= (int16_t)SYM_SPFfight && (int16_t)o->frame < (int16_t)SYM_SPFPen)
            o->attribute ^= HI(3);                                /* the old fight frames face the other way */
    }
    for (int i = 0; i < 6; i++) {                                 /* roster numbers and positions, 2 players per 3 bytes */
        struct SortObj *a = sortobj(i * 2), *b = sortobj(i * 2 + 1);
        const uint8_t *p = f->players[i];
        a->rostnum = p[0];
        uint16_t d0 = p[1] & 0xF;
        a->position = (int16_t)(d0 == 0xF ? -1 : d0);
        b->rostnum = (uint8_t)(p[1] >> 4 | p[2] << 4);
        d0 = (p[2] >> 4) & 0xF;
        b->position = (int16_t)(d0 == 0xF ? -1 : d0);
    }
    g.puckz[0] = (uint16_t)(int16_t)(int8_t)f->puckz;
    sortobj(0xF)->Zpos = (int8_t)f->shadowz;
    g.lastsfx = (uint16_t)(int16_t)(int8_t)f->lastsfx;
    uint16_t d7 = f->elapsed;
    g.PadControlBits = (uint16_t)((g.PadControlBits & 0xF000) | (f->padbits & 0xFFF));
    g.crowdframe = f->crowdframe;
    g.glovecords = f->glovecords;
    g.PBnum = (uint16_t)((f->pbnum << 8) | (g.PBnum & 0xFF));
    g.PadControlBits34 = (uint16_t)((f->padbits34 << 4) | 0xF00F);
    if (g.sflags & HI(sfscrl)) {
        const struct RinkObj *o = cam;
        if (g.sflags3 & 0x20) {
            cam->Zpos = 0;
            o = &sortobj((int16_t)cam->Yfrac)->rink;      /* (the object it follows) */
        }
        UpdateCameraPos(o, cam);
    } else {
        g.Hpos = f->hpos;
        g.Vpos = f->vpos;
        if (rev) {
            g.Hpos = (uint16_t)-g.Hpos;
            g.Vpos = (uint16_t)-g.Vpos;
            ClampReplayView();
        }
    }
    g.sflags4 &= (uint16_t)~HI(7);
    return d7;
}

/* replay94.asm:ClampReplayView. Clamp Hpos to -$3C..$3C and Vpos to -$C8..$100. */
void ClampReplayView(void)
{
    if (view_hpos() < (int16_t)g.Hpos) g.Hpos = (uint16_t)view_hpos();   /* (the wide view's rink: less far) */
    else if (!(-view_hpos() <= (int16_t)g.Hpos)) g.Hpos = (uint16_t)-view_hpos();
    if (0x100 < (int16_t)g.Vpos) g.Vpos = 0x100;
    else if (!(-0xC8 <= (int16_t)g.Vpos)) g.Vpos = (uint16_t)-0xC8;
}

/* replay94.asm:updatereplay. Called every frame to save a replay frame at recbpr: the 16 objects
 * (x, y, frame and palette bits packed in a long each), 6 x 3 bytes of roster numbers and
 * positions, the puck heights, the last sound, the elapsed count, the pad bits and the view.
 * Not in a highlight. With sf2drec (recording stopped) the frame at recbpr is overwritten until
 * replaydelay runs out. */
void updatereplay(void)
{
    if (g.gmode & (1 << gmhl)) return;
    int advance;
    if (!(g.sflags2 & HI(sf2drec))) advance = 1;
    else if (g.replaydelay == 0) advance = 0;
    else {
        g.replaydelay--;
        advance = (int16_t)g.replaydelay >= 0;
        if (!advance) g.replaydelay = 0;
    }
    if (advance) {
        g.recbpr += REPLAY_FRAME;
        if (g.recbpr == REPLAYEND) {
            g.sflags |= HI(sfwrap);
            g.recbpr = M68K_RAM;
        }
    }
    struct ReplayFrame *f = replay_frame(g.recbpr);
    for (int i = 0; i < 16; i++) {
        const struct SortObj *o = sortobj(i);
        uint32_t l = (uint32_t)(o->Xpos & 0x3FF);                 /* clr.l, then x at 2 */
        l |= (uint32_t)(uint16_t)((uint16_t)o->Ypos << 6) << 4;  /* asl.w #6 then asl.l #4 */
        uint16_t hi = (uint16_t)(l >> 16);
        hi |= (uint16_t)(o->frame << 4);
        hi |= (uint16_t)((o->attribute & 0x1800) << 3);
        f->obj[i] = ((uint32_t)hi << 16) | (l & 0xFFFF);
    }
    for (int i = 0; i < 6; i++) {
        const struct SortObj *a = sortobj(i * 2), *b = sortobj(i * 2 + 1);
        uint8_t *p = f->players[i];
        p[0] = a->rostnum;
        uint16_t d0 = (uint16_t)a->position;
        if ((int16_t)d0 < 0) d0 = 0xF;
        p[1] = (uint8_t)((d0 & 0xF) | b->rostnum << 4);
        d0 = (uint16_t)b->position;
        if ((int16_t)d0 < 0) d0 = 0xF;
        p[2] = (uint8_t)(b->rostnum >> 4 | d0 << 4);
    }
    f->puckz = (uint8_t)g.puckz[0];
    f->shadowz = (uint8_t)sortobj(0xF)->Zpos;
    f->lastsfx = (uint8_t)g.lastsfx;
    g.lastsfx |= 0x80;
    f->elapsed = (uint8_t)elapsed;
    f->padbits = g.PadControlBits;
    f->crowdframe = g.crowdframe;
    f->glovecords = g.glovecords;
    f->pbnum = (uint8_t)(g.PBnum >> 8);
    f->padbits34 = (uint8_t)(g.PadControlBits34 >> 4);
    f->hpos = g.Hpos;
    f->vpos = g.Vpos;
}
