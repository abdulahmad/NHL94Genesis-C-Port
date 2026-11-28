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
#define ANTH_FACE 4                                     /* (the anthem's lines face the viewer: vtoa(0, 100)) */
#define ANTH_VPOS (-18)                                 /* (the PC shows centre ice half way down its rink view) */                        /* the anthem's line up (1 once the spots are taken); the anthem's own frames */
static uint16_t anth_t0;
/* the pace of the skate to the spots, pixels a frame: NHL 95 PC's skaters skate there at their top speed (5400 to 8400, 1.3 to 2.05
 * rink units a game frame at 60 a second: 16 x speed / 65536); its rink is 0.89 times the cartridge's along (264 against 298
 * to the end boards), so 1.5 to 2.3 of the cartridge's pixels, about its own top speed (velocity 8000: 17 x 8000 / 65536) */
#define SKATE_SPEED 2
static int ez_frames(const struct SortObj *pl, int ax, int ay, int bx, int by, int f, int f0);   /* (the PC's skate to a spot: below) */
static int ez_pose(struct SortObj *pl, int ax, int ay, int bx, int by, int f, int f0, int r);
static int dot_face(const struct SortObj *pl, int x, int y);
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
 * rink in view). The start of a period keeps the cartridge's, and so does the way back from the penalty screen: the horizontal view is
 * still up there, and the cartridge's forceblack, its set up in the dark and the fade in are the way back to the rink (without them the
 * half set up rink showed for a frame, the face off window over the penalty screen's tiles, then a cut). */
int hold_nofade(void) { return RULE(RULE_HOLD) && !anth && g.gameclock != g.PerTimeTotal && !(g.sflags & HI(sfhor)); }

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

/* the frames from the end of the song until all stand in place at their spots (the PC's skate there: ez_pose) and the referee
 * (at SKATE_SPEED) at his */
static int anth_skate(void)
{
    int lx[12], ly[12], far = anth_way(108, 0, (int16_t)g.fox > 0 ? (int16_t)g.fox + 15 : (int16_t)g.fox - 15, (int16_t)g.foy) / SKATE_SPEED;
    anth_line(lx, ly);
    for (int i = 0; i < 12; i++) {
        if (sortobj(i)->position <= 0) continue;
        int d = ez_frames(sortobj(i), lx[i], ly[i], spot[i].x, spot[i].y, spot_face[i], ANTH_FACE);
        if (d > far) far = d;
    }
    return far + 2;
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
    if (!goal_sound_busy() || (g.gmode & (1 << gmhl))) {   /* no siren or song going or coming: the speech now; in a highlight never kept
                                                            * for later (the announcer leaves a highlight's goals unsaid, and a speech
                                                            * kept would come after it, in the game) */
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
        for (int i = 0; i < 12; i++, pl++) { spot[i].x = pl->Xpos; spot[i].y = pl->Ypos; spot_face[i] = (int16_t)dot_face(pl, pl->Xpos, pl->Ypos); }
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
        if (skating) {                              /* (on his way as the PC's skate to a spot: speeding up, the scrape stop, turning; then in place) */
            if (ez_pose(pl, lx, ly, spot[i].x, spot[i].y, spot_face[i], ANTH_FACE, t - anth_len) || (pl->Xpos == spot[i].x && pl->Ypos == spot[i].y)) continue;
            x = spot[i].x;
            y = spot[i].y;
            pl->facedir = spot_face[i];
        } else {
            pl->facedir = ANTH_FACE;                            /* (all face the viewer: facedir 4, as the PC's skaters) */
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
static uint16_t sk_frame[12];                            /* each skater's picture at the whistle: the set up blanks them (frame 0) till his animation's next step */
static int16_t sk_face[12];                              /* the way each skater faces at his spot (bits 0-2); bit 11 set: bits 8-10 the way he faced at
                                                         * the whistle, which he turns from before he sets off (kept in the state's word, no new field) */
#define SK_FACE(i) (sk_face[i] & 7)
#define SK_FACE0(i) (sk_face[i] & 0x800 ? sk_face[i] >> 8 & 7 : -1)
static int16_t sk_f0[12];                                /* (the way each faced at the whistle, hold_skate_begin to hold_skate_arm) */
static int16_t sk_puckx, sk_pucky;                       /* where the puck lay when the whistle blew: the referee picks it up there */
#define SK_PICKUP 96                                    /* (of 256 along the referee's way: he reaches the puck, then carries it to the dot) */
static uint8_t sk_gate[12];                             /* 1: this place has a new player: the old one leaves by the gate to the bench, the new one comes in by it */
static int16_t sk_delay[12];                            /* frames each waits at the bench gate before he skates on (the start of a period: one after another) */
static int sk_pick = SK_PICKUP, sk_pstart, sk_lock;
static int sk_dark;                                      /* (the face off set up behind the fade from the penalty screen: the view is not the whistle's) */
static uint16_t sk_view[4];                             /* the view at the whistle (Vpos, Hpos, yc1, xc1): the set up scrolls it to the dot at once, the walk there keeps it */
/* The bench (a line change between plays, and the start of a period). NHL 95 PC's assbench / assleaveice and asseben are the
 * cartridge's own, moved to its rink: the player going off skates to his team's gate (cartridge x -88h - 8, y 50h for the
 * visitors, -50h for the home team; the PC's -A8h, 41h / -32h), glides the last 10h to it turning a step every 8 frames to face
 * down the ice (facing 4), then hops the boards (SPAwallleft, facing 2) and is gone (frame 0). The one coming on hops in at
 * his own place along the bench (asseben: x -88h, y (SCnum - 6, one more from 0) x 0Eh) with SPAwallright, facing 2, then skates
 * to his spot. Every part is a fixed time from the game's own state (the hops' lengths from their animations), so every copy
 * of the game agrees. */
#define DOOR_X (-0x88 - 8)
#define DOOR_Y(pl) (PFLAG(pl, pfteam) ? 0x50 : -0x50)
#define DOOR_GLIDE 0x10                                 /* (the last stretch to the gate: gliding, turning) */
#define ENTER_X (-0x88)
#define BENCH_GAP 8                                     /* (frames on the bench between the one going off and the one coming on) */
#define REF_START_X (-0x78)                             /* (the PC's referee at the start of play: StartHL2, x -78h, y 0) */

static void skate_pose(void);

void hold_skate_begin(void)
{
    sk_cap = 0;
    if (!RULE(RULE_HOLD)) return;
    sk_dark = (g.sflags & HI(sfhor)) != 0;              /* (back from the penalty screen: the cartridge's fade (hold_nofade), the view set up on the dot in the dark) */
    struct SortObj *pl = sortobj(0);
    for (int i = 0; i < 12; i++, pl++) { sk_from[i].x = pl->Xpos; sk_from[i].y = pl->Ypos; sk_pnum[i] = (int8_t)pl->pnum; sk_delay[i] = 0; sk_frame[i] = pl->frame; sk_f0[i] = (int16_t)(pl->facedir & 7); }
    sk_pick = SK_PICKUP;
    sk_pstart = 0;
    sk_puckx = (int16_t)g.puckx[0]; sk_pucky = (int16_t)g.pucky;
    sk_view[0] = g.Vpos; sk_view[1] = g.Hpos; sk_view[2] = g.yc1; sk_view[3] = g.xc1;
    sk_cap = 1;
}

static int plen(int ax, int ay, int bx, int by) { double dx = bx - ax, dy = by - ay; return (int)sqrt(dx * dx + dy * dy); }
/* NHL 95 PC's skate to a spot between plays. Its face off set up (puckfaceoff 516E1: PenShotStart 511B4) gives each skater on the
 * ice asstakeposition (52720, asstab 27h), and the drop waits until all are in place (puckfaceoff2 51BDB: AllInPlace 51440). Nobody
 * is moved there: each skates with the game's own skating, from rest at the face off's set up, so he speeds up and stops.
 *  - Speeding up: skateto (5E93B) aims him at the spot, doplayeracc (5E16D) / noturn0 strides (SPAskate) and playeracc (5EDAD) adds
 *    his acceleration every frame (dirtab 200 times his agility and weight; the cartridge's: 200 x ((40h - weight / 4 + legstr) / 2)
 *    / 32) as long as his speed stays under MaxSpeed for his speed rating (the cartridge's (20 + legspd / 2) x 275, an odd rating
 *    half a step more). updateplayers (5C40F) takes 1/64 of it off every frame, as the cartridge's does.
 *  - Stopping: skateto leads the aim by his velocity's high byte and calls him close enough within 0Ch of the spot; doplayeracc's
 *    direction 9 is dostop (5F745): over 1000h it plays the scrape stop SPAstop (the snow spray, SPFstop: the cartridge's frames 145
 *    to 160, two a direction, 4 frames each and looping; the PC's 144 to 159, native/pcspr.c's pc_frame), and stopna (5F82A) takes
 *    a step off every frame (the cartridge's 96h). Within 20 of the spot asstakeposition uses SkateToSpot (4E292): within 19h a
 *    speed over 9C4h loses a quarter a frame, under 640h he is given 640h toward the spot (gliding in), and within 8 he is put on it.
 *  - In place: on the spot asstakeposition turns him a step toward the dot every 8 frames (SPAglide), then he is in place.
 *  - The stride is not paced by his speed in either game: SPAskate's steps are 10 frames each (the cartridge's and the PC's frames
 *    tables), updateanim (5CAEF) counts them down a frame at a time.
 * Here the same steps in one dimension along his way (the game's state alone decides them: his ratings and the frames since he set
 * off; nothing is kept). Two changes: skateto's aim is looked at every frame, not every 12th (the stop starts where it should, not
 * up to 12 frames early or late), and the last 8 pixels are glided, not jumped (SkateToSpot's snap is a visible jump of up to 8). */
#define EZ_CAP 400                                      /* (frames: the walk's own cap, sk_dur) */
enum { EZ_SKATE, EZ_STOP, EZ_THERE };
struct ez { int32_t pos; int v, ph, k; };               /* (pos: 16.16 pixels along the way; v: the game's velocity units, 17 x v / 65536 a frame) */

static int ez_acc(const struct SortObj *pl)
{
    int d2 = ((0x40 - (pl->weight >> 2)) + pl->legstr) & 0xFF;
    int a = 200 * (d2 >> 1) >> 5;
    return a < 40 ? 40 : a;
}

static int ez_top(const struct SortObj *pl)
{
    int s = pl->legspd > 0x1E ? 0x1E : pl->legspd, idx = (s >> 1) & 0xF;
    return (20 + idx) * 275 + ((s & 1) && idx < 15 ? 137 : 0);
}

/* r frames along a way of d pixels (stop 0: he goes straight on at the end, the bench gate's glide follows).
 * The stop: NHL 95 PC's stopna takes 200 + legstr off each frame, SkateToSpot cuts a quarter within 19h and gives a slow skater
 * 640h, and asstakeposition snaps him on within 8 (the PC's own frames: about 40 of the scrape stop's spray, then some 25 at 640h
 * still in it, then the jump onto the spot). Here one even brake of that strength (200 + legstr a frame) that ends on the spot:
 * he brakes once what is left of the way is what that brake needs, and each frame takes off what brings him to rest there
 * (speed squared over twice the way left), so there is no crawl at 640h and no jump. Integers only. */
static struct ez ez_run(const struct SortObj *pl, int d, int r, int stop)
{
    struct ez s = { 0, 0, EZ_SKATE, 0 };
    int32_t D = (int32_t)d << 16;
    int a = ez_acc(pl), top = ez_top(pl), brake = 200 + pl->legstr;
    if (d < 2) { s.ph = EZ_THERE; s.pos = D; return s; }
    for (; s.k < r && s.k < EZ_CAP; s.k++) {
        int64_t rem = D - s.pos, sp = (int64_t)s.v * 17;                       /* (16.16 pixels: the way left, a frame's way) */
        if (stop && s.ph == EZ_SKATE && sp * sp + 0x8000 * sp >= 2 * rem * (brake * 17)) s.ph = EZ_STOP;   /* (the brake's way is what is left: dostop) */
        if (s.ph == EZ_STOP) {
            int64_t dec = sp * sp / (2 * rem);
            sp -= dec;
            if (sp < 0x1800) sp = 0x1800;                                       /* (the last of it: a tenth of a pixel a frame at least) */
            s.v = (int)(sp / 17);
            s.pos += (int32_t)sp;
        } else {
            if (s.v + a <= top) s.v += a;                                       /* (playeracc: under his top speed) */
            if (s.v > 0) s.v -= (s.v >> 6) ? (s.v >> 6) : 1;                   /* (updateplayers' drag) */
            s.pos += (int32_t)s.v * 17;
        }
        if (s.pos >= D || (stop && D - s.pos < (1 << 15))) { s.pos = D; s.ph = EZ_THERE; s.k++; break; }
    }
    return s;
}

/* the turns (8 frames each) from the way he came to facing f, the shorter way round */
static int ez_turns(int dx, int dy, int f)
{
    int d = (f - (vtoa((int16_t)dx, (int16_t)dy) & 7)) & 7;
    return d > 4 ? 8 - d : d;
}

/* Setting off: NHL 95 PC's doplayeracc turns a skater toward the way skateto gives him before he goes. Standing, it adds
 * doplayeracc_ftab's 10h times 300h (less his speed squared over 65536, at least 180h) to facedir's 16.16 a frame, and only
 * accelerates (playeracc) once he is moving: from rest he turns where he stands, a step (45 degrees) every 5.3 frames, then
 * skates. f0: the way he faced (-1: none known, no turn); dx, dy: his way. The frames of it */
#define EZ_TURN 0x3000                                  /* (10h x 300h: a frame's turn, of the 10000h a step) */
static int ez_turn_steps(int f0, int dx, int dy)
{
    if (f0 < 0 || (!dx && !dy)) return 0;
    int d = ((vtoa((int16_t)dx, (int16_t)dy) & 7) - f0) & 7;
    return d > 4 ? 8 - d : d;
}
static int ez_turn_frames(int f0, int dx, int dy) { return (ez_turn_steps(f0, dx, dy) * 0x10000 + EZ_TURN - 1) / EZ_TURN; }

/* r frames into that turn: his facing, the glide (no stride while he stands) */
static void ez_turn_pose(struct SortObj *pl, int f0, int dx, int dy, int r)
{
    int to = vtoa((int16_t)dx, (int16_t)dy) & 7, d = (to - f0) & 7, k = r * EZ_TURN >> 16;
    if (k > ez_turn_steps(f0, dx, dy)) k = ez_turn_steps(f0, dx, dy);
    pl->facedir = (int16_t)((f0 + (d <= 4 ? k : -k)) & 7);
    pl->Xvel = 0; pl->Yvel = 0;
    pl->pflags &= (uint8_t)~(1 << pfalock);
    SetSPA(pl, SPAglide);
}

/* the way a skater faces at his face off spot: NHL 95 PC's asstakeposition turns him toward the dot (fox, foy), a step every 8
 * frames, once there (a goalie: the cartridge's own, the set up's). Not the set up's facing: that faces where the puck lies when
 * it runs, which between plays (RULE_HOLD) is still where the whistle blew, and before the anthem wherever it was left */
static int dot_face(const struct SortObj *pl, int x, int y)
{
    int dx = (int16_t)g.fox - x, dy = (int16_t)g.foy - y;
    if (pl->position <= 0 || (!dx && !dy)) return pl->facedir & 7;
    return vtoa((int16_t)dx, (int16_t)dy) & 7;
}

/* the frames from setting off until he is in place: turned to his way (f0: the way he faced), there, and turned to face f
 * (asstakeposition) */
static int ez_frames(const struct SortObj *pl, int ax, int ay, int bx, int by, int f, int f0)
{
    int d = plen(ax, ay, bx, by);
    if (d < 2) return 0;
    struct ez s = ez_run(pl, d, EZ_CAP, 1);
    return ez_turn_frames(f0, bx - ax, by - ay) + s.k + ez_turns(bx - ax, by - ay, f) * 8;
}

/* a skater r frames after he set off from a to b, facing f once there (f0 the way he faced): placed and posed as the PC's would
 * be. 0 once in place */
static int ez_pose(struct SortObj *pl, int ax, int ay, int bx, int by, int f, int f0, int r)
{
    int dx = bx - ax, dy = by - ay, d = plen(ax, ay, bx, by);
    if (d >= 2) {
        int t0 = ez_turn_frames(f0, dx, dy);
        if (r < t0) { ez_turn_pose(pl, f0, dx, dy, r); pl->Xpos = (int16_t)ax; pl->Ypos = (int16_t)ay; return 1; }
        r -= t0;
    }
    struct ez s = ez_run(pl, d, r, 1);
    if (s.ph != EZ_THERE) {
        int run = (int)(s.pos >> 8), l = d * 256;                           /* (256ths of a pixel) */
        pl->Xpos = (int16_t)(ax + (int)((int64_t)dx * run / l));
        pl->Ypos = (int16_t)(ay + (int)((int64_t)dy * run / l));
        pl->facedir = (int16_t)vtoa((int16_t)dx, (int16_t)dy);
        pl->Xvel = (int16_t)(dx * s.v / d);
        pl->Yvel = (int16_t)(dy * s.v / d);
        pl->pflags &= (uint8_t)~(1 << pfalock);
        if (s.ph == EZ_SKATE) SetSPA(pl, SPAskate);
        else if (pl->SPA == SPAstop || s.v > 0x1000) SetSPA(pl, SPAstop);   /* (dostop: the scrape stop's spray over 1000h, kept to the spot as the PC's is) */
        else SetSPA(pl, SPAglide);                       /* (a short way, never that fast: gliding in) */
        { static int ezlog = -1; if (ezlog < 0) ezlog = getenv("HOLDEZ") != NULL; if (ezlog) fprintf(stderr, "ez vc %d hp %d vp %d sc %d r %d d %d ph %d v %d pos %d x %d y %d xv %d yv %d spa %x frame %d\n", g.Vcount, (int16_t)g.Hpos, (int16_t)g.Vpos, pl->SCnum, r, d, s.ph, s.v, (int)(s.pos >> 16), pl->Xpos, pl->Ypos, pl->Xvel, pl->Yvel, pl->SPA, pl->frame); }   /* (a test: each skater's speed, phase and frame) */
        return 1;
    }
    int n = ez_turns(dx, dy, f), k = (r - s.k) / 8 + 1, fa = vtoa((int16_t)dx, (int16_t)dy) & 7;
    if (d < 2) { n = 0; fa = f; }
    if (r > s.k + n * 8 && pl->Xpos == bx && pl->Ypos == by) return 0;   /* (in place: left to the face off's own code) */
    if (k > n) k = n;
    int way = ((f - fa) & 7) <= 4 ? 1 : -1;
    pl->Xpos = (int16_t)bx; pl->Ypos = (int16_t)by;
    pl->Xvel = 0; pl->Yvel = 0;
    pl->facedir = (int16_t)((fa + way * k) & 7);
    if (r == s.k) { pl->pflags &= (uint8_t)~(1 << pfalock); SetSPA(pl, SPAglide); }   /* (on the spot: the glide, whatever the stop's step) */
    return r < s.k + n * 8;
}


/* an animation's frames in a direction (updateanim: each step lasts its time + 1) */
static int anim_len(uint16_t spa, int dir)
{
    const uint8_t *a = spa_table(spa);
    a += (int16_t)rd16(a + dir * 2);
    int n = 0;
    for (int k = 0; k < 64; k++, a += 4) {
        int16_t tm = (int16_t)rd16(a + 2);
        n += (tm < 0 ? -tm : tm) + 1;
        if (tm < 0) break;
    }
    return n;
}

/* the turns (8 frames each) from the way he skated to the gate to facing down the ice, the shorter way round (assbench) */
static int door_turns(int i)
{
    const struct SortObj *pl = sortobj(i);
    int f = vtoa((int16_t)(DOOR_X + DOOR_GLIDE - sk_from[i].x), (int16_t)(DOOR_Y(pl) - sk_from[i].y)) & 7, d = (4 - f) & 7;
    return d > 4 ? 8 - d : d;
}

/* a bench skater's parts, in frames from the end of his wait: to the gate, the glide and turns, the hop off, the bench, the hop
 * on (the start of a period: only the hop on, then the skate) */
struct benchway { int start, skate, turn, off, gap, on; };
static struct benchway bench_way(int i)
{
    struct benchway w = { 0, 0, 0, 0, 0, 0 };
    if (!sk_pstart) {
        w.start = ez_turn_frames(SK_FACE0(i), DOOR_X + DOOR_GLIDE - sk_from[i].x, DOOR_Y(sortobj(i)) - sk_from[i].y);   /* (turning to the gate first) */
        w.skate = ez_run(sortobj(i), plen(sk_from[i].x, sk_from[i].y, DOOR_X + DOOR_GLIDE, DOOR_Y(sortobj(i))), EZ_CAP, 0).k;   /* (speeding up, on into the glide)*/
        w.turn = (door_turns(i) > 0 ? door_turns(i) : 1) * 8;
        w.off = anim_len(SPAwallleft, 2);
        w.gap = BENCH_GAP;
    }
    w.on = anim_len(SPAwallright, 2);
    return w;
}

/* the frames from the end of his wait to his spot */
static int walk_frames(int i)
{
    const struct SortObj *pl = sortobj(i);
    if (!sk_gate[i]) return ez_frames(pl, sk_from[i].x, sk_from[i].y, sk_to[i].x, sk_to[i].y, SK_FACE(i), SK_FACE0(i));
    struct benchway w = bench_way(i);
    return w.start + w.skate + w.turn + w.off + w.gap + w.on + ez_frames(pl, sk_via[i].x, sk_via[i].y, sk_to[i].x, sk_to[i].y, SK_FACE(i), 2);
}

void hold_skate_arm(void)
{
    if (!sk_cap) return;
    sk_cap = 0;
    if (anth) return;                                   /* (the anthem's own walk to the spots) */
    int pstart = g.gameclock == g.PerTimeTotal;         /* (the start of a period: every skater comes on from his bench, not from where the last period ended) */
    int far = 0, order[2] = { 0, 0 };
    struct SortObj *pl = sortobj(0);
    for (int i = 0; i < 12; i++, pl++) {
        sk_to[i].x = pl->Xpos; sk_to[i].y = pl->Ypos; face_set = 1;
        spot_face[i] = (int16_t)dot_face(pl, pl->Xpos, pl->Ypos);
        sk_face[i] = (int16_t)(spot_face[i] | 0x800 | (pstart && pl->position > 0 ? 2 : sk_f0[i]) << 8);   /* (from the bench: the hop on leaves him facing 2) */
        int fx = sk_from[i].x, fy = sk_from[i].y;
        sk_gate[i] = 0;
        sk_delay[i] = 0;
        sk_via[i].x = ENTER_X;                          /* (his place along the bench, asseben) */
        sk_via[i].y = (int16_t)(((int16_t)pl->SCnum - 6 + ((int16_t)pl->SCnum >= 6)) * 0xE);
        sk_pstart = pstart;
        if (pstart && pl->position > 0) {               /* (over the boards one after another: the two teams' turns side by side) */
            int away = PFLAG(pl, pfteam) ? 1 : 0;
            sk_from[i] = sk_via[i];
            sk_gate[i] = 1;
            sk_delay[i] = (int16_t)(order[away]++ * 12);
            int d0 = sk_delay[i] + walk_frames(i);
            if (d0 > far) far = d0;
            continue;
        }
        if (pl->position <= 0 || fx < -300 || fx > 300 || fy < -300 || fy > 300) { sk_from[i] = sk_to[i]; continue; }
        if ((int8_t)pl->pnum != sk_pnum[i]) sk_gate[i] = 1;   /* (a line change: off through the gate, the next one on over the boards) */
        int d = walk_frames(i);
        if (d > far) far = d;
    }
    sk_dur = far + 1;
    if (pstart) {                                       /* (the referee comes on too, with the puck: from the PC's start of play to the dot) */
        sk_puckx = REF_START_X; sk_pucky = 0;
        sk_pick = 0;
        int rl = plen(REF_START_X, 0, (int16_t)g.fox, (int16_t)g.foy) / SKATE_SPEED + 1;
        if (rl > sk_dur) sk_dur = rl;
    }
    if (sk_dur < 24) return;
    if (sk_dur > 400) sk_dur = 400;
    sk_t0 = g.Vcount;
    sk_on = 1;
    g.yleader = 0;                                      /* (the PC's drop: the view on the dot, no lead) */
    if (!pstart && !sk_dark) { g.Vpos = sk_view[0]; g.Hpos = sk_view[1]; g.yc1 = sk_view[2]; g.xc1 = sk_view[3]; }   /* (no cut to the dot: the view follows the referee's puck there) */
    if (getenv("HOLDLOG")) fprintf(stderr, "hold: skate to the spots, %d frames, at %d (clock %d of %d)\n", sk_dur, g.Vcount, g.gameclock, g.PerTimeTotal);
}

/* After the face off's set up has picked the controlled players (from the spots, as the cartridge does): the skaters put back where
 * they are on their way (the walk's first frame) and the sprites sorted again. The set up's own sort showed them at the spots through
 * its work (unpacking the face off's art), a frame or more, then the walk took them back: the jump cut at the whistle. NHL 95 PC's
 * players carry on from where they were. (A period's start fades to black: nothing to do.) */
void hold_skate_place(void)
{
    if (!sk_on || sk_pstart || anth) return;
    skate_pose();                                       /* (the walk's first frame: the skaters, the puck where it lay, the view held) */
    struct SortObj *pl = sortobj(0);
    for (int i = 0; i < 12; i++, pl++)                  /* (and as they looked: not blank for a frame) */
        if (!pl->frame && (int8_t)pl->pnum == sk_pnum[i] && pl->Xpos == sk_from[i].x && pl->Ypos == sk_from[i].y) pl->frame = sk_frame[i];
    SprSort();
}

/* NHL 95 PC's assrefgoalpa (4ED7C) does not fetch a puck out of play (past |x| 0A0h or |y| 112h, the PC's rink): assrefwhistle
 * (4E8EF) takes the referee to the timekeeper (0A0h, 0) and assrefdrop (4F5BF) has him skate to the dot with the puck it gives him
 * (puckc 10h, puckz -100: not on the ice). Here a puck past the cartridge's boards (88h, 12Ah) at the whistle: he skates to the
 * timekeeper's side (TK_X, 0) instead of to it, with no puck on the ice, and from there carries one to the dot. Before, he skated
 * out of the rink to where it lay (PR #167's walk, then #193's for the game's referee) */
#define TK_X 0x78
static int sk_oop(void) { return !sk_pstart && (sk_puckx > 0x88 || sk_puckx < -0x88 || sk_pucky > 0x12A || sk_pucky < -0x12A); }
static void pick_spot(int *x, int *y)
{
    if (sk_oop()) { *x = TK_X; *y = 0; return; }
    *x = sk_puckx; *y = sk_pucky;
    /* (a puck against the boards: he reaches it from 8 inside a skater's limits, checkwallcoll's 80h, 126h and corner circles of 40h
     * about +-40h, +-0E6h, not from in the boards) */
    const int lx = 0x80 - 8, ly = 0x126 - 8, cx = 0x40, cy = 0xE6, r = 0x40 - 8;
    if (*x > lx) *x = lx;
    if (*x < -lx) *x = -lx;
    if (*y > ly) *y = ly;
    if (*y < -ly) *y = -ly;
    int ax = *x < 0 ? -*x : *x, ay = *y < 0 ? -*y : *y;
    if (ax > cx && ay > cy) {
        double qx = ax - cx, qy = ay - cy, d = sqrt(qx * qx + qy * qy);
        if (d > r) { ax = cx + (int)(qx * r / d); ay = cy + (int)(qy * r / d); *x = *x < 0 ? -ax : ax; *y = *y < 0 ? -ay : ay; }
    }
}

/* where the referee picks the puck up (where it lay at the whistle, or the timekeeper's for one out of play; valid with
 * hold_skate_ref() >= 0); he reaches it at SK_PICKUP of the 256 */
void hold_skate_puck(int *x, int *y, int *pickup) { pick_spot(x, y); *pickup = sk_pick; }

/* 1 while the face off is to be set up and its walk is not yet known: the puck's assignment is the face off's, new (pfna: its set
 * up, puckfaceoff2's, runs in the next pass, hold_skate_begin to hold_skate_arm). The referee stays where he is till then */
int hold_skate_pending(void)
{
    if (!RULE(RULE_HOLD)) return 0;
    if (sk_cap) return 1;
    const struct SortObj *pk = sortobj(puckscnum);
    int asn = pk->asslist[pk->assnum & 7];
    return (asn == pfaceoff || asn == pfaceoff2) && PFLAG(pk, pfna);
}

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
    int sx, sy, ex = ref_spot_x() * 256, ey = (int16_t)g.foy * 256;
    pick_spot(&sx, &sy);
    sx *= 256; sy *= 256;
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
    int px, py;
    pick_spot(&px, &py);
    *x += (px * 256 - *x) / n;
    *y += (py * 256 - *y) / n;
    return 1;
}

/* a bench skater before he is on the ice again (r: frames from the end of his wait). 0 once he skates to his spot */
static int bench_pose(int i, struct SortObj *pl, int r)
{
    struct benchway w = bench_way(i);
    int dy = DOOR_Y(pl);
    pl->Xvel = 0; pl->Yvel = 0;
    if (r < 0) goto benched;                            /* (the start of a period: on the bench, his turn to come) */
    if (r < w.start) {                                  /* (turning where he stood toward the gate: the PC's doplayeracc from rest) */
        ez_turn_pose(pl, SK_FACE0(i), DOOR_X + DOOR_GLIDE - sk_from[i].x, dy - sk_from[i].y, r);
        pl->Xpos = sk_from[i].x; pl->Ypos = sk_from[i].y;
        return 1;
    }
    r -= w.start;
    if (r < w.skate) {                                  /* (to the gate) */
        int ax = sk_from[i].x, ay = sk_from[i].y, bx = DOOR_X + DOOR_GLIDE, l = plen(ax, ay, bx, dy);
        if (l < 1) l = 1;
        struct ez s = ez_run(pl, l, r, 0);                /* (speeding up from rest as the PC's skateto does) */
        int run = (int)(s.pos >> 8);
        pl->Xpos = (int16_t)(ax + (int)((int64_t)(bx - ax) * run / (l * 256)));
        pl->Ypos = (int16_t)(ay + (int)((int64_t)(dy - ay) * run / (l * 256)));
        stride(pl, bx - ax, dy - ay, s.v * 17.0 / 65536);
        return 1;
    }
    r -= w.skate;
    if (r < w.turn) {                                   /* (gliding the last of it, a step round every 8 frames) */
        int f = vtoa((int16_t)(DOOR_X + DOOR_GLIDE - sk_from[i].x), (int16_t)(dy - sk_from[i].y)) & 7, n = door_turns(i), k = r / 8 + 1;
        if (k > n) k = n;
        int dir = ((4 - f) & 7) <= 4 ? 1 : -1;
        pl->facedir = (int16_t)((f + dir * k) & 7);
        pl->Xpos = (int16_t)(DOOR_X + DOOR_GLIDE - DOOR_GLIDE * (r + 1) / w.turn);
        pl->Ypos = (int16_t)dy;
        if (r == 0) SetSPA(pl, SPAglide);
        return 1;
    }
    r -= w.turn;
    if (r < w.off) {                                    /* (over the boards) */
        pl->Xpos = DOOR_X; pl->Ypos = (int16_t)dy;
        pl->facedir = 2;
        if (r == 0) { SetSPA(pl, SPAwallleft); pl->pflags |= 1 << pfalock; }
        return 1;
    }
    r -= w.off;
    if (r < w.gap) goto benched;
    r -= w.gap;
    if (r < w.on) {                                     /* (over the boards onto the ice) */
        pl->Xpos = sk_via[i].x; pl->Ypos = sk_via[i].y;
        pl->facedir = 2;
        if (r == 0) { SetSPA(pl, SPAwallright); pl->pflags |= 1 << pfalock; }
        return 1;
    }
    if (r == w.on) pl->pflags &= (uint8_t)~(1 << pfalock);
    return 0;
benched:                                                /* (on the bench: not seen, as the cartridge's assbench leaves him) */
    pl->Xpos = sk_via[i].x; pl->Ypos = sk_via[i].y;
    pl->pflags &= (uint8_t)~(1 << pfalock);
    if (pl->SPA) SetSPA(pl, 0);
    pl->frame = 0;
    return 1;
}

static void skate_players(int t)
{
    struct SortObj *pl = sortobj(0);
    for (int i = 0; i < 12; i++, pl++) {
        int d = sk_gate[i] ? 2 : plen(sk_from[i].x, sk_from[i].y, sk_to[i].x, sk_to[i].y);
        if (d < 2) continue;
        int r = t - sk_delay[i];
        if (sk_gate[i] && t < sk_dur && bench_pose(i, pl, r)) continue;
        int ax = sk_from[i].x, ay = sk_from[i].y;
        if (sk_gate[i]) {                               /* (on the ice again, from his place along the bench) */
            struct benchway w = bench_way(i);
            r -= w.start + w.skate + w.turn + w.off + w.gap + w.on;
            ax = sk_via[i].x; ay = sk_via[i].y;
            d = plen(ax, ay, sk_to[i].x, sk_to[i].y);
        }
        if (t < sk_dur) {                               /* (on his way: speeding up, the scrape stop, gliding in, turning to face the puck) */
            ez_pose(pl, ax, ay, sk_to[i].x, sk_to[i].y, SK_FACE(i), sk_gate[i] ? 2 : SK_FACE0(i), r);   /* (whatever the set up gave him: a face off man's stance waits for the dot) */
            continue;
        }
        /* (the walk's last frame: the face off men's stance comes once all are there, src/logic.c) */
        if (t == sk_dur || pl->Xpos != sk_to[i].x || pl->Ypos != sk_to[i].y) {
            pl->Xpos = sk_to[i].x; pl->Ypos = sk_to[i].y;
            pl->facedir = SK_FACE(i);
            pl->pflags &= (uint8_t)~(1 << pfalock);
            SetSPA(pl, SPAglide);
        }
        pl->Xvel = 0;
        pl->Yvel = 0;
    }
}

static void skate_pose(void)
{
    int t = (int16_t)(g.Vcount - sk_t0);
    skate_players(t);
    if (getenv("HOLDSK") && !(t % 6)) { struct SortObj *q = sortobj(1); fprintf(stderr, "sk t %d dur %d: x %d y %d frame %d spa %d face %d\n", t, sk_dur, q->Xpos, q->Ypos, q->frame, q->SPA, q->facedir); }
    int rx, ry, hand = 0;
    struct SortObj *pk = sortobj(puckscnum);
    if (sk_pstart) {                                    /* (the start of a period: the referee has the puck in his hand; it stays at the dot, and the view with it) */
        pk->Xpos = (int16_t)g.fox; pk->Ypos = (int16_t)g.foy;
        g.puckx[0] = g.fox; g.pucky = g.foy;
        ST_W(g.puckz[0]);
        hand = t < sk_dur;
    } else if (t < sk_dur && t * 256 / sk_dur < sk_pick) {      /* (the referee is still on his way to the puck where it lay) */
        int qx, qy;
        pick_spot(&qx, &qy);
        pk->Xpos = (int16_t)qx; pk->Ypos = (int16_t)qy;
        g.puckx[0] = (uint16_t)qx; g.pucky = (uint16_t)qy;
        if (sk_oop()) { ST_W(g.puckz[0]); hand = 1; }           /* (out of play: the timekeeper's puck, not on the ice till he has it) */
        else g.puckz[0] = 0;
    } else if (t < sk_dur && hold_skate_carry(&rx, &ry)) {   /* the referee carries the puck to the dot (the host's picture of him follows it) */
        pk->Xpos = (int16_t)(rx / 256); pk->Ypos = (int16_t)(ry / 256);
        g.puckx[0] = (uint16_t)pk->Xpos; g.pucky = (uint16_t)pk->Ypos;
        ST_W(g.puckz[0]);                                /* (in his hand, as at the dot: not drawn. On the ice (0) it went along at his feet) */
        hand = 1;
    } else if (t >= sk_dur) {
        pk->Xpos = (int16_t)g.fox; pk->Ypos = (int16_t)g.foy;
        g.puckx[0] = g.fox; g.pucky = g.foy;
        ST_W(g.puckz[0]);
    }
    (void)hand;                                         /* (the puck in his hand: puckz negative; its shadow is off, hold_shadow) */
    g.sflags |= HI(sfslock);                            /* (the view is the walk's: it follows the referee, no cut to where the puck lay) */
    sk_lock = 1;
    /* NHL 95 PC's checkwindow between plays: the camera follows the referee (object 10h) as he steps to the puck (RefStep: toward
     * his target, never past it), then the puck's carrier, who is the referee (the lead from his y speed), and locks onto the dot
     * (fox, foy) once he skates to it within 40 (0x640 squared); it moves only when its point leaves a margin (28h across, 0Ah
     * along; none at the dot), by a share of the gap. Here the referee's way is the walk's own (the game's state alone: where the
     * puck lay, the dot, the frames since the whistle), so every copy of the game moves the view alike */
    {
        int ty, tx, mx = 0x28, my = 0xA;
        int fx = (int16_t)g.fox, fy = (int16_t)g.foy;
        if (sk_pstart || t >= sk_dur) { tx = fx; ty = fy; mx = my = 0; }
        else if (hold_skate_carry(&rx, &ry)) {          /* (he carries the puck to the dot: the view follows him) */
            tx = rx / 256; ty = ry / 256;
            if ((tx - fx) * (tx - fx) + (ty - fy) * (ty - fy) < 0x640) { tx = fx; ty = fy; mx = my = 0; }
        } else {                                        /* (he skates to where the puck lay, from about where the view was at the whistle) */
            int tp = (sk_pick * sk_dur + 255) / 256, k = t < tp ? t : tp;
            if (tp < 1) tp = 1;
            int qx, qy;
            pick_spot(&qx, &qy);
            tx = (int16_t)sk_view[3] + (qx - (int16_t)sk_view[3]) * k / tp;
            ty = (int16_t)sk_view[2] + (qy - (int16_t)sk_view[2]) * k / tp;
        }
        g.yleader = 0;
        int vh = view_hpos();
        if (ty < -0xC8) ty = -0xC8;
        if (ty > 0x100) ty = 0x100;
        if (tx > vh) tx = vh;
        if (tx < -vh) tx = -vh;
        int dv = ty - (int16_t)g.Vpos, dh = tx - (int16_t)g.Hpos;
        dv = dv > my ? dv - my : dv < -my ? dv + my : 0;   /* (the margin: the point may wander that far from the middle) */
        dh = dh > mx ? dh - mx : dh < -mx ? dh + mx : 0;
        int sv = dv / 6, sh = dh / 6;                    /* (a share of the gap, at least 1, at most 4 a frame: the view glides) */
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

/* NHL 95 PC draws the puck's shadow only with the puck on the ice (puckz 0 or more) and not in the referee's hand (puckc 10h):
 * its frame code (hockey.asm, and changeplayer's copy) skips frame 189h otherwise. Its face off set up and assrefdrop put the
 * puck off the ice (puckz -100) till he drops it, so from the whistle (or the anthem's line up) to the drop neither is seen.
 * Here the puck is off the ice the same way (puckz's high byte FFh: the anthem, the walk's carry, the set up at the dot), and
 * the cartridge's shadow (sort object 15, frame SPFpuck: palette 0's grey-pink colour 8) sat on the dot, over a centre ice
 * logo. Frame 0 is the shadow's way to be off (puckshadow, addframe2); only the shadow's own frame is touched (not the goal
 * siren's, which it also draws). It is the cartridge's again once the puck is on the ice at the dot (the drop): the out of
 * play puck's frame 0 (collide.c, over the glass) is the game's own and stays till the set up's reset_ice_objects */
static void hold_shadow(void)
{
    if (!(RULE(RULE_HOLD) || RULE(RULE_REFEREE) || anth)) return;
    struct SortObj *pk = sortobj(puckscnum), *shadow = pk + 1;
    if ((int16_t)g.puckz[0] < 0) { if (shadow->frame == SPFpuck) shadow->frame = 0; }
    else if (shadow->frame == 0 && pk->Xpos == (int16_t)g.fox && pk->Ypos == (int16_t)g.foy) shadow->frame = SPFpuck;
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
    hold_shadow();
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
    if (!flag(fs) || !flag(so) || sst < 0 || sst > 2 || sf < 0 || sf > 900 || !flag(stm) || spl < 0 || spl > 31 || sa1 < -1 || sa1 > 31 || sa2 < -1 || sa2 > 31) return -1;
    int kc = hs_r8(&p), ko = hs_r8(&p), kd = hs_r16(&p), kt0 = hs_r16(&p);
    if (!flag(kc) || !flag(ko) || kd < 0 || kd > 400 || (ko && kd < 24)) return -1;
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
    sg.on = so; sg.started = sst ? 1 : 0; sg.limit = (uint16_t)sl; sg.frames = sf; sg.tm = team_struct(stm); sg.player = spl; sg.a1 = sa1; sg.a2 = sa2;
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
