# Handoff

Where the C-port work stands in this repository. Plan: `alignment-plan/c_ports_plan.md` in the project files.
Project-wide handoff (always current, read it first): `alignment-plan/c_ports_HANDOFF.md` in the project files; how the project is run: `alignment-plan/HARNESS.md`.

Rules: [AGENTS.md](../AGENTS.md), [RULES.md](RULES.md), [VANILLA.md](VANILLA.md).

## Done

* Phase 0: credit in the README and the release READMEs, AGENTS.md, CLAUDE.md, docs/RULES.md, docs/VANILLA.md, this file.
* Phase 1 (part): the names gate `tools/check_names.py` and its baseline `tools/names_baseline.json`.

## Results (2026-11-03)

* E1: `test/crosscheck.sh ROM` plays gens94-c's 8 scripts (156k frames) on this build: 8 of 8 identical to gens94-c's
  `test/expected.txt`. INDIO's reference hashes are reproducible from public code.
* E4: tag v0.2.0 on the same scripts (`make WERROR= nhl94` on GCC 13): 8 of 8 identical. v0.2.0, v0.3.6 Classic and
  gens94-c are one game on these runs.
* E2: 1nput's ROM checks on a fresh Linux machine (`tools/e2_setup.sh ROM` sets it up): `make lockstep` 18 of 18 vectors
  pass (about 180k vblanks against the cartridge in Genesis Plus GX); `make test` 10,276 of 10,276 capture records pass,
  0 failed, 570 skipped (interrupted calls), 29 captures; `make golden-check` 20 of 20 runs identical, 2 of 2 roster set
  runs, 7 of 7 states, keys / restart / reset OK.
* E3: gens94-c's `game/` in this repository's lockstep host (gens94-c `test/oracle/build.sh`, `lockstep.py`): 18 of 18
  vectors pass; two planted bugs were caught.
* E5: coverage of the 838 original routines (gcov build, `tools/coverage.py`). gens94-c's 8 scripts, lockstep and the
  golden runs together: 793 ran (94.6%), 93.0% of their lines (`test/coverage.txt`: the 45 never reached, the partly
  run). Lockstep alone, the part compared with the cartridge every vblank: 777 (92.7%), 89.5% of lines
  (`test/coverage_lockstep.txt`, 61 never reached). E5 part 2 added two vectors (`demo_long`, `shootout_full`):
  lockstep 20 of 20, 785 routines (93.7%), 53 never reached. Both were cut before the frame where the cartridge's
  slowdown away from the game loop's wait takes them out of step (a lockstep limit, not a difference in the code). `tools/golden.py` fix: a run whose snapshot is missing crashed
  the recorder (a 2-tuple where 3 are unpacked).
* `VANILLA=1` (`make VANILLA=1 nhl94`: `build/nhl94-vanilla`, docs/VANILLA.md): gens94-c's 8 scripts 8 of 8 identical to
  its `expected.txt`; lockstep 18 of 18. Its first run caught a stand-in that skipped reading the teams' tables at power on.

**gens94-c is ignored from 2026-11-03 (Abdul)**: no work or cross-checks on it; the E1/E3 results above are history. `test/crosscheck.sh` is not run any more.

## Next, in order

1. **Coverage, part 3** (plan E5): new lockstep vectors for the 53 routines lockstep never reaches
   (`test/coverage_lockstep.txt`: injuries, fights, breakaways, the period stats screen, manual goalie, record holders).
2. ~~`VANILLA=1` build~~ (done 2026-11-03, see Results).
3. **Ask 1nput** whether he takes renames and the file re-cut upstream. Until then the names gate only reports.
4. **Slowdown option** (off by default): a 68k cost model, then lockstep with lag frames checked (plan E6).

## Needs

* The ROM (never committed): the 1 MB US/Europe cartridge, SHA-1 `7b1489ab501258133bf03e5f0892b77d04312580`
* The asm repository beside this one: `../NHL94Genesis`.
