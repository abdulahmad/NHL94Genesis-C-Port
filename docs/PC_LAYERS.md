# PC pictures and the layer order

The PC options (`--pc-sprites`, `--pc-ref`, `--pc-logos`, `--pc-anthems`) draw NHL 95 PC's pictures over the cartridge's finished
picture (`native/pcspr.c`, `pcspr_present`). The game is not changed. This page says where each picture goes in depth,
so it is always above what it should be above and below what it should be below.

## The cartridge (NHL 94, Genesis VDP)

The VDP composes each pixel from these layers, back to front:

| # | Layer | What NHL 94 puts there |
|---|---|---|
| 1 | backdrop colour | black around the picture |
| 2 | plane B, low priority | the rink: ice, lines, circles, the centre ice crest |
| 3 | plane A, low priority | little in play |
| 4 | sprites, low priority | skaters, goalies, nets, puck and its shadow, the player arrows and markers. In the link order, the first entry is on top. `SprSort` / `setsortcords` write the 16 objects in `OOlist` order, so nearer objects come first |
| 5 | plane B, high priority | the near boards |
| 6 | plane A, high priority (and the window plane in its area) | the face-off and penalty windows (`fo_window`, `dobitmap` with `printa`'s priority bit), the PK/PP and "Period Over" banners, the clock and score boxes |
| 7 | sprites, high priority | the sprites whose attribute has bit 15 set |

The VDP also has line limits: 20 sprites and 320 pixels a line in H40, and a sprite at X 0 hides the rest of its line.

## The PC (NHL 95 PC, `Gameloop` / `DrawRinkOverlays`)

The PC draws each frame in this order: the rink zones, which carry the centre ice logo and its line. Then the penalty box markers,
`DrawRinkObjects`, the player markers and the puck. Then the 17 objects in `OOlist` order (far to near), with their jersey numbers.
Then the end boards art, the text overlay (`DrawTextOverlay`: the anthem's flag box and the face-off box), the call banner, and the
mouse pointer. The HUD panel is a separate strip.

## The combined model

Each PC picture takes the place of the cartridge picture it stands for:

| PC picture | Its place | So it is |
|---|---|---|
| centre ice logo and red line | plane B, the crest's cells | under every sprite, every high-priority tile, every window and box |
| skater, goalie, net | its own sprite's place in the link order, at that sprite's priority bit | behind the sprites before it (nearer objects, markers, arrows) and behind high tiles when it is low priority, but never cut to the cartridge sprite's shape |
| referee (`--pc-ref`) | a low-priority sprite, put in the link order by depth among the PC's skaters (in front of those farther up the screen, behind the nearer ones) | behind the windows, banners and boxes, and in depth order with the players |
| anthem flag box | over everything, like the PC's text overlay | on top |

How it is done: `layer_mask` copies the VDP and takes out the cartridge sprites that the PC pictures replace. It puts solid
pieces covering the PC picture's whole box into the copied sprite table at that place and priority, using tiles nothing else
uses (`free_tiles`). It then draws the lines twice, with the pieces in two colours. Where the two drawings differ, the piece shows,
so the PC picture shows there. The VDP itself decides what is in front, so every rule above, the window plane and the line limits
included, comes out the same as for the cartridge's own sprites. The cartridge sprite's leftover pixels are still given back their
background from a drawing without them.

The PC pictures are drawn back to front: the logo, then the skaters from the last listed to the first, with the referee at his
depth, then the flag box. The PC pictures in front of one another are layered by that drawing order, not by the masks.

## Fades

Colour RAM fades one component at a time (`cramfade`: a step of 2 to red, green and blue in turn, toward `palfadenew`). The PC
pictures get the same treatment, per component:

- Toward black, each component is lowered by the most that any colour has come down since the last steady palette.
- Up from black, each component is capped at the brightest that any colour has reached.

`forceblack` leaves colour RAM black with no fade running, and `palfadenew` still holds the colours to come back to. That counts
as black too. Nothing is drawn while the display is off or every colour is black. Before, the PC pictures flashed for one frame at
full brightness on the black screen before the penalty screen.

## Mirrored frames

The cartridge mirrors a skater's frame for half the directions, and the PC draws the same frames mirrored (`DrawFrameSprite`'s
flip). The jersey crest is drawn with colour numbers C0h-C4h, D0h-D4h, E0h-E4h and F0h-F4h. Each number is one column of the
crest, and the team's remap colours it. NHL 95 PC's `DrawSprite` (1CD73) swaps remap entries 0 and 4, and 1 and 3, of those four
groups while it draws a mirrored sprite, so the crest reads the right way round. `crest_flip` in pcspr.c does the same.
