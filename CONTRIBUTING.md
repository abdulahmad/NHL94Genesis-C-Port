# Contributing to NHL94Genesis-C-Port

The Genesis C port base (1nput's ninety-four), checked against the cartridge by lockstep and golden hashes.

Read [AGENTS.md](AGENTS.md) first (gates, units, credit). Alignment rules: [docs/RULES.md](docs/RULES.md). Where this
repo stands: [docs/C_PORTS_HANDOFF.md](docs/C_PORTS_HANDOFF.md). What "vanilla" allows: [docs/VANILLA.md](docs/VANILLA.md).

Work on a short branch off the current `main` and open one pull request into `main` per topic; it is merged with a
normal merge commit. Every commit must pass the checks in [AGENTS.md](AGENTS.md).

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

## Netplay security

Everything that comes over the network is hostile. The rules and a review checklist are in Overbar's
`native/overbar/docs/NETPLAY_SECURITY.md`; read them before changing netplay, a saved state, a file route or the season code. In short:
never run, load or write code from network data; a peer never chooses an address; check every length before use
(`len > size - at`, never `at + len > size`); validate a state before it is applied and clean it after; a peer's names are text, not paths.
`sh native/overbar/tests/netsec.sh .` must print `netsec ok` (CI runs it in the `netsec` job). When a change adds a network path or a state field,
add a hostile test for it.
