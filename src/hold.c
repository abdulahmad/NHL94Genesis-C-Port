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

static uint16_t until;                                /* the vblank (g.Vcount) the wait ends at */
static int pending;
static int anth;                                      /* the wait is the anthems' */

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
    until = (uint16_t)(g.Vcount + frames);
    pending = 1;
    anth = 1;
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

void hold_tick(void)
{
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

void hold_reset(void) { pending = 0; anth = 0; sg.on = 0; }
