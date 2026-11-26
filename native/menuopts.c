/* The setup screen's options kept between runs (Abdul: "period length, penalties, line changes aren't being
 * remembered"). The cartridge sets them at every power on (DefaultMenus: defmenuoptions) and never saves them;
 * this host keeps them in a small text file beside the program (out/setup.ini, as the save RAM) and puts them
 * back after DefaultMenus. The game's code only calls setup_options_hook (src/game.h), which is NULL unless the
 * host sets it: the window does (menuopts_use), headless runs only with --setup-options FILE, so the tests,
 * golden hashes and lockstep play the cartridge's defaults. A netplay session never reads or writes the file:
 * its options are the session's (the host's setup screen, played by everyone).
 *
 * Kept: Play Mode, Players, Team 1, Team 2, Period Length, Goalies, User Records, Penalties, Line Changes
 * (OptPlayMode ... OptLine, $FFD048-$FFD058). In a Shootout the cartridge turns User Records, Penalties and Line
 * Changes off and keeps the player's in TmpOptUserRec ... TmpOptLine for when it leaves; those are kept then.
 * Continue Playoffs comes back only when the save RAM holds playoffs played (as the menu itself skips it). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "hal.h"
#include "rom.h"
#include "rosters.h"
#include "symbols_gen.h"
#include "native.h"

static char path[1100];                                   /* the file; empty: not kept (headless without --setup-options) */
static uint16_t last[9];                                  /* what the file holds (written again only when it changes) */
static int have_last;

static const char *const keys[9] = { "play_mode", "players", "team1", "team2", "period_length", "goalies",
                                     "user_records", "penalties", "line_changes" };

void menuopts_use(const char *file) { snprintf(path, sizeof path, "%s", file ? file : ""); have_last = 0; }

static int menuopts_read(uint16_t v[9])
{
    FILE *f = path[0] ? fopen(path, "r") : NULL;
    char line[200];
    int got = 0;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (line[0] == '#' || !eq) continue;
        *eq = 0;
        for (int i = 0; i < 9; i++)
            if (!strcmp(line, keys[i])) {
                char *end;
                long n = strtol(eq + 1, &end, 10);
                if (end != eq + 1 && n >= 0 && n < 0x100) { v[i] = (uint16_t)n; got |= 1 << i; }
            }
    }
    fclose(f);
    return got == 0x1FF;                                  /* (all nine, or the cartridge's defaults) */
}

static void menuopts_write(const uint16_t v[9])
{
    FILE *f = fopen(path, "w");
    if (!f) {
        static int said;
        if (!said++) fprintf(stderr, "setup options: %s cannot be written\n", path);
        return;
    }
    fprintf(f, "# NHL '94: the setup screen's options, put back at the next start (the cartridge's at every power on otherwise)\n");
    for (int i = 0; i < 9; i++) fprintf(f, "%s=%u\n", keys[i], v[i]);
    fclose(f);
}

/* the options as the player chose them: in a Shootout, the three it turns off as they were before it */
static void menuopts_now(uint16_t v[9])
{
    memcpy(v, g.options, sizeof g.options);
    if (g.OptPlayMode == 4) { v[6] = g.TmpOptUserRec; v[7] = g.TmpOptPen; v[8] = g.TmpOptLine; }
}

/* playoffs saved in the save RAM (ContinuePlayoffs' test), the game's RAM left as it was */
static int playoffs_saved(void)
{
    static struct Ram keep;
    memcpy(&keep, &g, sizeof g);
    ReadPassBits(pass_bits(RAM(passbits)));
    int saved = (g.gamelevel | g.bosgames) != 0;
    memcpy(&g, &keep, sizeof g);
    return saved;
}

static void power_on(void)
{
    uint16_t v[9];
    if (!menuopts_read(v)) return;
    const uint8_t *lim = rom_ptr(g.FourWayPlay ? SYM_OptionLimits4Way : SYM_OptionLimits);
    for (int i = 0; i < 9; i++) {                         /* each in its range (the menu keeps them there from then on) */
        uint16_t hi = rd16(lim + 2 * i);
        if (i == 1) hi += 2;                                /* (Players: ChangeOption's widest; the menu's own pass keeps it in its mode's range) */
        if (i == 2 || i == 3) hi = rosters.teams;          /* (this roster set's teams) */
        if (v[i] >= hi) return;
    }
    if (v[0] == 1 && !playoffs_saved()) v[0] = 2;        /* (Continue Playoffs with none saved: new playoffs, as the menu goes on) */
    memcpy(g.options, v, sizeof g.options);
    if (v[0] == 4) {                                      /* Shootout: the player's own for when it is left, its own now */
        g.TmpOptUserRec = v[6]; g.TmpOptPen = v[7]; g.TmpOptLine = v[8];
        g.OptUserRec = 1; g.OptPen = 0; g.OptLine = 1;
    }
    memcpy(last, v, sizeof last);
    have_last = 1;
    fprintf(stderr, "setup options: %s\n", path);
}

static void menu(void)
{
    uint16_t v[9];
    if (native_net_on()) return;                          /* (a session's are the host's: never kept) */
    menuopts_now(v);
    if (have_last && !memcmp(v, last, sizeof last)) return;
    memcpy(last, v, sizeof last);
    have_last = 1;
    menuopts_write(v);
}

static void hook(int when)
{
    if (when == SETUP_OPTS_POWER_ON) power_on();
    else menu();
}

/* at each power on (native_game): the hook for a game that keeps them, none in a session or without a file */
void menuopts_power_on(void)
{
    have_last = 0;
    setup_options_hook = path[0] && !native_net_on() ? hook : NULL;
    if (setup_options_hook) {                             /* (what the file holds: a pass of the menu that changes nothing writes nothing) */
        uint16_t v[9];
        if (menuopts_read(v)) { memcpy(last, v, sizeof last); have_last = 1; }
    }
}
