/* The stoppage pauses. NHL 95 PC stops play after a goal or a penalty for as long as its announcer takes ("Los Angeles
 * goal number 99, assisted by number 4 and number 7"); the cartridge goes straight back to the face off. Two rules ask
 * for the wait: RULE_HOLD (the between-play time of the PC game, announcer or not) and RULE_ANNOUNCE (the rink
 * announcer's speech must finish). Either way the face off's drop waits until that long has passed since the stoppage
 * began (the clips' lengths in 95 PC's XBRUCE2.VIV, fixed here: the game must not depend on whether a machine has the
 * file). What the game already spends on the replay and the face off set up counts towards it.
 * RULE_ANNOUNCE 2: a home goal's speech starts after the goal song, and the wait covers the song and then the speech. */
#include <stdio.h>
#include <stdlib.h>
#include "game.h"
#include "rules.h"

#define PFLAG(o, b) ((o)->pflags & (1 << (b)))

static uint16_t until;                                /* the vblank (g.Vcount) the wait ends at */
static int pending;
static int anth;                                      /* the wait is the anthems' */
#define ANTH_SKATE 90                                  /* frames the skaters take from their lines to the face off spot after the anthem */
static int anth_pose, anth_len;                        /* the anthem's line up (1 once the spots are taken); the anthem's own frames */
static uint16_t anth_t0;
static struct { int16_t x, y; } spot[12];

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
    *dir = (fx > 0 ? 6 : 2);
    *moving = k > 0 && k < ANTH_SKATE;
    if (*moving) *dir = tx > 108 ? 2 : 6;
    return 1;
}

void hold_goal_after_song(int frames, struct Team *tm, int player, int a1, int a2)
{
    if (getenv("HOLDLOG")) fprintf(stderr, "hold: frame %d goal, song playing %d, delay %d\n", g.Vcount, song_playing(), (int16_t)g.songdelay);
    if (!song_playing() && (int16_t)g.songdelay <= 0) {   /* no song coming: the speech now */
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
        for (int i = 0; i < 12; i++, pl++) { spot[i].x = pl->Xpos; spot[i].y = pl->Ypos; }
        anth_pose = 1;
    }
    int skating = t >= anth_len;
    int slot[2] = {0, 0};
    struct SortObj *pl = sortobj(0);
    for (int i = 0; i < 12; i++, pl++) {
        if (pl->position <= 0) continue;                /* (goalies stay in their nets) */
        int team = PFLAG(pl, pfteam) ? 1 : 0;
        int n = slot[team]++;
        int lx = n * 40 - 80 + (team ? 4 : -4), ly = team ? 14 : -60;
        int x = lx, y = ly;
        if (skating) {
            int k = t - anth_len;
            if (k > ANTH_SKATE) k = ANTH_SKATE;
            x = lx + (spot[i].x - lx) * k / ANTH_SKATE;
            y = ly + (spot[i].y - ly) * k / ANTH_SKATE;
            if (k < ANTH_SKATE) pl->facedir = (int16_t)vtoa((int16_t)(spot[i].x - lx), (int16_t)(spot[i].y - ly));
            else pl->facedir = (int16_t)vtoa((int16_t)((int16_t)g.puckx[0] - x), (int16_t)((int16_t)g.pucky - y));
        } else {
            pl->facedir = (int16_t)vtoa((int16_t)(120 - x), (int16_t)(100 - y));
        }
        pl->Xpos = (int16_t)x;
        pl->Ypos = (int16_t)y;
        pl->Xvel = 0;
        pl->Yvel = 0;
    }
}

void hold_tick(void)
{
    if (anth && pending) anthem_pose();
    if (!sg.on) return;
    int playing = song_playing() || (int16_t)g.songdelay > 0;
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
    if (!pending) return 0;
    if (sg.on) return 1;
    if ((wanted() || anth) && (int16_t)(until - g.Vcount) > 0) return 1;
    pending = 0;
    anth = 0;
    return 0;
}

void hold_reset(void) { pending = 0; anth = 0; anth_pose = 0; sg.on = 0; }
