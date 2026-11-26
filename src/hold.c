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
#define ANTH_SKATE 90                                  /* frames the skaters take from their lines to the face off spot after the anthem */
static int anth_pose, anth_len, anth_lock;
#define ANTH_VPOS (-18)                                 /* (the PC shows centre ice half way down its rink view) */                        /* the anthem's line up (1 once the spots are taken); the anthem's own frames */
static uint16_t anth_t0;
static struct { int16_t x, y; } spot[12];
static int16_t spot_face[12];
static int face_set;                         /* the way each faced at his spot as the game set him up */

/* a goal's speech held back until its song has ended */
static struct { int on, started; uint16_t limit; int frames; struct Team *tm; int player, a1, a2; } sg;

static int wanted(void) { return rules[RULE_HOLD] || rules[RULE_ANNOUNCE]; }

void hold_for(int frames)
{
    if (!wanted()) return;
    if (frames > 900) frames = 900;
    if (pending && (int16_t)(until - g.Vcount) > 0) until = (uint16_t)(until + frames);
    else until = (uint16_t)(g.Vcount + frames);
    pending = 1;
}

/* the anthems before the first face off: the drop waits for them whatever the rules say */
void hold_anthem(int frames)
{
    if (frames > 60 * 120) frames = 60 * 120;
    anth_len = frames;
    if (getenv("HOLDLOG")) fprintf(stderr, "hold: anthem %d frames at %d\n", frames, g.Vcount);
    anth_t0 = g.Vcount;
    anth_pose = 0;
    until = (uint16_t)(g.Vcount + frames + ANTH_SKATE);
    pending = 1;
    anth = 1;
}

int hold_anthem_pending(void) { return anth; }

/* RULE_HOLD: a face off in the middle of play comes with no fade to black (the PC game has none: the players skate to their dots with the
 * rink in view). The start of a period keeps the cartridge's. */
int hold_nofade(void) { return rules[RULE_HOLD] && !anth && g.gameclock != g.PerTimeTotal; }

/* the frames into the anthem's song (-1 when no song is on: before it, after it, or no anthem) */
int hold_anthem_time(void)
{
    if (!anth || !pending || !anth_pose) return -1;
    int t = (int16_t)(g.Vcount - anth_t0);
    return t >= 0 && t < anth_len ? t : -1;
}

/* the referee stands at the side while the song plays, then goes to the face off spot with the players */
int hold_anthem_ref(int *x, int *y, int *dir, int *moving)
{
    if (!anth || !pending || !anth_pose) return 0;
    int t = (int16_t)(g.Vcount - anth_t0) - anth_len;
    int fx = (int16_t)g.fox, fy = (int16_t)g.foy;
    int tx = (fx > 0 ? fx + 15 : fx - 15), ty = fy;
    int k = t < 0 ? 0 : t > ANTH_SKATE ? ANTH_SKATE : t;
    *x = 108 + (tx - 108) * k / ANTH_SKATE;
    *y = 0 + (ty - 0) * k / ANTH_SKATE;
    *dir = 6;                                           /* (he stands on the right: facing the rink, the lines of players) */
    *moving = k > 0 && k < ANTH_SKATE;
    if (*moving) *dir = tx > 108 ? 2 : 6;
    return 1;
}

/* the goal's own sound: the siren (the cartridge's or the PC's), then the goal song when there is one */
static int goal_sound_busy(void)
{
    if (song_playing() || (int16_t)g.songdelay > 0) return 1;
    if (siren_busy_hook && siren_busy_hook()) return 1;
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
        if (getenv("HOLDLOG")) for (int i = 0; i < 12; i++) fprintf(stderr, "hold: anthem player %d pos %d asn %d facedir %d\n", i, sortobj(i)->position, sortobj(i)->asslist[sortobj(i)->assnum & 7], sortobj(i)->facedir);
    }
    int skating = t >= anth_len;
    int slot[2] = {0, 0};
    struct SortObj *pl = sortobj(0);
    for (int i = 0; i < 12; i++, pl++) {
        if (pl->position <= 0) continue;                /* (goalies stay in their nets) */
        int team = PFLAG(pl, pfteam) ? 1 : 0;
        int n = slot[team]++;
        int lx = n * 40 - 80 + (team ? 4 : -4), ly = team ? 0x30 : -0x3A;   /* (the PC's lines: x lane * 28h - 50h, y +30h / -3Ah) */
        int x = lx, y = ly;
        if (skating) {
            int k = t - anth_len;
            if (k > ANTH_SKATE) k = ANTH_SKATE;
            x = lx + (spot[i].x - lx) * k / ANTH_SKATE;
            y = ly + (spot[i].y - ly) * k / ANTH_SKATE;
            if (k < ANTH_SKATE) pl->facedir = (int16_t)vtoa((int16_t)(spot[i].x - lx), (int16_t)(spot[i].y - ly));
            else pl->facedir = spot_face[i];
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
#define SKATE_SPEED 2                                   /* pixels a frame (a skater's top speed in the game is about 2: velocity 8000) */
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
int (*hold_ref_pos_fn)(int *x, int *y);                 /* the host's referee, on the rink (0: none): he carries the puck to the dot */

void hold_skate_begin(void)
{
    sk_cap = 0;
    if (!rules[RULE_HOLD]) return;
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
            if (pl->Xpos != sk_to[i].x || pl->Ypos != sk_to[i].y) {
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
            if (run == 0 || pl->SPA == 0 || pl->SPA == SPAglide) SetSPA(pl, SPAskate);
            if (l < 1) l = 1;
            pl->Xpos = (int16_t)(ax + (bx - ax) * r / l);
            pl->Ypos = (int16_t)(ay + (by - ay) * r / l);
            pl->facedir = (int16_t)vtoa((int16_t)(bx - ax), (int16_t)(by - ay));
            double ll = sqrt((double)(bx - ax) * (bx - ax) + (double)(by - ay) * (by - ay));
            if (ll < 1) ll = 1;
            pl->Xvel = (int16_t)((bx - ax) * 8192.0 / ll);       /* (his speed, so the skating animation runs at the pace of the strides) */
            pl->Yvel = (int16_t)((by - ay) * 8192.0 / ll);
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
    } else if (t < sk_dur && hold_ref_pos_fn && hold_ref_pos_fn(&rx, &ry)) {   /* the referee carries the puck to the dot */
        pk->Xpos = (int16_t)rx; pk->Ypos = (int16_t)ry;
        g.puckx[0] = (uint16_t)rx; g.pucky = (uint16_t)ry;
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

void hold_reset(void) { if (sk_lock) { g.sflags &= (uint16_t)~HI(sfslock); sk_lock = 0; } sk_on = 0; sk_cap = 0; pending = 0; anth = 0; anth_pose = 0; sg.on = 0; }
