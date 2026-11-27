/* Colour player photos (cardcolor.h). The cartridge's 48 x 48 grey photos in colour, from the same
 * photo in the player's own PC game files: NHL 95 PC's PORTRnnn.QFS (on its disc) or NHL 94 PC's
 * PORTRAIT.NSH (in the installed game or on its disc). Abdul's choice (2026-11-25): the card keeps the
 * cartridge's picture and most of its light, the colour is the PC photo's; the rules are the plan doc's
 * "Agreed method", the maths cardcolor_xfer.c.
 *
 * Where the files come from: beside the program, loose (PORTR000.QFS ..., PORTRAIT.NSH, any case) or
 * a disc image of either game in its folders (Overbar's ob_cd_find: .iso, .cue, .ccd ...), or the
 * folder NHL94_PC_PORTRAITS names. Nothing is shipped: cardcolor_table.inc only says which portrait
 * is which card's photo, and where it sits on the card.
 *
 * Or the finished faces: the folder NHL94-colored-faces (EA-NHL-Tools has it: CardNNN_Name.png, 48 x 48,
 * all 144 cards, Abdul's approved colours) beside the program, in NHL94_PC_PORTRAITS's folder, or where
 * NHL94_COLORED_FACES names (the PNGs in it or in its faces/). A card with its face there shows that face, pixel for pixel; the others
 * are made from the PC files as above. The folder's SegaCD_*.png colour the Sega CD's own photos of
 * players the cartridge has none of (segacd.c's, drawn with no ROM picture). cardcolor_faces.inc only says which picture is CardNNN.
 * Its Player_TEAM_Name.png are faces of players the game shows a stock picture for (on the cartridge or the
 * disc): the player is found by his name in the roster, so they follow him to any team.
 *
 * How it is shown: the game draws a picture as it always does (its tiles, its map, its grey palette).
 * picture_drawn_hook notes which picture went where (its tiles' first char, its 36 map entries); each
 * frame the picture is found on its plane, and the pixels the VDP shows from it are replaced by the
 * colour picture's, as bright as the grey palette is at that moment (fades). The game, its RAM and the
 * VDP are never written, so frame hashes, states, replays and online play are the same either way. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "rom.h"
#include "rosters.h"
#include "native.h"
#include "wide.h"
#include "cardcolor.h"
#include "cardcolor_xfer.h"
#include "overbar/cdimage.h"
#include "overbar/third_party/stb/stb_image.h"   /* (Overbar links it) */
#include <dirent.h>

static const cc_card table[] = {
#include "cardcolor_table.inc"
};
#define NCARDS ((int)(sizeof table / sizeof table[0]))
static const uint32_t faces[] = {                  /* CardNNN's picture: the hash of its tiles */
#include "cardcolor_faces.inc"
};
#define NFACES ((int)(sizeof faces / sizeof faces[0]))
static const struct { uint32_t hash; const char *name; } cdfaces[] = {   /* the Sega CD's own photos, by the hash of */
    { 0xD735D385u, "SegaCD_LA_JariKurri" },        /* picturebuf as its photo_hook fills it (the count word, */
    { 0x57321F39u, "SegaCD_PIT_RickTocchet" },     /* 36 tiles): only these two are faces the cartridge lacks */
};
#define NCD ((int)(sizeof cdfaces / sizeof cdfaces[0]))
#define NNAMED 64                                  /* Player_TEAM_FullName.png: faces of players with only a stock picture */
#define NALL (NFACES + NCD + NNAMED)               /* faces: the cartridge's CardNNN - 1, then the Sega CD's, then the named */
#define FACES_DIR "NHL94-colored-faces"
#define SHOWN 4                                    /* pictures on one screen (the scouting report has two) */
#define QFS_FILES 24

static struct {
    SDL_atomic_t on, found;                        /* the Game page's row; the search ended */
    int have;                                      /* bit 0 95 PC, bit 1 94 PC, bit 2 faces */
    uint8_t *qfs[QFS_FILES];                       /* the 95 PC banks as read (RefPack), NULL none */
    long qfs_n[QFS_FILES];
    uint8_t *nsh;
    long nsh_n;
    uint8_t made[NALL];                            /* 0 not yet, 1 made (or a face read), 2 could not be */
    uint8_t rgb[NALL][CC_N * CC_N * 3];
    char named[NNAMED][40];                        /* the named faces' players: their names, letters and digits, lower case */
    char cardname[NFACES][40];                     /* CardNNN_Name.png's Name, the same way ("" none) */
    int nnamed;
    struct shown {
        int used, card;
        uint16_t start, count, attr;               /* its first char, its tiles, the map's attribute bits */
        uint32_t tiles_hash;                       /* its tiles as VRAM holds them (picturebuf's) */
        uint16_t entry[36];                        /* its map entries as written to the plane */
        uint8_t full[16][3];                       /* its palette line at full light: 3-bit levels */
    } shown[SHOWN];
} C;

/* ---- the files ---------------------------------------------------------------------------- */

static uint8_t *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *b = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long size = ftell(f);
        if (size > 0 && size < (64L << 20) && fseek(f, 0, SEEK_SET) == 0 && (b = malloc((size_t)size))) {
            if (fread(b, 1, (size_t)size, f) == (size_t)size) *n = size;
            else { free(b); b = NULL; }
        }
    }
    fclose(f);
    return b;
}

/* dir/name as named, upper case or lower case */
static uint8_t *loose(const char *dir, const char *name, long *n)
{
    char path[1200], low[64];
    size_t k;
    for (k = 0; name[k] && k < sizeof low - 1; k++) low[k] = (char)(name[k] >= 'A' && name[k] <= 'Z' ? name[k] + 32 : name[k]);
    low[k] = 0;
    const char *names[2] = { name, low };
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s%s%s", dir, dir[0] && dir[strlen(dir) - 1] != '/' && dir[strlen(dir) - 1] != '\\' ? "/" : "", names[i]);
        uint8_t *b = slurp(path, n);
        if (b) return b;
    }
    return NULL;
}

static int find95_in(const char *dir)
{
    char name[16];
    int got = 0;
    for (int i = 0; i < QFS_FILES; i++) {
        snprintf(name, sizeof name, "PORTR%03d.QFS", i);
        if ((C.qfs[i] = loose(dir, name, &C.qfs_n[i]))) got++;
    }
    return got;
}

/* s's letters and digits, lower case, to out (cap bytes) */
static void squash(const char *s, size_t n, char *out, int cap)
{
    int k = 0;
    for (size_t i = 0; i < n && s[i] && k < cap - 1; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[k++] = c;
    }
    out[k] = 0;
}

/* the faces in dir (CardNNN....png, SegaCD_....png, Player_TEAM_Name.png): how many */
static int faces_in_one(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    int got = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        const char *n = e->d_name;
        size_t len = strlen(n);
        int k = -1;
        for (int i = 0; i < NCD; i++)
            if (len == strlen(cdfaces[i].name) + 4 && !SDL_strncasecmp(n, cdfaces[i].name, len - 4) && !SDL_strcasecmp(n + len - 4, ".png")) k = NFACES + i;
        if (k < 0 && len > 11 && !SDL_strncasecmp(n, "player_", 7) && !SDL_strcasecmp(n + len - 4, ".png")) {   /* Player_TEAM_Name.png */
            const char *u = strchr(n + 7, '_');
            if (!u || C.nnamed >= NNAMED) continue;
            char key[40];
            squash(u + 1, (size_t)(n + len - 4 - (u + 1)), key, sizeof key);
            int dup = 0;
            for (int i = 0; i < C.nnamed; i++) dup |= !strcmp(C.named[i], key);
            if (!key[0] || dup) continue;
            k = NFACES + NCD + C.nnamed;
            strcpy(C.named[C.nnamed++], key);
        }
        if (k < 0) {                                       /* CardNNN_Name.png */
            if (len < 11 || SDL_strncasecmp(n, "card", 4) || SDL_strcasecmp(n + len - 4, ".png")) continue;
            if (n[4] < '0' || n[4] > '9' || n[5] < '0' || n[5] > '9' || n[6] < '0' || n[6] > '9') continue;
            if (n[7] != '_' && n[7] != '.') continue;
            k = (n[4] - '0') * 100 + (n[5] - '0') * 10 + (n[6] - '0') - 1;
            if (k < 0 || k >= NFACES) continue;
            if (n[7] == '_') squash(n + 8, len - 12, C.cardname[k], sizeof C.cardname[k]);
        }
        if (C.made[k]) continue;
        char path[1200];
        long size = 0;
        snprintf(path, sizeof path, "%s/%s", dir, n);
        uint8_t *b = slurp(path, &size);
        int w, h, ch;
        uint8_t *px = b ? stbi_load_from_memory(b, (int)size, &w, &h, &ch, 3) : NULL;
        if (px && w == CC_N && h == CC_N) { memcpy(C.rgb[k], px, sizeof C.rgb[k]); C.made[k] = 1; got++; }
        else { fprintf(stderr, "Colour photos: %s is not a 48 x 48 picture\n", path); C.made[k] = 2; }
        stbi_image_free(px);
        free(b);
    }
    closedir(d);
    return got;
}

/* the faces in dir's faces/ (EA-NHL-Tools keeps them there), then any loose in dir: a face in both is faces/'s, the
 * newer layout's (an older copy left loose beside it is not read) */
static int faces_in(const char *dir)
{
    char sub[1216];                                    /* (dir is at most 1200) */
    snprintf(sub, sizeof sub, "%s/faces", dir);
    int got = faces_in_one(sub);
    return got + faces_in_one(dir);
}

static int finder(void *arg)
{
    char *base = arg;
    const char *env = getenv("NHL94_PC_PORTRAITS"), *fenv = getenv("NHL94_COLORED_FACES");
    const char *dirs[3] = { env && env[0] ? env : NULL, base, "." };
    char fdir[1200];
    int nf = fenv && fenv[0] ? faces_in(fenv) : 0;
    for (int d = 0; d < 3 && !nf; d++) {
        if (!dirs[d]) continue;
        snprintf(fdir, sizeof fdir, "%s%s" FACES_DIR, dirs[d], dirs[d][0] && dirs[d][strlen(dirs[d]) - 1] != '/' && dirs[d][strlen(dirs[d]) - 1] != '\\' ? "/" : "");
        nf = faces_in(fdir);
    }
    if (nf) { C.have |= 4; fprintf(stderr, "Colour photos: %d colour faces from " FACES_DIR "\n", nf); }
    for (int d = 0; d < 3 && !(C.have & 1); d++) if (dirs[d] && find95_in(dirs[d])) C.have |= 1;
    for (int d = 0; d < 3 && !(C.have & 2); d++) if (dirs[d] && (C.nsh = loose(dirs[d], "PORTRAIT.NSH", &C.nsh_n))) C.have |= 2;
    if (!(C.have & 1)) {                                   /* the 95 PC disc */
        static const char *const need[1] = { "PORTR000.QFS" };
        ob_cd *cd = ob_cd_find(base, need, 1, NULL, 0);
        if (!cd) cd = ob_cd_find(".", need, 1, NULL, 0);
        if (cd) {
            char name[16];
            for (int i = 0; i < QFS_FILES; i++) {
                uint8_t *b = NULL;
                snprintf(name, sizeof name, "PORTR%03d.QFS", i);
                long n = ob_cd_read_file(cd, name, &b);
                if (n > 0) { C.qfs[i] = b; C.qfs_n[i] = n; C.have |= 1; }
                else free(b);
            }
            fprintf(stderr, "Colour photos: NHL 95 PC portraits from %s\n", ob_cd_path(cd));
            ob_cd_close(cd);
        }
    }
    if (!(C.have & 2)) {                                   /* the 94 PC disc */
        static const char *const need[1] = { "PORTRAIT.NSH" };
        ob_cd *cd = ob_cd_find(base, need, 1, NULL, 0);
        if (!cd) cd = ob_cd_find(".", need, 1, NULL, 0);
        if (cd) {
            long n = ob_cd_read_file(cd, "PORTRAIT.NSH", &C.nsh);
            if (n > 0) { C.nsh_n = n; C.have |= 2; fprintf(stderr, "Colour photos: NHL 94 PC portraits from %s\n", ob_cd_path(cd)); }
            ob_cd_close(cd);
        }
    }
    if (!C.have) fprintf(stderr, "Colour photos: no " FACES_DIR ", NHL 95 PC or NHL 94 PC portraits found\n");
    SDL_free(base);
    SDL_MemoryBarrierRelease();
    SDL_AtomicSet(&C.found, 1);
    return 0;
}

int cardcolor_searched(void) { return SDL_AtomicGet(&C.found); }
void cardcolor_wait(void) { while (!SDL_AtomicGet(&C.found)) SDL_Delay(5); }
int cardcolor_found(void)
{
    if (!SDL_AtomicGet(&C.found)) return 0;
    SDL_MemoryBarrierAcquire();
    return C.have;
}
void cardcolor_set(int on) { SDL_AtomicSet(&C.on, on ? 1 : 0); }

/* ---- the game's pictures ------------------------------------------------------------------ */

/* the card (CardNNN - 1) of the picture at tiles, -1 none */
static int card_of(uint32_t tiles)
{
    if (tiles >= rom_limit) return -1;
    uint16_t count = rd16(rom_ptr(tiles));
    if (!count || count > 36 || tiles + 2u + count * 24u > rom_limit) return -1;
    uint32_t h = cc_hash(rom_ptr(tiles), 2 + count * 24);
    for (int k = 0; k < NFACES; k++) if (faces[k] == h) return k;
    return -1;
}

/* card k's entry in the PC files' table, NULL none */
static const cc_card *pc_of(int k)
{
    for (int i = 0; i < NCARDS; i++) if (table[i].hash == faces[k]) return &table[i];
    return NULL;
}

/* card k's colour picture (a face read, or made once from the PC files): 1, 0 when it cannot be */
static int make(int k, uint32_t tiles, uint32_t map)
{
    if (C.made[k]) return C.made[k] == 1;
    C.made[k] = 2;
    const cc_card *c = pc_of(k);
    if (!c) return 0;
    cc_img im;
    int ok = 0;
    if (c->src == CC_PC95 && c->file < QFS_FILES && C.qfs[c->file]) ok = cc_qfs_shape(C.qfs[c->file], C.qfs_n[c->file], c->tag, &im);
    else if (c->src == CC_PC94 && C.nsh) ok = cc_nsh_shape(C.nsh, C.nsh_n, c->tag, &im);
    if (!ok) return 0;
    uint32_t pal = ART(PicturePalette) + rd32(rom_ptr(ART(PicturePalette))) + 0x20;
    double grey[CC_N * CC_N];
    cc_genesis(rom_ptr(tiles), rom_ptr(map), rom_ptr(pal), grey);
    ok = cc_colour(grey, &im, c, C.rgb[k]);
    cc_img_free(&im);
    if (ok) C.made[k] = 1;
    return ok;
}

/* the Sega CD photo in picturebuf (no ROM picture): its face, -1 none */
static int cd_of(void)
{
    if (rd16(g.picturebuf) != 36) return -1;
    uint32_t h = cc_hash(g.picturebuf, 2 + 36 * 32);
    for (int i = 0; i < NCD; i++) if (cdfaces[i].hash == h) return NFACES + i;
    return -1;
}

/* the named face (Player_TEAM_Name.png) of team's roster player slot, -1 none: his name as the roster
 * has it, or cut short there (John Vanbiesbrk: the file's name has every letter of it, in order, and
 * the same first five) */
static int named_of(uint16_t team, uint16_t player)
{
    if (!SDL_AtomicGet(&C.found) || team >= ROSTERS_MAX) return -1;
    SDL_MemoryBarrierAcquire();
    const struct RosterTeam *T = rosters_team(team);
    if (player >= T->players) return -1;
    const struct RosterText *t = &T->player[player].name;
    char key[40];
    int len = (t->s[0] << 8 | t->s[1]) - 2;
    if (len < 0) len = 0;
    if (len > RS_TEXT - 2) len = RS_TEXT - 2;
    squash((const char *)t->s + 2, (size_t)len, key, sizeof key);
    if (strlen(key) < 5) return -1;
    for (int i = 0; i < C.nnamed; i++) {
        const char *f = C.named[i], *r = key;
        if (strncmp(f, key, 5)) continue;
        for (; *f && *r; f++) if (*f == *r) r++;
        if (!*r && C.made[NFACES + NCD + i] == 1) return NFACES + NCD + i;
    }
    for (int i = 0; i < NFACES; i++) {                    /* his cartridge card's face, by its file's name (John Vanbiesbrk on */
        const char *f = C.cardname[i], *r = key;           /* Florida with the Sega CD's rosters: no Rangers picture of him left) */
        if (!*f || strncmp(f, key, 5) || C.made[i] != 1) continue;
        for (; *f && *r; f++) if (*f == *r) r++;
        if (!*r) return i;
    }
    for (int u = 0; u < ROSTERS_MAX; u++) {               /* his photo on another team (John Vanbiesbrk: NYR's, on Florida) */
        const struct RosterTeam *U = &roster_team[u];
        for (int i = 0; i < U->pictures && i < RS_PICTURES; i++) {
            if (u == team || U->picture[i].slot >= U->players) continue;
            const struct RosterText *n = &U->player[U->picture[i].slot].name;
            int m = (n->s[0] << 8 | n->s[1]) - 2;
            if (m < 0) m = 0;
            if (m > RS_TEXT - 2) m = RS_TEXT - 2;
            char other[40];
            squash((const char *)n->s + 2, (size_t)m, other, sizeof other);
            if (strcmp(other, key)) continue;
            uint32_t pic = U->picture[i].pic;
            int k = card_of(pic + 8);
            if (k < 0) continue;
            uint32_t map = rd32(rom_ptr(pic + 4)) == 0 ? ART(PicturePalette) + rd32(rom_ptr(ART(PicturePalette) + 4)) : pic + rd32(rom_ptr(pic + 4));
            if (make(k, pic + 8, map)) return k;
        }
    }
    return -1;
}

static void drawn(uint32_t tiles, uint32_t map, uint16_t start, uint16_t attr)
{
    uint32_t who = picture_who;
    picture_who = 0;
    for (int i = 0; i < SHOWN; i++) if (C.shown[i].used && C.shown[i].start == start) C.shown[i].used = 0;   /* (its chars hold this one now) */
    if (!SDL_AtomicGet(&C.on) || !cardcolor_found() || map + 4u + 72u > rom_limit) return;
    int k = tiles ? card_of(tiles) : cd_of();
    if (k < 0 && who) k = named_of((uint16_t)(who >> 16), (uint16_t)((who & 0xFFFF) - 1));   /* (a stock picture: his own face, if named) */
    if (k < 0 || rd16(rom_ptr(map)) != 6 || rd16(rom_ptr(map + 2)) != 6) return;
    if (tiles && k < NFACES && card_of(tiles) == k ? !make(k, tiles, map) : C.made[k] != 1) return;
    uint16_t count = rd16(g.picturebuf);
    struct shown *s = NULL;
    for (int i = 0; i < SHOWN && !s; i++) if (!C.shown[i].used) s = &C.shown[i];
    if (!s) s = &C.shown[0];
    s->card = k;
    s->start = start;
    s->count = count;
    s->attr = attr;
    s->tiles_hash = cc_hash(g.picturebuf + 2, count * 32);
    for (int i = 0; i < 36; i++) s->entry[i] = (uint16_t)((rd16(rom_ptr(map + 4 + 2u * i)) + start) ^ attr);
    uint32_t pal = ART(PicturePalette) + rd32(rom_ptr(ART(PicturePalette))) + 0x20;
    for (int i = 0; i < 16; i++) {
        uint16_t w = tiles ? rd16(rom_ptr(pal + 2u * i)) : g.palfadenew[0x20 + i];   /* (the disc's photo: its own colours) */
        s->full[i][0] = (uint8_t)((w >> 1) & 7);
        s->full[i][1] = (uint8_t)((w >> 5) & 7);
        s->full[i][2] = (uint8_t)((w >> 9) & 7);
    }
    s->used = 1;
}

int cardcolor_covers(uint16_t team, uint16_t player)
{
    int have = cardcolor_found();
    if (!SDL_AtomicGet(&C.on) || !have) return 0;
    const struct RosterTeam *T = rosters_team(team);
    for (int i = 0; i < T->pictures; i++) {                /* (DrawPlayerPicture's: the team's featured pictures) */
        if (T->picture[i].slot != player) continue;
        int k = card_of(T->picture[i].pic + 8);
        if (k < 0) break;                                  /* (a stock picture in the list: Anaheim's, Florida's) */
        if (C.made[k] == 2) return 0;
        if (C.made[k] == 1) return 1;
        const cc_card *c = pc_of(k);
        return c && (have & (c->src == CC_PC95 ? 1 : 2));
    }
    return named_of(team, player) >= 0;                    /* (a stock picture, his face named: the cartridge's, in colour) */
}

void cardcolor_start(void)
{
    static int started;
    if (started) return;
    started = 1;
    picture_drawn_hook = drawn;
    char *base = SDL_GetBasePath();
    SDL_Thread *t = SDL_CreateThread(finder, "cardcolor", base ? base : SDL_strdup("."));
    if (t) SDL_DetachThread(t);
    else finder(base ? base : SDL_strdup("."));
}

/* ---- each frame ----------------------------------------------------------------------------- */

static uint16_t vword(const mdrt_vdp *v, uint32_t a)
{
    a &= 0xFFFE;
    return (uint16_t)(v->s.vram[a] << 8 | v->s.vram[a + 1]);
}

/* s's 6 x 6 entries on a plane (table at base, w x h cells): 1 with the top-left cell */
static int locate(const mdrt_vdp *v, const struct shown *s, uint32_t base, int w, int h, int *cx, int *cy)
{
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            if ((vword(v, base + (uint32_t)(r * w + c) * 2) & 0x7FFF) != (s->entry[0] & 0x7FFF)) continue;
            int ok = 1;
            for (int i = 1; i < 36 && ok; i++) {
                int rr = (r + i / 6) % h, cc = (c + i % 6) % w;
                ok = (vword(v, base + (uint32_t)(rr * w + cc) * 2) & 0x7FFF) == (s->entry[i] & 0x7FFF);
            }
            if (ok) { *cx = c; *cy = r; return 1; }
        }
    return 0;
}

void cardcolor_present(const mdrt_vdp *v, uint32_t *pic, int w, int h, int pitch)
{
    static const int cells[4] = { 32, 64, 32, 128 };
    if (!SDL_AtomicGet(&C.on)) return;
    int any = 0;
    struct { int plane, cx, cy, pw, ph; } where[SHOWN];
    for (int i = 0; i < SHOWN; i++) {
        struct shown *s = &C.shown[i];
        where[i].plane = 0;
        if (!s->used) continue;
        if ((uint32_t)s->start * 32u + s->count * 32u > 0x10000u || cc_hash(v->s.vram + (uint32_t)s->start * 32u, s->count * 32) != s->tiles_hash) {
            s->used = 0;                                   /* (its chars hold something else now) */
            continue;
        }
        int pw = cells[v->s.reg[16] & 3], ph = cells[(v->s.reg[16] >> 4) & 3];
        uint32_t ta = (uint32_t)(v->s.reg[2] & 0x38) << 10, tb = (uint32_t)(v->s.reg[4] & 7) << 13;
        int width = mdrt_vdp_width(v);
        uint32_t tw = width == 320 ? (uint32_t)(v->s.reg[3] & 0x3C) << 10 : (uint32_t)(v->s.reg[3] & 0x3E) << 10;
        int ww = width == 320 ? 64 : 32;
        if (locate(v, s, ta, pw, ph, &where[i].cx, &where[i].cy)) where[i].plane = WIDE_FROM_A, where[i].pw = pw, where[i].ph = ph;
        else if (locate(v, s, tb, pw, ph, &where[i].cx, &where[i].cy)) where[i].plane = WIDE_FROM_B, where[i].pw = pw, where[i].ph = ph;
        else if (locate(v, s, tw, ww, 32, &where[i].cx, &where[i].cy)) where[i].plane = WIDE_FROM_WINDOW, where[i].pw = ww, where[i].ph = 32;
        any |= where[i].plane != 0;
    }
    if (!any || !(v->s.reg[1] & 0x40)) return;
    static uint8_t idx[WIDE_MAX_W];
    static uint32_t at[WIDE_MAX_W];
    if (w > WIDE_MAX_W) w = WIDE_MAX_W;
    for (int y = 0; y < h; y++) {
        wide_line_at(v, y, idx, at);
        for (int x = 0; x < w; x++) {
            int p = WIDE_AT_PLANE(at[x]);
            if (!p || !idx[x]) continue;
            for (int i = 0; i < SHOWN; i++) {
                if (where[i].plane != p) continue;
                const struct shown *s = &C.shown[i];
                int px = WIDE_AT_X(at[x]), py = WIDE_AT_Y(at[x]);
                int cx = ((px >> 3) - where[i].cx + where[i].pw) % where[i].pw, cy = ((py >> 3) - where[i].cy + where[i].ph) % where[i].ph;
                if (cx >= 6 || cy >= 6) continue;
                int c = idx[x], line = (s->entry[cy * 6 + cx] >> 13) & 3;   /* (the map's own entry, attr xor'd in)*/
                if ((c >> 4) != line) break;
                uint16_t now = v->s.cram[c];
                int lv[3] = { (now >> 1) & 7, (now >> 5) & 7, (now >> 9) & 7 };
                const uint8_t *full = s->full[c & 15];
                if (lv[0] != lv[1] || lv[1] != lv[2] || lv[0] > full[0]) break;   /* (not its grey: the palette is someone else's now) */
                double k = full[0] ? (double)lv[0] / full[0] : 1.0;
                const uint8_t *rgb = C.rgb[s->card] + ((cy * 8 + (py & 7)) * CC_N + cx * 8 + (px & 7)) * 3;
                uint32_t o = 0xFF000000u;
                for (int ch = 0; ch < 3; ch++) {
                    int vch = (int)(rgb[ch] * k * 255.0 / 252.0 + 0.5);
                    o |= (uint32_t)(vch > 255 ? 255 : vch) << (16 - 8 * ch);
                }
                pic[y * pitch + x] = o;
                break;
            }
        }
    }
}
