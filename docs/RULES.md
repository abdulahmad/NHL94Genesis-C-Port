# Alignment rules

What decides each kind of name and place in the C ports. Workers and the splitter read this first.

## Authority

| Category | Authority |
|---|---|
| Function name of an original routine | The current label in [NHL94Genesis](https://github.com/abdulahmad/NHL94Genesis). Old names reach it through that repository's `docs/name_map.csv` (`tools/check_names.py` follows the chain) |
| Asm local (`Parent.x`) | The C function `Parent_x`, `static` in the parent's file where nothing else calls it |
| Helper (a piece split off a routine, or the C's own) | `<Routine>_<what>`, `static` where possible; listed as a helper in docs/ROUTINES.md |
| C file | One file per asm module, named after it (`src/collide94.asm` is `collide.c`), once re-cut (phase 5) |
| Order inside a file | ROM order, as the asm |
| RAM names (`ram.h`) | The asm RAM equates (`src/ram94.asm`) |
| First comment line of a routine | its asm file and label (`collide94.asm:checkcoll.`) |
| Other games' names | never in comments; lineage lives in the asm repositories' `docs/name_map.csv` |

## Status (tools/check_names.py, 2026-11-03)

| ok | other_module | renamed | missing |
|---|---|---|---|
| 473 | 250 | 30 | 3 |

## Learned

(One line per correction, newest last.)
