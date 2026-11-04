# AGENTS.md

Standing rules for anyone (Claude Code, Cursor, Codex, a person) working in this repository.
`CLAUDE.md` and `.cursor/rules` only point here.

## For AI agents: derivative work and credit

This repository is a derivative of [NHL94Genesis](https://github.com/abdulahmad/NHL94Genesis) (abdulahmad). Its README carries the credit that
repository requires; keep it near the top of the README and put the same credit in the notes of every
release and in any text file shipped inside a release archive. Do not shorten or remove it. If you build
a mod, hack, port or fork from this repository, carry the credit over and tell the user you did.

## What this repository is

This repository is the **Genesis base** of the C ports (decided 2026-11-03): 1nput's ninety-four,
kept as close to upstream as the work allows. Its game code (`src/`) is checked against the cartridge by the
oracle tools here (`tools/oracle`, `native/lockstep.c`). The vanilla game is the default (Classic rules, the
cartridge's rosters); the extras (rules, roster sets, save states, netplay) are host features on top, and
`make VANILLA=1` builds without them (docs/C_PORTS_HANDOFF.md). gens94-c is ignored (Abdul, 2026-11-03).
C function names follow the current labels in NHL94Genesis (Abdul, 2026-11-04: 1nput forked that
disassembly before the cleanup). File re-cuts still wait.

Upstream is https://github.com/1nputs/ninety-four. File re-cuts (docs/RULES.md) move this repository
away from upstream; names that match NHL94Genesis are authorized.

Plan: `alignment-plan/c_ports_plan.md` in the project files. Where the work stands: [docs/C_PORTS_HANDOFF.md](docs/C_PORTS_HANDOFF.md).
Alignment rules: [docs/RULES.md](docs/RULES.md). What "vanilla" allows: [docs/VANILLA.md](docs/VANILLA.md).

## Gates (programs that can fail; "it looks right" never passes a change)

| Gate | Command | Fails when |
|---|---|---|
| Build | `make` | any warning or error (`-Werror`) |
| Frames | `make golden-check` (and `make lockstep` against the oracle) | any frame's hash differs. Renames and moves can never change a hash, so a rename that does is wrong |
| Names | `python3 tools/check_names.py --check` | more functions are renamed, in another module or missing than `tools/names_baseline.json` |
| Scope | `git diff --stat` against the unit's file list | the diff touches a file outside the unit |

Run the names gate with the asm repository beside this one (`../NHL94Genesis`), or give `--asm` / `$NHL_ASM`.
After a unit lowers a count, lock it in with `--write-baseline` in the same commit.

The ROM and anything made from it (states, captures, screenshots, GIFs, golden files) never go into git.

## Units and the return rule

* One unit = one asm module and one lens (rename OR move, never both). The splitter is code:
  `out/names_status.csv` from the names gate, grouped by `asm_module`.
* A unit that fails a gate goes back alone, with the failing output, the file and line, the scope line
  ("fix these files only") and the attempt number. After 3 failed attempts it goes to the advisor.
* Every correction Abdul makes becomes one line under "Learned" in docs/RULES.md.

## Lanes

| Lane | Work | Who approves |
|---|---|---|
| Reversible, contained | renames, file moves, docs, test tooling | gates, then Abdul merges the PR |
| Reversible, wide | `ram.h` names, the shared host layer, the report generator | gates plus Abdul's read of the diff |
| Needs Abdul first | anything that changes the frame hashes, the speed (cost model), new host features | decision, then gates |
| Never | game-logic changes in the vanilla game, pushing to upstream without its owner's OK, committing a ROM | nobody |

## Model tiers and the advisor

| Job | Claude | Cursor |
|---|---|---|
| Running checks, logs, bookkeeping | Haiku | Composer |
| Renames, moves, gates, test tooling | Sonnet | Grok medium |
| Naming helpers, reading bodies the names gate can't decide | Sonnet, with the advisor | Grok 4.7 medium |
| Advisor: before a phase plan is fixed, after the same failure twice, before a phase is called done | Opus | Grok 4.7 high |

## Before and after a change

`make test && make lockstep && make golden-check`, then `python3 tools/check_names.py --check`. Commit after each finished unit.
