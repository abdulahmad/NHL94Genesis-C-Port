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
#include <string.h>
#include "game.h"
#include "rom.h"
#include "symbols_gen.h"
#include "native.h"
#include "segacd.h"
#include "overbar/cdimage.h"

#define SONG_GAIN 0.70f                     /* the disc's tracks against the FM chip's mix */
#define BED_GAIN (SONG_GAIN * 0x300 / 0x400)
#define CROWD_LIVE 20                       /* frames the crowd bed keeps going after the game's last updatesound */

static struct {
    SDL_atomic_t music, crowd;              /* the Game page's rows (segacd_set_music / _crowd) */
    ob_cd *cd;                              /* the disc (NULL: none found, or not looked for yet) */
    SDL_atomic_t found;                     /* 1: the search ended (cd is set, or NULL for none) */
    int track, loop, bed;                   /* playing: the track (0 none), looped, the crowd bed */
    double at;                              /* its frame, at 44.1 kHz */
    long frames;                            /* its length */
    int song_on_cd;                         /* the game's song is the disc's now: its FM notes are dropped */
    int crowd_live;                         /* frames left of the game's crowd (updatesound) */
    uint8_t ym_addr;                        /* the YM2612's part 1 address latch (the relay's last) */
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

void segacd_start(void)
{
    static int started;
    if (started) return;
    started = 1;
    cd_sound_hook = segacd_event;
    char *base = SDL_GetBasePath();
    SDL_Thread *t = SDL_CreateThread(finder, "segacd", base ? base : SDL_strdup("."));
    if (t) SDL_DetachThread(t);
    else finder(base ? base : SDL_strdup("."));
}

void segacd_wait(void) { while (!SDL_AtomicGet(&S.found)) SDL_Delay(5); }

ob_cd *segacd_disc(void)
{
    if (!SDL_AtomicGet(&S.found)) return NULL;
    SDL_MemoryBarrierAcquire();
    return S.cd;
}

void segacd_set_music(int on) { SDL_AtomicSet(&S.music, on ? 1 : 0); }
void segacd_set_crowd(int v) { SDL_AtomicSet(&S.crowd, v); }

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

void segacd_event(int ev, uint16_t n)
{
    static int log = -1;
    if (log < 0) log = SDL_getenv("SEGACD_LOG") != NULL;
    if (log && ev != CD_EV_CROWD) fprintf(stderr, "segacd: frame %ld %s %02X (track %d)\n", native_frames(), ev == CD_EV_SONG ? "song" : ev == CD_EV_STOP ? "stop" : "turnoff", n, S.track);
    switch (ev) {
    case CD_EV_SONG: {
        S.song_on_cd = 0;
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
    case CD_EV_TURNOFF:                       /* p_turnoff (pause, the full silences): all of it */
        S.song_on_cd = 0;
        stop();
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
/* output channel out (0-2, 4-6; 6 the PCM one too) is playing the disc's song's notes */
static int owned(int out, int pcm)
{
    if (!S.song_on_cd) return 0;
    int t = song_track();
    if (t < 0) return 0;
    for (int k = pcm ? 5 : 0; k < (pcm ? 6 : 5); k++) {
        struct Channel *ch = &g.fm_channels[k];
        if ((pcm || ch->out == out) && (ch->key & 7) == t && ch->key != 0xFF) return 1;
    }
    return 0;
}

uint8_t segacd_ym(int port, uint8_t v)
{
    if (port == 0) { S.ym_addr = v; return v; }
    if (port != 1 || !S.song_on_cd) return v;
    if (S.ym_addr == 0x28 && (v & 0xF0) && owned(v & 7, 0)) return (uint8_t)(v & 0x07);   /* key on: kept off */
    if (S.ym_addr == 0x2A && owned(6, 1)) return 0x80;                                     /* a drum sample: silence */
    return v;
}

uint8_t segacd_psg(uint8_t v)
{
    if ((v & 0xF0) == 0xF0 && !(crowd_bits() & 1)) return 0xFF;   /* the noise channel's volume: off */
    return v;
}

void segacd_mix(int16_t *out, int frames, unsigned rate)
{
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
