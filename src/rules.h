/* Game rules: what a game is played by beyond the original. These are native extras, not in the
 * cartridge: each rule's value is a number (an on / off rule 0 or 1, a list's choice its index),
 * and rule_classic has the original's ("Classic"),
 * which every check (make test, lockstep, golden) plays by. The host sets the rules before a game
 * starts (the menu's Game rules page, --rule on the command line); every player plays by the same
 * ones for the whole game, and states keep them. The game code reads them here, never the
 * settings file. */
#pragma once
#include <stdint.h>
struct Team;

enum {
    RULE_WEIGHT_FIX,            /* 1: a check's weights summed in full (the original's 8-bit sum wraps,
                                 * so a heavy skater checked by a lighter pad player falls easily) */
    RULE_VIEW,                  /* 1: the rink 320 wide (src/view.h): the camera moves less, players
                                 * count as off screen further out (a pad takes its goalie later) */
    /* the Sega CD's changes to the game (NHL94Genesis docs/segacd_differences.md), each 0 the
     * cartridge's, 1 the disc's: the overlay's Game page sets them */
    RULE_CD_SKATING,            /* playeracc adds the crowd meter boost twice (+4, not +2) */
    RULE_CD_CORNERS,            /* checkcornercoll's corner radius squared $42, not $1000 */
    RULE_CD_PSBOX,              /* doinput: the pad does nothing while the penalty-shot box is up */
    RULE_CD_PASSWORD,           /* a playoff password's pad layout read as the disc reads it */
    RULE_CD_CLEARSHOT,          /* ClearShotData clears its $72 words, not $E4 (past the buffer) */
    RULE_CD_COLOURS,            /* HFD's home and OTW's away uniforms in the disc's colours */
    RULE_CD_SOUNDS,             /* the disc's sound ids: the breakaway "ding" 2 (not 1) in breakaway, asspuckc and
                                 * assnearest; dopass's sound always $10 (not $10-$13 by the puck's height) */
    RULE_CD_SONGS,              /* the disc starts songs at once: the home goal song (not 120 frames later), the
                                 * faceoff song in puckfaceoff (not queued for the drop) */
    RULE_CD_PERIOD_END,         /* PeriodOver_sp without the two vblank waits and the p_turnoff between them */
    RULE_HOLD,                  /* play waits between a goal, a penalty or a penalty shot and the next face off for
                                 * about as long as NHL 95 PC's announcer takes to say it (src/hold.c) */
    RULE_FIGHT,                 /* NHL 93's fighting (src/fight.c): hard checks between opposing skaters can start a fight */
    RULE_ANNOUNCE,              /* the rink announcer's timing (src/hold.c): 1 the speech starts with the goal or penalty and play waits
                                 * until it is done, 2 a home goal's speech waits for the goal song to end */
    RULE_COUNT
};
/* the original's values */
#define RULES_CLASSIC { 0 }
extern const uint8_t rule_classic[RULE_COUNT];
extern uint8_t rules[RULE_COUNT];   /* each rule's value now (Classic until the platform sets them) */

/* RULE_HOLD (src/hold.c): the stoppage's pause (NHL 95 PC's between-play time; RULE_ANNOUNCE also uses it, for the speech). hold_for makes the next face off wait
 * until frames vblanks have passed since now (a later call adds to what is left); hold_wait is 1 while it
 * must; hold_reset forgets it (a state loaded). The wait is in the game's own vblank count, not in audio:
 * every machine of a session waits the same, with the announcer or without. */
void hold_for(int frames);
void hold_anthem(int frames);
int hold_anthem_time(void);                         /* the frames into the anthem's song; -1 when it is not playing */
int hold_anthem_pending(void);                      /* 1 once the anthem has been asked for, until the face off drops */
int hold_anthem_ref(int *x, int *y, int *dir, int *moving);   /* the anthem's referee: its place on the rink (1 while the anthem and the skate to the spots last) */
/* RULE_ANNOUNCE 2: a goal's speech (pa_hook PA_GOAL with these arguments) starts when the goal song has ended; the face off waits for
 * both. frames is the speech's length. hold_tick is called every game frame. */
void hold_goal_after_song(int frames, struct Team *tm, int player, int a1, int a2);
void hold_tick(void);
int hold_wait(void);
void hold_reset(void);

/* RULE_CD_COLOURS: a uniform's 16 colours (c) as the Sega CD has them, for the cartridge team art
 * (rosters_art) on the side away (0 home, 1 away); a colour changed only where it is the cartridge's */
void cd_colours(uint16_t art, int away, uint16_t c[16]);
