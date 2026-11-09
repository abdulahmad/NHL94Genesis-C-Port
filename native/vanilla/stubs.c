/* The VANILLA=1 build (make VANILLA=1 nhl94: build/nhl94-vanilla): the game as the cartridge plays
 * it, with the host extras left out: game rules, roster sets (and the team art and banners they
 * make), save states and rewind, and online play (native/rules.c, rosters.c, romsrc.c, art.c,
 * banner.c, ratings.c, state.c, net.c). src/ is the same code as the default build's, with the
 * Classic rules and the cartridge's teams it plays by default (docs/VANILLA.md). These stand-ins
 * give the rest of the host what those files gave it: no pages, no options, the original's rules
 * and teams, and a plain "not in this build" for the command line's options that need them. */
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "native.h"
#include "rosters.h"
#include "rom.h"

static const char not_here[] = "not in the vanilla build (make VANILLA=1): build the default one for it";

/* game rules: always the original's */
const char *rules_arg(const char *arg) { (void)arg; return not_here; }
int rules_classic(void) { return 1; }
void rules_power_on(void) { memcpy(rules, rule_classic, sizeof rules); }
const mdrt_sdl_option *rules_page(int *count) { *count = 0; return NULL; }
const mdrt_sdl_page *rules_pages(int *count) { *count = 0; return NULL; }
void rules_page_text(int page, const int *values, char *text, size_t size, void *user) { (void)page; (void)values; (void)user; if (size) *text = 0; }
int rules_page_pick(int which, const char *path, char *why, size_t why_size, void *user) { (void)which; (void)path; (void)user; snprintf(why, why_size, "%s", not_here); return -1; }
void rules_page_detail(int which, int value, char *text, size_t size, void *user) { (void)which; (void)value; (void)user; if (size) *text = 0; }
void rules_page_use(const int *values, void *user) { (void)values; (void)user; }
int rules_page_changed(int which, int *values, void *user) { (void)which; (void)values; (void)user; return 0; }
int rules_page_session(int *values, void *user) { (void)values; (void)user; return 0; }

/* roster sets: always the cartridge's teams (src/rosters.c's from power on) */
const char *rosters_error(void) { return not_here; }
void rosters_next(const char *dir) { (void)dir; }
int rosters_next_is_cartridge(void) { return 1; }
void rosters_power_on(void) { rom_art = NULL; rom_limit = ROM_SIZE; rosters_retail(); }   /* (the teams' tables read from the cartridge) */
const char *rosters_sram(const char *base) { return base; }
int rosters_check(const char *dir, int describe) { (void)dir; (void)describe; return -1; }
int rosters_export(const char *dir) { (void)dir; return -1; }
int rosters_pack(const char *dir, const char *zip_path) { (void)dir; (void)zip_path; return -1; }
int rosters_extract(const struct romsrc *s, const char *dir, const char *name) { (void)s; (void)dir; (void)name; return -1; }
int romsrc_hack(struct romsrc *s, const uint8_t *rom, uint32_t size, const uint8_t *cart, char *why, size_t why_size)
{ (void)s; (void)rom; (void)size; (void)cart; snprintf(why, why_size, "%s", not_here); return -1; }

/* the art map (texture packs): none */
const struct art_piece *art_map(int *n) { *n = 0; return NULL; }
const struct art_piece *art_piece_at(uint32_t number) { (void)number; return NULL; }
const char *art_piece_name(const struct art_piece *p, char *out, size_t size) { (void)p; if (size) *out = 0; return out; }
int art_map_overlaps(void) { return 0; }
void art_map_report(void) { printf("art map: not in the vanilla build\n"); }

/* save states and rewind: none */
void native_states_init(mdrt *m, const char *path, int rewind) { (void)m; (void)path; (void)rewind; }
int native_load_state_file(const char *path) { fprintf(stderr, "%s: save states are %s\n", path, not_here); return -1; }
void native_save_state_at(long frame, const char *path) { (void)frame; fprintf(stderr, "%s: save states are %s\n", path, not_here); }
const char *native_state_check(const void *state, size_t size, void *user) { (void)state; (void)size; (void)user; return not_here; }
void native_states_wait(void) {}

/* online play: none */
const char *native_net_identity(const char *setup) { (void)setup; return ""; }
int native_net_open(const mdrt_net_options *o, const char *setup, const void *settings, size_t settings_size, native_input_fn input,
                    char *err, size_t errsize)
{ (void)o; (void)setup; (void)settings; (void)settings_size; (void)input; snprintf(err, errsize, "online play is %s", not_here); return -1; }
int native_net_connect(void) { return 0; }
int native_net_four_way(void) { return 0; }
void native_net_start(mdrt *m) { (void)m; }
void native_net_stop_at(long frame) { (void)frame; }
void native_net_wait(void) {}
int native_net_on(void) { return 0; }
const void *native_net_settings(size_t *size) { *size = 0; return NULL; }
void native_net_load_at(long frame, const char *path) { (void)frame; (void)path; }
void native_net_power_on(mdrt *m) { (void)m; }
void native_net_close(void) {}
