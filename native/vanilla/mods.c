/* The VANILLA=1 build's stand-ins for mods/ (make VANILLA=1 nhl94, docs/VANILLA.md): what the cartridge does at each
 * place src/ calls a customization. mods/ is left out of that build, so the game is src/ alone; these are the
 * cartridge's own behaviour there (the 94 cartridge's assfight, assfwatch and checkfight are a bare rts, it has no
 * fight input and no referee on the ice), checked by make lockstep-vanilla against the cartridge. */
#include "game.h"
#include "rom.h"
#include "rules.h"
#include "symbols_gen.h"

/* mods/fight.c: NHL 93's fights. The cartridge has none. */
void assfight(struct Team *tm, struct SortObj *o) { (void)tm; (void)o; }                    /* (rts) */
void assfwatch(struct Team *tm, struct SortObj *o) { (void)tm; (void)o; }                   /* (rts) */
void checkfight(struct SortObj *a2, struct SortObj *a3) { (void)a2; (void)a3; }             /* (rts) */
int (*fight_cheat_hook)(void);                                                              /* (no fights: never asked) */
void fightinput(uint16_t d0, uint16_t d1, struct SortObj *o) { (void)d0; (void)d1; (void)o; }
uint16_t fight_frame_shown(uint16_t fr, int facedir) { (void)facedir; return fr; }
const uint8_t *spa_table(uint16_t spa) { return rom_ptr(SYM_SPAlist + spa); }              /* (updateanim: SPAlist's entry) */

/* mods/referee.c: NHL 95 PC's referee in play. The cartridge has no referee on the ice. */
void referee_update(void) {}
void referee_cx(int16_t x, int16_t y, struct SortObj *o) { (void)x; (void)y; (void)o; }
void referee_puck(struct SortObj *puck) { (void)puck; }
int referee_get(struct referee_view *v) { (void)v; return 0; }

/* mods/hold.c: the between-play pause, the announcer's wait, the anthem and the skate to the spots. The cartridge goes
 * straight back to the face off. */
void hold_for(int frames) { (void)frames; }
void hold_anthem(int frames) { (void)frames; }
int hold_anthem_pending(void) { return 0; }
int hold_anthem_frames(int set, int home_art) { (void)set; (void)home_art; return 0; }
int hold_anthem_song(void) { return 0; }
int hold_nofade(void) { return 0; }
int hold_anthem_time(void) { return -1; }
int hold_anthem_ref(int *x, int *y, int *dir, int *moving) { (void)x; (void)y; (void)dir; (void)moving; return 0; }
void hold_siren(void) {}
void hold_goal_after_song(int frames, struct Team *tm, int player, int a1, int a2) { (void)frames; (void)tm; (void)player; (void)a1; (void)a2; }
void hold_skate_begin(void) {}
void hold_skate_arm(void) {}
void hold_skate_place(void) {}
void hold_skate_puck(int *x, int *y, int *pickup) { *x = *y = 0; *pickup = 0; }
int hold_skate_pstart(void) { return 0; }
int hold_skating(void) { return 0; }
int hold_skate_ref(void) { return -1; }
int hold_skate_carry(int *x, int *y) { (void)x; (void)y; return 0; }
int hold_skate_ref_step(int *x, int *y) { (void)x; (void)y; return 0; }
void hold_tick(void) {}
int hold_wait(void) { return 0; }
void hold_reset(void) {}

/* mods/penalties.c: NHL 96 Genesis's double minors. The cartridge's penalties are its Penaltylist's minutes, and a power
 * play goal lets out the first player in the box who is not matched (PenGoalStuff). */
uint8_t pen_minutes(uint16_t pen, uint8_t mins) { (void)pen; return mins; }
int32_t pen_goal_keep(const struct Team *tm, uint8_t box, uint16_t st) { (void)tm; (void)box; (void)st; return -1; }

/* mods/moves.c: NHL 95's fake shot, drop pass and shot block. The cartridge has none: a wind up goes on, A let go passes, A held changes
 * lines, a pass is its own, a hold with no one in reach is a hook, check4check only checks, and a stick on the puck steals or catches. */
void moves_frame(void) {}
int moves_fake_pc(uint16_t d1, struct SortObj *o) { (void)d1; (void)o; return 0; }
int moves_fake_g(struct SortObj *o) { (void)o; return 0; }
int moves_fake_pending(void) { return 0; }
uint16_t moves_shot_spa(uint16_t spa) { return spa; }
int moves_fake_end(struct SortObj *o) { (void)o; return 0; }
int moves_drop_g(uint16_t d3, struct SortObj *o) { (void)d3; (void)o; return 0; }
int moves_dropping(void) { return 0; }
uint16_t moves_pass_spa(uint16_t spa) { return spa; }
int moves_drop_time(int16_t *t) { (void)t; return 0; }
void moves_drop_loose(void) {}
int moves_scatter(struct SortObj *o, struct SortObj *rcv) { (void)o; (void)rcv; return 0; }
int moves_block_hold(struct SortObj *o) { (void)o; return 0; }
int moves_block_cpu(struct SortObj *o) { (void)o; return 0; }
void moves_block_try(struct SortObj *o) { (void)o; }
int moves_blocking(const struct SortObj *o) { (void)o; return 0; }

/* mods/goalie.c and mods/anims.c: the PC goalie's animations. The cartridge's goalie saves and hops the boards in its own, and every
 * frame is a cartridge frame. */
uint16_t goalie_save_spa(uint16_t spa, int16_t d3, const struct PuckCross *pc, const struct SortObj *gk) { (void)d3; (void)pc; (void)gk; return spa; }
uint16_t goalie_wall_spa(const struct SortObj *o, uint16_t spa) { (void)o; return spa; }
uint16_t mod_frame(uint16_t fr) { return fr; }
int mod_frame_pc(uint16_t fr) { (void)fr; return -1; }
const uint8_t *mod_spa_table(uint16_t spa) { return rom_ptr(SYM_SPAlist + spa); }
