/* Game rules: what a game is played by beyond the original. These are native extras, not in the
 * cartridge: each rule's value is a number (an on / off rule 0 or 1, a list's choice its index),
 * and rule_classic has the original's ("Classic"),
 * which every check (make test, lockstep, golden) plays by. The host sets the rules before a game
 * starts (the menu's Game rules page, --rule on the command line); every player plays by the same
 * ones for the whole game, and states keep them. The game code reads them here, never the
 * settings file. */
#pragma once
#include <stddef.h>
#include <stdint.h>
struct Team;
struct SortObj;
struct PuckCross;

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
                                 * about as long as NHL 95 PC's announcer takes to say it (mods/hold.c) */
    RULE_FIGHT,                 /* NHL 93's fighting (mods/fight.c): hard checks between opposing skaters can start a fight */
    RULE_ANNOUNCE,              /* the rink announcer's timing (mods/hold.c): 1 the speech starts with the goal or penalty and play waits
                                 * until it is done, 2 a home goal's speech waits for the goal song to end */
    RULE_REFEREE,               /* 1: NHL 95 PC's referee in play (mods/referee.c): skaters bounce off him, the puck deflects
                                 * off him, a check knocks him down, every third knock down a penalty */
    RULE_ANTHEM,                /* the national anthem before the first face off (mods/hold.c): 1 NHL 95 PC's song, 2 NHL 96 PC's
                                 * recording. The players line up and the drop waits for the anthem's own length, fixed per
                                 * recording: every machine waits the same whether it has the PC game's files or not */
    RULE_SIREN,                 /* the goal siren the rink announcer waits for (RULE_ANNOUNCE 2, mods/hold.c): 1 NHL 95 PC's, its fixed
                                 * 7 seconds (the Game page's PC goal siren row sets it from the file the host has). The cartridge's
                                 * siren plays in the game either way; the host that has the PC's file plays that in its place */
    RULE_PENALTIES,             /* NHL 96 Genesis's double minors (mods/penalties.c): 1 as 96G, 2 also served as the NHL does (a power
                                 * play goal ends only a double minor's running 2 minutes, never a major) */
    RULE_FAKESHOT,              /* the fake shot (mods/moves.c): 1 NHL 95 PC's (B or A early in the wind up), 2 NHL 95 Genesis's (A let go
                                 * in the attacking zone) */
    RULE_DROPPASS,              /* the drop pass (mods/moves.c): 1 NHL 95 PC's (a pass to a trailing team-mate when the lane ahead is
                                 * shut), 2 NHL 95 Genesis's (A with the pad pulled back) */
    RULE_SHOTBLOCK,             /* blocking shots (mods/moves.c): 1 NHL 95 PC's (dives, slides and knee blocks), 2 NHL 95 Genesis's (the
                                 * kneeling block) */
    RULE_GOALIE,                /* the goalie's saves (mods/goalie.c): goaliesave by 1 NHL 95 PC's rules, 2 NHL 96 Genesis's, 3 NHL 95
                                 * Genesis's, 4 NHL 96 PC's (and the goalie's own hop over the boards, but in 3) */
    RULE_FIGHT_RATINGS,         /* 1: with RULE_FIGHT, each skater's fighting on NHL 93's scale, from NHLPA 93 and NHL 96 PC (the player's own
                                 * files, the host's in a session: native/fightrate.c); 0 the cartridge's (mods/fight.c fight_rating) */
    RULE_TICKER_HL,             /* 1: ticker highlights from the pregame (mods/ticker.c): InitScores plays 0-3 periods of the other games, not
                                 * 0-2, so the pregame's UpdateScores can send a close game to overtime and DoHiLights plays it there */
    RULE_PENCALL,               /* 1: with RULE_REFEREE, NHL 95 PC's penalty call (mods/referee.c): no fade at the whistle; the referee skates to
                                 * the timekeeper's side and signals the call, then a cut to the penalty screen, and a cut back from it */
    RULE_COUNT
};
/* the original's values */
#define RULES_CLASSIC { 0 }
extern const uint8_t rule_classic[RULE_COUNT];
extern uint8_t rules[RULE_COUNT];   /* each rule's value now (Classic until the platform sets them) */
/* src/ reads a rule only as RULE(r). The vanilla build (make VANILLA=1, -DNHL94_VANILLA on src/) makes every one the
 * cartridge's 0, so each customized branch in the game's code is compiled out and src/ is the cartridge's path alone. */
#ifdef NHL94_VANILLA
#define RULE(r) ((void)(r), 0)
#else
#define RULE(r) (rules[r])
/* debug (mods/fight.c's checkfight): the host's "the next check starts a fight" (FIGHT typed, offline); 1 once, then 0. NULL: none */
extern int (*fight_cheat_hook)(void);

#endif

/* RULE_HOLD (mods/hold.c): the stoppage's pause (NHL 95 PC's between-play time; RULE_ANNOUNCE also uses it, for the speech). hold_for makes the next face off wait
 * until frames vblanks have passed since now (a later call adds to what is left); hold_wait is 1 while it
 * must; hold_reset forgets it (a state loaded). The wait is in the game's own vblank count, not in audio:
 * every machine of a session waits the same, with the announcer or without. */
void hold_for(int frames);
void hold_anthem(int frames);
int hold_skate_carry(int *x, int *y);               /* the puck in the referee's hand on his way to the dot, 256ths of a pixel (0: not now) */
int hold_skate_ref_step(int *x, int *y);            /* the referee's place on his walk to the dot, 256ths of a pixel, from where he is (0: no walk) */
int hold_skate_pending(void);                    /* 1 between the face off's set up and the walk's start (the referee stays where he is) */
void hold_skate_begin(void);                        /* the face off set up: the skaters' places before it moves them ... */
void hold_skate_arm(void);                          /* ... and after: with RULE_HOLD they skate from the first to the second */
void hold_skate_place(void);                        /* ... once the controlled players are picked: the skaters shown where they are on their way */
int hold_skate_ref(void);                           /* the referee's progress along his way to the dot, 0 to 256 (-1: none) */
int hold_anthem_time(void);                         /* the frames into the anthem's song; -1 when it is not playing */
int hold_anthem_pending(void);                      /* 1 once the anthem has been asked for, until the face off drops */
int hold_anthem_frames(int set, int home_art);      /* RULE_ANTHEM's wait for the home team's anthem (set: the rule's value) */
int hold_anthem_song(void);                         /* 1 while the anthem's song lasts (the cartridge's songs stay quiet) */
void hold_skate_puck(int *x, int *y, int *pickup);  /* the puck's whistle place, and when (of 256) the referee has it */
int hold_skate_pstart(void);                         /* 1 while the skaters (and the referee) come on at the start of a period */
int hold_skating(void);                             /* 1 while the skaters walk to the spots (the drop's time waits for them) */
int hold_nofade(void);                             /* RULE_HOLD, a face off in the middle of play: no fade to black (1); RULE_PENCALL, back from the penalty screen: a cut (2) */
int hold_anthem_ref(int *x, int *y, int *dir, int *moving);   /* the anthem's referee: its place on the rink (1 while the anthem and the skate to the spots last) */
/* RULE_ANNOUNCE 2: a goal's speech (pa_hook PA_GOAL with these arguments) starts when the goal song has ended; the face off waits for
 * both. frames is the speech's length. hold_tick is called every game frame. */
void hold_goal_after_song(int frames, struct Team *tm, int player, int a1, int a2);
void hold_tick(void);
int hold_wait(void);
void hold_reset(void);
/* the pause's state (mods/hold.c) and the face off's held back assignments (src/logic.c), kept in every state: HOLD_STATE_SIZE bytes.
 * hold_state_load(NULL): none (a state from before them); -1 for bytes that are not this game's, nothing changed */
#define HOLD_STATE_SIZE 372
size_t hold_state_save(uint8_t *out);
int hold_state_load(const uint8_t *in);
void hold_siren(void);                              /* the goal siren starts (RULE_SIREN: its fixed length from now) */
/* the siren's part of a state (HOLD_SIREN_SIZE bytes; NULL: none, a state from before it); -1 for bytes not this game's */
#define HOLD_SIREN_SIZE 3
size_t hold_siren_save(uint8_t *out);
int hold_siren_load(const uint8_t *in);
#define FACEOFF_DEFER_SIZE 38
size_t faceoff_defer_save(uint8_t *out);
int faceoff_defer_check(const uint8_t *in);
int faceoff_defer_load(const uint8_t *in);

/* RULE_REFEREE (mods/referee.c): the referee is the game's own object, kept in every state. referee_update once a pass after
 * the skaters; referee_cx in checkplcoll's walk (skater o moved to x, y); referee_puck in checkpuckcoll's. referee_get: where
 * he is for the host's picture (0 while he is off the ice or the rule is off). The state's bytes: REFEREE_STATE_SIZE;
 * referee_state_load(NULL) starts him off the ice (a state from before him), -1 for bytes that are not a referee's. */
struct referee_view {
    int32_t x, y;                                   /* the rink position, 1/65536 pixels */
    int32_t vx, vy;                                 /* the speed, 1/65536 pixels a frame */
    int at_dot, drop;                               /* at the face off spot; the frames left of the puck drop */
    int fall, fall_dir;                             /* the frames into his fall (-1: standing), the way he faced */
    int dir, anim, at;                              /* his facedir; his picture (REF_GLIDE ...) and the frames into it */
    int sig;                                        /* REF_SIGNAL: which call (REF_SIG_ROUGH ...) */
};
enum { REF_GLIDE, REF_SKATE, REF_STOP, REF_TURNL, REF_TURNR, REF_SIGNAL };   /* (SPArefglide, SPArefskate, SPArefstop, SPArefturnl / r; the call's
                                                                             * signal, SPArefrough ... by referee_view's sig: refsigspa) */
enum { REF_SIG_ROUGH, REF_SIG_CHARGE, REF_SIG_SLASH, REF_SIG_XCHECK, REF_SIG_HOOK, REF_SIG_TRIP, REF_SIG_INTERFER, REF_SIG_HOLD,
       REF_SIG_PENSHOT, REF_SIGS };                 /* (NHL 95 PC's SPArefrough, charge, slash, xcheck, hook, trip, interfer, hold, penshot) */
int referee_call_made(void);                        /* RULE_PENCALL: the call made, the penalty screen cut to (till the face off) */
int pencall_ready(void);                            /* RULE_PENCALL (chkprogress): 0 the cartridge's way; -1 the referee is still making the call;
                                                     * 1 he has made it: cut to the penalty screen (no fade) */
void referee_update(void);
void referee_cx(int16_t x, int16_t y, struct SortObj *o);
void referee_puck(struct SortObj *puck);
int referee_get(struct referee_view *v);
/* NHL 95 PC's skating for him on a SortObj of its own (the game's referee's, or the host's picture's: it changes nothing of the
 * game): referee_skate_start puts him on at x, y; referee_follow is a frame of assreffollow (his spot, skateto, doplayeracc);
 * referee_glide a frame of updateplayers' glide and the boards (2: a hard hit) */
void referee_skate_start(struct SortObj *o, int16_t x, int16_t y);
void referee_follow(struct SortObj *o);
int referee_glide(struct SortObj *o);
#define REFEREE_STATE_SIZE 38                       /* (version 12 of the rules' part; 11: REFEREE_STATE_SIZE_11; 10: REFEREE_STATE_SIZE_10; 9 and before: REFEREE_STATE_SIZE_9) */
#define REFEREE_STATE_SIZE_11 33
#define REFEREE_STATE_SIZE_10 29
#define REFEREE_STATE_SIZE_9 22
size_t referee_state_save(uint8_t *out);
int referee_state_load(const uint8_t *in, size_t n);

/* RULE_PENALTIES (mods/penalties.c): pen_minutes, the minutes of penalty pen whose Penaltylist entry has mins (InProgress, the
 * penalty summary); pen_goal_keep, at a power play goal (PenGoalStuff) for player box of team tm in the box, his tmpdst word st:
 * -1 he goes out (the cartridge's), -2 not him (the next one), else he stays, the goal counts, and this is his tmpdst now */
uint8_t pen_minutes(uint16_t pen, uint8_t mins);
int32_t pen_goal_keep(const struct Team *tm, uint8_t box, uint16_t st);

/* RULE_FAKESHOT, RULE_DROPPASS, RULE_SHOTBLOCK (mods/moves.c), NHL 95's moves. moves_frame once a game frame (95G's CheckNewCarrier and
 * ShotTimer); moves_fake_pc in doinput_ispc's shot mode (d1 the new presses: 1 the wind up became a fake); moves_fake_g where A let go
 * passes (1: a fake shot or nothing, no pass); moves_fake_pending in SetShotMode (1: a 95G fake is starting, skip the penalty shot
 * box), moves_shot_spa the wind up it starts, moves_fake_end at doshot (1: the fake ends there, no shot); moves_drop_g where A is
 * pressed (d3 the held pad: 1 a drop pass starts); moves_dropping in dopass (1: a 95G drop pass), moves_pass_spa its swing,
 * moves_drop_time in passtoa0 (t the eighths of a second to the meeting, 1 a drop), moves_drop_loose for a drop to nobody,
 * moves_scatter (1: the pass was dropped to rcv, dopass ends); moves_block_hold in Holdplayer with no impact (1: a block started),
 * moves_block_cpu at check4check's start and moves_block_try at its end; moves_blocking in puckstick (1: his stick deflects the puck,
 * no steal, no catch). The state's bytes: MOVES_STATE_SIZE (moves_state_load(NULL): none, a state from before them) */
void moves_frame(void);
int moves_fake_pc(uint16_t d1, struct SortObj *o);
int moves_fake_g(struct SortObj *o);
int moves_fake_pending(void);
uint16_t moves_shot_spa(uint16_t spa);
int moves_fake_end(struct SortObj *o);
int moves_drop_g(uint16_t d3, struct SortObj *o);
int moves_dropping(void);
uint16_t moves_pass_spa(uint16_t spa);
int moves_drop_time(int16_t *t);
void moves_drop_loose(void);
int moves_scatter(struct SortObj *o, struct SortObj *rcv);
int moves_block_hold(struct SortObj *o);
int moves_block_cpu(struct SortObj *o);
void moves_block_try(struct SortObj *o);
int moves_blocking(const struct SortObj *o);
#define MOVES_STATE_SIZE 6
size_t moves_state_save(uint8_t *out);
int moves_state_load(const uint8_t *in);

/* RULE_GOALIE (mods/goalie.c): goalie_save, another game's goaliesave (1 when it made the save; 0 the cartridge's makes it);
 * goalie_wall_spa, the hop over the boards (assbench, asseben) */
int goalie_save(const struct PuckCross *pc, int16_t goalline, struct SortObj *gk);
uint16_t goalie_wall_spa(const struct SortObj *o, uint16_t spa);

/* RULE_TICKER_HL (mods/ticker.c): InitScores's randomd0 range, the other games' periods before the pregame (the cartridge's 3: 0-2) */
uint16_t ticker_init_periods(void);

/* mods/anims.c: the animations the PC's pictures need. Their frames are numbered from MODF_BASE (below the 1024 a replay keeps): mod_frame
 * is the cartridge's frame a mod frame stands for (its art, its hot spot, its save odds; any other frame is itself), mod_frame_pc the PC's
 * picture (-1: none). Their animations are MOD_SPA and up (mod_spa_table, which spa_table asks) */
#define MODF_BASE 0x360
#define MOD_SPA 0x5000u
uint16_t mod_frame(uint16_t fr);
int mod_frame_pc(uint16_t fr);
const uint8_t *mod_spa_table(uint16_t spa);

/* RULE_CD_COLOURS: a uniform's 16 colours (c) as the Sega CD has them, for the cartridge team art
 * (rosters_art) on the side away (0 home, 1 away); a colour changed only where it is the cartridge's */
void cd_colours(uint16_t art, int away, uint16_t c[16]);
