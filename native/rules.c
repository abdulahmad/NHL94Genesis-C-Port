/* Game rules on the native side (src/rules.h): the catalogue the menu's Game rules page and the
 * command line work from, the rules of the next game, and the state part that keeps a game's
 * session (its rules, and its roster set: rosters.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "native.h"
#include "rules.h"

static const char *const off_on[2] = { "off", "on" };
static const char *const fight_names[4] = { "off", "NHL 93", "NHL 92", "hybrid" };   /* (fighting: mods/fight.c) */
static const char *const announce_names[3] = { "talks over the face off", "right after the goal", "after the goal song" };
static const char *const view_names[3] = { "classic", "wide", "wide play" };   /* (src/view.h: wide the picture alone, wide play the game too) */
static const char *const view_labels[3] = { "256 wide", "320 wide (the picture)", "320 wide (played by it)" };
static const char *const anthem_names[3] = { "off", "NHL 95 PC", "NHL 96 PC" };
static const char *const fight_rating_names[2] = { "cartridge", "NHLPA 93 + NHL 96" };   /* (RULE_FIGHT_RATINGS: native/fightrate.c) */
static const char *const siren_names[2] = { "cartridge", "NHL 95 PC" };
static const char *const penalty_names[3] = { "cartridge", "NHL 96 Genesis", "NHL rules" };   /* (double minors: mods/penalties.c) */
static const char *const move95_names[3] = { "off", "NHL 95 PC", "NHL 95 Genesis" };   /* (NHL 95's moves: mods/moves.c) */
static const char *const ticker_names[2] = { "cartridge", "from the pregame" };   /* (RULE_TICKER_HL: mods/ticker.c) */
static const char *const pencall_names[2] = { "cartridge", "PC referee" };   /* (RULE_PENCALL: mods/referee.c) */
static const char *const goalie_names[5] = { "NHL 94 Genesis", "NHL 95 PC", "NHL 96 Genesis", "NHL 95 Genesis", "NHL 96 PC" };   /* (mods/goalie.c; the numbers kept from the first two) */

/* the rules */
const mdrt_sdl_option rule_options[RULE_COUNT] = {
    [RULE_WEIGHT_FIX] = { "rules.weight_fix", "Weight bug fix", 2, off_on, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_VIEW] = { "rules.view", "Rink view", 3, view_names, view_labels, 0, 0, 0, 0, NULL, NULL, NULL },
    /* the Sega CD's changes (the overlay's Game page: native/frontend.c) */
    [RULE_CD_SKATING] = { "rules.cd_skating", "Sega CD skating", 2, off_on, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_CD_CORNERS] = { "rules.cd_corners", "Sega CD corner bounce", 2, off_on, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_CD_PSBOX] = { "rules.cd_penalty_shot_box", "Sega CD penalty shot box", 2, off_on, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_CD_PASSWORD] = { "rules.cd_password", "Sega CD playoff password", 2, off_on, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_CD_CLEARSHOT] = { "rules.cd_clear_shots", "Sega CD shot data fix", 2, off_on, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_CD_COLOURS] = { "rules.cd_colours", "Sega CD uniforms", 2, off_on, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_CD_SOUNDS] = { "rules.cd_sounds", "Sega CD sound choices", 2, off_on, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_CD_SONGS] = { "rules.cd_songs", "Sega CD song timing", 2, off_on, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_CD_PERIOD_END] = { "rules.cd_period_end", "Sega CD period end", 2, off_on, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_FIGHT] = { "rules.fighting", "Fighting", 4, fight_names, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_HOLD] = { "rules.hold_play", "PC between-play pause", 2, off_on, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_ANNOUNCE] = { "rules.announce_timing", "Rink announcer: when he speaks", 3, announce_names, NULL, 0, 0, 0, 0, NULL, NULL, NULL },   /* (the cartridge's by default: Recommended picks right after the goal) */
    [RULE_REFEREE] = { "rules.referee_in_play", "PC referee in play", 2, off_on, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_ANTHEM] = { "rules.anthems", "National anthems", 3, anthem_names, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_FIGHT_RATINGS] = { "rules.fight_ratings", "Fight ratings", 2, fight_rating_names, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_SIREN] = { "rules.goal_siren", "Goal siren (the announcer waits for it)", 2, siren_names, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_PENALTIES] = { "rules.double_minors", "Double minors and majors", 3, penalty_names, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_FAKESHOT] = { "rules.fake_shot", "Fake shot", 3, move95_names, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_DROPPASS] = { "rules.drop_pass", "Drop pass", 3, move95_names, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_SHOTBLOCK] = { "rules.shot_block", "Shot blocking", 3, move95_names, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_GOALIE] = { "rules.goalie_anims", "Goalie saves", 5, goalie_names, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_TICKER_HL] = { "rules.ticker_highlights", "Ticker highlights", 2, ticker_names, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_PENCALL] = { "rules.penalty_call", "Penalty call", 2, pencall_names, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
};

/* a choice's value: its number, or (a list's) its index */
static int value_of(int rule, int choice) { return rule_options[rule].numbers ? rule_options[rule].numbers[choice] : choice; }

/* a value the rule takes, or -1 */
static int valid(int rule, long v)
{
    const mdrt_sdl_option *op = &rule_options[rule];
    if (op->numbers) return v >= op->min && v <= op->max ? (int)v : -1;
    return v >= 0 && v < op->count ? (int)v : -1;
}

/* the options: the roster set (the cartridge's or the Sega CD's, chosen on the overlay's Game page),
 * then the rules, then Game Speed (Smooth the default; Original the cartridge's slowdown). Speed is
 * a host setting, not a game rule: Classic hashes stay the same. */
enum { P_ROSTERS, P_RULES };
#define P_SPEED (P_RULES + RULE_COUNT)
mdrt_sdl_option rule_page[P_SPEED + 1];
static const char *const speed_names[] = { "smooth", "original" };
static const char *const speed_labels[] = { "Smooth", "Original" };

static void page_init(void)
{
    const char *const *names, *const *labels;
    if (rule_page[0].key) return;
    int n = rosters_list(&names, &labels);
    if (n > 1 + ROSTERS_BUILTINS) n = 1 + ROSTERS_BUILTINS;   /* (the cartridge's and the built-in ones: no roster sets of the player's own) */
    rule_page[P_ROSTERS] = (mdrt_sdl_option){ "rules.rosters", "Rosters", n, names, labels, 0, 0, 0, 0, NULL, NULL, NULL };
    memcpy(rule_page + P_RULES, rule_options, sizeof rule_options);
    rule_page[P_SPEED] = (mdrt_sdl_option){ "game.speed", "Game Speed", 2, speed_names, speed_labels, 0, 0, 0, 0, NULL, NULL, NULL };
}

const mdrt_sdl_option *rules_page(int *count)
{
    page_init();
    *count = P_SPEED + 1;
    return rule_page;
}

/* the Game rules page, and Speed (Smooth the default). The roster set is not a page: the overlay's
 * Game page turns the Sega CD's on and off. */
static const mdrt_sdl_page pages[2] = {
    { "Game rules", "Classic: the original game", P_RULES, RULE_COUNT },
    { "Speed", "Smooth: one vblank a pass (the default). Original: the cartridge's slowdown", P_SPEED, 1 },
};

const mdrt_sdl_page *rules_pages(int *count)
{
    page_init();
    *count = 2;
    return pages;
}

/* a rule changed: nothing else follows it */
int rules_page_changed(int which, int *values, void *user)
{
    (void)which; (void)values; (void)user;
    return 0;
}

/* in a session (the window's thread, while it lasts): the host's choices, which the game plays by */
int rules_page_session(int *values, void *user)
{
    uint8_t v[RULE_COUNT];
    int sent, host;
    if (!native_net_session(v, &sent, &host)) return 0;
    int k = host ? values[P_ROSTERS] : sent ? rosters_session_entry() : 0;   /* (the host: its own choice, which was sent) */
    if (k < 0) return 0;                                  /* (not listed yet: asked again) */
    if (k >= rule_page[P_ROSTERS].count) rule_page[P_ROSTERS].count = k + 1;
    values[P_ROSTERS] = k;
    for (int i = 0; i < RULE_COUNT; i++) values[P_RULES + i] = v[i];
    return 1;
}

void rules_page_use(const int *values, void *user)
{
    char dir[600];
    rosters_next(rosters_list_dir(values[P_ROSTERS], dir, sizeof dir));   /* (NULL: the cartridge's) */
    rules_next(values + P_RULES, user);
    clock_set_original(values[P_SPEED]);
    native_net_identity("");                              /* (the session's identity follows Game Speed) */
}

static uint8_t next[RULE_COUNT] = RULES_CLASSIC;   /* the rules of the next game */

/* the platform's values: a number rule's number, a list's index (one out of range: Classic) */
void rules_next(const int *values, void *user)
{
    (void)user;
    for (int i = 0; i < RULE_COUNT; i++) next[i] = (uint8_t)(valid(i, values[i]) < 0 ? rule_classic[i] : values[i]);
    if (!native_net_on()) {                               /* (the stoppage waits only time the face off: a change counts at once when playing alone) */
        rules[RULE_HOLD] = next[RULE_HOLD];
        rules[RULE_ANNOUNCE] = next[RULE_ANNOUNCE];
        rules[RULE_SIREN] = next[RULE_SIREN];
    }
}

/* the Widescreen setting at 16:9: the rink's picture is the wide view's (320, RULE_VIEW 1), so the game fills the
 * screen. The picture alone: the game plays by the cartridge's 256 (src/view.h), unless the rule itself asks for wide
 * play (2), which 16:9 leaves as it is. Not a rule of its own: a session draws wide when any of its machines is 16:9
 * (native_net_settings sends it), a check by Classic */
static volatile int widescreen;
void rules_widescreen(int on) { widescreen = on; }
int rules_widescreen_on(void) { return widescreen; }

void rules_power_on(void)
{
    memcpy(rules, next, sizeof rules);
    if (widescreen && !native_net_on() && !rules[RULE_VIEW]) rules[RULE_VIEW] = 1;   /* (in a session the host's settings say it) */
}

void rules_next_get(uint8_t *v) { memcpy(v, next, sizeof next); }

void rules_next_send(uint8_t *v)
{
    rules_next_get(v);
    if (widescreen && !v[RULE_VIEW]) v[RULE_VIEW] = 1;   /* (the wide picture; wide play stays) */
}

int rules_next_set(const uint8_t *v, int count)
{
    uint8_t x[RULE_COUNT];
    if (count > RULE_COUNT) return -1;                    /* (a build with more rules: not this one) */
    memcpy(x, rule_classic, sizeof x);
    for (int i = 0; i < count; i++) {
        if (valid(i, v[i]) < 0) return -1;
        x[i] = v[i];
    }
    memcpy(next, x, sizeof next);
    return 0;
}

int rules_classic(void) { return !memcmp(next, rule_classic, sizeof next); }

/* names[0..count) joined with ", " into out */
static void joined(char *out, size_t size, const char *const *names, int count)
{
    size_t at = 0;
    out[0] = 0;
    for (int k = 0; k < count && at < size; k++) at += (size_t)snprintf(out + at, size - at, "%s%s", k ? ", " : "", names[k]);
}

/* NAME=VALUE: the rule's key with or without "rules.", the value's name or its number (a number
 * rule's list index).
 * NULL, or what is wrong (for the player: the names and numbers it takes) */
const char *rules_arg(const char *arg)
{
    static char why[400];
    char list[300];
    const char *eq = strchr(arg, '=');
    if (!eq) return "a rule is NAME=VALUE";
    size_t n = (size_t)(eq - arg);
    for (int i = 0; i < RULE_COUNT; i++) {
        const mdrt_sdl_option *op = &rule_options[i];
        const char *key = op->key, *bare = strchr(key, '.') + 1;
        if (!((strlen(key) == n && !strncmp(arg, key, n)) || (strlen(bare) == n && !strncmp(arg, bare, n)))) continue;
        for (int v = 0; v < op->count; v++)
            if (!strcmp(eq + 1, op->names[v])) { next[i] = (uint8_t)value_of(i, v); return NULL; }
        char *end;
        long v = strtol(eq + 1, &end, 10);
        if (end != eq + 1 && !*end && valid(i, v) >= 0) { next[i] = (uint8_t)v; return NULL; }
        joined(list, sizeof list, op->names, op->count);
        if (op->numbers) snprintf(why, sizeof why, "%s takes %s, or %d to %d%s", bare, list, op->min, op->max, op->unit);
        else snprintf(why, sizeof why, "%s takes %s (or 0 to %d)", bare, list, op->count - 1);
        return why;
    }
    const char *bares[RULE_COUNT];
    for (int i = 0; i < RULE_COUNT; i++) bares[i] = strchr(rule_options[i].key, '.') + 1;
    joined(list, sizeof list, bares, RULE_COUNT);
    snprintf(why, sizeof why, "no such rule (the rules: %s)", list);
    return why;
}

/* the state's part, the game's session: a version, the rules' count, each rule's value (a state
 * from a build with fewer rules leaves the rest Classic; one with more is not this build's), then
 * the roster set: its checksum (8 bytes, 0 the cartridge's) and folder (a length byte, the name;
 * none in a netplay session, whose states stay in memory and whose hash the peers compare), then
 * (version 6) the referee's REFEREE_STATE_SIZE bytes (RULE_REFEREE: mods/referee.c), then (version 7) the
 * pause's HOLD_STATE_SIZE (RULE_HOLD, the anthems: mods/hold.c), then (version 8) the goal siren's
 * HOLD_SIREN_SIZE (RULE_SIREN: mods/hold.c), then (version 9) NHL 95's moves' MOVES_STATE_SIZE (mods/moves.c), then (version 13) the referee's walk's HOLD_WALK_SIZE (mods/hold.c, with the announcer's from version 16), then (version 15) the goalie's GOALIE_STATE_SIZE (mods/goalie.c). The
 * roster set's tables are in the ROM the state's RAM points into, so a state loads with its own
 * roster set or not at all. Version 16 is this layout; version 15 is it with the walk's part before the announcer's (HOLD_WALK_SIZE_13 bytes); version 14 is that without the goalie's part (none then); version 13 is that with RULE_VIEW's 1 the wide view played by (now 2,
 * "wide play": a state from before loads as it was played; 1 is now the wide picture alone); version 12 is it without the referee's walk (none then); version 11 is it with the referee's part before his penalty call (REFEREE_STATE_SIZE_11 bytes); version 10 is it with the referee's part before his fight watch (REFEREE_STATE_SIZE_10 bytes); version 9 is it with the referee's part before his skating (REFEREE_STATE_SIZE_9 bytes); version 8 is it without the moves (none then); version 7 is it without the siren (none then); version 6 without the pause (none then),
 * version 5 without the referee too (he starts off the ice). Version 4 (before the game's own extras
 * were taken out) held 24 rules and 64 bytes of plus/minus stats; it loads when those extras are at
 * their original values (OLD4: each old rule's place now, or -1 and the original value it needs). */
#define OLD4_COUNT 24
static const struct { int8_t now; uint8_t classic; } old4[OLD4_COUNT] = {
    [0] = { -1, 0 }, [1] = { -1, 10 }, [2] = { RULE_WEIGHT_FIX, 0 }, [3] = { -1, 0 }, [4] = { -1, 0 },
    [5] = { -1, 100 }, [6] = { -1, 100 }, [7] = { -1, 100 }, [8] = { -1, 100 }, [9] = { -1, 100 },
    [10] = { -1, 100 }, [11] = { -1, 0 }, [12] = { -1, 100 }, [13] = { -1, 100 }, [14] = { RULE_VIEW, 0 },
    [15] = { RULE_CD_SKATING, 0 }, [16] = { RULE_CD_CORNERS, 0 }, [17] = { RULE_CD_PSBOX, 0 },
    [18] = { RULE_CD_PASSWORD, 0 }, [19] = { RULE_CD_CLEARSHOT, 0 }, [20] = { RULE_CD_COLOURS, 0 },
    [21] = { RULE_CD_SOUNDS, 0 }, [22] = { RULE_CD_SONGS, 0 }, [23] = { RULE_CD_PERIOD_END, 0 },
};

size_t rules_state_save(void *user, uint8_t *out)
{
    (void)user;
    const char *dir = native_net_on() ? "" : rosters_dir();   /* (netplay: the folder is the player's own, not the game's: the checksum says) */
    size_t n = strlen(dir) > 255 ? 255 : strlen(dir), at = 2 + RULE_COUNT;
    if (out) {
        uint64_t h = rosters_hash();
        out[0] = 16;
        out[1] = RULE_COUNT;
        memcpy(out + 2, rules, RULE_COUNT);
        for (int k = 0; k < 8; k++) out[at + k] = (uint8_t)(h >> (56 - 8 * k));
        out[at + 8] = (uint8_t)n;
        memcpy(out + at + 9, dir, n);
        referee_state_save(out + at + 9 + n);
        hold_state_save(out + at + 9 + n + REFEREE_STATE_SIZE);
        hold_siren_save(out + at + 9 + n + REFEREE_STATE_SIZE + HOLD_STATE_SIZE);
        moves_state_save(out + at + 9 + n + REFEREE_STATE_SIZE + HOLD_STATE_SIZE + HOLD_SIREN_SIZE);
        hold_walk_save(out + at + 9 + n + REFEREE_STATE_SIZE + HOLD_STATE_SIZE + HOLD_SIREN_SIZE + MOVES_STATE_SIZE);
        goalie_state_save(out + at + 9 + n + REFEREE_STATE_SIZE + HOLD_STATE_SIZE + HOLD_SIREN_SIZE + MOVES_STATE_SIZE + HOLD_WALK_SIZE);
    }
    return at + 9 + n + REFEREE_STATE_SIZE + HOLD_STATE_SIZE + HOLD_SIREN_SIZE + MOVES_STATE_SIZE + HOLD_WALK_SIZE + GOALIE_STATE_SIZE;
}

/* the referee's part of a version's state (6 to 9: before his skating; 10: before his fight watch; 11: before his penalty call; 12 on: REFEREE_STATE_SIZE) */
/* the walk's part of a version's state (13 to 15: the referee's walk; 16: and the announcer's last penalty's team) */
static size_t walk_size(int v) { return v >= 16 ? HOLD_WALK_SIZE : v >= 13 ? HOLD_WALK_SIZE_13 : 0; }

static size_t ref_size(int v) { return v >= 12 ? REFEREE_STATE_SIZE : v == 11 ? REFEREE_STATE_SIZE_11 : v == 10 ? REFEREE_STATE_SIZE_10 : v >= 6 ? REFEREE_STATE_SIZE_9 : 0; }

/* where the roster set's part starts in a state's part, or 0 when it is not this build's */
static size_t roster_part(const uint8_t *in, size_t size)
{
    if (size < 2 || in[0] < 4 || in[0] > 16) return 0;
    if (in[0] == 4 ? in[1] > OLD4_COUNT : in[1] > RULE_COUNT) return 0;
    size_t at = 2u + in[1] + (in[0] == 4 ? 64u : 0u);       /* (version 4: the plus/minus stats) */
    size_t tail = ref_size(in[0]) + (in[0] >= 7 ? HOLD_STATE_SIZE : 0) + (in[0] >= 8 ? HOLD_SIREN_SIZE : 0)
                  + (in[0] >= 9 ? MOVES_STATE_SIZE : 0) + walk_size(in[0])
                  + (in[0] >= 15 ? GOALIE_STATE_SIZE : 0);   /* (version 6: the referee; 7: the pause; 8: the siren; 9: NHL 95's moves; 13: the referee's walk; 15: the goalie's poke wait; 16: the announcer's) */
    return size >= at + 9 && size - tail >= at + 9 && size == at + 9 + in[at + 8] + tail ? at : 0;
}

int rules_state_rosters(const uint8_t *in, size_t size, uint64_t *hash)
{
    size_t at = roster_part(in, size);
    if (!at) return -1;
    *hash = 0;
    for (int k = 0; k < 8; k++) *hash = *hash << 8 | in[at + k];
    return 0;
}

int rules_state_load(void *user, const uint8_t *in, size_t size)
{
    (void)user;
    size_t at = roster_part(in, size);
    if (!at) return -1;
    uint64_t h = 0;
    char dir[256] = "";
    for (int k = 0; k < 8; k++) h = h << 8 | in[at + k];
    memcpy(dir, in + at + 9, in[at + 8]);
    dir[in[at + 8]] = 0;
    uint8_t v[RULE_COUNT];
    memcpy(v, rule_classic, sizeof v);
    for (int i = 0; i < in[1]; i++) {
        if (in[0] == 4) {
            if (old4[i].now < 0) {
                if (in[2 + i] != old4[i].classic) { fprintf(stderr, "the state: it was played with a rule this game no longer has\n"); return -1; }
                continue;
            }
        }
        int r = in[0] == 4 ? old4[i].now : i;
        int x = valid(r, in[2 + i]);
        if (x < 0) { fprintf(stderr, "the state: a rule out of range\n"); return -1; }
        if (r == RULE_VIEW && in[0] < 14 && x == 1) x = 2;   /* (before version 14 the wide view was played by: wide play) */
        v[r] = (uint8_t)x;
    }
    const uint8_t *referee = in[0] >= 6 ? in + at + 9 + in[at + 8] : NULL;   /* (before version 6: none, he starts off the ice) */
    const uint8_t *siren = in[0] >= 8 ? referee + ref_size(in[0]) + HOLD_STATE_SIZE : NULL;   /* (before version 8: none) */
    if (siren && siren[0] > 1) { fprintf(stderr, "the state: the goal siren's part is not one of this game's\n"); return -1; }
    const uint8_t *moves = in[0] >= 9 ? siren + HOLD_SIREN_SIZE : NULL;   /* (before version 9: none) */
    if (moves && (moves[0] & 0xF8)) { fprintf(stderr, "the state: the moves' part is not one of this game's\n"); return -1; }
    const uint8_t *walk = in[0] >= 13 ? moves + MOVES_STATE_SIZE : NULL;   /* (before version 13: none, he walks from the puck) */
    const uint8_t *goalie = in[0] >= 15 ? walk + walk_size(in[0]) : NULL;   /* (before version 15: none, no poke waited on) */
    if (hold_walk_check(walk, walk_size(in[0])) < 0) { fprintf(stderr, "the state: the referee's walk's part is not one of this game's\n"); return -1; }
    if (referee && referee_state_load(referee, ref_size(in[0])) < 0) { fprintf(stderr, "the state: the referee's part is not one of this game's\n"); return -1; }
    const uint8_t *pause = in[0] >= 7 ? referee + ref_size(in[0]) : NULL;   /* (before version 7: none, no pause) */
    if (pause && hold_state_load(pause) < 0) { fprintf(stderr, "the state: the pause's part is not one of this game's\n"); return -1; }
    if (rosters_use(dir, h) < 0) { fprintf(stderr, "the state: %s\n", rosters_error()); return -1; }
    if (!referee) referee_state_load(NULL, 0);
    memcpy(rules, v, sizeof rules);
    if (!pause) hold_state_load(NULL);                     /* (a state from before the pause was kept: every machine that loads it starts without) */
    hold_siren_load(siren);                                /* (checked above; before version 8: no siren counted) */
    moves_state_load(moves);                               /* (checked above; before version 9: no move under way) */
    hold_walk_load(walk, walk_size(in[0]));                /* (checked above; before version 13: from the puck) */
    goalie_state_load(goalie);                             /* (before version 15: no poke waited on) */
    return 0;
}
