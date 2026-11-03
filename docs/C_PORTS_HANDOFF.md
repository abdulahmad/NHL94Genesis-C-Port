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

## Next, in order

1. **Lockstep on gens94-c** (plan E3), test tooling only.
2. **Coverage** (plan E5; first pass in `test/coverage.txt`): add the golden runs and lockstep sessions; `test/coverage.txt` lists every
   routine and branch never reached.
3. **`VANILLA=1` build**: the same `src/`, without native/rules.c, rosters.c, romsrc.c, art.c, banner.c, ratings.c,
   net.c, state.c and their menu pages; its golden runs must equal the default build's Classic runs.
4. **Ask 1nput** whether he takes renames and the file re-cut upstream. Until then the names gate only reports.
5. **Slowdown option** (off by default): a 68k cost model, then lockstep with lag frames checked (plan E6).

## Needs

* The ROM (never committed): the 1 MB US/Europe cartridge, SHA-1 `7b1489ab501258133bf03e5f0892b77d04312580`
* The asm repository beside this one: `../NHL94Genesis`.
