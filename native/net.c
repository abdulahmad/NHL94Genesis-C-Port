/* Netplay on Overbar's netplay core (overbar/net.h, a room with one match for the whole stay) and its
 * console features (overbar/console.h): two to four machines, a host and its guests, from power on;
 * the players at each (--here, the Online page's Players here) on the pads in turn, the host's first,
 * then each guest's as it came, up to the host's players in all (--players): two machines with one
 * each, the host pad 1 and the guest pad 2; four with one each; two each; two and one and one ...
 * EA's 4 Way Play is in the ports on every machine when pads 3 or 4 are played. A player who leaves
 * ends the game for all. Every pad works in the menus, as on the cartridge.
 *
 * The game's resumable point is the game loop's wait (setvideo's, where states are taken: state.c),
 * and a rollback goes on from there as a loaded state does (restart_at native_resume_game_loop).
 * Step s of the match is the console's frame s + 1: at the wait of frame f the core is asked for
 * step f, this machine's buttons given for step f + delay, and the frame's pad source reads the
 * step's buttons. The identity the machines compare: the build (its commit) and the ROM. What is
 * sent, the host's for all (native_net_settings, made where the session is: the command line): the
 * next game's rules and its roster set (a package: a folder's files packed). Every session plays
 * from a new cartridge (save RAM blank), and no player's save RAM is written while it lasts: seasons
 * and records go online in a state, the host's loaded for everyone (headless --net-load FRAME:FILE),
 * which must be of the session's roster set (native_state_check).
 *
 * The host's reset and its state for everyone ride on the host's buttons (OB_CONSOLE_RESET, the
 * script's RESET; OB_CONSOLE_LOAD): every machine powers its console on again, or loads the state,
 * at the wait after the step that carries them (once a pass: a rollback that plays the step again
 * does it again). Headless, a run that reaches --frames plays on until every machine has every
 * button up to it: the host then ends the match and says goodbye a moment later; a guest stops when
 * the match ends.
 *
 * The window (frontend.c, its Online page): the session opens on the main thread, which polls it in
 * the room (native_net_window_poll) until the match begins; the window then powers the console on
 * again for it, and from there only the game thread touches the session (the main thread reads a line
 * it leaves, and asks it to leave). A session that ends leaves the console playing on alone, on the
 * window's pads. */
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>
#include "game.h"
#include "native.h"
#include "rules.h"
#include "build_id.h"
#include "overbar/net.h"
#include "overbar/console.h"

#define CHECK_EVERY 60
#define ROLLBACK 30                     /* frames one rollback plays again, at most */
#define NONE 0xFFFFFFFFu

static ob_net *net;
static ob_console con;
static mdrt *console;
static int headless, started, players_total;
static const void *room_set;            /* the settings given at open (the host's: the match's too) */
static size_t room_set_size;
static native_input_fn local_fn;
static long stop_at = -1;
static uint16_t latched[MDRT_PADS];     /* the buttons of step latched_step, as the game is given them */
static uint32_t latched_step = NONE;
static int event;                       /* that step's console event (OB_CONSOLE_DO_RESET, _DO_LOAD) */
static const void *event_state;
static size_t event_size;
static long load_done = -1, reset_done = -1;   /* the frame whose event was carried out this pass */
static long resets, loads;
static long said_at = -1;
/* the window's session */
static int window, window_go;           /* (main thread) a window session; its match has begun */
static atomic_int window_leave, window_over;   /* asked to leave; the game thread is done with the session */
static atomic_flag line_lock = ATOMIC_FLAG_INIT;
static char window_line[128];           /* what the session does now, or why it ended (the game thread's) */
static int window_live;                 /* (with the line) the session goes on */

static void nap(void)
{
    struct timespec t = { 0, 1000000 };
    nanosleep(&t, NULL);
}

const char *native_net_identity(const char *setup)
{
    static char identity[256];
    uint64_t h = 0xCBF29CE484222325ull;                   /* (the command line's part, hashed: the string stays short) */
    for (const char *s = setup ? setup : ""; *s; s++) h = (h ^ (uint8_t)*s) * 0x100000001B3ull;
    snprintf(identity, sizeof identity, "ninety-four %s, NHL 94 (UE) %08x, setup %016llx, %s", NHL94_BUILD, NATIVE_ROM_CRC32,
             (unsigned long long)h, clock_original() ? "original" : "smooth");
    return identity;
}

/* ---- the session's settings: the host's, the next game's for all ----------------------------------
 * "N94S", a version byte (2), then little-endian: the rules (a count byte, each rule's value), the
 * roster set (a u32 size, 0 for the cartridge's, a package's bytes) */
static uint8_t *settings;
static uint8_t session_rules[RULE_COUNT];               /* the settings the game plays by (the window's pages show them) */
static int session_rosters, session_host;
static atomic_int session_known;

const void *native_net_settings(size_t *size)
{
    uint8_t v[RULE_COUNT], *pkg;
    long n = rosters_next_package(&pkg);
    if (n < 0) { fprintf(stderr, "netplay: the roster set: %s: the session plays the cartridge's teams\n", rosters_error()); n = 0; }
    rules_next_get(v);
    size_t total = 5 + 1 + RULE_COUNT + 4 + (size_t)n;
    free(settings);
    if (!(settings = malloc(total))) { free(pkg); *size = 0; return NULL; }
    uint8_t *o = settings;
    memcpy(o, "N94S", 4); o[4] = 2; o += 5;
    *o++ = RULE_COUNT; memcpy(o, v, RULE_COUNT); o += RULE_COUNT;
    for (int k = 0; k < 4; k++) *o++ = (uint8_t)((size_t)n >> 8 * k);
    if (n) memcpy(o, pkg, (size_t)n);
    free(pkg);
    *size = total;
    return settings;
}

/* the settings b (size bytes) the next game's: NULL, or what is wrong */
static const char *settings_use(mdrt *m, const uint8_t *b, size_t size)
{
    if (!b || size < 6 || memcmp(b, "N94S", 4) || b[4] != 2) return "none from this build";
    size_t at = 5, k = b[at++];
    if (size < at + k + 4) return "cut short";
    const uint8_t *v = b + at;
    at += k;
    size_t n = (size_t)b[at] | (size_t)b[at + 1] << 8 | (size_t)b[at + 2] << 16 | (size_t)b[at + 3] << 24;
    at += 4;
    if (size != at + n) return "its roster set cut short";
    if (rules_next_set(v, (int)k) < 0) return "a rule out of range";
    if (rosters_next_bytes(b + at, n) < 0) return rosters_error();
    mdrt_sram *r = mdrt_get_sram(m);                      /* (a new cartridge's on every machine) */
    memset(r->bytes, 0xFF, r->size);
    rules_next_get(session_rules);
    session_rosters = n != 0;
    session_host = ob_net_is_host(net);
    atomic_store(&session_known, 1);
    return NULL;
}

int native_net_session(uint8_t *v, int *sent, int *host)
{
    if (!atomic_load(&session_known)) return 0;
    memcpy(v, session_rules, RULE_COUNT);
    *sent = session_rosters;
    *host = session_host;
    return 1;
}

/* the session the game plays in now, while it lasts */
static ob_net *playing(void)
{
    return net && console && started && ob_net_state(net) == OB_NET_GO ? net : NULL;
}

void native_net_power_on(mdrt *m)
{
    atomic_store(&session_known, 0);
    if (!playing()) return;
    size_t size = 0;
    const void *b = ob_net_settings(net, &size);
    const char *why = settings_use(m, b, b ? size : 0);
    if (!why) return;
    fprintf(stderr, "netplay: the host's settings: %s: the session ends\n", why);   /* (the game alone, as it would have been) */
    ob_net_quit(net);
}

/* ---- states: the console's (its parts: the game's own, added at power on) ---- */
static size_t st_save(void *user, void *out, size_t cap)
{
    (void)user;
    size_t need = mdrt_get_state(console, NULL, 0);
    if (need > cap) return need;
    return mdrt_get_state(console, out, cap);
}
static int st_load(void *user, const void *in, size_t size)
{
    (void)user;
    return mdrt_set_state(console, in, size) ? -1 : 0;
}
/* a state as another machine of this build loads it: the same bytes (the console's state holds no address) */
static size_t st_share(void *user, const void *state, size_t size, void *out, size_t cap)
{
    (void)user;
    if (size <= cap) memcpy(out, state, size);
    return size;
}
static int st_take(void *user, const void *in, size_t size)
{
    latched_step = NONE;                                  /* (the match goes on from it: a desync's, the host's state) */
    load_done = reset_done = -1;
    return st_load(user, in, size);
}

int native_net_open(const mdrt_net_options *o, const char *setup, const void *set, size_t set_size, native_input_fn input,
                    char *err, size_t errsize)
{
    static const ob_net_states states = { NULL, st_save, st_load, st_share, st_take };
    ob_online on;
    memset(&on, 0, sizeof on);
    snprintf(on.name, sizeof on.name, "%s", o->name);
    on.role = o->role;                                    /* (the same numbers: host, join, both) */
    snprintf(on.addr, sizeof on.addr, "%.95s", o->peer);
    on.port = o->port ? o->port : MDRT_UDP_PORT;
    on.delay = o->delay;
    on.here = o->here > 0 ? o->here : 1;
    on.all = o->players > 0 ? o->players : 2;
    players_total = on.all;
    room_set = set;
    room_set_size = set ? set_size : 0;
    ob_net_game gm = { .identity = native_net_identity(setup), .pads = MDRT_PADS, .settings = set, .settings_size = set ? set_size : 0,
                      .states = &states, .rollback = ROLLBACK, .room = 1, .host_bits = OB_CONSOLE_BITS };
    if (gm.settings_size > OB_NET_MAX_SETTINGS) { snprintf(err, errsize, "the game's settings are too big to send (its roster set)"); return 0; }
    net = ob_net_new(&on, &gm, err, (int)errsize);
    if (!net) return 0;
    if (o->sim[0] || o->sim[1] || o->sim[2] || o->sim[3]) ob_net_simulate(net, o->sim[0], o->sim[1], o->sim[2], o->sim[3]);
    ob_console_init(&con);
    local_fn = input;
    return 1;
}

int native_net_on(void) { return playing() != NULL || (headless && !started); }

/* pads, for a person: "pad 2", "pads 1 and 2", "pads 2, 3 and 4" */
static void pads_text(unsigned pads, char *buf, size_t size)
{
    int n = 0, k = 0;
    for (int p = 0; p < MDRT_PADS; p++) n += pads >> p & 1;
    size_t at = (size_t)snprintf(buf, size, n == 1 ? "pad" : "pads");
    for (int p = 0; p < MDRT_PADS && at < size; p++)
        if (pads >> p & 1) at += (size_t)snprintf(buf + at, size - at, "%s%d", !k++ ? " " : k == n ? " and " : ", ", p + 1);
}

static void messages(void)
{
    static uint8_t small[256];
    int from, type, got;
    size_t size;
    while (net && (got = ob_net_recv(net, &from, &type, small, sizeof small, &size)) != 0) {
        uint8_t *big = NULL;
        const void *d = small;
        if (got < 0) {
            if (!(big = malloc(size ? size : 1)) || ob_net_recv(net, &from, &type, big, size, &size) != 1) { free(big); return; }
            d = big;
        }
        if (type == OB_NET_TYPE_CONSOLE) ob_console_message(&con, net, from, d, size);
        free(big);
    }
}

/* (the host) the match, once the players it waits for are in the room: the pads in turn, the host's
 * first, then each guest's by the order they came */
static int begin_match(char *err, size_t errsize)
{
    unsigned members = ob_net_members(net);
    int in = 0, pad = 0;
    for (int k = 0; k < OB_NET_MAX_MACHINES; k++) if (members >> k & 1) in += ob_net_here_of(net, k);
    if (in < players_total) return 0;
    ob_net_match m;
    memset(&m, 0, sizeof m);
    for (int p = 0; p < OB_NET_MAX_PADS; p++) m.owner[p] = -1;
    for (int k = 0; k < OB_NET_MAX_MACHINES; k++)
        for (int j = 0; (members >> k & 1) && j < ob_net_here_of(net, k) && pad < players_total && pad < MDRT_PADS; j++) m.owner[pad++] = (int8_t)k;
    m.delay = -1;
    m.settings = room_set;
    m.settings_size = room_set_size;
    if (ob_net_match_begin(net, &m, err, (int)errsize)) return -1;
    return 1;
}

/* headless: connect, then the game powers on (native_net_start) */
int native_net_connect(void)
{
    char err[200];
    fprintf(stderr, "netplay: connecting\n");
    for (;;) {
        int s = ob_net_poll(net);
        if (s == OB_NET_GO) break;
        if (s == OB_NET_ENDED) { fprintf(stderr, "netplay: %s\n", ob_net_why(net)); return 0; }
        if (s == OB_NET_LOBBY && ob_net_is_host(net) && begin_match(err, sizeof err) < 0) { fprintf(stderr, "netplay: %s\n", err); return 0; }
        nap();
    }
    headless = 1;
    char mine[32];
    pads_text(ob_net_my_pads(net), mine, sizeof mine);
    fprintf(stderr, "netplay: playing with %s, %s, %s, input delay %d\n", ob_net_is_host(net) ? "the guests" : ob_net_name_of(net, 0)[0] ? ob_net_name_of(net, 0) : "the host",
            ob_net_is_host(net) ? "hosting" : "joined", mine, ob_net_delay(net));
    unsigned members = ob_net_members(net);
    for (int k = 0; k < OB_NET_MAX_MACHINES; k++) {       /* (who plays which) */
        if (!(members >> k & 1)) continue;
        char pads[32];
        const char *name = ob_net_name_of(net, k);
        pads_text(ob_net_pads_of(net, k), pads, sizeof pads);
        fprintf(stderr, "netplay: %s%s, %s\n", name && name[0] ? name : k ? "a guest" : "the host", k == ob_net_me(net) ? " (here)" : "", pads);
    }
    return 1;
}

int native_net_four_way(void) { return net && (ob_net_all_pads(net) & 0xC); }

/* the frame's buttons: the step's (every pad's, as the core gave them) */
static void pad_source(void *user, uint16_t b[MDRT_PADS])
{
    (void)user;
    uint32_t f = (uint32_t)mdrt_frame_count(console);
    for (int p = 0; p < MDRT_PADS; p++) b[p] = latched_step == f ? latched[p] : 0;
}

/* a console powered on (its states' parts added): headless, this run's session from this power on */
void native_net_start(mdrt *m)
{
    console = m;
    if ((!headless && !window_go) || started || !net || atomic_load(&window_over)) return;
    mdrt_set_pad_source(m, pad_source, NULL);
    started = 1;
}

void native_net_stop_at(long frame) { stop_at = frame; }

static long load_frame = -1;            /* --net-load (the host, headless): from this frame, the state of load_path for everyone */
static const char *load_path;
void native_net_load_at(long frame, const char *path) { load_frame = frame; load_path = path; }

/* (headless, the host) --net-load's state for everyone, once its frame has come */
static void send_load(void)
{
    if (load_frame < 0 || !headless || !ob_net_is_host(net) || (long)mdrt_frame_count(console) < load_frame || ob_net_replaying(net)) return;
    load_frame = -1;
    FILE *f = fopen(load_path, "rb");
    uint8_t *b = NULL;
    long size = f && !fseek(f, 0, SEEK_END) ? ftell(f) : -1;
    if (size > 0 && !fseek(f, 0, SEEK_SET) && (b = malloc((size_t)size)) && fread(b, 1, (size_t)size, f) != (size_t)size) { free(b); b = NULL; }
    if (f) fclose(f);
    const char *why = !b ? "cannot read it" : !mdrt_state_fits(console, b, (size_t)size) ? "not a state of this build" :
                      native_state_check(b, (size_t)size, NULL);
    if (!why && ob_console_load(&con, net, b, (size_t)size)) why = "the session would not send it";
    if (why) fprintf(stderr, "netplay: --net-load %s: %s\n", load_path, why);
    else fprintf(stderr, "netplay: sending the state %s to everyone at frame %u\n", load_path, (unsigned)mdrt_frame_count(console));
    free(b);
}

static void report(void)
{
    ob_net_stats s;
    ob_net_stats_get(net, &s);
    fprintf(stderr, "netplay: frame %u, %ld rollbacks (%ld frames played again, at most %d), %ld frames given back, %ld desync checks, round trip %d ms, %ld resets, %ld loads\n",
            (unsigned)mdrt_frame_count(console), s.rollbacks, s.replayed, s.most_replayed, s.gave_back, s.checks, s.rtt_ms, resets, loads);
}

static void line_set(const char *line, int live)
{
    while (atomic_flag_test_and_set(&line_lock)) {}
    snprintf(window_line, sizeof window_line, "%.127s", line);
    window_live = live;
    atomic_flag_clear(&line_lock);
}

static void over(void)
{
    const char *why = ob_net_state(net) == OB_NET_ENDED ? ob_net_why(net) : ob_net_match_why(net);
    if (headless) fprintf(stderr, "netplay: %s\n", why && why[0] ? why : "the match ended");
    report();
    if (window) {                                         /* (the console plays on alone, on the window's pads) */
        line_set(why && why[0] ? why : "the match ended", 0);
        mdrt_set_pad_source(console, NULL, NULL);
        mdrt_set_quiet(console, 0);
        latched_step = NONE;
        ob_net_quit(net);
        atomic_store(&window_over, 1);
    }
}

/* (headless) the run's end: report, goodbye, done */
static void done(int whole)
{
    if (whole) report();
    else over();
    ob_net_quit(net);
    native_quit();
}

/* (headless, the host) every button up to the last frame known here: the match ends for all (the guests
 * play to where it ends), a moment more for them, goodbye */
static void finish(void)
{
    ob_net_match_end(net);
    struct timespec t0, t;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        if (ob_net_poll(net) == OB_NET_ENDED) break;
        messages();
        if (!(ob_net_members(net) & ~1u)) break;          /* (every guest has gone) */
        clock_gettime(CLOCK_MONOTONIC, &t);
        if ((t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000 > 3000) break;
        nap();
    }
    done(1);
}

/* the event of the step just played (the frame before this wait), once a pass */
static void carry_out(uint32_t f)
{
    if (latched_step + 1 != f || !event) return;
    if (event == OB_CONSOLE_DO_LOAD && load_done != (long)f) {
        load_done = f;
        if (mdrt_set_state(console, event_state, event_size)) {
            fprintf(stderr, "netplay: could not load the host's state at frame %u\n", (unsigned)f);
            ob_net_quit(net);
            if (headless) native_quit();
            return;
        }
        mdrt_set_frame_count(console, f);                 /* (the session's frames go on) */
        loads++;
        if (headless && said_at != (long)f) { said_at = f; fprintf(stderr, "netplay: a state loaded for everyone at frame %u\n", (unsigned)f); }
        latched_step = NONE;
        restart_at(native_resume_game_loop);
    }
    if (event == OB_CONSOLE_DO_RESET && reset_done != (long)f) {
        reset_done = f;
        latched_step = NONE;
        mdrt_power_on(console);
        resets++;
        restart_at(native_power_on_again);                /* (the game from its start, on a console powered on again) */
    }
}

/* at every wait, before the handshake (a rollback: the game loop from its wait, as a state loaded) */
void native_net_wait(void)
{
    if (atomic_load(&window_over)) return;                /* (the window's session is over: the console plays on alone) */
    ob_net *n = playing();
    if (!n) return;                                       /* (none, or over: the console plays on alone) */
    uint32_t f = (uint32_t)mdrt_frame_count(console);
    if (window && atomic_exchange(&window_leave, 0)) {    /* (the Online page's Leave) */
        line_set("you left", 0);
        mdrt_set_pad_source(console, NULL, NULL);
        mdrt_set_quiet(console, 0);
        latched_step = NONE;
        ob_net_quit(n);
        atomic_store(&window_over, 1);
        return;
    }
    if (window && f % 30 == 0 && !ob_net_replaying(n)) {
        char line[128];
        ob_net_status(n, line, sizeof line);
        line_set(line, 1);
    }
    carry_out(f);
    if (f && f % CHECK_EVERY == 0) ob_net_check(n, f - 1, mdrt_sim_hash(console));
    messages();
    send_load();
    uint16_t local[MDRT_PADS] = { 0 }, all[OB_NET_MAX_PADS];
    unsigned mine = ob_net_my_pads(n);
    int k = 0;
    for (int p = 0; p < MDRT_PADS; p++)
        if (mine >> p & 1) {
            local[k] = local_fn ? local_fn(p, k) : 0;
            if (local[k] & OB_CONSOLE_RESET) ob_console_reset(&con);   /* (the script's RESET: the host's alone) */
            k++;
        }
    ob_console_local(&con, n, local, k);
    int resumable = native_at_game_loop_wait(), r;
    while ((r = ob_net_frame(n, resumable, local, all)) == OB_NET_WAIT) {
        if (mdrt_idle(console)) { ob_net_quit(n); native_quit(); }   /* (the window closing: goodbye) */
        messages();
        if (headless) nap();
    }
    if (r == OB_NET_RESTART) {                            /* (a rollback: the steps since played again, their events too) */
        latched_step = NONE;
        load_done = reset_done = -1;
        restart_at(native_resume_game_loop);
    }
    if (r == OB_NET_RUN) {
        event = ob_console_step(&con, n, all, MDRT_PADS, 0, &event_state, &event_size);
        memcpy(latched, all, sizeof latched);
        latched_step = f;
        mdrt_set_quiet(console, ob_net_replaying(n) ? 1 : 0);   /* (a frame played again: not shown, not heard) */
        if (headless && stop_at >= 0 && (long)f >= stop_at && !ob_net_replaying(n) && ob_net_is_host(n) && ob_net_confirmed(n) > (uint32_t)stop_at)
            finish();
        return;
    }
    /* the match over (the host ended it), or the session */
    latched_step = NONE;
    if (headless) done(r == OB_NET_OVER && stop_at >= 0 && (long)f >= stop_at);
    else over();
}

/* ---- the window's sessions (frontend.c) ---- */

int native_net_window_start(const mdrt_net_options *o, native_input_fn input, char *msg, size_t size)
{
    size_t n;
    const void *set;
    if (net && !atomic_load(&window_over) && window) { snprintf(msg, size, "Already in a session: Leave first"); return -1; }
    if (net) { ob_net_free(net); ob_console_free(&con); net = NULL; }
    set = native_net_settings(&n);
    started = window_go = 0;
    latched_step = NONE;
    load_done = reset_done = -1;
    atomic_store(&window_over, 0);
    atomic_store(&window_leave, 0);
    window = 1;
    line_set("", 0);
    if (!native_net_open(o, "", set, n, input, msg, size)) { window = 0; return -1; }
    return 0;
}

int native_net_window_poll(void)
{
    char err[200];
    int s;
    if (!window || !net || window_go) return 0;
    s = ob_net_poll(net);
    messages();
    if (s == OB_NET_ENDED) {
        line_set(ob_net_why(net), 0);
        ob_net_free(net);
        ob_console_free(&con);
        net = NULL;
        window = 0;
        return 0;
    }
    if (s == OB_NET_LOBBY && ob_net_is_host(net) && begin_match(err, sizeof err) < 0) {
        line_set(err, 0);
        ob_net_quit(net);
        ob_net_free(net);
        ob_console_free(&con);
        net = NULL;
        window = 0;
        return 0;
    }
    if (s != OB_NET_GO) return 0;
    window_go = 1;                                        /* (from here the game thread's, from the console's power on) */
    line_set("playing", 1);
    return 1;
}

int native_net_window_session(void) { return window && window_go && !atomic_load(&window_over); }

void native_net_window_leave(void)
{
    if (!window || !net) return;
    if (window_go) { atomic_store(&window_leave, 1); return; }
    ob_net_quit(net);                                     /* (still in the room: the main thread's) */
    ob_net_free(net);
    ob_console_free(&con);
    net = NULL;
    window = 0;
    line_set("you left", 0);
}

int native_net_window_status(char *line, size_t size)
{
    int live;
    if (window && net && !window_go) { ob_net_status(net, line, (int)size); return 1; }
    while (atomic_flag_test_and_set(&line_lock)) {}
    snprintf(line, size, "%s", window_line);
    live = window_live && window && !atomic_load(&window_over);
    atomic_flag_clear(&line_lock);
    return live;
}

void native_net_close(void)
{
    if (net && console && ob_net_state(net) == OB_NET_GO) { report(); fprintf(stderr, "netplay: goodbye\n"); }
    if (net) { ob_net_quit(net); ob_net_free(net); }
    ob_console_free(&con);
    net = NULL;
}
