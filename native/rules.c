/* Game rules on the native side (src/rules.h): the catalogue the menu's Game rules page and the
 * command line work from, the rules of the next game, and the state part that keeps a game's
 * session (its rules, and its roster set: rosters.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "native.h"
#include "rules.h"

static const char *const off_on[2] = { "off", "on" };
static const char *const view_names[2] = { "classic", "wide" };
static const char *const view_labels[2] = { "256 wide", "320 wide" };

/* the rules */
const mdrt_sdl_option rule_options[RULE_COUNT] = {
    [RULE_WEIGHT_FIX] = { "rules.weight_fix", "Weight bug fix", 2, off_on, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_VIEW] = { "rules.view", "Rink view", 2, view_names, view_labels, 0, 0, 0, 0, NULL, NULL, NULL },
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
    [RULE_FIGHT] = { "rules.fighting", "Fighting", 2, off_on, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
    [RULE_HOLD] = { "rules.hold_play", "Pause for the announcer", 2, off_on, NULL, 0, 0, 0, 0, NULL, NULL, NULL },
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
    if (n > 2) n = 2;                                      /* (the cartridge's and the Sega CD's: no roster sets of the player's own) */
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
}

/* the Widescreen setting at 16:9: the rink is the wide view (320), so the game fills the screen. Not a
 * rule of its own: a session plays by the host's (native_net_settings sends it), a check by Classic */
static volatile int widescreen;
void rules_widescreen(int on) { widescreen = on; }
int rules_widescreen_on(void) { return widescreen; }

void rules_power_on(void)
{
    memcpy(rules, next, sizeof rules);
    if (widescreen && !native_net_on()) rules[RULE_VIEW] = 1;   /* (in a session the host's settings say it) */
}

void rules_next_get(uint8_t *v) { memcpy(v, next, sizeof next); }

void rules_next_send(uint8_t *v)
{
    rules_next_get(v);
    if (widescreen) v[RULE_VIEW] = 1;
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
 * none in a netplay session, whose states stay in memory and whose hash the peers compare). The
 * roster set's tables are in the ROM the state's RAM points into, so a state loads with its own
 * roster set or not at all. Version 5 is this layout. Version 4 (before the game's own extras
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
        out[0] = 5;
        out[1] = RULE_COUNT;
        memcpy(out + 2, rules, RULE_COUNT);
        for (int k = 0; k < 8; k++) out[at + k] = (uint8_t)(h >> (56 - 8 * k));
        out[at + 8] = (uint8_t)n;
        memcpy(out + at + 9, dir, n);
    }
    return at + 9 + n;
}

/* where the roster set's part starts in a state's part, or 0 when it is not this build's */
static size_t roster_part(const uint8_t *in, size_t size)
{
    if (size < 2 || in[0] < 4 || in[0] > 5) return 0;
    if (in[0] == 4 ? in[1] > OLD4_COUNT : in[1] > RULE_COUNT) return 0;
    size_t at = 2u + in[1] + (in[0] == 4 ? 64u : 0u);       /* (version 4: the plus/minus stats) */
    return size >= at + 9 && size == at + 9 + in[at + 8] ? at : 0;
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
        int x = valid(in[0] == 4 ? old4[i].now : i, in[2 + i]);
        if (x < 0) { fprintf(stderr, "the state: a rule out of range\n"); return -1; }
        v[in[0] == 4 ? old4[i].now : i] = (uint8_t)x;
    }
    if (rosters_use(dir, h) < 0) { fprintf(stderr, "the state: %s\n", rosters_error()); return -1; }
    memcpy(rules, v, sizeof rules);
    hold_reset();                                          /* (a state keeps no pause: every machine that loads it starts without) */
    return 0;
}
