# ninety-four 🏒

> **Credits:** built on [NHL94Genesis](https://github.com/abdulahmad/NHL94Genesis) by abdulahmad, with chaos, StaticEck, McMarkis (Mhopkinsinc) and Drezz, and special thanks to smozoma and kingraph. See [Credits](#credits).

*NHL '94, rebuilt in C.*

**The greatest hockey game ever made, running natively on your machine, with no emulator in
sight.** All of the roughly 1,500 routines of EA's 1993 Sega Genesis classic have been rewritten
by hand in C and checked against the original, frame by frame, until the two can't be told
apart. The same one-timers, the same diving saves, the same organ, the same Gretzky. Smooth 60
frames a second by default; the cartridge's slowdown is an option (`--original`).

And things the cartridge never had:

- **Rollback netplay.** Two to four machines over the internet or a LAN, up to four players with
  EA's 4 Way Play, and rollback so no one waits on anyone's connection. Host from the menu, and
  the others join. Linux, Windows and Mac all play each other.
- **Sega CD options**: the changes the NHL '94 Sega CD made to the game, the disc's music, sound
  effects, opening screens, intro movie and player cards, each off until you turn it on. The
  game itself is the original, exactly.
- **NHL 95 PC options**: things NHL 95 PC did that the cartridge did not, built from your own copy of that game's files, each
  off until you turn it on. The first is the rink announcer; **White ice** and the **Hat trick hats** are others.
- **Save states**: eight slots with a picture each, the Autosave when you quit, and hold F4 to
  rewind the goal you just gave up.
- **All of it online.** The host's choices go to every machine. Seasons go online in a save state: anyone saves one, and the host loads it for everyone.

Hold F6 to fast-forward, slow the replay of that one-timer down, step through it a frame at a
time, and press F12 to keep it. Scored a beauty? **F10 saves the last 20 seconds** as a GIF to
share.

## Download

[The latest release](https://github.com/1nputs/ninety-four/releases/latest) has builds ready to
run:

- **Linux** (x86-64, glibc 2.34 or newer): `ninety-four-X-linux-x86_64.tar.gz`. It needs SDL2
  (`sudo apt install libsdl2-2.0-0` on Debian and Ubuntu).
- **Windows** (10 or newer, 64-bit): `ninety-four-X-windows-x86_64.zip`, with `SDL2.dll` inside.
- **macOS** (11 or newer, Apple Silicon or Intel): `ninety-four-X-macos-universal.zip`, one app.
  It is not signed by a registered Apple developer, so the first time, Control-click it and choose
  Open (its README says more).

Unpack it anywhere and run it. The first time, it asks for your ROM in a file browser and
remembers it: bring your own dump of the 1 MB retail cartridge (US/Europe). For online play,
every machine needs the same release.

Or build it from source and you're on the ice in a minute (what to install first is under
[Build](#build)):

```
git clone --recursive https://github.com/1nputs/ninety-four.git && cd ninety-four
make nhl94
./build/nhl94          # the first time, it asks for your ROM (or give it with --rom FILE)
```

**PortMaster / RG35XX:** zip layout and launch script are in [`portmaster/`](portmaster/). Handheld first launch is 60 Hz, overlay only, SDL_Renderer / KMSDRM (no desktop OpenGL). Copy your dump to `roms/nhl94.gen`. `./portmaster/pack.sh` builds `nhl94g.zip`. Private zip; the ROM is never included.

| | Player 1 | Player 2 |
|---|---|---|
| D-pad | arrows | I J K L |
| A / B / C | Z / X / C | Q / W / E |
| Start | Enter | Tab |

Game controllers work too (the first two are players 1 and 2).

| Key | |
|---|---|
| Esc / `` ` `` | overlay menu, open and close. Backspace backs out of a page |
| F12 | stats |
| Alt+Enter | full screen |

## Playing

### The menu

Esc, `` ` ``, or a controller's Back button opens it, and Esc or `` ` `` closes it. Backspace backs out of a page. Arrows and Enter, or the d-pad, A and B, work it. The window's menu bar (windowed desktop) changes the same video settings and does not list Resume or Quit.
The picture is Overbar's 4:3 dest of a 240-line canvas with the 224-line source unpadded (H40 PAR 1.0, H32 PAR 5:4).

- **Game ROM**: your cartridge dump, in a file browser.
- **Reset**, and **Save state** / **Load state**: eight slots, each with a picture of the
  moment, in `states/` beside the game. States are taken during play (not in the menus or a
  pause). Quitting saves the **Autosave** (where you were, in play), first on the Load page: the
  game always starts fresh, and loads it only when you choose it.
- **Video**: a pixel-art upscaler (Scale2x, Scale3x or MMPX, which smooth the staircases without
  blurring or adding colours, or xBR, which blends its edges smooth); sharp, integer or fit
  scaling; square pixels or 4:3; the colours' levels (standard, full range or the console's own);
  dithering blended as a television blended it; a picture tube's scanlines and its aperture
  grille, slot mask or shadow mask; full screen; and **Vsync**, so nothing tears (on unless you
  turn it off).
- **Capture**: the last 20 seconds are always kept. This page saves that replay, or records
  until you stop it. They go to `videos/` as GIFs (exact colours, 30 frames a second, no sound).
  The page sets how long the replay is and a GIF's size.
- **Sound**: volume, and the FM core and chip (hear the difference as you play).
- **Players**: which controller each player has, a controller's buttons (press the new one),
  turbo A / B / C, the stick's dead zone, and EA's **4 Way Play** for four players (as on the
  cartridge: set it, Reset, then choose Players: Three or Four in the game's setup screen).
- **Game rules**: the weight bug fix and the wide rink view (below).
- **Online play**: two to four machines, any players at each (below).
- **Options** and **Keys**: fast-forward speed, the FPS counter, and every key of the pads and
  the controls (pick one and press the new key, with Shift, Ctrl or Alt if you like).

All of it is kept in `settings.ini`, in the folder you run the game from, beside its saves; you
can edit it while the game is closed. The macOS app opened from the Finder keeps them in
`~/Library/Application Support/ninety-four`. On a Mac, hold Fn for the function keys.

### Sega CD options

The overlay's **Game** page has the changes the NHL '94 Sega CD made to the game, each one off
(the cartridge's way) until you turn it on. They were found in the disc's disassembly
([NHL94Genesis `docs/segacd_differences.md`](https://github.com/abdulahmad/NHL94Genesis/blob/main/docs/segacd_differences.md)).
Online, every machine plays by the host's.

- **Sega CD skating**: the crowd meter boost is added twice, so every player gets going about 2 to 5% quicker.
- **Sega CD corner bounce**: a checked player bounces off the corner boards almost anywhere in a corner (the disc's radius is about 8, the cartridge's 64).
- **Sega CD penalty shot box**: the pad does nothing while the PENALTY SHOT box is up (a direction only shortens it).
- **Sega CD playoff password**: a password's pad layout read as the disc reads it (which pads play which team in a continued playoff).
- **Sega CD shot data fix**: a new game clears only the shot buffer (the cartridge runs past it into the sound driver's tables).
- **Sega CD uniforms**: Hartford's home and Ottawa's away colours as the disc has them.
- **Sega CD sound choices**: the breakaway "ding" is sound 2, not 1 (in breakaway, asspuckc and assnearest), and a pass always makes the same sound (the cartridge picks one of four by the puck's height).
- **Sega CD song timing**: the home team's goal song starts at once (the cartridge waits 2 seconds), and the faceoff song late in a period starts when the faceoff is set up, not queued for the drop.
- **Sega CD period end**: the end of a period goes straight to the intermission song, without the cartridge's two frame waits and the sound cut between them.
- **Sega CD rosters (restarts)**: the disc's teams: 31 players fewer, with its goalie counts, forward counts and lines. It is a
  built-in roster set (`--rosters sega-cd`, `rules.rosters = sega-cd`), made from your cartridge, with records of its own; turning it on or off starts the console again.

The disc's sound needs your own rip of the NHL '94 Sega CD. Put it beside the program, in the same folder or up to three folders below. Any common rip works: a `.cue` with one file per track or one file for the whole disc, a CloneCD `.ccd`, or a data track `.iso` / `.bin` with numbered track files beside it ("Track 02.wav", "02 - Name.bin"). The game finds the disc by the files on its data track, so the file names don't matter. These rows change only what you hear on this machine, so each machine in an online game can set them differently:

- **Music**: *Sega CD* plays the disc's CD audio in place of the FM songs. The organ and goal songs (`$30`-`$77`) are tracks 7-78, played once. The title is track 4, intermission and end of game are track 5, and the scouting report is track 3, all looped. The sound effects stay as they are (see Sound effects). A song with no track on the disc plays on FM.
- **Crowd**: *cartridge* is the PSG noise crowd. *Sega CD* is the disc's crowd recording, track 2, looped at the disc's level whenever no CD song is playing during a game. *both* plays them together.
- **Sound effects**: *Sega CD* plays the disc's own effects in place of the cartridge's. The disc has its own effect player in MAIN.BIN, started with the cartridge's effect numbers: scripts of FM notes, through MAIN.BIN's own FM patches, and of samples on the CD's PCM chip from the bank in GRAPH2.BIN (the puck, passes, shots, boards, checks, the crowd's boos and cheers, the whistle). Each effect is rendered once, in the background, the first time the row is on, and then mixed into the sound while the cartridge's notes for that effect are kept off the chips. The game itself is unchanged. Off in online play.
- **Ron Barr scouting voice**: the disc's own scouting report, Ron Barr's whole spiel. "Hi, I'm Ron Barr for EA Sports", the home team's welcome, "Let's take a look at tonight's matchups", a write-up of each team, then each matchup's two players (centre, wings, defence, goalie), the matchup turning to them as he talks, then "Hit the start button ...". A and C turn the matchups any time, as on the cartridge, and when he starts on a player the report goes to that player's matchup. The text is the disc's (GRAPH2.BIN), and each paragraph waits for his voice to finish, as on the disc. *on* plays him with no song underneath, the way the disc does. *over the music* keeps the scouting song (FM, or the disc's track 3 with Music on Sega CD) playing under him. A paragraph's clip is `RON<line + 1>.PCM` by its line in the disc's text, plus one clip per team for the welcome. Not in online play (the cartridge's report there).
- **Opening screens**: *Sega CD* shows the disc's own opening in place of the cartridge's: its EA SPORTS logo, built up a piece at a time with its voice, its High Score Productions page with the logo's four sparkles, then (after the intro movie, when that is on) its license page over the goalie, before the title song starts, unless the Title page is the disc's too: then the license page comes once, after the disc's title, as the disc shows it while it loads the game. The license page moves on after 10 seconds, or at a button. The cartridge's screens still run underneath with their timing, and the disc's pictures fade in and out with them.
- **Title page**: *Sega CD* shows the disc's title page in place of the cartridge's, with the disc's own credits rolling up a page at a time with its timing (the first page 4 seconds, then a page about every 4 seconds, about a minute and a half in all), and then its license page, which moves on after 10 seconds or at a button. Start leaves the credits, as on the disc. The title song is the cartridge's, or the disc's track 4 with *Music: Sega CD*.
- **Intro movie**: *on* plays the disc's opening movie (`HOCKEY.SEG`, with its sound) between the High Score Productions page and the title, as the disc does. The game waits while it plays. Any key, button or click skips it. It plays once a run, and never in an online game. When the movie options want made files (the cached de-dither, 30 fps, colour) that aren't made yet, they start being made as soon as the disc is found, at the start of the opening, so they are usually well along by the time the movie comes. If they are still being made, the movie plays the frames made so far: it waits at its start ("Preparing the movie") only until the rest will be made before it gets there, and waits again if it ever catches up. It never plays ahead of what is made. The closing NHL HOCKEY logo (from frame 810) always shows the disc's own dithered pictures, because the de-dither blurs the texture the dither draws there, and it still gets 30 fps when that is on.
- **Movie de-dither**: the intro movie's dithered colour (the checkerboards the Sega CD drew for colours between its palette's) evened out. *live* does it frame by frame. *cached* makes a better 2x version once from your own disc, on a second thread, and keeps it as `segacd-intro.dedither` beside the program; it plays live until that is made. Left and right change it while the movie plays. Overbar's frame post-process (`docs/POSTFX.md` there).
- **Movie frame rate**: *30 fps* puts two pictures between each pair of the disc's 10 a second, drawn by Overbar's frame generation (the RIFE 4.6 model, MIT, in `rife-v4.6/` beside the program, run on the CPU). They are made once from your own disc, on a second thread, from the cached de-dither's pictures, and kept as `segacd-intro.smooth` beside the program (the cached de-dither is made in the same pass). That takes a few minutes, less on more cores, and the movie plays at its own rate until it is done. A cut between two shots is held, not blended. Needs the de-dither *live* or *cached*.
- **Movie colour**: *enhanced* gives each shot of the intro its own correction (black and white points, grey balance and saturation) worked out from its own pictures, so ice, skin and team colours look natural. The table takes a few seconds to make and is kept as `segacd-intro.colour` (about a kilobyte). It is applied live.
- **Movie 50/50 demo**: *50/50 split* plays the intro movie with the disc's own picture on the left half, at its own 10 a second, and the enhanced one on the right half: the cached de-dither, the colour and 30 fps, whatever those three rows say. A line divides the halves, labelled *Original* and *Enhanced*. The right half plays at the disc's rate until the 30 fps pictures are made. Off in every preset.
- **Movie scaling**: *same as the game*, or *its own*: the intro movie then uses its own look, set on the overlay's **Scaling (Movie)** page (the whole Scaling page: Scale Mode and its options, CRT TV colors, Picture height, Monitor), so the low-quality movie can be filtered harder than the game. It starts as a soft CRT. Its Sharp list also has *1x (smooth)*: the movie goes straight to the window size with no pixel doubling first, so it looks smooth. The movie keeps its shape (never stretched). Recommended uses *its own*; Cartridge and Sega CD use *same as the game*. The two rows are one setting.
- **Player photos**: *Sega CD* shows the disc's photos on the player cards. The disc has 211 of them, each team's starting six plus the All-Stars, and matches them to players by team and name, so a roster set's players get theirs too. Almost all are the cartridge's own pictures. Only Jari Kurri, Rick Tocchet and Ken Wregget get a photo where the cartridge shows a stock picture. Never in an online game.
- **Career stats**: *B on a player card* opens the disc's own player card screen, rebuilt from its data and its layout code (PLAYER.BIN): the team's page with his photo in the frame (or the disc's stock photo), his name, size, birthplace and birthday, his position and number in the disc's big letters, and his career seven seasons and three columns at a time under "Regular" or "Playoff". Up and down scroll the seasons, left and right the columns; Enter, Space or A turns to the team's card (its arena photo, name and arena, and the six advantage lines), as does B on a team's card, and then to the team's video card, which plays the disc's own clips of that team (up to 13, with their sound, in the TV frame; left and right pick the clip, up plays it again; de-dithered when FMV de-dither is on). Enter again goes back to the player. Esc, Backspace or a click closes it. Every one of the cartridge's 684 players has a career; a roster set's own teams get a plain page with the same words. Never in an online game.
- **Colour player photos**: the cartridge's grey photos on the player cards and the scouting report shown in colour, from the same photo in your own NHL 95 PC or NHL 94 PC files. Put NHL 95 PC's disc image (or its `PORTR000.QFS` to `PORTR023.QFS`) or NHL 94 PC's `PORTRAIT.NSH` (or its disc image) beside the program; nothing of either game is shipped. 113 of the cartridge's photos are the same photo in one of them (93 in NHL 95 PC); the card keeps the cartridge's picture and most of its light and takes the PC photo's colour. A photo neither game has stays grey. It is drawn over the picture you see, so the game, its saves and online play are the same either way. On in *Recommended*, off in *Cartridge* and *Sega CD*; the row says so when no files were found. `tools/cardcolor_check.c` checks the maths against the prototype's pictures. Headless: `--pc-photos` (the folder `NHL94_PC_PORTRAITS` names is looked in first).
- Headless: `--segacd-music`, `--segacd-crowd 0|1|2` and `--segacd-voice 0|1|2` mix the disc into `--wav`. `--segacd-screens` and `--segacd-title` put the opening and title pages into `--shot` / `--shots`, and `--segacd-photos` the player cards' photos. The golden lines hash the game's own sound, so they don't change.

### NHL 95 PC options

The **Game** page also has what NHL 95 PC added. None of it is in the game's own state, so replays, saved states and online play are the same with it
on or off, and each machine in an online game can set it for itself. The sounds come from your own NHL 95 PC: nothing of it is in this repository or a release.

- **Rink announcer**: the arena announcer of NHL 95 PC, which has the expansion teams (Anaheim and Florida) as well as the other 24.
  It says, in 95 PC's words and with its voice: a goal ("Los Angeles goal number 99, assisted by number 4, and number 7"), a penalty
  ("Montreal penalty on number 12, 2 minutes, hooking", "and number 14" for another of the same team's stoppage, then "at 12 minutes 30
  seconds" after the last), a penalty shot ("Toronto penalty shot number 5, at 8 minutes 2 seconds"), "one minute left" (when the clock
  reaches 1:00 in a period) and, at the end of a game, the three stars ("first star: Los Angeles, number 99"). A goal or a penalty shot
  takes over from what is being said; a "one minute left" waits for a free voice, as in 95 PC. The announcer is quiet in the demo.
  The clips are the rink calls of `XBRUCE2.VIV` (the ones for numbers, teams, penalties and the calls themselves). Put the file beside the
  program (up to three folders below), loose or on a rip of the NHL 95 PC disc (`.cue`, `.ccd` or `.iso` / `.bin`): the game looks in the disc images
  too. Without it the row says "(no XBRUCE2.VIV found)". The clip rate is 11025 Hz.
- **White ice**: the rink drawn white, as on the PC games, in place of the cartridge's pale cyan (and the glass's sheen in white). It needs no
  files from anywhere and changes only the picture, in the 4:3 and the wide rink; menus and the opening film keep their colours. Off in every
  preset (a taste). Headless: `--white-ice`.
- **Hat trick hats**: as on NHL 94 SNES, a home skater's third goal sends hats flying onto the ice from both sides, tumbling, landing and sliding
  to a stop with a shadow each, until the next face off. The hats are the SNES game's own sprites in the home team's colours, read from your own
  `NHL '94 (USA).sfc` (or `.smc`) put beside the program (up to three folders below); without it the row says "(no NHL 94 SNES ROM found)". Nothing
  of the game changes (the hats have a random generator of their own), so states, replays and online play are the same with it on or off. Headless:
  `--hats ROM`; `HATS_DEMO=1` starts the hats at once, for looking at them.
- **National anthems**: before the first face off the visitors' anthem is played and then the home team's when it is the other country ("O Canada" for CGY EDM MTL OTT QUE TOR VAN WPG, the "Star Spangled Banner" for the others), NHL 95 PC's own songs (`USA.KMS`, `CANADA.KMS` and the FM timbres `PCFF004.PAT`/`PCFF00n.TIM`, from the same disc image) played by a C sequencer on Nuked OPL3 (`native/nuked_opl3.c`, LGPL 2.1+, licence in `native/LICENSE-nuked-opl3`). The face off waits for them and the cartridge's own songs stay quiet meanwhile. As on 95 PC the players stand in two lines facing the flag, the referee waits at the side and the flag (`UFLAG.PPV`/`CFLAG.PPV`, from the disc image, waving) is on the ice while the song plays; then the players and the referee skate to the face off spot, the face off window comes up and the usual time to the drop runs. The players' places are the game's own objects, so every copy of the game agrees. Game page row "National anthems"; `--pc-anthems`, `--pc-anthem-wav FILE` writes both as a wav.
- **Centre ice logo**: NHL 95 PC's logo of the home team (its `<TEAM>.MAP`/`.TIL` and `RINK.PAL`, from the same disc image) in place of the cartridge's crest. The cartridge's crest is taken out of the picture, the PC's drawn over the rink only (windows, scoreboard and skaters stay over it), and the ice, circle and centre line baked into the PC's tiles are dropped so the logo adapts to the cyan or white ice. Dallas uses the PC's Minnesota logo; the All-Star teams keep the cartridge's. The row also offers **NHL 94 PC**: its logos (the two crests split by the centre line, `<TEAM>.MAP`/`.TIL`, `RINKPAL.QPP`) from an NHL 94 PC folder beside the program (`NHL94PC`, or `NHL94PC_DIR`), with NHL 95's logo for the teams 94 lacks (Florida, Anaheim). Game page row "Centre ice logo"; `--pc-logos`, `--pc-logos94`.
- **Referee on the ice**: NHL 95 PC's referee (its frames, from the same files as the PC player sprites) skates beside the play like the PC's, stands at the face off spot facing the dot and drops the puck. Visual only: the game does not know of it. It is drawn under whatever the cartridge puts over the ice (windows, scoreboard, outlines) and behind skaters nearer the camera. Command line: `--pc-ref`.
- **Referee physics** (Game page row: off, visual only, full; visual only in Recommended, off in Cartridge and Sega CD). *Visual only*: skaters who run into the referee on the ice bump him aside (NHL 95 PC's velocity exchange), only in the picture: play is unchanged. *Full*: NHL 95 PC's referee in play (a game rule, `rules.referee_in_play`, the host's online, off in every preset): he is the game's own object (`mods/referee.c`, kept in every saved state and rollback, the same on every machine whether or not it has the PC files), skaters bounce off him (checkcx: weight 7 against the skater's, the mover keeps his place), the puck bounces off him (puckbody, never touched), a pad player's check that lands knocks him down (FallDown, 48 frames), and every third such check is a penalty on the checker: 95 PC's 16h, Abuse of Official, here Roughing (the cartridge has no abuse of officials penalty), called, shown and announced as any other. Drawn whenever it is on. Headless: `--rule referee_in_play=on` (`REFEREE_LOG=1` logs each bump, bounce, knock down and penalty); `--pc-ref-bumps` the visual only one.
- **PC player sprites**: draws NHL 95 PC's skaters and goalies in place of the cartridge's, in the teams' PC colours, from the player's own
  files (`000_049.PPV` ... `11001149.PPV`, `RINKPAL.QFS`, `HOMEPALS.BIN` and `AWAYPALS.BIN`, beside the program or on the NHL 95 PC disc image).
  PC frame numbers are the cartridge's less one (less nine from the siren frames on), so where the PC has no matching frame (the cartridge's extra goalie
  frames) the cartridge's sprite stays. The game is untouched, only the sprite table: states, replays and online play are the same on or off.
  Headless: `--pc-sprites`.
- **Rink announcer: when he speaks** (a game rule: the host's choice holds for everyone online; on by default and in Recommended; turning the announcer on moves "talks over the face off" to "right after the goal"): *right after the goal* the
  speech starts at the goal, penalty or penalty shot as on NHL 95 PC, and play waits until the announcer has had time to say it (fixed lengths, the
  same on every machine: about 6 s for a goal with an assist, 6 s a penalty, 7.5 s a penalty shot, with the time the replay and the face off set
  up already take counted), then the puck drops. *After the goal song*: a home goal's speech waits for the goal song to end, and the face off waits
  for both (the siren first: the cartridge's, or with the PC goal siren NHL 95 PC's fixed 7 s, the `goal_siren` rule the siren row sets
  from the file the host has, so every machine of a session waits the same); away goals and penalties have no song, so the speech is after the siren. Headless: `--rule announce_timing="right after the goal"`. "talks over the face off" is the cartridge pace; the celebration then does not wait for the speech.
- **PC goal siren and PC end of period horn** (host rows, on in Recommended): NHL 95 PC's goal siren (its sound $9C, 7 s) and period end horn
  ($91, 2.6 s) play in place of the cartridge's, from the player's own `PCFF001.DIG` (beside the program or on the NHL 95 PC disc image). The game is
  untouched, only the sound: the cartridge's sound still plays in the game (its sound RAM), only its notes are kept from the chips; without the file
  the cartridge's sounds are heard. At one minute left the PC says "one minute left" with the rink announcer on
  (the cartridge's beep is not heard), else sounds its one minute horn ($97) with the horn row on. Headless: `--pc-sfx PCFF001.DIG --wav out.wav`
  (and `--rule goal_siren=1`), or `--pc-siren` for the files found beside the program as the window finds them.
- **PC between-play pause** (a game rule, off in every preset): the same waits after a goal, a penalty or a penalty shot even without the announcer,
  as the PC game stops play. At every whistle the skaters and the referee also skate from where they were to the face off spot (the game's own
  setup has already placed them, so it is the same for every copy of the game) instead of appearing there (a player replaced by a line change leaves by the gate to the bench and the next one comes in by it; the referee carries the puck to the dot), and the drop waits until they have arrived.
  Headless: `--rule hold_play=1`.
- **Fighting**: fights in the style of NHL '93, ported from its code. The row has four choices. *NHL 93*: a hard check between two players with
  fighting ratings starts a fight (the 93 odds: fighting ratings, checks taken, how long since the last fight). *NHL 92*: the 92 roll, the two
  players' aggression plus how hard the hit was, so any hard hit between aggressive players can start one. *Hybrid*: 92's aggression roll, but only
  between players whose fighting rating is at least 2. When a fight starts both drop their gloves, throw high and low punches or grab (human
  players punch with the buttons, on any of the four pads), one goes down, maybe injured, and play restarts at a face off. A game rule (the host's
  choice holds for everyone online), off in every preset. Headless: `--rule fighting=off|"NHL 93"|"NHL 92"|hybrid`. The 94 cartridge has no
  fight art (its fight frames are blank): put your own NHLPA Hockey 93 cartridge (512 KB, a name with "93" in it, .bin/.md/.gen) beside the
  program, or name it with `NHL93_ROM`, and the fighters are drawn with 93's own fight frames (also with PC sprites on: NHL 95 PC has no
  fights). Without it they are drawn with the 94 cartridge's holding, hooking and falling frames. `HOLDTEST=fight:FRAME` forces a fight.
- Headless test: `HOLDTEST=goal:FRAME,pen:FRAME` (with `--announcer FILE`, optional `HOLDPEN=30` for hooking) forces a goal or a penalty at that vblank and logs each change of the stoppage wait (`ANNOUNCER_LOG=1` the clips); the frames are the game's own vblanks, a 30 second period lasts about 1000.
- Headless: `--announcer FILE` plays the announcer from that `XBRUCE2.VIV` into `--wav`; `ANNOUNCER_LOG=1` prints each clip said.

### Game rules

A few things the cartridge never offered, for every player and the whole game. They are set
before a game starts (a change made during one shows from the next Reset), and a saved state
keeps its game's rules. Off, the game is the original exactly.

- **Weight bug fix**: the original sums a check's weights in a byte that wraps, so a heavy
  skater checked by a lighter player falls easily. On, the weights are summed in full.
- **Rink view**: 320 wide instead of the cartridge's 256, showing the whole width of the ice with
  the camera moving less, the scoreboard and overlays in the middle (the pause screen stays 256
  wide, as on the cartridge).

`settings.ini` takes `rules.weight_fix = on` and `rules.view = wide`; `--rule weight_fix=on` and
`--rule view=wide` take the same.

### Online play

Two to four machines, over the internet or a LAN, with rollback: each game plays its own players'
buttons at once and guesses the others', and when theirs arrive different it plays the last few
frames again with them before drawing the next, so no one waits. The menu's **Online play** page:
**Name**; **Role**: host, join, or (two machines) both connect at once (most home routers let
that through without a forwarded port); the address and the port; this machine's LAN address;
the input delay (frames between pressing and the game seeing it, 2 by default: more means fewer
rollbacks over a long way); **Players here** (how many play at this machine: its players 1, 2 and
3); and, for the host, **Total players**. The host waits until they are all in, then the game
powers on everywhere: the host's players on pads 1 onward, then each machine's as it joined. Two
machines with a player each, choose Players: Two - Head to Head (or Two - Teammates) on the setup
screen; four machines with one each, two with two, three with two and one and one, one and three:
with pads 3 or 4 played EA's 4 Way Play is in the ports on every machine (Players: Three or Four).
If anyone leaves, the game is over for everyone, each told who left. From the command line:
`--host [PORT]`, `--join ADDR[:PORT]`, `--peer ADDR[:PORT]` (two machines both connecting,
`--port` your own), `--delay N`, `--name NAME`, `--here N`, `--players N` (the host's).

The game is the host's: its Game rules page's choices go to every other machine
before the game starts, rosters as their package (the NHL 2026 edition's, every photo included, is
about 1.7 MB), so no one else needs a copy of them. Every game online starts from a new
cartridge (default lines, no records): every machine must play the same save RAM, or the games
would differ at once, so no one's save file is read for it or written by it, during the session or
after it while the game plays on alone. Seasons, lines and records go online in a state instead:
anyone's **Save state** saves one every machine has played (all its buttons known), and the
host's **Load state** loads one for everyone, at the same frame on every machine, the game and its
save RAM as they were saved (a state of other rosters than the session's is refused, saying so;
a guest's Load state says only the host can). So a season played alone goes online by saving a
state, hosting, and loading it; and closing the window in a session saves the Autosave as ever.
While the session lasts every machine's Game rules page shows the host's choices,
greyed (their own come back after it), and the host's rosters stay on the others' Rosters pages
until they quit. The window's pause, slow motion, fast-forward and rewind are not to be had while
you play together, and only the host can reset: the menu's **Reset** (chosen twice) powers every
machine's console on again at the same frame, the game from its start for everyone and the
session going on, the save RAM as it is then (the cartridge's battery keeps it). Every machine
needs the same build of the game and the US/Europe cartridge, or it is refused.

## Rosters

The game plays the cartridge's own rosters. The menu has no Rosters page: there are no roster sets
of your own to choose (the Sega CD's teams are the overlay's **Sega CD rosters** row). The tools
that make roster sets (`--export-rosters`, `--extract-rom`, `--check-rosters`, `--pack-rosters`,
and `--rosters` on the command line) are kept for development; [docs/ROSTERS.md](docs/ROSTERS.md)
describes the files.

## Build

The hardware side is [pulseman](https://github.com/1nputs/pulseman), our Mega Drive runtime,
included as a git submodule in `ext/pulseman` (the upstream commit, unchanged). The host menu,
overlay and scaler are [Overbar](https://github.com/abdulahmad/overbar), the git submodule
`native/overbar`. Clone with `--recursive`, or after a plain clone run `git submodule update
--init`. `make nhl94` builds both. The window, sound and input side is Overbar's platform layer
(`native/overbar/src/plat.c`, `plat_pads.c`; pulseman's own SDL2 front end is not used), so pulseman
itself is not changed. Besides that you need GCC (C and
C++), `make`, `pkg-config` and SDL2. The game builds with Clang as well and plays bit for bit
the same: `make golden-cross` builds it and pulseman with Clang (`XCC=` and `XCXX=` name it, or
`ZIG=path/to/zig` takes Zig's) and checks its golden runs against the GCC build's (Clang 22 here:
every run identical). The builds made and tested here use GCC. Your saves go to `out/` in this
repository, and your states to `states/`.

**Linux** (built and tested here, Debian 13, GCC 14):

```
sudo apt install build-essential pkg-config libsdl2-dev          # Debian, Ubuntu
sudo dnf install gcc gcc-c++ make pkgconf-pkg-config SDL2-devel  # Fedora
sudo pacman -S --needed base-devel sdl2                          # Arch
make nhl94
./build/nhl94 --rom "/path/to/NHL 94.gen"
```

**macOS** (the download is cross-built here with Zig's Clang for Apple Silicon and Intel, and
played on a Mac, online too; building on a Mac itself is not yet tried). To build it there,
install GCC from [Homebrew](https://brew.sh) and name it (Apple's `gcc` is really Clang):

```
brew install gcc sdl2 pkg-config
make CC=gcc-15 CXX=g++-15 nhl94          # the version brew installed: ls $(brew --prefix)/bin/gcc-*
./build/nhl94 --rom "/path/to/NHL 94.gen"
```

**Windows** (cross-built here with MinGW-w64 GCC 14 without a warning, and run on Windows),
with [MSYS2](https://www.msys2.org), in its *UCRT64* shell:

```
pacman -S --needed git make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2
make CC=gcc CXX=g++ nhl94
./build/nhl94.exe --rom "C:/path/to/NHL 94.gen"
```

The C++ runtime is linked in, so outside the MSYS2 shell `build/nhl94.exe` needs only
`SDL2.dll` beside it (`cp /ucrt64/bin/SDL2.dll build/`).

If a compiler newer than these warns about something, `make WERROR= nhl94` builds anyway; please
tell us what it said.

`make release VERSION=X` makes the downloads for all three from clean at the commit checked out
(`tools/release.sh`, which says what it needs: MinGW-w64, Zig, SDL2's macOS framework, llvm-lipo,
rcodesign):
`ninety-four-X-linux-x86_64.tar.gz`, `ninety-four-X-windows-x86_64.zip` and
`ninety-four-X-macos-universal.zip`, a `ninety-four.app` for macOS 11 or newer, signed ad hoc as
a whole under its bundle identifier, so macOS can ask it for the local network.

## For agents: where this stands (21 October 2026)

- **What it is.** A hand-written C reimplementation of NHL '94 (Genesis, US/Europe retail, SHA-1
  `7b1489ab501258133bf03e5f0892b77d04312580`). `src/` has one C file per asm module of the
  community's byte-exact rebuild (34 modules, about 1,500 routines, the same names). The Z80's
  661-byte sound relay is ported too (`src/z80.c`). It runs on **pulseman**
  ([1nputs/pulseman](https://github.com/1nputs/pulseman), MIT, the git submodule `ext/pulseman`;
  its C API keeps the `mdrt_` prefix): the VDP, PSG, YM2612, pads and save RAM as C modules,
  with an SDL2 platform (here, Overbar's platform layer in `native/overbar`). There is no 68k or Z80
  emulation. A change to pulseman's core is committed and pushed there, then the submodule's new
  commit is committed here; host changes go into Overbar or `native/`.
- **How it is verified.** Genesis Plus GX with a hook (the oracle, a development tool only)
  records real play. `make test` replays 10,276 recorded routine calls. `make lockstep` runs 18
  whole sessions against the oracle, checking work RAM and VDP at every vblank. `make
  golden-check` compares the native build's own output with a recorded build, bit for bit (20
  runs, about 227,000 frames, the rules' and rosters' own among them), plus save-state round
  trips. `make golden-cross` builds the game and pulseman with Clang and checks the same runs
  against it: the game plays bit for bit the same whatever compiles it, as netplay between
  builds needs; `make golden-ubsan` checks them with the undefined behaviour sanitizer. `make
  netplay-check` plays nine sessions from power on (headless, over the loopback through a
  simulated network: delay, jitter, loss, duplicates; the host's rosters and rules
  sent; input delay 0; two to four machines, 4 Way Play: two each, one each on four, two and
  one and one, one and three; the host's reset; a state with records loaded for everyone), every
  machine's every frame as a solo run of the same buttons. `make rulestest` checks the game rules on the game's own routines (and the Rosters page's
  ratings against the cards' for every player). `make rosterstest` checks the rosters: the
  cartridge's exported and built again, its tables and art the cartridge's; a 32-team one; 22 broken ones refused; and
  with hack ROMs in `../roms` (or `HACKS=`), each extracted, built to its teams, colours and art
  as the hack has them, played and packed. pulseman is checked on the oracle's vectors (`make
  runtime-check`) and on this game's own recordings (`make runtime-selfcheck`).
- **Done.** The harness, the game-play core, video and sound, the frame drivers, native running,
  lockstep, the whole front end (it boots from power on), a cleanup, golden runs, native save
  states and rewind. Then the game rules (`src/rules.h`: numbers, Classic the original's, kept in
  states) and rosters (`src/rosters.h`'s tables; `native/rosters.c` reads and builds them,
  `native/romsrc.c` reads hacks, `native/art.c` the pictures, `docs/ROSTERS.md` the format), the
  Rosters and Game rules pages. Rollback netplay (`native/net.c`: Overbar's `ob_net` and its console
  features, resuming at the game loop's wait as a state does; `--host`, `--join`, `--peer` headless,
  the window's Online page (the console powers on again when the match begins; on screen check
  to come); the host's rosters and rules sent; two to four machines, any
  players at each, up to four; the host's reset and its state loaded for everyone on the same
  frame; a desync goes on from the host's state). The downloads (`make release`, `tools/release.sh`: Linux, Windows, a universal macOS
  app). In pulseman: the console's state as a value, a client-owned clock
  with line-by-line drawing and an H-interrupt hook, a bus map, FM cores and models chosen at run
  time, a trace recorder, PNG and zip, the client's option pages, and vsync.
- **Native state** (the owner's direction): the game's state is native structs (the 68000's
  offsets, the host's byte order, any compiler), generated with the address map by
  `tools/gen_ram.py`: the team structs, the night's games, the replay frames, the sound driver's
  slots, channels and voices, the sort objects and their drawn parts (the pads, the arrows, the
  replay camera), the menus, the faceoff, the plane maps, the clock and the rest; byte buffers in
  the machine's own formats (Strings, the sprite table, the DMA list, records) are host bytes
  (`ram_bytes`). The console's big-endian view is made on demand (`src/mem.h`, `src/ram.c`'s map,
  `ram_pack`/`ram_unpack` for the checks and states); what still goes by 68000 address is listed
  at the top of `src/mem.h`. The rosters are native tables too (`src/rosters.h`'s `roster_team`: read
  off the cartridge for its own rosters, made from the rosters' folder for another); its art stays in
  the cartridge's formats, read by number (the cartridge's, or the rosters' own past it), and the art
  map (`native/art.c`) says what every number is: `--art-map` prints it, and every golden run checks
  that each byte the VDP takes by DMA is in a piece of it (`--art-check`), for texture packs later.
- **Deferred by the owner.** WebAssembly (the game's code compiles for it now; a browser platform
  for pulseman is the job). The cartridge's slowdown is an option (`--original`); the 68k cost
  model is size-weighted from lockstep traces (`native/costs.inc`). Lockstep lag-check in Original
  is parked (play hashes still fail; Smooth still resyncs).
- **Read first.** `ext/pulseman/README.md` and `docs/API.md` there; the comments at the top
  of `tools/oracle/vectors.py`, `tools/golden.py` and `tools/hybrid/oracle_shim.c`. The working
  notes (the handoff guide, the plan, the playbook for another game) are kept outside the
  repository.
- **Rules.** The ROM and everything made from it (states, traces, captures, screenshots, GIFs
  such as `tools/goal_gif.py` makes) stay out of git. Commit after each finished job.
  Before and after a change: `make test`, `make lockstep`, `make golden-check`; for a runtime
  change, also `make runtime-check` and `make runtime-selfcheck`.

```
./tools/fetch_refs.sh && tools/asm/build_vasm.sh     # references, the oracle core, the byte-exact rebuild
cp /path/to/rom "NHL 94 (UE) [!].gen"                # your ROM at the repo root
make oracle && make vectors                          # the oracle; every state and vector (tools/oracle/vectors.py)
tools/hybrid/capture_all.sh                          # the 29 capture sessions (~15 min, 3.6 GB)
make test && make lockstep && make golden-check      # the checks
```

## Features

From the least ambitious to the most.

- [x] The whole game from power on: exhibition, playoffs, shootouts, user records, every menu and screen
- [x] FM and PSG music and effects, the Z80 sound relay ported to C
- [x] Keyboard and two game controllers
- [x] Window scaling, full screen, 4:3 aspect
- [x] Battery save (save RAM) kept in a file
- [x] Smooth 60 frames a second (no cartridge slowdown)
- [x] Save states (the menu's slots) and rewind (F4)
- [x] FM core and chip model choice (`--fm-core nuked|ymfm`, `--fm-model ym2612|ym3438`)
- [x] Headless runs: screenshots, WAV, scripted input, RAM watch (`--watch`), trace recording (`--record`)
- [x] Mute, volume, audio device and latency options
- [x] A settings file (window, scaling, volume, keys), every key rebindable
- [x] A menu: the ROM in a file browser, reset, video, sound and the FM chip live, key rebinding
- [x] Integer and sharp-bilinear scaling, hi-DPI
- [x] Pixel-art upscalers: Scale2x, Scale3x, MMPX 2x and 4x, xBR 2x, 3x and 4x
- [x] Screenshot key (PNG)
- [x] Fast-forward, slow motion, frame step
- [x] Controller remapping, player slots, turbo, dead zones
- [x] Capture: the replay (the last 20 seconds) and recordings, as GIFs
- [x] Save slots with thumbnails; the Autosave on quit (states during play)
- [x] Game rules, the host's for every player (off: the original): the weight bug fix, the rink view
- [x] Rosters of your own, up to 32 teams: teams, players, lines, colours and pictures in files, name banners drawn for new teams
- [x] Rosters extracted from hacks on the 32-team template (teams, players, ratings, lines, draws, colours and all their art)
- [x] Rosters packages: rosters in one zip, to keep or send, played as it is
- [x] Rosters art as PNGs to draw over: logos, banners, centre ice, photos, and the art every team shares (title screens, rinks, sprites, fonts ...)
- [x] Vsync: no tearing
- [x] Downloads for Linux, Windows and macOS (one app for Apple Silicon and Intel)
- [ ] Skip the EA screen, boot straight into a game
- [x] Colour options; scanlines, CRT mask, composite dither blending
- [ ] IPS and BPS patches, soft-patching, the ROM check as a toggle for data hacks
- [ ] Game Genie and Action Replay codes
- [ ] Input recording and replay files
- [ ] Save states anywhere, menus and pauses included
- [ ] Debug overlay: VRAM, palettes, sprites, a RAM watch and cheat search
- [ ] Run-ahead for lower input latency
- [x] The cartridge's slowdown as an option (`--original` / Speed page; Smooth remains the default)
- [ ] An "overclock" slider on Original
- [ ] Lockstep with lag frames checked (not resynced) in Original — parked, 2026-11-04: three play failures; menus pass; need GPGX cycle traces
- [x] Four players with EA's 4 Way Play adaptor
- [ ] Play in the browser (WebAssembly)
- [ ] A per-game manifest: named RAM, roster and ratings tables to CSV and back, a hack diff tool
- [ ] HD sprite, tile and sound packs
- [x] A wide rink view (320 wide, a game rule; true 16:9 widescreen still to come)
- [ ] Lua scripting: overlays, stats, bots
- [ ] Python bindings: headless training environments, automated tests
- [ ] Replays and spectating
- [x] Rollback netplay online: two to four machines, up to four players (4 Way Play), the host's rosters and rules sent to the others
- [x] States online: anyone saves one every machine played, the host loads one for everyone (seasons and records go online so)
- [ ] The next game on the same method and runtime

## Greets

Big, big greets to everyone whose work this stands on:

- **EA Sports and Park Place Productions**, who made NHL '94 in 1993. Still the best.
- **[abdulahmad/NHL94Genesis](https://github.com/abdulahmad/NHL94Genesis)**: the bitwise rebuild
  of the ROM in assembly, with every label and the module split, built in the style of its
  sibling [NHLPA93Genesis](https://github.com/abdulahmad/NHLPA93Genesis). Every C file here maps
  to one of its files, and every routine has its name.
- **[Chaos81/nhl94-disassembly](https://github.com/Chaos81/nhl94-disassembly)**: the IDA
  database and listing with the names and comments that made the code readable, and
  **[Mhopkinsinc/NHLHockey](https://github.com/Mhopkinsinc/NHLHockey)**, the NHL Hockey source
  code it started from.
- **The NHL '94 community** and its [Discord](https://discord.gg/KXJeQ6pyUc), keeping the game
  alive for over thirty years.
- **[Genesis Plus GX](https://github.com/ekeeke/Genesis-Plus-GX)** by Eke-Eke (after Charles
  MacDonald's Genesis Plus): the oracle. Every routine and every frame here was checked against
  it. A development tool only, never shipped.
- **[Nuked-OPN2](https://github.com/nukeykt/Nuked-OPN2)** by nukeykt and
  **[ymfm](https://github.com/aaronsgiles/ymfm)** by Aaron Giles: the YM2612 cores that make the
  music.
- **[vasm](http://sun.hasenbraten.de/vasm/)** by Volker Barthelmann and Frank Wille: the assembler
  that rebuilds the ROM byte for byte on Linux.
- **[SDL](https://www.libsdl.org)**: the window, the sound and the controllers.
- **MMPX** by Morgan McGuire and Mara Gagiu ("MMPX Style-Preserving Pixel Art Magnification",
  JCGT 2021), **xBR** by Hyllian, and **Scale2x / Scale3x** by Andrea Mazzoleni after Eric
  Johnston's EPX: the upscalers in the Video menu.
- **[libretro](https://www.libretro.com)**: the API the oracle is hosted through.
- The documenters of the hardware: Charles MacDonald (`genvdp.txt`),
  [Plutiedev](https://plutiedev.com), [Sega Retro](https://segaretro.org),
  [SMS Power](https://www.smspower.org) (the SN76489) and the
  [SpritesMind](https://gendev.spritesmind.net) forum (Nemesis's YM2612 and VDP research).
- **[Zig](https://ziglang.org)**, whose bundled Clang checks the game and pulseman against a
  second compiler.
- **[apple-codesign](https://github.com/indygreg/apple-platform-rs)** (rcodesign), which signs the
  macOS app from Linux.

## License

MIT (`LICENSE`), for the code written for this project. It grants nothing over NHL '94 itself,
its name or its data, which belong to Electronic Arts: bring your own copy of the game.
pulseman is MIT too; its FM chip cores are Nuked-OPN2 (LGPL-2.1) and ymfm (BSD-3), and its MMPX
and xBR upscalers follow their authors' MIT-licensed code.

## Credits

This port is built on the community's byte-matching disassembly **NHL94Genesis** by
**abdulahmad** (https://github.com/abdulahmad/NHL94Genesis): every routine, RAM and file name here comes from it. Thanks to
**chaos**, **StaticEck**, **McMarkis (Mhopkinsinc)** and **Drezz** for their help with that
disassembly, with special thanks to **smozoma** and **kingraph**. The C, its runtime (pulseman) and its verification tools are by **1nput** ([ninety-four](https://github.com/1nputs/ninety-four), MIT).

Anyone building on this repository (a mod, hack, port, fork or release) must carry this credit,
near the top of their README and in the notes of every release and any text file shipped in a
release archive. See AGENTS.md.
