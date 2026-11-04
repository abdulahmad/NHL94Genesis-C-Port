# Contributing to NHL94Genesis-C-Port

The Genesis C port base (1nput's ninety-four), checked against the cartridge by lockstep and golden hashes.

Read [AGENTS.md](AGENTS.md) first (gates, units, credit). Alignment rules: [docs/RULES.md](docs/RULES.md). Where this
repo stands: [docs/C_PORTS_HANDOFF.md](docs/C_PORTS_HANDOFF.md). What "vanilla" allows: [docs/VANILLA.md](docs/VANILLA.md).

Work stays on `claude/project-thread-4gad59`. Do not branch from `main` and do not merge to `main`. PRs from that
branch are for review only.

This repository is a derivative of [NHL94Genesis](https://github.com/abdulahmad/NHL94Genesis). Keep the credit near
the top of the README and in every release, as AGENTS.md requires.

## Gates

| Gate | Command |
|---|---|
| Build | `make` |
| Frames | `make test && make lockstep && make golden-check` |
| Names | `python3 tools/check_names.py --check` |

A change passes only when those programs say so. Renames and moves must not change a frame hash. Other games' names
never go in comments; lineage lives in the asm repo's `docs/name_map.csv`.

The ROM is never committed.
