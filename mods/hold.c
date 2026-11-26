/* The stoppage pauses. NHL 95 PC stops play after a goal or a penalty for as long as its announcer takes ("Los Angeles
 * goal number 99, assisted by number 4 and number 7"); the cartridge goes straight back to the face off. Two rules ask
 * for the wait: RULE_HOLD (the between-play time of the PC game, announcer or not) and RULE_ANNOUNCE (the rink
 * announcer's speech must finish). Either way the face off's drop waits until that long has passed since the stoppage
 * began (the clips' lengths in 95 PC's XBRUCE2.VIV, fixed here: the game must not depend on whether a machine has the
 * file). What the game already spends on the replay and the face off set up counts towards it.
 * RULE_ANNOUNCE 2: a home goal's speech starts after the goal song, and the wait covers the song and then the speech. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "game.h"
#include "rules.h"
#include "anim_gen.h"
#include "view.h"

#define PFLAG(o, b) ((o)->pflags & (1 << (b)))

static uint16_t until;                                /* the vblank (g.Vcount) the wait ends at */
static int pending;
static int anth;                                      /* the wait is the anthems' */
static int anth_pose, anth_len, anth_lock;
#define ANTH_VPOS (-18)                                 /* (the PC shows centre ice half way down its rink view) */                        /* the anthem's line up (1 once the spots are taken); the anthem's own frames */
static uint16_t anth_t0;
/* the pace of the skate to the spots, pixels a frame: NHL 95 PC's skaters skate there at their top speed (5400 to 8400, 1.3 to 2.05
 * rink units a game frame at 60 a second: 16 x speed / 65536); its rink is 0.89 times the cartridge's along (264 against 298
 * to the end boards), so 1.5 to 2.3 of the cartridge's pixels, about its own top speed (velocity 8000: 17 x 8000 / 65536) */
#define SKATE_SPEED 2
static struct { int16_t x, y; } spot[12];
static int16_t spot_face[12];
static int face_set;                         /* the way each faced at his spot as the game set him up */

/* a goal's speech held back until its song has ended */
static struct { int on, started; uint16_t limit; int frames; struct Team *tm; int player, a1, a2; } sg;

static int wanted(void) { return RULE(RULE_HOLD) || RULE(RULE_ANNOUNCE); }

void hold_for(int frames)
{
    if (!wanted()) return;
    if (frames > 900) frames = 900;
    if (pending && (int16_t)(until - g.Vcount) > 0) until = (uint16_t)(until + frames);
    else until = (uint16_t)(g.Vcount + frames);
    pending = 1;
}

/* the anthem before the first face off (RULE_ANTHEM: src/logic.c asks with the rule's fixed length): the drop waits for it */
void hold_anthem(int frames)
{
    if (frames > 60 * 120) frames = 60 * 120;
    anth_len = frames;
    if (getenv("HOLDLOG")) fprintf(stderr, "hold: anthem %d frames at %d\n", frames, g.Vcount);
    anth_t0 = g.Vcount;
    anth_pose = 0;
    until = (uint16_t)(g.Vcount + frames + 300);        /* (until the spots are known: anthem_pose sets the skate's own time) */
    pending = 1;
    anth = 1;
}

int hold_anthem_pending(void) { return anth; }

/* RULE_ANTHEM: the frame of each anthem's last note (60 a second; the sound fades out to its 25% level there), when the crowd
 * cheers and the skaters leave their lines for the face off spots. NHL 95 PC's USA.KMS and CANADA.KMS as native/pcsong.c
 * renders them (12.76 s and 11.56 s long: the music ends at 12.25 s and 11.10 s); NHL 96 PC's ANTHUS.ASF and ANTHCAN.ASF
 * (352981 and 323384 samples at 22050 Hz, 16.01 s and 14.67 s: the last note fades out by 13.05 s and 10.05 s, the rest is
 * the hall's echo). The PC games cheer and send the players off a countdown after the song starts (95 PC assintroline:
 * 1FEh / 23Ah game frames at 60 a second; 96 PC RunPreGameSkate: 23Ah at 62.5 a second, 9.1 s) and resume play once all
 * stand at their spots (96 PC: at most 334h frames, 13.1 s); 96 PC's 9.1 s is at the end of O Canada. Fixed here: the
 * game must not depend on whether a machine has the files, or which of them */
static const int16_t anthem_len[2][2] = { { 735, 666 }, { 783, 603 } };
static int canadian(int art) { return art == 3 || art == 7 || art == 11 || art == 15 || art == 18 || art == 22 || art == 23 || art == 25; }
int hold_anthem_frames(int set, int home_art) { return set >= 1 && set <= 2 ? anthem_len[set - 1][canadian(home_art)] : 0; }

int hold_anthem_song(void) { return anth && pending && (int16_t)(g.Vcount - anth_t0) < anth_len; }

/* RULE_HOLD: a face off in the middle of play comes with no fade to black (the PC game has none: the players skate to their dots with the
 * rink in view). The start of a period keeps the cartridge's. */
int hold_nofade(void) { return RULE(RULE_HOLD) && !anth && g.gameclock != g.PerTimeTotal; }

/* the frames into the anthem's song (-1 when no song is on: before it, after it, or no anthem) */
int hold_anthem_time(void)
{
    if (!anth || !pending || !anth_pose) return -1;
    int t = (int16_t)(g.Vcount - anth_t0);
    return t >= 0 && t < anth_len ? t : -1;
}

static int anth_way(int ax, int ay, int bx, int by) { double dx = bx - ax, dy = by - ay; return (int)sqrt(dx * dx + dy * dy); }

/* the anthem's line of each skater (the PC's lines: x lane * 28h - 50h, y +30h / -3Ah; goalies have none: 0) */
static void anth_line(int lx[12], int ly[12])
{
    int slot[2] = {0, 0};
    const struct SortObj *pl = sortobj(0);
    for (int i = 0; i < 12; i++, pl++) {
        if (pl->position <= 0) { lx[i] = ly[i] = 0; continue; }
        int team = PFLAG(pl, pfteam) ? 1 : 0, n = slot[team]++;
        lx[i] = n * 40 - 80 + (team ? 4 : -4);
        ly[i] = team ? 0x30 : -0x3A;
    }
}

/* the frames from the end of the song until all (the referee too) stand at their spots, each skating at SKATE_SPEED */
static int anth_skate(void)
{
    int lx[12], ly[12], far = anth_way(108, 0, (int16_t)g.fox > 0 ? (int16_t)g.fox + 15 : (int16_t)g.fox - 15, (int16_t)g.foy);
    anth_line(lx, ly);
    for (int i = 0; i < 12; i++) {
        if (sortobj(i)->position <= 0) continue;
        int d = anth_way(lx[i], ly[i], spot[i].x, spot[i].y);
        if (d > far) far = d;
    }
    return far / SKATE_SPEED + 2;
}

/* the referee stands at the side while the song plays, then goes to the face off spot with the players */
int hold_anthem_ref(int *x, int *y, int *dir, int *moving)
{
    if (!anth || !pending || !anth_pose) return 0;
    int t = (int16_t)(g.Vcount - anth_t0) - anth_len;
    int fx = (int16_t)g.fox, fy = (int16_t)g.foy;
    int tx = (fx > 0 ? fx + 15 : fx - 15), ty = fy;
    int d = anth_way(108, 0, tx, ty), run = t < 0 ? 0 : t * SKATE_SPEED;
    if (run > d) run = d;
    *x = 108 + (d ? (tx - 108) * run / d : 0);
    *y = 0 + (d ? (ty - 0) * run / d : 0);
    *dir = 6;                                           /* (he stands on the right: facing the rink, the lines of players) */
    *moving = run > 0 && run < d;
    if (*moving) *dir = tx > 108 ? 2 : 6;
    return 1;
}

/* RULE_SIREN: NHL 95 PC's goal siren (sample 564913A7 of PCFF001.DIG, 77078 bytes at 11025 Hz: 7.0 s) in frames of the game. Fixed
 * here, as the anthems' are: the announcer's wait must not depend on whether a machine has the file, or on its mixer */
#define PC_SIREN_FRAMES 420
static struct { int on; uint16_t t0; } siren;

/* the goal's siren starts (src/collide.c): with RULE_SIREN its fixed length counts from now (a demo plays the cartridge's) */
void hold_siren(void)
{
    siren.on = RULE(RULE_SIREN) && g.demoflag;
    siren.t0 = g.Vcount;
}

/* the goal's own sound: the siren (the cartridge's or, RULE_SIREN, the PC's), then the goal song when there is one */
static int goal_sound_busy(void)
{
    if (song_playing() || (int16_t)g.songdelay > 0) return 1;
    if (siren.on) return (uint16_t)(g.Vcount - siren.t0) < PC_SIREN_FRAMES;   /* (the cartridge's siren plays, unheard: the PC's length) */
    for (int i = 0; i < 8; i++) {                        /* (the cartridge's siren, sfx_siren_cmdstream, in a track slot) */
        int32_t st = (int32_t)g.fm_slots[i].stream;
        if (st >= 0x2CCCA && st < 0x2CEF2) return 1;
    }
    return 0;
}

void hold_goal_after_song(int frames, struct Team *tm, int player, int a1, int a2)
{
    if (getenv("HOLDLOG")) fprintf(stderr, "hold: frame %d goal, song playing %d, delay %d\n", g.Vcount, song_playing(), (int16_t)g.songdelay);
    if (!goal_sound_busy()) {                              /* no siren or song going or coming: the speech now */
        if (pa_hook) pa_hook(PA_GOAL, tm, player, a1, a2);
        hold_for(frames);
        return;
    }
    sg.on = 1;
    sg.started = 0;
    sg.limit = (uint16_t)(g.Vcount + 600);          /* (a song that never starts: 10 s at most) */
    sg.frames = frames;
    sg.tm = tm; sg.player = player; sg.a1 = a1; sg.a2 = a2;
    pending = 1;
}

/* a skater moved by the wait rather than by his own speed strides (dx, dy: his way; pace: pixels a frame), so the skating
 * animation runs in step with where he goes, facing it (the game's speed of 4096 is a pixel a frame) */
static void stride(struct SortObj *pl, int dx, int dy, double pace)
{
    double l = sqrt((double)dx * dx + (double)dy * dy);
    if (l < 1) l = 1;
    SetSPA(pl, SPAskate);
    pl->facedir = (int16_t)vtoa((int16_t)dx, (int16_t)dy);
    pl->Xvel = (int16_t)(dx * 4096.0 * pace / l);
    pl->Yvel = (int16_t)(dy * 4096.0 * pace / l);
}

/* NHL 95 PC's anthem: the players stand in two lines facing the flag while the song plays, then skate to the face off spot.
 * The skaters' places are the game's own objects, so every copy of the game agrees. */
static void anthem_pose(void)
{
    int16_t t = (int16_t)(g.Vcount - anth_t0);
    if (!anth_pose) {                                   /* (the face off set up has just placed them: remember the spots) */
        struct SortObj *pl = sortobj(0);
        for (int i = 0; i < 12; i++, pl++) { spot[i].x = pl->Xpos; spot[i].y = pl->Ypos; spot_face[i] = pl->facedir; }
        face_set = 1;
        anth_pose = 1;
        until = (uint16_t)(anth_t0 + anth_len + anth_skate());   /* (the drop: once the last of them is at his spot) */
        if (getenv("HOLDLOG")) for (int i = 0; i < 12; i++) fprintf(stderr, "hold: anthem player %d pos %d asn %d facedir %d\n", i, sortobj(i)->position, sortobj(i)->asslist[sortobj(i)->assnum & 7], sortobj(i)->facedir);
        if (getenv("HOLDLOG")) fprintf(stderr, "hold: anthem skate %d frames after the song's %d\n", anth_skate(), anth_len);
    }
    int skating = t >= anth_len;
    int lxs[12], lys[12];
    anth_line(lxs, lys);
    struct SortObj *pl = sortobj(0);
    for (int i = 0; i < 12; i++, pl++) {
        if (pl->position <= 0) continue;                /* (goalies stay in their nets) */
        int lx = lxs[i], ly = lys[i];
        int x = lx, y = ly;
        if (skating) {
            int dx = spot[i].x - lx, dy = spot[i].y - ly, d = anth_way(lx, ly, spot[i].x, spot[i].y);
            int run = (t - anth_len) * SKATE_SPEED;
            if (run < d) {                              /* (on his way, at the skate's pace) */
                pl->Xpos = (int16_t)(lx + dx * run / d);
                pl->Ypos = (int16_t)(ly + dy * run / d);
                stride(pl, dx, dy, SKATE_SPEED);
                continue;
            }
            x = spot[i].x;
            y = spot[i].y;
            pl->facedir = spot_face[i];
            if (run - SKATE_SPEED < d) SetSPA(pl, SPAglide);   /* (there: he stops, the skating stride ended whatever its step; the face off's own code gives the stance) */
        } else {
            pl->facedir = (int16_t)vtoa(0, 100);                /* (all face the viewer: facedir 4, as the PC's skaters) */
        }
        pl->Xpos = (int16_t)x;
        pl->Ypos = (int16_t)y;
        pl->Xvel = 0;
        pl->Yvel = 0;
    }
    if (!skating) {                                     /* (the PC's view: centre ice half way down the rink view, held still) */
        g.sflags |= HI(sfslock);
        g.Vpos = g.yc1 = ANTH_VPOS;
        g.Hpos = g.xc1 = 0;
        anth_lock = 1;
    } else if (anth_lock) { g.sflags &= (uint16_t)~HI(sfslock); anth_lock = 0; }
}

/* NHL 95 PC's between-play: the skaters do not appear at the face off spots; each skates there from where he was when
 * the whistle blew (the referee too: native/pcspr.c asks hold_skate_ref). The game's own setup has already placed them, so
 * every copy of the game agrees; this only walks them there over the wait. Only with RULE_HOLD. */
static int sk_cap, sk_on, sk_dur;
static uint16_t sk_t0;
static struct { int16_t x, y; } sk_from[12], sk_to[12], sk_via[12];
static int8_t sk_pnum[12];
static int16_t sk_face[12];                              /* the way each skater faces at his spot, as the game's own set up has it */
static int16_t sk_puckx, sk_pucky;                       /* where the puck lay when the whistle blew: the referee picks it up there */
#define SK_PICKUP 96                                    /* (of 256 along the referee's way: he reaches the puck, then carries it to the dot) */
static uint8_t sk_gate[12];                             /* 1: this place has a new player: the old one leaves by the gate to the bench, the new one comes in by it */
static int16_t sk_delay[12];                            /* frames each waits at the bench gate before he skates on (the start of a period: one after another) */
static int sk_pick = SK_PICKUP, sk_pstart, sk_lock;
static uint16_t sk_view[4];                             /* the view at the whistle (Vpos, Hpos, yc1, xc1): the set up scrolls it to the dot at once, the walk there keeps it */
/* NHL 95 PC's bench gate (assbench, asspsclear): x -A8h, y -32h for the home team, 41h for the visitors; a player comes on through it */
#define GATE_X (-0xA8)
#define GATE_Y(away) ((away) ? 0x41 : -0x32)
#define GATE_IN (-0x70)                                 /* (just inside the boards: out of the gate straight onto the ice, then to his spot) */
#define REF_START_X (-0x78)                             /* (the PC's referee at the start of play: StartHL2, x -78h, y 0) */

void hold_skate_begin(void)
{
    sk_cap = 0;
    if (!RULE(RULE_HOLD)) return;
    struct SortObj *pl = sortobj(0);
    for (int i = 0; i < 12; i++, pl++) { sk_from[i].x = pl->Xpos; sk_from[i].y = pl->Ypos; sk_pnum[i] = (int8_t)pl->pnum; sk_delay[i] = 0; }
    sk_pick = SK_PICKUP;
    sk_pstart = 0;
    sk_puckx = (int16_t)g.puckx[0]; sk_pucky = (int16_t)g.pucky;
    sk_view[0] = g.Vpos; sk_view[1] = g.Hpos; sk_view[2] = g.yc1; sk_view[3] = g.xc1;
    sk_cap = 1;
}

static int plen(int ax, int ay, int bx, int by) { double dx = bx - ax, dy = by - ay; return (int)sqrt(dx * dx + dy * dy); }
static int path_way(int i) { return sk_gate[i] ? plen(sk_from[i].x, sk_from[i].y, sk_via[i].x, sk_via[i].y) + plen(sk_via[i].x, sk_via[i].y, sk_to[i].x, sk_to[i].y) : plen(sk_from[i].x, sk_from[i].y, sk_to[i].x, sk_to[i].y); }
static int path_len(int i) { return sk_delay[i] * SKATE_SPEED + path_way(i); }   /* (the time to his spot, in pixels: his wait at the gate too) */

void hold_skate_arm(void)
{
    if (!sk_cap) return;
    sk_cap = 0;
    if (anth) return;                                   /* (the anthem's own walk to the spots) */
    int pstart = g.gameclock == g.PerTimeTotal;         /* (the start of a period: every skater comes on from his bench, not from where the last period ended) */
    int far = 0, order[2] = { 0, 0 };
    struct SortObj *pl = sortobj(0);
    for (int i = 0; i < 12; i++, pl++) {
        sk_to[i].x = pl->Xpos; sk_to[i].y = pl->Ypos; sk_face[i] = pl->facedir; spot_face[i] = pl->facedir; face_set = 1;
        int fx = sk_from[i].x, fy = sk_from[i].y;
        sk_gate[i] = 0;
        sk_delay[i] = 0;
        if (pstart && pl->position > 0) {               /* (through his team's gate, one after another: the two teams' turns side by side) */
            int away = PFLAG(pl, pfteam) ? 1 : 0;
            sk_from[i].x = GATE_X; sk_from[i].y = (int16_t)GATE_Y(away);
            sk_via[i].x = GATE_IN; sk_via[i].y = (int16_t)GATE_Y(away);
            sk_gate[i] = 1;
            sk_delay[i] = (int16_t)(order[away]++ * 12);
            int d0 = path_len(i);
            if (d0 > far) far = d0;
            continue;
        }
        if (pl->position <= 0 || fx < -300 || fx > 300 || fy < -300 || fy > 300) { sk_from[i] = sk_to[i]; continue; }
        if ((int8_t)pl->pnum != sk_pnum[i]) {             /* (a line change: out through the gate, the next one in) */
            sk_gate[i] = 1;
            sk_via[i].x = GATE_X; sk_via[i].y = (int16_t)GATE_Y(PFLAG(pl, pfteam));
        }
        int d = path_len(i);
        if (d > far) far = d;
    }
    sk_dur = far / SKATE_SPEED + 1;
    if (pstart) {                                       /* (the referee comes on too, with the puck: from the PC's start of play to the dot) */
        sk_pstart = 1;
        sk_puckx = REF_START_X; sk_pucky = 0;
        sk_pick = 0;
        int rl = plen(REF_START_X, 0, (int16_t)g.fox, (int16_t)g.foy) / SKATE_SPEED + 1;
        if (rl > sk_dur) sk_dur = rl;
    }
    if (sk_dur < 24) return;
    if (sk_dur > 300) sk_dur = 300;
    sk_t0 = g.Vcount;
    sk_on = 1;
    g.yleader = 0;                                      /* (the PC's drop: the view on the dot, no lead) */
    if (!pstart) { g.Vpos = sk_view[0]; g.Hpos = sk_view[1]; g.yc1 = sk_view[2]; g.xc1 = sk_view[3]; }   /* (no cut to the dot: the view follows the referee's puck there) */
    if (getenv("HOLDLOG")) fprintf(stderr, "hold: skate to the spots, %d frames, at %d (clock %d of %d)\n", sk_dur, g.Vcount, g.gameclock, g.PerTimeTotal);
}

/* where the puck lay at the whistle (valid with hold_skate_ref() >= 0); the referee reaches it at SK_PICKUP of the 256 */
void hold_skate_puck(int *x, int *y, int *pickup) { *x = sk_puckx; *y = sk_pucky; *pickup = sk_pick; }

/* 1 while the skaters come on at the start of a period (the referee too: he starts at the puck's place, hold_skate_puck) */
int hold_skate_pstart(void) { return sk_on && sk_pstart; }

/* 1 while the skaters (and the referee) are on their way to the spots */
int hold_skating(void) { return sk_on; }

/* the referee's progress along his way to the dot, 0 to 256 (-1: no walk to the spots) */
int hold_skate_ref(void)
{
    if (!sk_on) return -1;
    int t = (int16_t)(g.Vcount - sk_t0);
    return t >= sk_dur ? 256 : t * 256 / sk_dur;
}

/* the referee's spot by the dot (assrefatdot: 15 off it, on the side away from centre ice) */
static int ref_spot_x(void) { int fx = (int16_t)g.fox; return fx > 0 ? fx + 15 : fx - 15; }

/* where the referee has the puck on his way to the dot, in 256ths of a pixel: from where it lay at the whistle to his spot by
 * the dot, from SK_PICKUP of the walk to its end. Only the game's state decides it (the walk's time, the puck's place, the
 * dot), so every copy of the game puts the puck in the same place whatever its host draws. 0: he does not carry it now */
int hold_skate_carry(int *x, int *y)
{
    if (!sk_on || sk_pstart) return 0;
    int pr = hold_skate_ref();
    if (pr < sk_pick || pr >= 256) return 0;
    int sx = sk_puckx * 256, sy = sk_pucky * 256, ex = ref_spot_x() * 256, ey = (int16_t)g.foy * 256;
    int k = pr - sk_pick, kn = 256 - sk_pick;
    *x = sx + (ex - sx) * k / kn;
    *y = sy + (ey - sy) * k / kn;
    return 1;
}

/* the referee's place on his walk, in 256ths of a pixel (x, y in: where he is now). The start of a period: from the PC's start
 * of play to his spot; else to the puck where it lay (each call a step: the frames left to SK_PICKUP share what is left of
 * the way), then with it to the dot (hold_skate_carry). For a referee of the game's own (RULE_REFEREE: mods/referee.c), whose
 * place is in every state: one call a frame. 0: no walk */
int hold_skate_ref_step(int *x, int *y)
{
    if (!sk_on) return 0;
    int pr = hold_skate_ref(), ex = ref_spot_x() * 256, ey = (int16_t)g.foy * 256;
    if (sk_pstart) {
        if (pr > 256) pr = 256;
        *x = REF_START_X * 256 + (ex - REF_START_X * 256) * pr / 256;
        *y = ey * pr / 256;
        return 1;
    }
    if (hold_skate_carry(x, y)) return 1;
    if (pr >= 256) { *x = ex; *y = ey; return 1; }
    int t = (int16_t)(g.Vcount - sk_t0), tp = (sk_pick * sk_dur + 255) / 256, n = tp - t;   /* (tp: the first frame he has it) */
    if (n < 1) n = 1;
    *x += (sk_puckx * 256 - *x) / n;
    *y += (sk_pucky * 256 - *y) / n;
    return 1;
}

static void skate_pose(void)
{
    int t = (int16_t)(g.Vcount - sk_t0);
    struct SortObj *pl = sortobj(0);
    for (int i = 0; i < 12; i++, pl++) {
        int d = path_way(i);
        if (d < 2) continue;
        int run = (t - sk_delay[i]) * SKATE_SPEED;
        if (run < 0) {                                  /* (still at the bench gate, his turn to come) */
            pl->Xpos = sk_from[i].x; pl->Ypos = sk_from[i].y;
            pl->facedir = (int16_t)vtoa((int16_t)(sk_via[i].x - sk_from[i].x), (int16_t)(sk_via[i].y - sk_from[i].y));
            pl->Xvel = 0; pl->Yvel = 0;
            if (pl->SPA != SPAglide) SetSPA(pl, SPAglide);
            continue;
        }
        if (run >= d || t >= sk_dur) {                  /* (there: face the puck, glide) */
            /* (the frame he gets there, or the walk's last: the stride stops whatever its step, as the PC's asstakeposition
             * sets its glide at the spot; the face off men's stance comes once all are there, src/logic.c) */
            if (run - SKATE_SPEED < d || t == sk_dur || pl->Xpos != sk_to[i].x || pl->Ypos != sk_to[i].y) {
                pl->Xpos = sk_to[i].x; pl->Ypos = sk_to[i].y;
                pl->facedir = sk_face[i];
                SetSPA(pl, SPAglide);
            }
        } else {
            int ax = sk_from[i].x, ay = sk_from[i].y, bx = sk_to[i].x, by = sk_to[i].y, r = run, l = d;
            if (sk_gate[i]) {
                int l1 = plen(ax, ay, sk_via[i].x, sk_via[i].y);
                if (run < l1) { bx = sk_via[i].x; by = sk_via[i].y; l = l1; }
                else { ax = sk_via[i].x; ay = sk_via[i].y; r = run - l1; l = d - l1; }
            }
            if (l < 1) l = 1;
            pl->Xpos = (int16_t)(ax + (bx - ax) * r / l);
            pl->Ypos = (int16_t)(ay + (by - ay) * r / l);
            stride(pl, bx - ax, by - ay, SKATE_SPEED);   /* (whatever the set up gave him: a face off man's stance waits for the dot) */
            continue;
        }
        pl->Xvel = 0;
        pl->Yvel = 0;
    }
    if (getenv("HOLDSK") && !(t % 6)) { struct SortObj *q = sortobj(1); fprintf(stderr, "sk t %d dur %d: x %d y %d frame %d spa %d face %d\n", t, sk_dur, q->Xpos, q->Ypos, q->frame, q->SPA, q->facedir); }
    int rx, ry;
    struct SortObj *pk = sortobj(puckscnum);
    if (sk_pstart) {                                    /* (the start of a period: the referee has the puck in his hand; it stays at the dot, and the view with it) */
        pk->Xpos = (int16_t)g.fox; pk->Ypos = (int16_t)g.foy;
        g.puckx[0] = g.fox; g.pucky = g.foy;
        ST_W(g.puckz[0]);
    } else if (t < sk_dur && t * 256 / sk_dur < sk_pick) {      /* (the referee is still on his way to the puck where it lay) */
        pk->Xpos = sk_puckx; pk->Ypos = sk_pucky;
        g.puckx[0] = (uint16_t)sk_puckx; g.pucky = (uint16_t)sk_pucky;
        g.puckz[0] = 0;
    } else if (t < sk_dur && hold_skate_carry(&rx, &ry)) {   /* the referee carries the puck to the dot (the host's picture of him follows it) */
        pk->Xpos = (int16_t)(rx / 256); pk->Ypos = (int16_t)(ry / 256);
        g.puckx[0] = (uint16_t)pk->Xpos; g.pucky = (uint16_t)pk->Ypos;
        g.puckz[0] = 0;
    } else if (t >= sk_dur) {
        pk->Xpos = (int16_t)g.fox; pk->Ypos = (int16_t)g.foy;
        g.puckx[0] = g.fox; g.pucky = g.foy;
        ST_W(g.puckz[0]);
    }
    g.sflags |= HI(sfslock);                            /* (the view is the walk's: still while the referee fetches the puck, no cut to where it lay) */
    sk_lock = 1;
    if (sk_pstart || t * 256 / sk_dur >= sk_pick) {     /* (the puck is at the dot or on its way there: the view settles on the dot, as the PC's puts it there for the drop) */
        g.yleader = 0;
        int ty = (int16_t)g.foy, tx = (int16_t)g.fox, vh = view_hpos();
        if (ty < -0xC8) ty = -0xC8;
        if (ty > 0x100) ty = 0x100;
        if (tx > vh) tx = vh;
        if (tx < -vh) tx = -vh;
        int dv = ty - (int16_t)g.Vpos, dh = tx - (int16_t)g.Hpos;      /* (no dead zone: the dot ends up in the middle; at most 4 a frame, the view glides) */
        int sv = dv / 6, sh = dh / 6;
        if (!sv && dv) sv = dv > 0 ? 1 : -1;
        if (!sh && dh) sh = dh > 0 ? 1 : -1;
        if (sv > 4) sv = 4;
        if (sv < -4) sv = -4;
        if (sh > 4) sh = 4;
        if (sh < -4) sh = -4;
        g.Vpos = (uint16_t)((int16_t)g.Vpos + sv);
        g.Hpos = (uint16_t)((int16_t)g.Hpos + sh);
    }
    g.yc1 = g.Vpos; g.xc1 = g.Hpos;
    if (t >= sk_dur) {
        sk_on = sk_pstart = 0;
        if (sk_lock) { g.sflags &= (uint16_t)~HI(sfslock); sk_lock = 0; }
    }
}

void hold_tick(void)
{
    if (getenv("HOLDVIEW")) fprintf(stderr, "view: %d vpos %d hpos %d yc1 %d yleader %d puck %d,%d sk %d\n", g.Vcount, (int16_t)g.Vpos, (int16_t)g.Hpos, (int16_t)g.yc1, (int16_t)g.yleader, (int16_t)g.puckx[0], (int16_t)g.pucky, sk_on);
    if (getenv("HOLDMOVE") && !(g.Vcount % 120)) {         /* (a test: how much the skaters move, to see the AI is alive after the face off) */
        int sum = 0, ass = 0;
        struct SortObj *q = sortobj(0);
        for (int i = 0; i < 12; i++, q++) { sum += abs(q->Xvel) + abs(q->Yvel); if (q->position > 0 && q->asslist[q->assnum & 7] != pnothing) ass++; }
        fprintf(stderr, "holdmove: frame %d speed %d active %d\n", g.Vcount, sum, ass);
    }
    if (sk_on) skate_pose();
    if (anth && pending) anthem_pose();
    if (face_set && !sk_on && !(anth && pending)) {      /* (the wings wait for the draw facing the way the game set them; the face off's own code turns them away) */
        const struct SortObj *pk = sortobj(puckscnum);
        int asn = pk->asslist[pk->assnum & 7];
        if (asn == pfaceoff || asn == pfaceoff2) {
            struct SortObj *q = sortobj(0);
            for (int i = 0; i < 12; i++, q++)
                if (q->position > 0 && q->position != 4 && q->asslist[q->assnum & 7] == afaceoff) q->facedir = spot_face[i];
        } else face_set = 0;
    }
    if (!sg.on) return;
    int playing = goal_sound_busy();
    if (playing) sg.started = 1;
    if (!(sg.started && !playing) && (int16_t)(sg.limit - g.Vcount) > 0) return;
    if (getenv("HOLDLOG")) fprintf(stderr, "hold: frame %d speech starts (started %d)\n", g.Vcount, sg.started);
    sg.on = 0;                                          /* the song is over: the speech, and the wait for it */
    if (pa_hook) pa_hook(PA_GOAL, sg.tm, sg.player, sg.a1, sg.a2);
    until = (uint16_t)(g.Vcount + sg.frames);
    pending = 1;
}

int hold_wait(void)
{
    if (sk_on) return 1;                                /* (the players are still skating to the spots) */
    if (!pending) return 0;
    if (sg.on) return 1;
    if ((wanted() || anth) && (int16_t)(until - g.Vcount) > 0) return 1;
    pending = 0;
    anth = 0;
    if (anth_lock) { g.sflags &= (uint16_t)~HI(sfslock); anth_lock = 0; }
    return 0;
}

void hold_reset(void) { siren.on = 0; if (sk_lock) { g.sflags &= (uint16_t)~HI(sfslock); sk_lock = 0; } sk_on = 0; sk_cap = 0; pending = 0; anth = 0; anth_pose = 0; sg.on = 0; face_set = 0; }

/* The pause's state, kept in every state (native/rules.c's part): a rollback, or a machine that joins, goes on with the wait,
 * the anthem's line up and the walk to the spots where they were (little-endian, the same on every host). Then the face off's
 * assignments the anthem holds back (src/logic.c). */
static uint8_t *hs_w8(uint8_t *p, int v) { *p = (uint8_t)v; return p + 1; }
static uint8_t *hs_w16(uint8_t *p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)((unsigned)v >> 8); return p + 2; }
static int hs_r8(const uint8_t **p) { return *(*p)++; }
static int hs_r16(const uint8_t **p) { int v = (int16_t)((*p)[0] | (*p)[1] << 8); *p += 2; return v; }

size_t hold_state_save(uint8_t *out)
{
    if (!out) return HOLD_STATE_SIZE;
    uint8_t *p = out;
    p = hs_w16(p, until); p = hs_w8(p, pending); p = hs_w8(p, anth); p = hs_w8(p, anth_pose); p = hs_w16(p, anth_len); p = hs_w8(p, anth_lock); p = hs_w16(p, anth_t0);
    for (int i = 0; i < 12; i++) { p = hs_w16(p, spot[i].x); p = hs_w16(p, spot[i].y); p = hs_w16(p, spot_face[i]); }
    p = hs_w8(p, face_set);
    p = hs_w8(p, sg.on); p = hs_w8(p, sg.started); p = hs_w16(p, sg.limit); p = hs_w16(p, sg.frames); p = hs_w8(p, sg.tm == team_struct(1));
    p = hs_w16(p, sg.player); p = hs_w16(p, sg.a1); p = hs_w16(p, sg.a2);
    p = hs_w8(p, sk_cap); p = hs_w8(p, sk_on); p = hs_w16(p, sk_dur); p = hs_w16(p, sk_t0);
    for (int i = 0; i < 12; i++) {
        p = hs_w16(p, sk_from[i].x); p = hs_w16(p, sk_from[i].y); p = hs_w16(p, sk_to[i].x); p = hs_w16(p, sk_to[i].y); p = hs_w16(p, sk_via[i].x); p = hs_w16(p, sk_via[i].y);
        p = hs_w8(p, sk_pnum[i]); p = hs_w16(p, sk_face[i]); p = hs_w8(p, sk_gate[i]); p = hs_w16(p, sk_delay[i]);
    }
    p = hs_w16(p, sk_puckx); p = hs_w16(p, sk_pucky); p = hs_w16(p, sk_pick); p = hs_w8(p, sk_pstart); p = hs_w8(p, sk_lock);
    for (int k = 0; k < 4; k++) p = hs_w16(p, sk_view[k]);
    p += faceoff_defer_save(p);
    return (size_t)(p - out);
}

static int flag(int v) { return v == 0 || v == 1; }

/* a state's bytes, or NULL (a state from before them: no pause). -1 for bytes that are not a pause of this game's (nothing
 * changed then: a state is checked before it is used) */
int hold_state_load(const uint8_t *in)
{
    if (!in) { hold_reset(); return faceoff_defer_load(NULL); }
    const uint8_t *p = in;
    int u = hs_r16(&p), pe = hs_r8(&p), an = hs_r8(&p), ap = hs_r8(&p), al = hs_r16(&p), alk = hs_r8(&p), at0 = hs_r16(&p);
    if (!flag(pe) || !flag(an) || !flag(ap) || al < 0 || al > 60 * 120 || !flag(alk)) return -1;
    const uint8_t *spots = p;
    p += 12 * 6;
    int fs = hs_r8(&p), so = hs_r8(&p), sst = hs_r8(&p), sl = hs_r16(&p), sf = hs_r16(&p), stm = hs_r8(&p), spl = hs_r16(&p), sa1 = hs_r16(&p), sa2 = hs_r16(&p);
    if (!flag(fs) || !flag(so) || !flag(sst) || sf < 0 || sf > 900 || !flag(stm) || spl < 0 || spl > 31 || sa1 < -1 || sa1 > 31 || sa2 < -1 || sa2 > 31) return -1;
    int kc = hs_r8(&p), ko = hs_r8(&p), kd = hs_r16(&p), kt0 = hs_r16(&p);
    if (!flag(kc) || !flag(ko) || kd < 0 || kd > 300 || (ko && kd < 24)) return -1;
    const uint8_t *walk = p;
    for (int i = 0; i < 12; i++) {
        p += 12;
        hs_r8(&p); hs_r16(&p);
        int gt = hs_r8(&p), dl = hs_r16(&p);
        if (!flag(gt) || dl < 0 || dl > 300) return -1;
    }
    int px = hs_r16(&p), py = hs_r16(&p), pk = hs_r16(&p), ps = hs_r8(&p), lk = hs_r8(&p);
    if ((pk != 0 && pk != SK_PICKUP) || !flag(ps) || !flag(lk)) return -1;
    const uint8_t *view = p;
    p += 8;
    if (faceoff_defer_check(p) < 0) return -1;
    until = (uint16_t)u; pending = pe; anth = an; anth_pose = ap; anth_len = al; anth_lock = alk; anth_t0 = (uint16_t)at0;
    for (int i = 0; i < 12; i++) { spot[i].x = (int16_t)hs_r16(&spots); spot[i].y = (int16_t)hs_r16(&spots); spot_face[i] = (int16_t)hs_r16(&spots); }
    face_set = fs;
    sg.on = so; sg.started = sst; sg.limit = (uint16_t)sl; sg.frames = sf; sg.tm = team_struct(stm); sg.player = spl; sg.a1 = sa1; sg.a2 = sa2;
    sk_cap = kc; sk_on = ko; sk_dur = kd; sk_t0 = (uint16_t)kt0;
    for (int i = 0; i < 12; i++) {
        sk_from[i].x = (int16_t)hs_r16(&walk); sk_from[i].y = (int16_t)hs_r16(&walk); sk_to[i].x = (int16_t)hs_r16(&walk); sk_to[i].y = (int16_t)hs_r16(&walk);
        sk_via[i].x = (int16_t)hs_r16(&walk); sk_via[i].y = (int16_t)hs_r16(&walk);
        sk_pnum[i] = (int8_t)hs_r8(&walk); sk_face[i] = (int16_t)hs_r16(&walk); sk_gate[i] = (uint8_t)hs_r8(&walk); sk_delay[i] = (int16_t)hs_r16(&walk);
    }
    sk_puckx = (int16_t)px; sk_pucky = (int16_t)py; sk_pick = pk; sk_pstart = ps; sk_lock = lk;
    for (int k = 0; k < 4; k++) sk_view[k] = (uint16_t)hs_r16(&view);
    faceoff_defer_load(p);
    return 0;
}

/* RULE_SIREN's siren in every state: on, the vblank it began at */
size_t hold_siren_save(uint8_t *out)
{
    if (out) { uint8_t *p = hs_w8(out, siren.on); hs_w16(p, siren.t0); }
    return HOLD_SIREN_SIZE;
}

int hold_siren_load(const uint8_t *in)
{
    if (!in) { siren.on = 0; siren.t0 = 0; return 0; }
    const uint8_t *p = in;
    int on = hs_r8(&p), t0 = hs_r16(&p);
    if (!flag(on)) return -1;
    siren.on = on;
    siren.t0 = (uint16_t)t0;
    return 0;
}
