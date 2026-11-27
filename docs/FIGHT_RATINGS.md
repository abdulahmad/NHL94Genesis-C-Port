# Fight ratings

The **Fight ratings** row on the overlay's Game page (rule `rules.fight_ratings`, `--rule fight_ratings=1`) decides who
fights, and how hard, when **Fighting** (NHL 93's fights, `mods/fight.c`) is on.

| Choice | What it does |
|---|---|
| Cartridge (off) | the 94 cartridge's own fighting values, as the cartridge has them |
| NHLPA 93 + NHL 96 | every skater's fighting on NHL 93's scale, from your NHLPA Hockey 93 cartridge and NHL 96 PC disc, by his name |

Presets: **Cartridge** and **Sega CD** use the cartridge's values; **Recommended** uses NHLPA 93 + NHL 96. With Fighting off the
row changes nothing at all, so Recommended plays the cartridge's game until Fighting is turned on. It is a game rule: online,
the host's choice and the host's files decide for everyone (below). Goalies always keep the cartridge's values.

It covers every roster set (the cartridge's, the Sega CD's, the 1993-94 sets and a roster folder of your own), because a skater is
found by his name, not by his place on a team.

## Why

NHL 93's fights start in SetInst: a skater starts one only when his fighting is at least `need = (40 - checks so far) / 4 + 10`
(10 to 20), and the other player's is at least 2 and at least 16 less twice the checks he has taken. Fighting is bits 1-3 of
each player's handed/fighting nibble: 0 to 14, even.

The 94 cartridge kept that nibble, but its values are a squeezed copy of NHLPA 93's: of its 619 skaters, 19 have 10 or more, so
almost nobody can start a fight. NHLPA 93 has 83 of 601. Matched player by player, the cartridge's values sit one or two steps
below NHLPA 93's (the cartridge's 8 is NHLPA 93's 10 at the median, its 4 is NHLPA 93's 2).

The ratings change only what the cartridge's fighting value changes with Fighting on: who fights (SetInst), how hard (assfight's
strength, energy x fighting), and the cartridge's own uses of the value (a check on a player with 0 counts three times toward the
fight count, a fighter's value sets how long a player he hurts is out) and the roster screen's Fighting column.

## The sources

Both are read from your own files when the program starts. No game's ratings are in this repository.

* **NHLPA Hockey 93** (Genesis, any revision): `NHL93_ROM`, or a 512 KB file with `93` in its name (.bin, .md, .gen, .smd)
  beside the program or in the folder it runs from, the same file the fights' art comes from (`native/fight93.c`). Its team table
  (TeamList: 26 long pointers to team records) is found by contents; each player is a String name and 8 bytes, and byte 4's low
  nibble is the handed/fighting nibble. Bits 1-3 are the rating, on the scale the fights mod plays by.
* **NHL 96 PC**: `NHL96_DISC`, or an image beside the program (`NHL96.iso`, `.cue`, `.bin`, `.img`; any image under that folder
  with `DATA/KEY.DB` and `DATA/ATT.DB`), or those two files copied to `DATA/` beside it. KEY.DB is 60 byte records (number,
  position letter, first name 16 bytes, last name 16 bytes, at +$24 the offset of the player's 21 bytes in ATT.DB); ATT.DB's byte 20
  is the rating 96 PC's SetInst and assfight read (`NHL96PC src/engine/collide/collide.asm` SetInst, `fight/fight.asm` assfight).
  Goalies (position G) are left out.

96 PC's rating is not on NHL 93's scale. 7 means the player never fights. Below 7 he is a fighter (SetInst draws from 15 for him,
from 10 for the rest) and the low 3 bits are his strength (assfight: `(rating & 7) + 8`); 8 and up he fights when pushed, with
strength `rating - 8`.

NHL 96 Genesis has no rating of its own (its fighting is aggression less 6), so it is not a source.

## The method

For each skater, by name:

1. In both games: the mean of NHLPA 93's value and NHL 96's (mapped, below), made even; a tie goes NHLPA 93's way, because
   NHLPA 93 is the same engine's scale and the season nearest the cartridge's rosters.
2. In one: that game's value (NHL 96's mapped).
3. In neither: his penalty minutes a game, from `native/fightrate_pim.inc` (public 1992-93 statistics), when he has some there.
4. Otherwise: the cartridge's own value, mapped onto NHL 93's scale.

### NHL 96 PC onto NHL 93's scale

| 96 PC | 0-4 | 5 | 6 | 7 | 8 | 9-11 | 12-15 |
|---|---|---|---|---|---|---|---|
| NHL 93 | 10 | 12 | 14 | 0 | 2 | 4 | 6 |

A 96 PC fighter can always start a fight on NHL 93's scale (10 and up), his strength setting 10, 12 or 14; one who never fights
gets 0, and a reluctant one 2 to 6 by strength. Checked against the 408 cartridge skaters in both games, the NHLPA 93 medians
are: 96's 5 and 6, 12; 4, 10; 7 and 8, 2 (7: 70 of 154 at 0); 9 to 11, 4; 12, 6. 96's 1 to 3 (38 players) have mixed NHLPA 93
values; they are fighters in 96 PC's own code, so they get 10.

### Penalty minutes onto NHL 93's scale

Fitted on the 340 players in both NHLPA 93 and NHL 96 PC with 20 or more games in NHL 96 PC's 1993-94 statistics
(`DATA/STATS94.DB`: games at +0, penalty minutes at +12, a word each): the median NHLPA 93 value in each band.

| PIM a game | under 0.25 | 0.25-1 | 1-1.5 | 1.5-2 | 2-3 | 3-4 | 4 and up |
|---|---|---|---|---|---|---|---|
| NHL 93 | 0 | 2 | 4 | 6 | 10 | 12 | 14 |

Under 20 games, the games short of 20 count as 1 minute each, so a few games do not decide it.

### The cartridge onto NHL 93's scale

The median NHLPA 93 value of the cartridge's skaters at each cartridge value (the players in both):

| Cartridge | 0 | 2 | 4 | 6 | 8 | 10 | 12 | 14 |
|---|---|---|---|---|---|---|---|---|
| NHL 93 | 0 | 2 | 2 | 6 | 10 | 12 | 12 | 14 |

### Names

The cartridge cut many names to fit (`Alexnder`, `Baumgartnr`, `Niedrmayer`, `Feathrston`). Names are compared as lower case
letters only (accents dropped; a hyphen or full stop splits words, an apostrophe is dropped), the first word against the first
name and the rest, run together, against the last:

* last name: the same (3), or one a subsequence of the other at three quarters of its length or more (2), or alike (1: twice the
  longest common subsequence over both lengths, 0.85 or more);
* first name: the same (3), a known short form (Gord / Gordon, Mike / Michael / Mikhail, Slava / Vyacheslav ...: 2), one a prefix
  of the other of 3 letters or more (2), a subsequence as above (2), or alike at 0.75 (1).

Both must match. The score is 4 x the last name's + the first name's; the best wins, and two different names at the best score
are no match (so Jim and Jason with one last name are never mixed up).

### The result

On the cartridge's roster set: 408 skaters are in both games, 69 only in NHLPA 93, 97 only in NHL 96 PC, 3 have penalty minutes
and 42 keep the cartridge's value (mapped). 72 can start a fight (10 and up), against the cartridge's 19. The Sega CD's set is the
cartridge's players less some, and comes out the same way (69 of 593).

`nhl94 --rom ROM --fight-ratings [@sega-cd | DIR]` prints every skater of a roster set: the cartridge's value, NHLPA 93's, NHL 96
PC's, the one played and where it came from (CSV). It reads your files, so keep its output out of the repository.

## Online play

Every machine of a session must play the same ratings, but the players' files can differ (one has no NHL 96 PC disc). The host
sends its sources with the session's settings (`native/net.c`, settings version 3): NHLPA 93's and NHL 96 PC's names and values
(`native/fightrate.c` fightrate_pack), and every machine, the host's too, rates by them for the session. What arrives is checked
before use (fightrate_session_use: at most 4096 names a game, each 1-40 bytes of text, NHLPA 93's values even 0-14, NHL 96's 0-15,
every length checked against what is left, nothing past the end) and a bad one ends the session. `make fightratetest` sends it
cut short, too long, with bad lengths, bad values and control characters. At the next power on outside a session each machine
uses its own files again.

## Adding penalty minutes

`native/fightrate_pim.inc` has a row `{ "First Last", games, penalty minutes }` for skaters in neither game: public 1992-93
regular season statistics (a traded player's season total). The 1993-94 roster sets add their own new players there.
