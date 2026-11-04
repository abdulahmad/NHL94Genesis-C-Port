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

## Where this repository stands

* The game code (`src/`) is the original path when the rules are Classic (the default) and the rosters are the
  cartridge's (the default). The rule and roster hooks stay in the code but do nothing then.
* Host extras beyond vanilla: game rules, roster sets, save states and rewind, rollback netplay, the GIF replay.
  They are opt-in. **`make VANILLA=1 nhl94`** builds `build/nhl94-vanilla` without them: native/rules.c, rosters.c,
  romsrc.c, art.c, banner.c, ratings.c, state.c and net.c are left out, and `native/vanilla/stubs.c` stands in for
  them (the original's rules, the cartridge's teams, no pages, a plain error for the options that need them).
  `src/` is the same code in both builds. Gate: the lockstep vectors pass on it against the cartridge (2026-11-04: 20 of 20).
* Slowdown: not implemented yet (upstream deferred it); it will be an option, off by default.
