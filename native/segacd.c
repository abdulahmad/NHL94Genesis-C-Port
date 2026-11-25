/* The Sega CD disc's sound beside the cartridge game: its CD audio tracks for the songs (the
 * Game page's "Sega CD music") and its crowd bed, track 2 (the "Crowd" row: the cartridge's PSG
 * noise, the disc's track, or both). The disc is the player's own rip, found beside the program
 * by its files (Overbar's ob_cd_find: any .cue / .ccd / .iso layout), never shipped.
 *
 * What plays (the disc's: docs/segacd_audio.md in NHL94Genesis; the cartridge-side mapping is the
 * MSU-MD branch's, docs/msu.md):
 *   songs $30-$77 (the organ, the goal songs)  track id - $29, once; then the crowd bed
 *   $78 title                                  track 4, looped
 *   $79 intermission, end of game              track 5, looped
 *   $7A scouting report                        track 3, looped
 *   the crowd bed                              track 2, looped, at the disc's fader $300 of $400
 * While the disc plays a song, the song's FM notes and PCM drums are not sent to the chips (the
 * sound effects still are). None of this is in the game: the song stream runs as on the
 * cartridge, so states, replays and online play are the same with it on or off; it only changes
 * what reaches the chips and the speakers. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "rom.h"
#include "symbols_gen.h"
#include "rosters.h"
#include "native.h"
#include "segacd.h"
#include "cardcolor.h"
#include "overbar/cdimage.h"

#define SONG_GAIN 0.70f                     /* the disc's tracks against the FM chip's mix */
#define BED_GAIN (SONG_GAIN * 0x300 / 0x400)
#define VOICE_GAIN 0.85f
#define VOICE_RATE 16276                    /* the RF5C164 at frequency $0400 (32552 Hz x $400 / $800) */
#define CROWD_LIVE 20                       /* frames the crowd bed keeps going after the game's last updatesound */

static struct {
    SDL_atomic_t music, crowd;              /* the Game page's rows (segacd_set_music / _crowd) */
    ob_cd *cd;                              /* the disc (NULL: none found, or not looked for yet) */
    SDL_atomic_t found;                     /* 1: the search ended (cd is set, or NULL for none) */
    int track, loop, bed;                   /* playing: the track (0 none), looped, the crowd bed */
    double at;                              /* its frame, at 44.1 kHz */
    long frames;                            /* its length */
    int song_on_cd;                         /* the game's song is the disc's now: its FM notes are dropped */
    uint8_t fx_slot[8];                     /* a track slot's effect is the disc's: its FM notes and samples are dropped */
    int crowd_live;                         /* frames left of the game's crowd (updatesound) */
    uint8_t ym_addr;                        /* the YM2612's part 1 address latch (the relay's last) */
    SDL_atomic_t voice;                     /* the Ron Barr row: 0 off, 1 on (the music held back), 2 on over the music */
    int16_t *clip;                          /* the clip playing (16276 Hz mono), NULL none */
    long clip_len;
    double clip_at;
    int clip_id, queue[8], nqueue;          /* its paragraph, and the ones waiting */
    SDL_atomic_t screens;                   /* the Opening screens row: 1 the disc's */
    SDL_atomic_t title;                     /* the Title page row: 1 the disc's (its credits, then the license page) */
    int title_live;                         /* the disc's title page is up (segacd_title_hook) */
    int hsp_t;                              /* frames of the High Score Productions page shown (its sparkles' clock) */
    SDL_atomic_t intro;                     /* the Intro movie row: 1 HOCKEY.SEG before the title */
    SDL_atomic_t photos, session, career;   /* the Player photos and Career stats rows; a session's console (none then: the machines keep the same RAM) */
    int intro_done;                         /* it played (once a run): 1 just now, 2 before */
    int hold;                               /* frames of black so far until the cartridge's frame is black (0 none) */
    int screen;                             /* the cartridge's opening screen now (CD_EV_SCREEN), 0 none */
    int pic_of;                             /* the picture in pic (SEGACD_PIC_* + 1), 0 none */
    uint32_t pic[320 * 224];
    float ramp;                             /* the picture's fade in (0-1), its own: the cartridge's frame gives the fade out */
    float full;                             /* the cartridge screen's brightest frame so far */
    int dark;                               /* its first black frame came (the one before has faded out) */
    float last;                             /* the last frame's brightness */
    int out;                                /* the last picture fading out, the opening over */
    uint32_t ea_t0;                         /* the EA SPORTS logo: SDL_GetTicks when it came up (0 not yet) */
    int16_t *ea_pcm;                        /* its voice (STARTER.BIN's PCM bank), loaded with it */
    long ea_len;
    double ea_rate;                         /* its rate (Hz) */
    double ea_at;                           /* (the mixer's) where it is, -1 not playing */
    SDL_atomic_t ea_go;                     /* 1: start it (the picture's thread to the mixer's) */
    SDL_atomic_t ea_halt;                   /* (the game's thread) the EA SPORTS screen was left: its voice stops */
} S;

static const char *const need[2] = { "HOCKEY.BIN", "GRAPH2.BIN" };

static int finder(void *dir)
{
    char note[128] = "";
    ob_cd *cd = ob_cd_find((const char *)dir, need, 2, note, sizeof note);
    if (!cd) cd = ob_cd_find(".", need, 2, note, sizeof note);
    if (cd) fprintf(stderr, "Sega CD disc: %s (%d audio tracks)\n", ob_cd_path(cd), ob_cd_audio_tracks(cd));
    else if (note[0]) fprintf(stderr, "Sega CD disc: none read (%s)\n", note);
    S.cd = cd;
    SDL_MemoryBarrierRelease();
    SDL_AtomicSet(&S.found, 1);
    SDL_free(dir);
    return 0;
}

static int photo_hook(uint16_t team, uint16_t player, uint8_t *buf, uint8_t colours[32]);
static int title_hook(int what);
static int scout_hook(int what);
static const uint8_t *scout_text(int n);

void segacd_start(void)
{
    static int started;
    if (started) return;
    started = 1;
    S.ea_at = -1;
    cd_event_hook = segacd_event;
    cd_picture_hook = photo_hook;
    cd_title_hook = title_hook;
    cd_scout_hook = scout_hook;
    cd_scout_text = scout_text;
    char *base = SDL_GetBasePath();
    SDL_Thread *t = SDL_CreateThread(finder, "segacd", base ? base : SDL_strdup("."));
    if (t) SDL_DetachThread(t);
    else finder(base ? base : SDL_strdup("."));
}

void segacd_wait(void) { while (!SDL_AtomicGet(&S.found)) SDL_Delay(5); }
int segacd_searched(void) { return SDL_AtomicGet(&S.found); }

ob_cd *segacd_disc(void)
{
    if (!SDL_AtomicGet(&S.found)) return NULL;
    SDL_MemoryBarrierAcquire();
    return S.cd;
}

void segacd_set_music(int on) { SDL_AtomicSet(&S.music, on ? 1 : 0); }
void segacd_set_crowd(int v) { SDL_AtomicSet(&S.crowd, v); }
void segacd_set_voice(int v) { SDL_AtomicSet(&S.voice, v); }
void segacd_set_screens(int v) { SDL_AtomicSet(&S.screens, v); }
void segacd_set_title(int v) { SDL_AtomicSet(&S.title, v); }
void segacd_set_intro(int v) { SDL_AtomicSet(&S.intro, v); }
void segacd_set_photos(int v) { SDL_AtomicSet(&S.photos, v); }
void segacd_set_session(int on) { SDL_AtomicSet(&S.session, on); }

/* (cd_picture_hook) the disc's photo of the card's player, by his team's abbreviation and his name */
static void plain(const struct RosterText *t, char *s, int cap)
{
    int n = (t->s[0] << 8 | t->s[1]) - 2, k = 0;
    for (int i = 0; i < n && k < cap - 1 && t->s[2 + i]; i++) s[k++] = (char)t->s[2 + i];
    while (k && s[k - 1] == ' ') k--;
    s[k] = 0;
}

/* (cd_career_hook, set by the window: B on a card; player $FFFF the team's card) the disc's card: the team's words
 * and its six advantage numbers (the team data's bytes 1 to 3, from the roster set, as the disc reads them: each
 * byte's high nibble first, so its "Power play" is the nibble the cartridge's code uses for killing penalties, and
 * the other way round), and the player's name, number and career */
static int (*career_ending)(void);
static void career_hook(uint16_t team, uint16_t player)
{
    if (!SDL_AtomicGet(&S.career) || SDL_AtomicGet(&S.session) || !segacd_disc()) return;
    const struct RosterTeam *T = rosters_team(team);
    if (player != 0xFFFF && player >= T->players) return;
    static segacd_card k;
    memset(&k, 0, sizeof k);
    plain(&T->abbr, k.abbr, sizeof k.abbr);
    plain(&T->city, k.city, sizeof k.city);
    plain(&T->nickname, k.nick, sizeof k.nick);
    plain(&T->arena, k.arena, sizeof k.arena);
    const int adv[6] = { T->pk, T->pp, T->home, T->away, T->forwards, T->defensemen };
    memcpy(k.adv, adv, sizeof adv);
    if (player != 0xFFFF) {
        k.player = 1;
        plain(&T->player[player].name, k.name, sizeof k.name);
        k.number = T->player[player].number;
        k.goalie = player < T->goalies;
        if (!segacd_career_get(S.cd, k.abbr, k.name, k.goalie, &k.career)) return;
    }
    segacd_card_show(S.cd, &k, career_ending);
}

void segacd_set_career(int v, int (*ending)(void))
{
    SDL_AtomicSet(&S.career, v);
    career_ending = ending;
    cd_career_hook = career_hook;
}

static int photo_hook(uint16_t team, uint16_t player, uint8_t *buf, uint8_t colours[32])
{
    if (!SDL_AtomicGet(&S.photos) || SDL_AtomicGet(&S.session) || !segacd_disc()) return 0;
    const struct RosterTeam *T = rosters_team(team);
    if (player >= T->players) return 0;
    if (cardcolor_covers(team, player)) return 0;      /* (the cartridge's photo in colour comes first) */
    char abbr[16], name[40];
    plain(&T->abbr, abbr, sizeof abbr);
    plain(&T->player[player].name, name, sizeof name);
    int ok = segacd_photo(S.cd, abbr, name, buf, colours);
    if (getenv("SEGACD_LOG")) fprintf(stderr, "segacd: photo %s %s: %s\n", abbr, name, ok ? "the disc's" : "none");
    return ok;
}
void (*segacd_intro_hook)(void);

static int music_on(void) { return SDL_AtomicGet(&S.music) && segacd_disc(); }
/* the crowd: bit 0 the cartridge's PSG noise, bit 1 the disc's track 2 */
static int crowd_bits(void)
{
    int v = SDL_AtomicGet(&S.crowd);
    if (!v || !segacd_disc() || ob_cd_track_frames(S.cd, 2) <= 0) return 1;
    return v == 1 ? 2 : 3;
}

static void play(int track, int loop, int bed)
{
    if (S.track == track && S.loop == loop) { S.bed = bed; return; }   /* (the same song asked again: it goes on) */
    S.frames = ob_cd_track_frames(S.cd, track);
    S.track = S.frames > 0 ? track : 0;
    S.loop = loop;
    S.bed = bed;
    S.at = 0;
}
static void stop(void) { S.track = 0; S.bed = 0; }

/* ---- Ron Barr: the disc's clip of a scouting paragraph ----
 * The script is the cartridge's ScoutTextScript, a paragraph a line; the disc keeps it at the start
 * of GRAPH2.BIN, the same lines, and records some of them: RON<line>.PCM, the line counted from 1
 * (paragraph $1B, "Hi, I'm Ron Barr for EA Sports.", is RON0028). The welcome (paragraph $1A, "Welcome
 * to a sold out ^, home of the =.") is a clip a team, named for the home team. */
static const char *const team_clip[28] = {          /* the cartridge's teams, in its order */
    "ANAHEIM", "BOSTON", "BUFFALO", "CALGORY", "CHICAGO", "DALLAS", "DETROIT", "EDMONTON", "FLORIDA", "HARTFORD",
    "LAKINGS", "MONTREAL", "NJERSEY", "NEWYORK", "NYRANGER", "OTTAWA", "PHILLY", "PITTSBUR", "QUEBEC", "SANJOSE",
    "STLOUIS", "TAMPBAY", "TORONTO", "VANCUVER", "WASHINGT", "WINNIPEG", "ALLSTARE", "ALLSTARW",
};

/* the clip's file for paragraph n: name (on the data track), 0 when it has none */
/* the disc's scouting text (GRAPH2.BIN's first block): its paragraphs as the disc counts them (scout94cd.asm:ScoutParaOffset: a
 * paragraph is a line, or lines ending " !" and the one after), where each starts and its line, which numbers its speech
 * (RON<line + 1>.PCM). Loaded once, on the game's thread */
static struct { uint8_t *text; long len; int tried, n; long at[0x120]; int line[0x120]; } ST;
static int scout_load(void)
{
    if (ST.tried) return ST.text != NULL;
    ST.tried = 1;
    ob_cd *cd = segacd_disc();
    uint8_t *b;
    long size = cd ? ob_cd_read_file(cd, "GRAPH2.BIN", &b) : 0;
    if (size < 16) return 0;
    uint32_t from = (uint32_t)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3], to = (uint32_t)b[4] << 24 | b[5] << 16 | b[6] << 8 | b[7];
    if (from < 8 || to <= from || to > (uint32_t)size || !(ST.text = malloc(to - from + 4))) { free(b); return 0; }
    memcpy(ST.text, b + from, to - from);
    ST.len = (long)(to - from);
    memcpy(ST.text + ST.len, "\r\n\0", 4);                 /* (a last line end, for a short file) */
    free(b);
    long i = 0;
    int line = 0;
    for (ST.n = 0; ST.n < (int)(sizeof ST.at / sizeof ST.at[0]) && i < ST.len; ST.n++) {
        ST.at[ST.n] = i;
        ST.line[ST.n] = line;
        for (;;) {
            line++;
            while (i < ST.len && ST.text[i] != 0xD) i++;
            i++;
            if (i < ST.len && ST.text[i] == 0xA) {
                i++;
                if (i >= 3 && ST.text[i - 3] == '!') continue;
            }
            break;
        }
    }
    return 1;
}
static const uint8_t *scout_text(int n)
{
    return scout_load() && n >= 0 && n < ST.n ? ST.text + ST.at[n] : NULL;
}
/* (cd_scout_hook) the disc's scouting report: with Ron Barr's voice and the disc's text; not in a session (the machines keep step) */
static int scout_hook(int what)
{
    if (what == CD_SCOUT_BUSY) return S.clip != NULL || S.nqueue > 0;
    return SDL_AtomicGet(&S.voice) && segacd_disc() && !SDL_AtomicGet(&S.session) && scout_load() && ST.n > 0xEA + 0x1B;
}

static int clip_name(int n, char *name, size_t size)
{
    if (n == 0x1A) {                                       /* the welcome: the home team's (by its art: a roster set's team plays as one of the cartridge's) */
        uint16_t t = rosters_art(team_struct(0)->tmteam);
        if (t >= 28) return 0;
        snprintf(name, size, "%s.PCM", team_clip[t]);
        return 1;
    }
    snprintf(name, size, "RON%04d.PCM", (scout_load() && n < ST.n ? ST.line[n] : n) + 1);   /* (by the paragraph's line: its speech's number) */
    return 1;
}

/* paragraph n's clip to play now; 0 when the disc has none */
static int clip_load(int n)
{
    char name[24];
    uint8_t *b;
    if (!clip_name(n, name, sizeof name)) return 0;
    long size = ob_cd_read_file(S.cd, name, &b);
    if (size <= 0) return 0;
    int16_t *c = SDL_malloc((size_t)size * sizeof *c);
    long len = 0;
    if (c)
        for (; len < size && b[len] != 0xFF; len++) {     /* RF5C164 bytes: bit 7 the sign (set: positive), 7 bits of size; $FF the end */
            int v = (b[len] & 0x7F) << 8;
            c[len] = (int16_t)(b[len] & 0x80 ? v : -v);
        }
    free(b);
    SDL_free(S.clip);
    S.clip = c;
    S.clip_len = c ? len : 0;
    S.clip_at = 0;
    S.clip_id = n;
    return c != NULL;
}

static void voice_stop(void)
{
    SDL_free(S.clip);
    S.clip = NULL;
    S.nqueue = 0;
    S.clip_id = -1;
}

/* the next waiting clip, if any */
static void voice_next(void)
{
    SDL_free(S.clip);
    S.clip = NULL;
    while (S.nqueue && !S.clip) {
        int n = S.queue[0];
        memmove(S.queue, S.queue + 1, (size_t)--S.nqueue * sizeof S.queue[0]);
        clip_load(n);
    }
}

static void paragraph(int n)
{
    char name[24];
    if (!SDL_AtomicGet(&S.voice) || !segacd_disc() || !clip_name(n, name, sizeof name) || ob_cd_file_size(S.cd, name) <= 0) return;
    if (S.clip && S.clip_id == n) return;                 /* (a frame played again) */
    for (int i = 0; i < S.nqueue; i++) if (S.queue[i] == n) return;
    if (!S.clip) clip_load(n);
    else if (S.nqueue < (int)(sizeof S.queue / sizeof S.queue[0])) S.queue[S.nqueue++] = n;
}

void segacd_event(int ev, uint16_t n)
{
    static int log = -1;
    if (log < 0) log = SDL_getenv("SEGACD_LOG") != NULL;
    if (log && ev != CD_EV_CROWD)
        fprintf(stderr, "segacd: frame %ld %s %02X (track %d)\n", native_frames(),
                ev == CD_EV_SONG ? "song" : ev == CD_EV_STOP ? "stop" : ev == CD_EV_PARAGRAPH ? "paragraph" : ev == CD_EV_SCREEN ? "screen" : ev == CD_EV_SFX ? "sfx" : "turnoff", n, S.track);
    switch (ev) {
    case CD_EV_SONG: {
        S.song_on_cd = 0;
        if (n == 0x7A && SDL_AtomicGet(&S.voice) == 1 && segacd_disc()) {   /* Ron Barr alone, as on the disc: no song */
            if (S.track && !S.bed) stop();
            S.song_on_cd = 1;
            return;
        }
        if (!music_on()) return;
        int track = n >= 0x30 && n <= 0x77 ? n - 0x29 : n == 0x78 ? 4 : n == 0x79 ? 5 : n == 0x7A ? 3 : 0;
        if (!track || ob_cd_track_frames(S.cd, track) <= 0) return;   /* (not on this disc: the FM song) */
        play(track, n >= 0x78, 0);
        S.song_on_cd = 1;
        break;
    }
    case CD_EV_STOP:                          /* stop_current_song: the looped songs pause; the organ plays out */
        S.song_on_cd = 0;
        if (S.track && S.loop && !S.bed) stop();
        break;
    case CD_EV_TURNOFF:                       /* p_turnoff (pause, the full silences, leaving the scouting report): all of it */
        S.song_on_cd = 0;
        stop();
        voice_stop();
        segacd_sfx_stop();
        break;
    case CD_EV_PARAGRAPH:
        paragraph(n);
        break;
    case CD_EV_SFX: {                         /* the disc's effect in place of the cartridge's (its slot's notes dropped) */
        int id = n & 0xFF, slot = n >> 8 & 15, disc = !SDL_AtomicGet(&S.session) && segacd_disc() && segacd_sfx_on(id);
        if (slot < 8) S.fx_slot[slot] = (uint8_t)disc;
        if (disc) segacd_sfx_play(id);
        break;
    }
    case CD_EV_SCREEN:
        if (n == 2 && !S.intro_done && SDL_AtomicGet(&S.intro) && segacd_intro_hook && segacd_disc()) {
            S.intro_done = 1;                 /* (the disc's movie before its title, as the disc plays it: the game waits) */
            segacd_intro_hook();
        }
        {
            int was = S.screen >= 1 && S.screen <= 5 && S.pic_of > 0 && S.screen != n;
            S.out = was && n != 5;            /* (the last picture fades out with the cartridge's screen before the next comes up) */
            S.dark = !was && S.last < 16 * 255;   /* (the next waits for the cartridge's black frame, unless it is black already) */
            if (n == 2 && (S.intro_done == 1 || S.screen == 5)) S.hold = 1;   /* (after the movie or the license page: black until the game's is) */
            if (S.intro_done == 1) S.intro_done = 2;
        }
        if (S.screen == 4 && n != 4) SDL_AtomicSet(&S.ea_halt, 1);   /* (skipped, or over: the logo's voice ends with it) */
        S.screen = n;
        S.ea_t0 = 0;
        S.hsp_t = 0;
        if (n != 2 && n != 3) S.title_live = 0;
        if (!S.out) { S.ramp = 0; S.full = 0; }
        break;
    case CD_EV_CROWD:
        S.crowd_live = CROWD_LIVE;
        if ((crowd_bits() & 2) && !S.track) play(2, 1, 1);
        break;
    }
}

/* the slot of the game's song (its stream at or past the first song's), its track, or -1 */
static int song_track(void)
{
    int32_t first = (int32_t)rd32(rom_ptr(SYM_SongPointerTable));
    for (int i = 0; i < 8; i++)
        if (first <= (int32_t)g.fm_slots[i].stream) return 7 - i;
    return -1;
}
/* output channel out (0-2, 4-6; 6 the PCM one too) is playing the disc's song's notes, or a disc effect's */
static int owned(int out, int pcm)
{
    int t = song_track();
    for (int k = pcm ? 5 : 0; k < (pcm ? 6 : 5); k++) {
        struct Channel *ch = &g.fm_channels[k];
        if ((!pcm && ch->out != out) || ch->key == 0xFF) continue;
        int slot = ch->key & 7;
        if (slot == t ? S.song_on_cd : S.fx_slot[slot]) return 1;
    }
    return 0;
}

uint8_t segacd_ym(int port, uint8_t v)
{
    if (port == 0) { S.ym_addr = v; return v; }
    if (port != 1) return v;
    if (S.ym_addr == 0x28 && (v & 0xF0) && owned(v & 7, 0)) return (uint8_t)(v & 0x07);   /* key on: kept off */
    if (S.ym_addr == 0x2A && owned(6, 1)) return 0x80;                                     /* a drum sample: silence */
    return v;
}

uint8_t segacd_psg(uint8_t v)
{
    if ((v & 0xF0) == 0xF0 && !(crowd_bits() & 1)) return 0xFF;   /* the noise channel's volume: off */
    return v;
}

static void mix_voice(int16_t *out, int frames, unsigned rate)
{
    double step = (double)VOICE_RATE / rate;
    for (int i = 0; i < frames && S.clip; i++) {
        long j = (long)S.clip_at;
        if (j + 1 >= S.clip_len) { voice_next(); continue; }
        double frac = S.clip_at - j;
        float v = (float)(S.clip[j] + (S.clip[j + 1] - S.clip[j]) * frac) * VOICE_GAIN;
        for (int c = 0; c < 2; c++) {
            float x = v + out[2 * i + c];
            out[2 * i + c] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
        }
        S.clip_at += step;
    }
}

/* (cd_title_hook, the game's thread) the disc's pages around the title: CD_TITLE_LICENSE: its license page before the title (the
 * Opening screens row); CD_TITLE_BEGIN: its own title page in place of the cartridge's (the Title page row); CD_TITLE_STEP: a frame
 * of its credits, 0 at their end. Not in a session (the machines keep step) */
static int title_hook(int what)
{
    ob_cd *cd = segacd_disc();
    if (!cd || SDL_AtomicGet(&S.session)) return 0;
    switch (what) {
    case CD_TITLE_LICENSE: {                   /* (once: after the disc's title when that is on, as the disc loads the game) */
        static uint32_t check[320 * 224];
        return SDL_AtomicGet(&S.screens) && !SDL_AtomicGet(&S.title) && segacd_picture(cd, SEGACD_PIC_LICENSE, check);
    }
    case CD_TITLE_BEGIN: return S.title_live = SDL_AtomicGet(&S.title) && segacd_title_begin(cd);
    case CD_TITLE_STEP: return S.title_live && segacd_title_step();
    }
    return 0;
}

void segacd_mix(int16_t *out, int frames, unsigned rate)
{
    segacd_sfx_mix(out, frames, rate);
    if (S.clip && rate) mix_voice(out, frames, rate);
    if (SDL_AtomicCAS(&S.ea_go, 1, 0)) { SDL_MemoryBarrierAcquire(); S.ea_at = 0; }
    if (SDL_AtomicCAS(&S.ea_halt, 1, 0)) S.ea_at = -1;
    if (S.ea_pcm && S.ea_at >= 0 && rate) {               /* (the EA SPORTS logo's voice) */
        double step = S.ea_rate / rate;
        for (int i = 0; i < frames; i++, S.ea_at += step) {
            long j = (long)S.ea_at;
            if (j + 1 >= S.ea_len) { S.ea_at = -1; break; }
            float v = (float)(S.ea_pcm[j] + (S.ea_pcm[j + 1] - S.ea_pcm[j]) * (S.ea_at - j)) * VOICE_GAIN;
            for (int c = 0; c < 2; c++) {
                float x = v + out[2 * i + c];
                out[2 * i + c] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
            }
        }
    }
    if (S.clip && !SDL_AtomicGet(&S.voice)) voice_stop();
    if (S.crowd_live > 0 && --S.crowd_live == 0 && S.bed) stop();   /* (the game's crowd stopped: the menus) */
    if (S.bed && !(crowd_bits() & 2)) stop();
    if (S.track && !S.bed && !music_on()) { stop(); S.song_on_cd = 0; }
    if (!S.track || !S.cd || !rate) return;
    float gain = S.bed ? BED_GAIN : SONG_GAIN;
    double step = (double)OB_CD_RATE / rate;
    int16_t buf[2 * 1100];
    int i = 0;
    while (i < frames && S.track) {
        long at = (long)S.at;
        int want = (int)((frames - i) * step) + 2;
        if (want > 1024) want = 1024;
        long got = ob_cd_track_read(S.cd, S.track, at, buf, want);
        if (got < 2) {                                             /* its end */
            if (S.loop) { S.at = 0; if (at == 0) { stop(); break; } continue; }
            stop();
            if (S.crowd_live > 0 && (crowd_bits() & 2)) play(2, 1, 1);   /* the organ done: the crowd bed again */
            break;
        }
        for (; i < frames; i++) {
            double f = S.at - at;
            int j = (int)f;
            if (j + 1 >= got) break;
            double frac = f - j;
            for (int c = 0; c < 2; c++) {
                float s = (float)(buf[2 * j + c] + (buf[2 * j + 2 + c] - buf[2 * j + c]) * frac) * gain + out[2 * i + c];
                out[2 * i + c] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
            }
            S.at += step;
        }
    }
}

/* ---- the opening screens: the disc's picture in place of the cartridge's ----
 * The cartridge's High Score Productions page shows the disc's (its static picture: the disc
 * animates the logo on it); the cartridge's title shows the disc's title page, and when the
 * cartridge's credits start rolling, the disc's license page over the goalie, the order the
 * disc boots in (title from STARTER.BIN, then the license from GRAPH2.BIN before the menus).
 * The cartridge's own screen goes on underneath (its timing, Start, its song), so the game is
 * the same; the picture fades in by itself and out with the cartridge's frame. */
/* the EA SPORTS logo's voice: the PCM bank at the end of STARTER.BIN's boot art (RF5C164 bytes up to the first $FF), played at
 * the frequency word of its sound table entry (SetupSpriteFrame's, file $54EC: type, sample, then the frequency range, $05A9):
 * 32552 Hz x $05A9 / $800, about 23 kHz */
#define EA_PCM_AT 50540L
#define EA_PCM_MAX 63284L
#define EA_SOUND_AT 0x54ECL

static void ea_voice_load(void)
{
    uint8_t *b;
    if (S.ea_pcm || !S.cd) return;
    long size = ob_cd_read_file(S.cd, "STARTER.BIN", &b);
    if (size >= EA_PCM_AT + EA_PCM_MAX) {
        int16_t *c = SDL_malloc((size_t)EA_PCM_MAX * sizeof *c);
        long len = 0;
        if (c)
            for (; len < EA_PCM_MAX && b[EA_PCM_AT + len] != 0xFF; len++) {
                int v = (b[EA_PCM_AT + len] & 0x7F) << 8;
                c[len] = (int16_t)(b[EA_PCM_AT + len] & 0x80 ? v : -v);
            }
        S.ea_len = len;
        unsigned fd = (unsigned)b[EA_SOUND_AT + 2] << 8 | b[EA_SOUND_AT + 3];
        S.ea_rate = 32552.0 * (fd >= 0x100 && fd < 0x1000 ? fd : 0x5A9) / 0x800;
        SDL_MemoryBarrierRelease();
        S.ea_pcm = c;
    }
    free(b);
}

/* the disc's EA SPORTS logo now: ScreenSetupA's script (starterscreen94cd.asm) at 60 frames a second puts the first
 * piece up at frame 11 with the voice, then one each 31 frames twice, then 5 frames, then one each 7 frames; 17 in all */
static int ea_pieces(uint32_t ms)
{
    int t = (int)(ms * 60 / 1000), n = 0;
    static const int first[4] = { 11, 42, 73, 104 };
    for (int i = 0; i < 4; i++) if (t >= first[i]) n = i + 1;
    if (t >= 109) n = 5 + (t - 109) / 7;
    return n > 17 ? 17 : n;
}

int segacd_present(uint32_t *frame, int w, int h)
{
    enum { EA = 100, HSP, TITLE };
    int on = SDL_AtomicGet(&S.screens), want = 0;
    switch (S.screen) {
    case 1: want = on ? HSP : 0; break;
    case 2: case 3: want = S.title_live ? TITLE : 0; break;
    case 4: want = on ? EA : 0; break;
    case 5: want = SEGACD_PIC_LICENSE + 1; break;      /* (asked for by the game: segacd_title_hook) */
    }
    if (S.out) want = S.pic_of;
    if (S.hold && segacd_disc()) {                         /* (black over what the game left up, until its own black frame) */
        float sum = 0;
        for (int y = 0; y < h && y < 224; y += 2)
            for (int x = 0; x < w && x < 320; x += 3) {
                uint32_t c = frame[y * 320 + x];
                sum += (float)(((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255));
            }
        if (sum < 16 * 255 || ++S.hold > 120) { S.hold = 0; S.dark = 1; S.last = sum; }
        else {
            for (int i = 0; i < 320 * 224; i++) frame[i] = 0xFF000000u;
            return 1;
        }
    }
    if (!segacd_disc() || !want) { S.last = 1e9f; S.out = 0; return 0; }
    if (want == EA) {                                      /* (the EA SPORTS logo: built a piece at a time from the disc's sprites) */
        if (!S.dark) S.ea_t0 = 0;
        else if (!S.ea_t0) { S.ea_t0 = SDL_GetTicks() | 1; ea_voice_load(); }
        int n = S.ea_t0 ? ea_pieces(SDL_GetTicks() - S.ea_t0) : 0;
        static int shown = -1;
        if (S.pic_of != want || n != shown) {
            if (S.pic_of != want) { S.ramp = 0; S.full = 0; }
            if (n >= 1 && shown < 1 && S.ea_pcm) SDL_AtomicSet(&S.ea_go, 1);
            S.pic_of = segacd_ea_logo(S.cd, n, S.pic) ? want : -want;
            shown = n;
        }
    } else if (want == HSP) {                              /* (its sparkles: a step a frame once it is up) */
        if (S.pic_of != want) { S.ramp = 0; S.full = 0; }
        S.pic_of = segacd_hsp(S.cd, S.dark ? S.hsp_t++ : 0, S.pic) ? want : -want;
    } else if (want == TITLE) {
        if (S.pic_of != want) { S.ramp = 0; S.full = 0; }
        segacd_title_draw(S.pic);
        S.pic_of = want;
    } else if (S.screen == 5 && !S.out) {                  /* (the license page: already in pic, its own fade in over whatever the game shows) */
        if (S.pic_of != want) {
            if (!segacd_picture(S.cd, SEGACD_PIC_LICENSE, S.pic)) { S.pic_of = -want; return 0; }
            S.pic_of = want; S.ramp = 0; S.full = 0;
        }
        if (S.ramp < 1) S.ramp += 1.0f / 24;
        float sum = 0;                                     /* (the game's screen under it: the fade out follows it) */
        for (int y = 0; y < h && y < 224; y += 2)
            for (int x = 0; x < w && x < 320; x += 3) {
                uint32_t c = frame[y * 320 + x];
                sum += (float)(((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255));
            }
        if (sum > S.full) S.full = sum;
        S.last = sum;
        float k = S.ramp < 1 ? S.ramp : 1;
        for (int i = 0; i < 320 * 224; i++) {
            uint32_t c = S.pic[i];
            frame[i] = 0xFF000000u | (uint32_t)(((c >> 16) & 255) * k) << 16 | (uint32_t)(((c >> 8) & 255) * k) << 8 | (uint32_t)((c & 255) * k);
        }
        S.dark = 1;
        return 1;
    } else if (S.pic_of != want) {
        S.pic_of = segacd_picture(S.cd, want - 1, S.pic) ? want : -want;
        S.ramp = 0;
        S.full = 0;
    }
    if (S.pic_of != want) return 0;                        /* (not on this disc) */
    float sum = 0;
    for (int y = 0; y < h && y < 224; y += 2)
        for (int x = 0; x < w && x < 320; x += 3) {
            uint32_t c = frame[y * 320 + x];
            sum += (float)(((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255));
        }
    S.last = sum;
    if (S.out) {                                           /* (fading out: until the cartridge's frame is black) */
        if (sum < 16 * 255) { S.out = 0; S.dark = 1; S.ramp = 0; S.full = 0; return 0; }
        if (S.full <= 0) { S.out = 0; S.hold = 1; S.ramp = 0; for (int i = 0; i < 320 * 224; i++) frame[i] = 0xFF000000u; return 1; }
        S.ramp = 1;
    } else if (sum < 16 * 255) {                                  /* (black, or nearly: the cartridge's screen between fades) */
        S.dark = 1;
        S.ramp = 0;
        S.full = 0;
    }
    if (!S.dark && !S.out) return 0;                       /* (the screen before it, still fading out) */
    if (sum > S.full) S.full = sum;
    float k = S.full > 0 ? sum / S.full : 0;
    if (S.ramp < 1 && sum >= 16 * 255) S.ramp += 1.0f / 24;   /* (the cartridge's cramfade: about 24 frames) */
    if (k > S.ramp) k = S.ramp;
    for (int i = 0; i < 320 * 224; i++) {
        uint32_t c = S.pic[i];
        frame[i] = 0xFF000000u | (uint32_t)(((c >> 16) & 255) * k) << 16 | (uint32_t)(((c >> 8) & 255) * k) << 8 | (uint32_t)((c & 255) * k);
    }
    return 1;
}
