/* NHL 95 PC's rink announcer's lines: which clips of XBRUCE2.VIV each call says (PaGoal, PaPenalty's QueuePenaltyClips,
 * PaPenaltyShot, QueueTimeClips, PaOneMinute, the stars), and how long it lasts. The host's voice (native/announcer.c) says
 * them; the stoppage's wait (mods/hold.c, RULE_HOLD / RULE_ANNOUNCE) takes their length, so the drop waits for the speech as
 * 95 PC's assrefdrop (4F5BF) waits for PaSpeechBusy. The length is the game's own, from the clips' lengths below (each rink
 * clip's size in the bank, as OpenSpeechBank reads it: size x 100 / 5512 ticks of 1/100 s), not from the voice playing: every
 * machine of a session waits the same, with the bank or without it, and a state or a rollback needs nothing of the host's.
 * SpeechTimerTick starts each next clip 26 ticks before the one before it ends (OpenSpeechBank's delay: its length less 1Ah),
 * so a line lasts the sum of its clips less 26 a joint. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "rules.h"
#include "rosters.h"

/* the rink clips' lengths in ticks (1/100 s), by name in strcmp order: NHL 95 PC's XBRUCE2.VIV (its .tea, .num, .cor, .pen and
 * .frm clips; the rest are Ron Barr's and the team names' speech clips, which no stoppage waits for) */
static const struct { char name[14]; uint8_t ticks; } clip_ticks[] = {
    { "0.NUM", 83 }, { "01.NUM", 102 }, { "02.NUM", 102 }, { "03.NUM", 102 }, { "04.NUM", 111 }, { "05.NUM", 120 },
    { "06.NUM", 120 }, { "07.NUM", 111 }, { "08.NUM", 102 }, { "09.NUM", 111 }, { "1.NUM", 74 }, { "10.NUM", 74 },
    { "11.NUM", 74 }, { "12.NUM", 74 }, { "13.NUM", 102 }, { "14.NUM", 102 }, { "15.NUM", 102 }, { "16.NUM", 92 },
    { "17.NUM", 102 }, { "18.NUM", 92 }, { "19.NUM", 102 }, { "1MINUTE.COR", 102 }, { "1SECOND.COR", 111 },
    { "1STSTAR.COR", 195 }, { "2.NUM", 74 }, { "20.NUM", 74 }, { "21.NUM", 102 }, { "22.NUM", 102 }, { "23.NUM", 111 },
    { "24.NUM", 111 }, { "25.NUM", 111 }, { "26.NUM", 111 }, { "27.NUM", 111 }, { "28.NUM", 102 }, { "29.NUM", 111 },
    { "2MIN.COR", 92 }, { "2NDSTAR.COR", 204 }, { "3.NUM", 74 }, { "30.NUM", 83 }, { "31.NUM", 102 }, { "32.NUM", 111 },
    { "33.NUM", 120 }, { "34.NUM", 120 }, { "35.NUM", 139 }, { "36.NUM", 120 }, { "37.NUM", 120 }, { "38.NUM", 102 },
    { "39.NUM", 120 }, { "3RDSTAR.COR", 204 }, { "4.NUM", 74 }, { "40.NUM", 83 }, { "41.NUM", 111 }, { "42.NUM", 102 },
    { "43.NUM", 111 }, { "44.NUM", 111 }, { "45.NUM", 130 }, { "46.NUM", 120 }, { "47.NUM", 120 }, { "48.NUM", 111 },
    { "49.NUM", 120 }, { "5.NUM", 83 }, { "50.NUM", 83 }, { "51.NUM", 111 }, { "52.NUM", 111 }, { "53.NUM", 120 },
    { "54.NUM", 120 }, { "55.NUM", 130 }, { "56.NUM", 120 }, { "57.NUM", 120 }, { "58.NUM", 102 }, { "59.NUM", 120 },
    { "5MIN.COR", 111 }, { "6.NUM", 83 }, { "60.NUM", 83 }, { "61.NUM", 111 }, { "62.NUM", 111 }, { "63.NUM", 120 },
    { "64.NUM", 120 }, { "65.NUM", 130 }, { "66.NUM", 120 }, { "67.NUM", 120 }, { "68.NUM", 111 }, { "69.NUM", 111 },
    { "7.NUM", 74 }, { "70.NUM", 92 }, { "71.NUM", 120 }, { "72.NUM", 120 }, { "73.NUM", 130 }, { "74.NUM", 120 },
    { "75.NUM", 139 }, { "76.NUM", 130 }, { "77.NUM", 130 }, { "78.NUM", 111 }, { "79.NUM", 130 }, { "8.NUM", 74 },
    { "80.NUM", 83 }, { "81.NUM", 102 }, { "82.NUM", 102 }, { "83.NUM", 111 }, { "84.NUM", 111 }, { "85.NUM", 120 },
    { "86.NUM", 120 }, { "87.NUM", 111 }, { "88.NUM", 111 }, { "89.NUM", 111 }, { "9.NUM", 83 }, { "90.NUM", 83 },
    { "91.NUM", 111 }, { "92.NUM", 102 }, { "93.NUM", 111 }, { "94.NUM", 111 }, { "95.NUM", 120 }, { "96.NUM", 111 },
    { "97.NUM", 120 }, { "98.NUM", 102 }, { "99.NUM", 111 }, { "ABUSE.PEN", 185 }, { "ANA.FRM", 139 }, { "ANA.TEA", 83 },
    { "ANDNUM.COR", 83 }, { "ASE.FRM", 222 }, { "ASE.TEA", 130 }, { "ASSTNUM.COR", 139 }, { "ASW.FRM", 222 }, { "ASW.TEA", 157 },
    { "AT.COR", 46 }, { "BOARDING.PEN", 120 }, { "BOS.FRM", 120 }, { "BOS.TEA", 83 }, { "BUF.FRM", 130 }, { "BUF.TEA", 74 },
    { "CGY.FRM", 130 }, { "CGY.TEA", 83 }, { "CHARGING.PEN", 111 }, { "CHECKBND.PEN", 185 }, { "CHI.FRM", 130 },
    { "CHI.TEA", 92 }, { "DAL.FRM", 130 }, { "DAL.TEA", 74 }, { "DET.FRM", 120 }, { "DET.TEA", 74 }, { "EDM.FRM", 130 },
    { "EDM.TEA", 83 }, { "ELBOWING.PEN", 139 }, { "FLA.FRM", 120 }, { "FLA.TEA", 83 }, { "GAMEMISC.COR", 148 },
    { "GOALNUM.COR", 148 }, { "HFD.FRM", 130 }, { "HFD.TEA", 83 }, { "HISTICK.PEN", 148 }, { "HOLDING.PEN", 120 },
    { "HOOKING.PEN", 102 }, { "INTERFER.PEN", 148 }, { "LA.FRM", 148 }, { "LA.TEA", 111 }, { "MINUTES.COR", 74 },
    { "MISCON.COR", 111 }, { "MTL.FRM", 130 }, { "MTL.TEA", 102 }, { "NJ.FRM", 139 }, { "NJ.TEA", 102 }, { "NUMBER.COR", 65 },
    { "NYI.FRM", 167 }, { "NYI.TEA", 83 }, { "NYR.FRM", 148 }, { "NYR.TEA", 92 }, { "ONELEFT.COR", 213 }, { "OTT.FRM", 130 },
    { "OTT.TEA", 74 }, { "PAUSE.COR", 35 }, { "PENNUM.COR", 130 }, { "PENSHOT.COR", 148 }, { "PENSNUM.COR", 148 },
    { "PHI.FRM", 148 }, { "PHI.TEA", 111 }, { "PIT.FRM", 139 }, { "PIT.TEA", 83 }, { "QUE.FRM", 130 }, { "QUE.TEA", 74 },
    { "ROUGHING.PEN", 111 }, { "SECONDS.COR", 92 }, { "SJ.FRM", 148 }, { "SJ.TEA", 111 }, { "SLASHING.PEN", 120 },
    { "SPEARING.PEN", 120 }, { "STL.FRM", 139 }, { "STL.TEA", 92 }, { "TB.FRM", 148 }, { "TB.TEA", 92 }, { "TOR.FRM", 130 },
    { "TOR.TEA", 83 }, { "TRIPPING.PEN", 111 }, { "VAN.FRM", 130 }, { "VAN.TEA", 102 }, { "WPG.FRM", 130 }, { "WPG.TEA", 83 },
    { "WSH.FRM", 139 }, { "WSH.TEA", 102 }, { "XCHECK.PEN", 139 },
};

static int ticks_of(const char *name)
{
    char up[14];
    int i = 0;
    for (; name[i] && i < 13; i++) up[i] = (char)(name[i] >= 'a' && name[i] <= 'z' ? name[i] - 32 : name[i]);
    up[i] = 0;
    int lo = 0, hi = (int)(sizeof clip_ticks / sizeof clip_ticks[0]) - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2, c = strcmp(up, clip_ticks[m].name);
        if (!c) return clip_ticks[m].ticks;
        if (c < 0) hi = m - 1; else lo = m + 1;
    }
    return -1;
}

static void add(struct pa_line *l, const char *fmt, int v)
{
    if (l->n >= PA_CLIPS) return;
    snprintf(l->clip[l->n], sizeof l->clip[0], fmt, v);
    l->n++;
}
static void adds(struct pa_line *l, const char *what, const char *abbr)
{
    if (l->n >= PA_CLIPS) return;
    snprintf(l->clip[l->n], sizeof l->clip[0], "%s.%s", abbr, what);
    l->n++;
}

/* the 95 PC abbreviation of each of the cartridge's teams, in its order (the art's: a roster set's team plays as one of them) */
static const char *const abbr95[28] = {
    "ANA", "BOS", "BUF", "CGY", "CHI", "DAL", "DET", "EDM", "FLA", "HFD", "LA", "MTL", "NJ", "NYI",
    "NYR", "OTT", "PHI", "PIT", "QUE", "SJ", "STL", "TB", "TOR", "VAN", "WSH", "WPG", "ASE", "ASW",
};
const char *pa_abbr(uint16_t art) { return art < 28 ? abbr95[art] : NULL; }

/* the player's number (the roster keeps it as BCD) */
static int jersey(int player, struct Team *tm)
{
    const struct RosterPlayer *p = (const struct RosterPlayer *)(const void *)GetPlayerNamePointer((uint16_t)player, tm);
    return (p->number >> 4) * 10 + (p->number & 15);
}

/* "at <minutes> <seconds>" (QueueTimeClips: 1 minute and 1 second have clips of their own; seconds after minutes are two digits) */
static void say_time(struct pa_line *l, int min, int sec)
{
    add(l, "at.cor", 0);
    add(l, "pause.cor", 0);
    if (min) {
        if (!sec) { if (min == 1) add(l, "1minute.cor", 0); else { add(l, "%d.num", min); add(l, "minutes.cor", 0); } }
        else add(l, "%d.num", min);
    }
    if (sec == 1) { if (!min) add(l, "1second.cor", 0); else add(l, "%02d.num", sec); }
    else if (sec > 1) {
        if (!min) { add(l, "%d.num", sec); add(l, "seconds.cor", 0); }
        else add(l, "%02d.num", sec);
    }
}

static const char *pen_clip(int pen)
{
    switch (pen) {
    case 0x12: case 0x1A: return "roughing";
    case 0x16: return "charging";
    case 0x18: return "slashing";
    case 0x1C: return "xcheck";
    case 0x1E: return "hooking";
    case 0x20: return "tripping";
    case 0x22: return "interfer";
    case 0x24: return "holding";
    }
    return NULL;                                        /* (fighting, the instigator: 95 PC has no clip) */
}

void pa_script(int ev, struct Team *tm, int player, int a, int b, int *last_pen_team, struct pa_line *l)
{
    l->n = 0;
    const char *abbr = tm ? pa_abbr(rosters_art(tm->tmteam)) : NULL;
    int away = tm == team_struct(1);
    switch (ev) {
    case PA_GOAL:                                       /* tm the scorer's team, player him, a and b the assists (-1 none) */
        if (!abbr) break;
        *last_pen_team = -1;
        adds(l, "tea", abbr);
        add(l, "goalnum.cor", 0);
        add(l, "%d.num", jersey(player, tm));
        if (a >= 0) { add(l, "pause.cor", 0); add(l, "asstnum.cor", 0); add(l, "%d.num", jersey(a, tm)); }
        if (b >= 0) { add(l, "pause.cor", 0); add(l, "andnum.cor", 0); add(l, "%d.num", jersey(b, tm)); }
        break;
    case PA_PENALTY: {                                  /* a the penalty, b its minutes | remaining penalties << 8 | the next one is this team's << 16 */
        int mins = b & 0xFF, remaining = b >> 8 & 0xFF, next_same = b >> 16 & 1;
        if (!abbr) break;
        int idx = *last_pen_team == away ? 2 : 1;
        *last_pen_team = away;
        if (idx == 1) {
            adds(l, "tea", abbr);
            add(l, next_same ? "pensnum.cor" : "pennum.cor", 0);
        } else add(l, "andnum.cor", 0);
        add(l, "%d.num", jersey(player, tm));
        add(l, "pause.cor", 0);
        if (mins == 4) { add(l, "4.num", 0); add(l, "minutes.cor", 0); }   /* (a double minor, RULE_PENALTIES: 95 PC has no clip of its own) */
        else add(l, mins == 5 ? "5min.cor" : "2min.cor", 0);
        const char *pc = pen_clip(a);
        if (pc) adds(l, "pen", pc);
        add(l, "pause.cor", 0);
        if (!remaining) { say_time(l, g.gameclock / 60, g.gameclock % 60); *last_pen_team = -1; }
        break;
    }
    case PA_PENSHOT:                                    /* tm the shooter's team, player him */
        if (!abbr) break;
        *last_pen_team = -1;
        adds(l, "tea", abbr);
        add(l, "penshot.cor", 0);
        add(l, "%d.num", jersey(player, tm));
        add(l, "pause.cor", 0);
        say_time(l, g.gameclock / 60, g.gameclock % 60);
        break;
    case PA_ONEMIN:
        add(l, "oneleft.cor", 0);
        break;
    case PA_STAR:                                       /* a the star, 0-2: "first star", "<team> number <n>" */
        if (!abbr) break;
        add(l, a == 0 ? "1ststar.cor" : a == 1 ? "2ndstar.cor" : "3rdstar.cor", 0);
        adds(l, "frm", abbr);
        add(l, "pause.cor", 0);
        add(l, "number.cor", 0);
        add(l, "%d.num", jersey(player, tm));
        break;
    }
}

int pa_line_frames(const struct pa_line *l)
{
    int t = 0, n = 0;
    for (int i = 0; i < l->n; i++) {
        int k = ticks_of(l->clip[i]);
        if (k < 0) continue;                            /* (not in the bank: not said) */
        t += k - (n ? 26 : 0);                          /* (each starts 26 ticks before the last ends) */
        n++;
    }
    return t > 0 ? (t * 60 + 99) / 100 : 0;
}
