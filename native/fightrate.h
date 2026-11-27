/* Fighting ratings from NHLPA Hockey 93 and NHL 96 PC (RULE_FIGHT_RATINGS, native/fightrate.c, docs/FIGHT_RATINGS.md) */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

void fightrate_start(void);                   /* read the player's NHLPA 93 cartridge and NHL 96 PC disc (once), hook mods/fight.c */
int fightrate_found93(void);                  /* how many NHLPA 93 skaters were read (0: no cartridge) */
int fightrate_found96(void);                  /* how many NHL 96 PC skaters were read (0: no disc) */

/* a skater's rating on NHL 93's scale (0-14, even) from his name and the cartridge's value (cart, even); *how (when not NULL):
 * "both", "93", "96", "pim" or "cart" (where it came from) */
int fightrate_value(const char *name, int cart, const char **how);

/* netplay: the sources the session plays by are the host's. pack: this machine's, as bytes (malloc'd, *size); use: a peer's
 * (hostile: checked, refused whole with a reason); clear: this machine's own again */
uint8_t *fightrate_pack(size_t *size);
const char *fightrate_session_use(const uint8_t *b, size_t size);
void fightrate_session_clear(void);

/* --fight-ratings: every skater of the roster set now, his cartridge value, NHLPA 93's, NHL 96's and the one played, as CSV */
void fightrate_report(FILE *out);
