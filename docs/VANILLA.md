# Vanilla

"Vanilla" means the level of change of the NHL94PC and NHL95PC SDL2 ports: the game's own code does exactly
what the original did, and only the host around it is new. Those ports run the retail code unchanged
("Nothing of the game is changed or patched"); this C port replaces the CPU, so the same promise is checked by
the frame hashes instead.

| Allowed (host) | Not in the vanilla game |
|---|---|
| Window, scaling, aspect, filters, fullscreen | Changes to game rules, rosters, teams, art or text |
| Settings file, key and pad bindings, controller setup | New items inside the game's own menus |
| Screenshots, audio capture, an FPS display | Fixing the original's bugs (the weight bug stays) |
| Fast-forward or pause as host keys | A speed other than the original's as the default |
| A headless runner, scripted input | Anything that changes `test` hashes without Abdul's approval |

Speed (decided 2026-11-03): the cartridge's slowdown is an **option, off by default**. Without it every pass of
the game loop takes one vblank (Smooth); with it a CPU cost model decides how many vblanks a pass takes, as the
cartridge does.

## The layers

| Folder | What | In `make VANILLA=1` |
|---|---|---|
| `src/` | the cartridge's game, routine for routine (lockstep against the cartridge) | yes |
| `mods/` | customizations that run inside the game and change play (NHL 93's fights, NHL 95 PC's referee in play, the between-play pause, the anthem and the skate to the spots, NHL 96 Genesis's double minors, NHL 95's fake shot, drop pass and shot blocks, the PC goalie's animations, the ticker's highlights from the pregame) | no: `native/vanilla/mods.c` stands in with the cartridge's own behaviour |
| `native/` | the host: window, sound, menus, netplay, and the picture and sound extras (PC sprites, announcer, Sega CD extras, box, widescreen) | yes (host), with `native/vanilla/stubs.c` for rules, roster sets, states and netplay |

`src/` reaches `mods/` only through the declarations in `src/game.h` and `src/rules.h`, and every such call is a
place where the cartridge does nothing or the original thing (`native/vanilla/mods.c` says which). With every option
off the default build plays as the vanilla one: `make lockstep` and `make lockstep-vanilla` both pass the cartridge's
vectors.

## Where this repository stands

* The game code (`src/`) is the original path when the rules are Classic (the default) and the rosters are the
  cartridge's (the default). The rule and roster hooks stay in the code but do nothing then.
* Host extras beyond vanilla: game rules, roster sets, save states and rewind, rollback netplay, the GIF replay.
  They are opt-in. **`make VANILLA=1 nhl94`** builds `build/nhl94-vanilla` without them: native/rules.c, rosters.c,
  romsrc.c, art.c, banner.c, ratings.c, state.c and net.c are left out, and `native/vanilla/stubs.c` stands in for
  them (the original's rules, the cartridge's teams, no pages, a plain error for the options that need them).
  `src/` is the same code in both builds. Gate: the lockstep vectors pass on it against the cartridge (2026-11-04: 20 of 20).
* **`tools/check_vanilla.sh`** (CI, smoke.yml): `src/` includes no host header and reads no setting, and
  `make VANILLA=1 nhl94` builds. Customizations still inside `src/` (fights, the between-play pause, the referee)
  are listed in the script's `MOD_FILES` while they move out to their own layer; that list only shrinks.
* Slowdown: an option, off by default (`--original` / `--speed original`, or the window's Speed page
  / `game.speed` in settings.ini). Smooth (the default) is one vblank a pass; Original uses a 68k
  cost model so a pass can take extra vblanks (`native/costs.inc`, fitted from lockstep traces).
  `--lockstep --original` is allowed (E6 parked: play hashes still fail; Smooth still resyncs).
  The overclock slider is not this unit.
