/* Colour player photos (cardcolor.c): the maths, with no SDL and nothing of the game in it, so a check
 * program can run it alone. The player's own NHL 95 PC or NHL 94 PC portrait of the same photo gives the
 * colour; the cartridge's grey picture keeps its shape and most of its light (Abdul's choice, 2026-11-25:
 * "option 4", the rules in the plan doc's "Agreed method"). */
#ifndef CARDCOLOR_XFER_H
#define CARDCOLOR_XFER_H
#include <stdint.h>

#define CC_N 48                                   /* a card's picture: 48 x 48 */

enum { CC_PC95 = 1, CC_PC94 = 2 };

/* a card the table knows: the picture's hash (cc_hash of its tile data), the source and the
 * portrait in it, and where the portrait sits on the card (found once, offline) */
typedef struct cc_card {
    uint32_t hash;
    uint8_t src;                                  /* CC_PC95 or CC_PC94 */
    uint8_t file;                                 /* 95 PC: PORTR<file>.QFS */
    char tag[5];                                  /* the portrait's name in the bank ("0035") */
    uint8_t flip;                                 /* the card is the portrait mirrored */
    int8_t h, oy, ox;                             /* the portrait's height on the card, and its top-left */
    double a;                                     /* its width, against its own shape */
} cc_card;

/* a source picture: 8-bit pixels and their colours */
typedef struct cc_img {
    int w, h;
    uint8_t *px;                                  /* malloc'd */
    uint8_t pal[256][3];
} cc_img;

/* FNV-1a, 32 bits */
uint32_t cc_hash(const uint8_t *p, long n);

/* RefPack (10 FB / 11 FB): the unpacked bytes (malloc'd) and their count, NULL when it is not RefPack */
uint8_t *cc_refpack(const uint8_t *in, long n, long *out_n);
/* portrait tag of a 95 PC PORTRnnn.QFS (as read from the disc) with its own palette ("!" + tag[1..]): 1, 0 none */
int cc_qfs_shape(const uint8_t *file, long n, const char *tag, cc_img *out);
/* portrait tag of the 94 PC PORTRAIT.NSH (its "!pal" colours): 1, 0 none */
int cc_nsh_shape(const uint8_t *file, long n, const char *tag, cc_img *out);
void cc_img_free(cc_img *im);

/* the cartridge's picture: tile data (the count word, then 3 bytes a pixel row; pic + 8 in the ROM),
 * its 6 x 6 map (the width and height words, then the entries) and the photo palette's 16 colours
 * (Genesis words, big-endian) -> grey levels 0-252 (level x 36), row by row */
void cc_genesis(const uint8_t *tiles, const uint8_t *map, const uint8_t *pal, double g[CC_N * CC_N]);

/* the colour picture: rgb, row by row, 0-255. 1, or 0 when the portrait does not fit the card */
int cc_colour(const double g[CC_N * CC_N], const cc_img *src, const cc_card *c, uint8_t rgb[CC_N * CC_N * 3]);

#endif
