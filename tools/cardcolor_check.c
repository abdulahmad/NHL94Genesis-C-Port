/* Checks the colour player photos' maths (native/cardcolor_xfer.c) against the approved prototype's
 * pictures. Needs the player's own files, so it is not part of make test:
 *
 *   make build/cardcolor_check
 *   build/cardcolor_check ROM PC95_DIR PORTRAIT.NSH REF_DIR [OUT_DIR]
 *
 * REF_DIR holds the prototype's output: CardNNN.f32 (48 x 48 rgb floats), CardNNN.g.f32 (the grey
 * card) and starts.txt ("CardNNN offset" a line: the picture in the ROM). Prints each card's mean and
 * largest difference; fails when a card's mean is over 2 levels or the cards' over 1. OUT_DIR gets
 * CardNNN.ppm. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../native/cardcolor_xfer.h"

static const cc_card table[] = {
#include "../native/cardcolor_table.inc"
};

static uint8_t *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*n + 1);
    if (b && fread(b, 1, (size_t)*n, f) != (size_t)*n) { free(b); b = NULL; }
    fclose(f);
    return b;
}

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)be16(p) << 16 | be16(p + 2); }

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: %s ROM PC95_DIR PORTRAIT.NSH REF_DIR [OUT_DIR]\n", argv[0]); return 2; }
    long romn, nshn;
    uint8_t *rom = slurp(argv[1], &romn), *nsh = slurp(argv[3], &nshn);
    if (!rom || !nsh) { fprintf(stderr, "cannot read the ROM or the NSH\n"); return 2; }
    const uint32_t PicturePalette = 0xC63F8;              /* (the retail ROM's) */
    const uint8_t *pal = rom + PicturePalette + be32(rom + PicturePalette) + 0x20;
    const uint8_t *common = rom + PicturePalette + be32(rom + PicturePalette + 4);
    char path[1024], card[16];
    snprintf(path, sizeof path, "%s/starts.txt", argv[4]);
    FILE *sf = fopen(path, "r");
    if (!sf) { fprintf(stderr, "no %s\n", path); return 2; }
    long start;
    int cards = 0, bad = 0, gbad = 0;
    double total = 0;
    while (fscanf(sf, "%15s %ld", card, &start) == 2) {
        const uint8_t *pic = rom + start;
        long n = be16(pic + 8);
        uint32_t h = cc_hash(pic + 8, 2 + n * 24);
        const cc_card *c = NULL;
        for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) if (table[i].hash == h) c = &table[i];
        if (!c) { printf("%s: not in the table (hash %08X)\n", card, h); bad++; continue; }
        double g[CC_N * CC_N];
        uint32_t mo = be32(pic + 4);
        cc_genesis(pic + 8, mo ? pic + mo : common, pal, g);
        long rn;
        snprintf(path, sizeof path, "%s/%s.g.f32", argv[4], card);
        float *rg = (float *)slurp(path, &rn);
        double gd = 0;
        for (int i = 0; rg && i < CC_N * CC_N; i++) gd += fabs(g[i] - rg[i]);
        if (!rg || gd > 0) { printf("%s: the grey card differs (%g)\n", card, gd); gbad++; }
        free(rg);
        cc_img im;
        int ok;
        if (c->src == CC_PC95) {
            snprintf(path, sizeof path, "%s/PORTR%03d.QFS", argv[2], c->file);
            long qn;
            uint8_t *q = slurp(path, &qn);
            ok = q && cc_qfs_shape(q, qn, c->tag, &im);
            free(q);
        } else ok = cc_nsh_shape(nsh, nshn, c->tag, &im);
        if (!ok) { printf("%s: no portrait %s\n", card, c->tag); bad++; continue; }
        uint8_t out[CC_N * CC_N * 3];
        if (!cc_colour(g, &im, c, out)) { printf("%s: no colour\n", card); bad++; cc_img_free(&im); continue; }
        cc_img_free(&im);
        snprintf(path, sizeof path, "%s/%s.f32", argv[4], card);
        float *ref = (float *)slurp(path, &rn);
        if (!ref) { printf("%s: no reference\n", card); bad++; continue; }
        double sum = 0, mx = 0;
        for (int i = 0; i < CC_N * CC_N * 3; i++) {
            double d = fabs(out[i] - ref[i]);
            sum += d;
            if (d > mx) mx = d;
        }
        free(ref);
        double mean = sum / (CC_N * CC_N * 3);
        total += mean;
        cards++;
        printf("%s %-6s mean %5.2f  max %5.1f%s\n", card, c->src == CC_PC95 ? "95 PC" : "94 PC", mean, mx, mean > 2 ? "  TOO FAR" : "");
        if (mean > 2) bad++;
        if (argc > 5) {
            snprintf(path, sizeof path, "%s/%s.ppm", argv[5], card);
            FILE *o = fopen(path, "wb");
            if (o) { fprintf(o, "P6\n48 48\n255\n"); fwrite(out, 1, sizeof out, o); fclose(o); }
        }
    }
    fclose(sf);
    double avg = cards ? total / cards : 99;
    printf("%d cards, mean difference %.3f, %d off, %d grey cards differ\n", cards, avg, bad, gbad);
    return bad || gbad || avg > 1.0;
}
