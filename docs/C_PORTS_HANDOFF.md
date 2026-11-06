# Handoff

Where the C-port work stands in this repository. Plan: `alignment-plan/c_ports_plan.md` in the project files.
Project-wide handoff (always current, read it first): `alignment-plan/c_ports_HANDOFF.md` in the project files; how the project is run: `alignment-plan/HARNESS.md`.

Rules: [AGENTS.md](../AGENTS.md), [RULES.md](RULES.md), [VANILLA.md](VANILLA.md).

## Now

G4 unused leftover ports stacked on G2/G3 [#7](https://github.com/abdulahmad/NHL94Genesis-C-Port/pull/7). `PuckComingToward` and `TerminateLogName` ported; `rtsStatScroll` parked. Names 840/0/0/0. Next: G5 rest-of-tree ROM order (G0 checker already proven). Do not merge to main. Frame gates not run here (no captures/vectors/golden).

## Done

* Phase 0: credit in the README and the release READMEs, AGENTS.md, CLAUDE.md, docs/RULES.md, docs/VANILLA.md, this file.
* Phase 1 (part): the names gate `tools/check_names.py` and its baseline `tools/names_baseline.json`.

## Results (2026-11-03; E2 / E5 part 3 / Original speed / E6 parked 2026-11-04)

* E1: `test/crosscheck.sh ROM` plays gens94-c's 8 scripts (156k frames) on this build: 8 of 8 identical to gens94-c's
  `test/expected.txt`. INDIO's reference hashes are reproducible from public code.
* E4: tag v0.2.0 on the same scripts (`make WERROR= nhl94` on GCC 13): 8 of 8 identical. v0.2.0, v0.3.6 Classic and
  gens94-c are one game on these runs.
* E2: 1nput's ROM checks (`tools/e2_setup.sh ROM`, vasm from the StarWolf3000 mirror). Re-run 2026-11-04 on GCC 13:
  `make lockstep` **23 of 23** vectors pass; `make test` 10,276 of 10,276 capture records pass, 0 failed, 570 skipped
  (interrupted calls), 29 captures; `make golden-check` 20 of 20 runs identical, 2 of 2 roster set runs, 7 of 7 states,
  keys / restart / reset OK. First try this session: `oracle_vdp_get` SIGSEGV (`movdqa` on GPGX `cram`, `ALIGNED_(4)`);
  fixed in `tools/hybrid/oracle_shim.c` + `-fno-tree-vectorize` (docs/RULES.md).
* E3: gens94-c's `game/` in this repository's lockstep host (gens94-c `test/oracle/build.sh`, `lockstep.py`): 18 of 18
  vectors pass; two planted bugs were caught.
* E5: coverage of the 838 original routines (gcov build, `tools/coverage.py`). gens94-c's 8 scripts, lockstep and the
  golden runs together: 793 ran (94.6%), 93.0% of their lines (`test/coverage.txt`: the 45 never reached, the partly
  run). Lockstep alone, the part compared with the cartridge every vblank: 777 (92.7%), 89.5% of lines
  (`test/coverage_lockstep.txt`, 61 never reached). E5 part 2 added two vectors (`demo_long`, `shootout_full`):
  lockstep 20 of 20, 785 routines (93.7%), 53 never reached. Both were cut before the frame where the cartridge's
  slowdown away from the game loop's wait takes them out of step (a lockstep limit, not a difference in the code). `tools/golden.py` fix: a run whose snapshot is missing crashed
  the recorder (a 2-tuple where 3 are unpacked).
* E5 part 3 (2026-11-04): three vectors (`pause_stats`, `demo_p2_pause`, `setup_demo`). Lockstep **23 of 23**,
  790 routines (94.3%), 91.0% of lines, 48 never reached. Newly reached: Player Stats, Period Stats, AttribWeight,
  SetupDemo, startpause2. `pause_stats` is the pause items lockstep had skipped (Player Stats, Period Stats,
  Manual Goalie, Team Roster to Weight). `setup_demo` is six minutes of no press on the setup screen
  (`ReadSetupPads` $5460). Remaining 48 are mostly rts / unreferenced (no fighting in 94, so assfight / AttribFighting
  stay dark), RNG play (injuries, breakaways, penalty shots), playoff highlights / the Cup, setup-screen record
  cards, or a lockstep cut (EndShootout).
* Original speed (2026-11-04): opt-in, Smooth the default. `--original` / `--slowdown` / `--speed original`,
  the window's Speed page (`game.speed`, default `smooth`). `native/clock.c` charges original 68k
  routines during a DoGameFrame pass; extra vblanks fire mid-logic so the next wait sees `elapsed` > 1
  (faceoff play: 29 of 30 passes lagged by one vblank, elapsed 2). C helpers are not charged; extra
  vblanks are guarded so the instrumented Z80 relay cannot loop.
  Gates on Smooth: lockstep **23 of 23**, `make test` 10,276 of 10,276, golden-check 20 of 20 / 2 of 2
  / 7 of 7, names 534/270/31/3 PASS. VANILLA=1 builds with the same clock. SNES not touched.
* E6 parked (2026-11-04, 3 Original lockstep failures). `native/costs.inc` is size-weighted from
  lockstep traces (`tools/clock_fit.py`), scaled to one extra a typical play pass so setvideo still
  waits. `--lockstep --original` is allowed: Smooth still resyncs; Original places lag in game logic
  by port-access count (cost model off during lockstep) and checks the game-loop wait instead of
  `take_state`. Menus pass (boot, title_start); pause_stats **3100 of 3100**; faceoff work RAM differs
  at oracle frame 243, 68k at setvideo. Failure 1: charge lagged-span median L=7 every pass, skips
  the wait. Failure 2: L=1 size-weighted extras on L=0 spans, hashes fail at frame 2. Failure 3:
  faceoff as above. Need GPGX event-pair cycle traces, not lockstep L. Overclock slider still open.
* `VANILLA=1` (`make VANILLA=1 nhl94`: `build/nhl94-vanilla`, docs/VANILLA.md): lockstep **20 of 20** against the cartridge
  (2026-11-04). `src/` is the same code as the default build; host extras are stubbed.
* **NHL94Genesis names** (2026-11-04, Abdul: 1nput forked before the cleanup). One asm module at a time, rename
  lens only. 31 `renamed` + 3 `missing` original routines now use the current NHL94Genesis labels
  (`TitleScreen`, `Pausemode_*`, `TeamIcons`, `Fprintsmall`, `setupEASportsMap`, …). Names gate
  **558 ok / 280 other_module / 0 renamed / 0 missing**. `other_module` rose 270→280 because ten of
  those routines already sat in a C file that is not the asm module; no file re-cut. This C port has
  no `name_map.csv`; lineage stays in NHL94Genesis `docs/name_map.csv`. Capture `--fn` lists in
  `tools/hybrid/` still use the pinned vasm names (af1b6f2) so re-recording keeps working.
  Smooth gates after the names: `make lockstep` **23 of 23**; `make test` 10,276 of 10,276, 0 failed,
  570 skipped, 29 captures; `make golden-check` 20 of 20 identical, 2 of 2 roster-set LIKE, 7 of 7
  states, keys / restart / reset OK.
* **File recut** (2026-11-04): one C file per 94G asm file. 280 `other_module` routines moved
  (logic from checks/input/assign/replay; setup from data/attract; middle/print/decompress from
  the old video.c; video from display; SegaInit into sega.c; start.c renamed main.c). Kept `src/`
  (already flat, same as 94G). Names gate **838 ok / 0 other_module / 0 renamed / 0 missing**.
  Smooth gates after the recut: `make lockstep` **23 of 23**; `make test` 10,276 of 10,276, 0 failed,
  570 skipped, 29 captures; `make golden-check` 20 of 20 identical, 2 of 2 roster-set LIKE, 7 of 7
  states, keys / restart / reset OK. Generated `*_tables_gen.h` / `*_strings_gen.h` names stay
  (they match the pin `make gen` still reads). Capture `--fn` lists still use pinned vasm names
  (`af1b6f2`).

**gens94-c is ignored from 2026-11-03 (Abdul)**: no work or cross-checks on it; the E1/E3 results above are history. `test/crosscheck.sh` is not run any more.

2026-11-04: file recut on `claude/project-thread-4gad59`; do not merge to main. E6 stays parked. SNES Smooth was not flipped.

## Next, in order

1. ~~Coverage, part 3~~ (done 2026-11-04; lockstep 23 of 23, 790/838, 48 never reached).
2. ~~`VANILLA=1` build~~ (done 2026-11-03; lockstep 20 of 20 re-checked 2026-11-04).
3. ~~**Slowdown option**~~ (done 2026-11-04): `--original` / `--speed original` / Speed page / `game.speed` in settings.ini. Smooth remains the default.
4. ~~**NHL94Genesis names**~~ (done 2026-11-04): 0 renamed, 0 missing.
5. ~~**File recut**~~ (done 2026-11-04): 0 other_module. One `src/<module>.c` per 94G asm file.
6. ~~**E6**~~ parked 2026-11-04 (3 Original lockstep failures; see Results). Overclock slider still to come. SNES Smooth was not flipped.
7. ~~**Leftover recut**~~ (2026-11-05): C files follow Abdul's leftover 94G includes (`arena.c`, `shootoutpath.c`, `ui.c`, `helpers.c`, `logos.c`, `chgplayer.c`; `ManualGoalieMenu` in `period.c`; `title.c` is TitleScreen/credits). Names gate 838/0/0/0.
8. ~~**Leftover ROM order**~~ (2026-11-06, [#4](https://github.com/abdulahmad/NHL94Genesis-C-Port/pull/4)): original routines in `period.c`, `ui.c`, `logos.c`, `chgplayer.c` follow 94G ROM order. `arena.c`, `shootoutpath.c`, `helpers.c`, `title.c` already did. Names gate 838/0/0/0.
9. ~~**G0 ROM-order checker**~~ (2026-11-06): `tools/check_rom_order.py --module M` compares original C defs with 94G globals that have a C function (skips statics and data labels). Self-test: arena/title OK, fourway/menu mismatch. Does not fail the tree until G5.
10. ~~**G1 gen inputs**~~ (2026-11-06): `make gen` tables/strings from current 94G (`logic94` / `video94` / `setup94` / `title94`+`ui94`; `defaultsprites_0` for dslist). Output `*_gen.h` names stay; committed array values unchanged. Oracle pin not rewritten (`make gen-pin` when present).
11. ~~**G2** capture `--fn` lists~~ (2026-11-06): `tools/hybrid/capture_all.sh` uses current 94G file names (`logic94` / `video94`; boot `TitleScreen`; drop `goalie94`). Session output names stay. helpers leftover names stay.
12. ~~**G3** leftover comments~~ (2026-11-06): `src/logic.c` `tables94.asm:asstab`; `test/ports.c` section comments name current modules.
13. ~~**G4** unused leftover code~~ (2026-11-06): `PuckComingToward` in `helpers.c`, `TerminateLogName` in `ui.c`. `rtsStatScroll` parked (no empty stub). Names 840/0/0/0. No `test/ports.c` runners.
14. **G5** rest-of-tree ROM order (13 modules) still waits. G0 checker is proven (arena/title green, fourway/menu red).

## Needs

* The ROM (never committed): the 1 MB US/Europe cartridge, SHA-1 `7b1489ab501258133bf03e5f0892b77d04312580`
* The asm repository beside this one: `../NHL94Genesis`.
