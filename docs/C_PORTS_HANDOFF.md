# Handoff

Where the C-port work stands in this repository. Plan: `alignment-plan/c_ports_plan.md` in the project files.
Rules: [AGENTS.md](../AGENTS.md), [RULES.md](RULES.md), [VANILLA.md](VANILLA.md).

## Done

* Phase 0: credit in the README and the release READMEs, AGENTS.md, CLAUDE.md, docs/RULES.md, docs/VANILLA.md, this file.
* Phase 1 (part): the names gate `tools/check_names.py` and its baseline `tools/names_baseline.json`.

## Next, in order

1. **Equivalence on our machine** (needs the ROM): `tools/fetch_refs.sh`, `make oracle && make vectors`,
   `tools/hybrid/capture_all.sh`, then `make test && make lockstep && make golden-check` (plan E2).
2. **gens94-c cross-check** (needs the ROM): play gens94-c's 8 scripts (`../gens94-c/test/scripts`) with this
   build's `--golden` and compare with `../gens94-c/test/expected.txt` (plan E1). Same for tag v0.2.0 (plan E4).
3. **Coverage** (plan E5): a gcov build over the golden runs and lockstep sessions; `test/coverage.txt` lists every
   routine and branch never reached.
4. **`VANILLA=1` build**: the same `src/`, without native/rules.c, rosters.c, romsrc.c, art.c, banner.c, ratings.c,
   net.c, state.c and their menu pages; its golden runs must equal the default build's Classic runs.
5. **Ask 1nput** whether he takes renames and the file re-cut upstream. Until then the names gate only reports.
6. **Slowdown option** (off by default): a 68k cost model, then lockstep with lag frames checked (plan E6).

## Needs

* The ROM (never committed): the 1 MB US/Europe cartridge, SHA-1 `7b1489ab501258133bf03e5f0892b77d04312580`
* The asm repository beside this one: `../NHL94Genesis`.
