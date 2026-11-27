/* make fightratetest: native/fightrate.c (RULE_FIGHT_RATINGS) on made-up players (no game's ratings here): the name matching, the
 * scales, and a session's sources from a peer, hostile ones refused whole (NETPLAY_SECURITY.md: every length checked) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "rosters.h"
#include "fightrate.h"
#include "overbar/cdimage.h"

/* what fightrate.c reaches outside itself (the test reads no files) */
struct rosters rosters;
struct RosterTeam roster_team[ROSTERS_MAX];
int (*fight_rating_hook)(const char *name, int cart);
ob_cd *ob_cd_find(const char *dir, const char *const *need, int nneed, char *note, int note_size) { (void)dir; (void)need; (void)nneed; (void)note; (void)note_size; return NULL; }
ob_cd *ob_cd_open(const char *path, const char *const *need, int nneed) { (void)path; (void)need; (void)nneed; return NULL; }
void ob_cd_close(ob_cd *cd) { (void)cd; }
const char *ob_cd_path(const ob_cd *cd) { (void)cd; return ""; }
long ob_cd_read_file(ob_cd *cd, const char *name, uint8_t **out) { (void)cd; (void)name; *out = NULL; return -1; }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* a session blob: 93's entries, then 96's ("name", value) */
struct e { const char *name; int v; };
static size_t blob(uint8_t *b, const struct e *a, int na, const struct e *c, int nc)
{
    size_t at = 0;
    memcpy(b, "FR1", 3); at = 3;
    const struct e *m[2] = { a, c };
    int n[2] = { na, nc };
    for (int k = 0; k < 2; k++) {
        b[at++] = (uint8_t)n[k]; b[at++] = (uint8_t)(n[k] >> 8);
        for (int i = 0; i < n[k]; i++) {
            size_t l = strlen(m[k][i].name);
            b[at++] = (uint8_t)l;
            memcpy(b + at, m[k][i].name, l); at += l;
            b[at++] = (uint8_t)m[k][i].v;
        }
    }
    return at;
}

static int val(const char *name, int cart, const char *want_how)
{
    const char *how = "";
    int v = fightrate_value(name, cart, &how);
    CHECK(!want_how || !strcmp(how, want_how), "%s: from %s, not %s", name, how, want_how);
    CHECK(v == fightrate_value(name, cart, NULL), "%s: the cached value differs", name);   /* (the cache gives the same) */
    return v;
}

int main(void)
{
    static uint8_t b[1 << 16];
    const struct e s93[] = { { "Rocky Brawler", 14 }, { "Alexander Quickhands", 0 }, { "Gordon Grinder", 10 }, { "Pat Middle", 4 },
                             { "Jim Lookalike", 6 }, { "Jason Lookalike", 12 } };
    const struct e s96[] = { { "Rocky Brawler", 5 }, { "Alexander Quickhands", 7 }, { "Ivan Newcomer", 6 }, { "Pat Middle", 11 },
                             { "Sven Shy", 8 } };
    size_t n = blob(b, s93, 6, s96, 5);
    CHECK(!fightrate_session_use(b, n), "a good blob refused: %s", fightrate_session_use(b, n));

    /* the scales: in both the mean (14 and 96's 5 = 12: 13, a tie towards 93's: 14); 96's 7 never fights (0); 0 and 7: 0 */
    CHECK(val("Rocky Brawler", 2, "both") == 14, "Rocky Brawler");
    CHECK(val("Alexander Quickhands", 2, "both") == 0, "Alexander Quickhands");
    CHECK(val("Ivan Newcomer", 2, "96") == 14, "96's 6: 14");
    CHECK(val("Sven Shy", 6, "96") == 2, "96's 8: 2");
    CHECK(val("Gordon Grinder", 2, "93") == 10, "93 alone");
    CHECK(val("Pat Middle", 2, "both") == 4, "4 and 96's 11 (4): 4");
    /* the cartridge's cut spellings, short forms and accents */
    CHECK(val("Alexnder Quickhands", 2, "both") == 0, "a letter cut from the first name");
    CHECK(val("Rocky Brawlr", 2, "both") == 14, "a letter cut from the last name");
    CHECK(val("Gord Grinder", 2, "93") == 10, "Gord for Gordon");
    CHECK(val("Ivan Newc\xF3mer", 2, "96") == 14, "an accent");
    CHECK(val("Rocky Brawler-Smith", 2, "cart") == 2, "a longer last name is another player");
    /* another first name with the same last name is not him; nor a name that is not two words */
    CHECK(val("Jeff Lookalike", 4, "cart") == 2, "Jeff is neither Jim nor Jason");
    CHECK(val("Jim Lookalike", 4, "93") == 6, "Jim");
    CHECK(val("Madonna", 8, "cart") == 10, "one word: the cartridge's 8 on 93's scale (10)");
    /* in neither: the cartridge's value on 93's scale */
    CHECK(val("Nobody Atall", 0, "cart") == 0 && val("Nobody Atall", 4, "cart") == 2 && val("Nobody Atall", 12, "cart") == 12, "the cartridge's scale");
    /* in neither, with penalty minutes (fightrate_pim.inc): Dixon Ward's 82 in 70 games, 1.17 a game: 4 */
    CHECK(val("Dixon Ward", 0, "pim") == 4, "penalty minutes");

    /* hostile blobs: each refused, and the session's sources stay as they were */
    uint8_t h[1 << 16];
    const char *why;
#define REFUSED(sz) do { why = fightrate_session_use(h, (sz)); CHECK(why, "a hostile blob taken (line %d)", __LINE__); \
        CHECK(val("Rocky Brawler", 2, "both") == 14, "a refused blob changed the sources"); } while (0)
    memcpy(h, b, n); h[0] = 'X'; REFUSED(n);                                    /* not ours */
    for (size_t cut = 0; cut < n; cut += 7) { memcpy(h, b, n); REFUSED(cut); }  /* cut short anywhere */
    memcpy(h, b, n); h[n] = 0; REFUSED(n + 1);                                  /* bytes past the end */
    memcpy(h, b, n); h[3] = 0xFF; h[4] = 0xFF; REFUSED(n);                       /* 65535 names: more than FR_MAX, and past the end */
    memcpy(h, b, n); h[5] = 0; REFUSED(n);                                      /* a name of 0 bytes */
    memcpy(h, b, n); h[5] = 200; REFUSED(n);                                    /* a name longer than 40, past the end */
    memcpy(h, b, n); h[6] = 7; REFUSED(n);                                      /* a control character in a name */
    memcpy(h, b, n); h[5 + 1 + 13] = 15; REFUSED(n);                            /* 93's value odd, past 14 */
    memcpy(h, b, n); h[5 + 1 + 13] = 3; REFUSED(n);                             /* 93's value odd */
    {   size_t at96 = 3 + 2;                                                    /* (96's first value past 15) */
        for (int i = 0; i < 6; i++) at96 += 2 + strlen(s93[i].name);
        memcpy(h, b, n); h[at96 + 2 + 1 + strlen(s96[0].name)] = 16; REFUSED(n); }
    {   size_t big = 3 + 2 + 4097 * 3 + 2;                                      /* 4097 names of one letter: too many */
        memcpy(h, "FR1", 3); h[3] = 4097 & 0xFF; h[4] = 4097 >> 8;
        for (int i = 0; i < 4097; i++) { h[5 + 3 * i] = 1; h[6 + 3 * i] = 'a'; h[7 + 3 * i] = 0; }
        h[big - 2] = h[big - 1] = 0;
        REFUSED(big); }
    /* a peer's empty sources: everyone plays the cartridge's values on 93's scale */
    uint8_t z[7] = { 'F', 'R', '1', 0, 0, 0, 0 };
    CHECK(!fightrate_session_use(z, sizeof z), "empty sources refused");
    CHECK(val("Rocky Brawler", 8, "cart") == 10, "empty sources: the cartridge's");
    fightrate_session_clear();
    CHECK(val("Rocky Brawler", 8, "cart") == 10, "this machine's own (none read here): the cartridge's");

    printf(fails ? "fightratetest: %d failed\n" : "fightratetest ok\n", fails);
    return fails != 0;
}
