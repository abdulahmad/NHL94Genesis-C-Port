# The 1993-94 rosters

Besides the cartridge's own rosters (the 1992-93 trade deadline) and the Sega CD's (1992-93, after the
expansion draft), the game can play two more sets of real rosters:

- **1993-94 opening night**: each team as it was for its first games of the 1993-94 season.
- **1993-94 trade deadline**: each team as it was after the March 21, 1994 trade deadline.

They are an option. Off, the game plays the cartridge's rosters, as it always did.

## Choosing them

- **Game options > Rosters (restarts)**: cartridge, Sega CD, 1993-94 opening night or 1993-94 trade
  deadline. A change restarts the game.
- **Game options > Rosters in the game's menu**: puts a **Rosters** line second on the game's own setup
  screen, under Play Mode. Left and right on it change the rosters. The game restarts straight back to
  the setup screen, with no EA SPORTS, high score or title screens. It is on in the Recommended preset
  and off in the Cartridge and Sega CD presets. The line is not shown during online play.
- Command line, for development: `--rosters @1993-94-opening` or `--rosters @1993-94-deadline`, and
  `--check-rosters` on either.

## Who plays

The research is in `tools/rosters9394/research/`, one file per team, from Hockey-Reference's 1993-94
roster pages and box scores, plus the January 22, 1994 All-Star Game from Wikipedia (`ALLSTAR.csv`, for
the All-Star teams).

- **Opening night**: the men who dressed in the team's first 3 games.
- **Trade deadline**: the men who dressed in the team's first 3 games after March 21, 1994.
- Regulars who missed those games (hurt or scratched) are added, so long as they didn't dress for
  another team in the same games. A regular here played at least 40 games for the team, or at least 15
  at 0.8 points a game.
- Each team has at least 2 goalies, 6 defencemen and 9 forwards, and at most 26 men (3 goalies,
  10 defencemen, 15 forwards).
- Lines are built from points per game. Goalies' start chances follow their share of the games.
- Names longer than the game can show (19 letters) are shortened.

Anaheim and Florida play their first season. Minnesota is now Dallas (the cartridge's Minnesota slot).

## Ratings

- **Players on the cartridge** keep the cartridge's ratings and photo. About 80% of the players in
  each set are matched to the cartridge by name, allowing for different spellings and nicknames.
- **Players new to the game** take their ratings from **NHL 95 Genesis**, read from your own copy of
  that game. Put it beside `nhl94.exe` with "95" in its file name (`.bin`, `.md`, `.gen` or `.smd`), or
  name it with the `NHL95G_ROM` environment variable. The hand, checking and stickhandling ratings
  are not taken from it, because NHL 95 stores them differently and NHL 94 doesn't use the last two.
- **Players in neither game** (about 30 a set), and every new player when there is no NHL 95 ROM, get
  ratings from a formula. It uses the player's 1993-94 numbers (goals, assists, penalty minutes and
  plus/minus a game, games played, height and weight; for goalies their goals-against average, save
  percentage and games played). It was fitted to the NHL 95 Genesis ratings of the players both games
  have (`tools/rosters9394/fit.json`). The fighting rating is fitted to the cartridge's.

No rating, name list or photo from any cartridge is in this repository. `make.py` reads the cartridges
you give it only to match names and fit the formula.

## Photos

- **Players with a photo on the NHL 94 cartridge** keep it.
- **Anyone else with a photo in NHL 95 Genesis** gets that one, read from the same NHL 95 ROM as the ratings. NHL 95 has a
  photo of each team's six line 1 starters (145 players), and it is grey like the cartridge's photos. About 30 of the
  players new to these sets have one (Brodeur, Osgood, Arnott, Yashin, Gratton and others), plus returning players the
  cartridge had no photo of.
- **Everyone else** shows the cartridge's stock picture, as on the cartridge.

NHL 94 PC and NHL 95 PC were checked first (their KEY.DB names a portrait for each player who has one). Neither has a
portrait of any player new to these sets: they only have photos for their stars. NHL 95 SNES has colour photos of the same six
starters a team as NHL 95 Genesis.

## Saved games and online play

Each roster set keeps its own records file, like any other set. Saved states and online play check the
rosters. Because new players' ratings and photos come from the NHL 95 ROM when it is there, both players need the
same thing in their game folders: either both with an NHL 95 Genesis ROM or both without one.

## Making them again

```
python3 -I tools/rosters9394/make.py --cart <NHL 94 Genesis image> --rom95 <NHL 95 Genesis image>
```

This rewrites `tools/rosters9394/players.csv` (who plays where in each set, readable in a spreadsheet)
and `native/rosters9394.inc`. Without `--cart` and `--rom95` it uses the name columns and the fit that
are already there. After it runs, `touch native/rosters.c` so that `make` builds it again.

## Jerseys

Between 1992-93 and 1993-94 the only team whose jersey changed was Minnesota, which became Dallas.
Anaheim and Florida were new teams, which the cartridge already has. The cartridge's jerseys are
already 1993-94's, so both sets use them. St. Louis's new jersey came in 1994-95.

## Fight ratings

With **Fight ratings** on ([FIGHT_RATINGS.md](FIGHT_RATINGS.md)), a skater's fighting comes from NHLPA 93 and NHL 96 PC by his
name, so it covers these sets too. The 51 skaters in these sets who are in neither game have a row in
`native/fightrate_pim.inc` with their 1993-94 games and penalty minutes, the season that file's scale was fitted on. They were
listed with `nhl94 --fight-ratings @1993-94-opening` (and `@1993-94-deadline`): the rows whose "from" said cart.
