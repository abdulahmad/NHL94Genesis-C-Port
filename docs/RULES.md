# Alignment rules

What decides each kind of name and place in the C ports. Workers and the splitter read this first.

## Authority

| Category | Authority |
|---|---|
| Function name of an original routine | The current label in [NHL94Genesis](https://github.com/abdulahmad/NHL94Genesis). Old names reach it through that repository's `docs/name_map.csv` (`tools/check_names.py` follows the chain) |
| Asm local (`Parent.x`) | The C function `Parent_x`, `static` in the parent's file where nothing else calls it |
| Helper (a piece split off a routine, or the C's own) | `<Routine>_<what>`, `static` where possible; listed as a helper in docs/ROUTINES.md |
| C file | One file per asm module, named after it (`src/collide94.asm` is `src/collide.c`; `src/` stays flat like 94G) |
| Order inside a file | ROM order, as the asm |
| RAM names (`ram.h`) | The asm RAM equates (`src/ram94.asm`) |
| First comment line of a routine | its asm file and label (`collide94.asm:checkcoll.`) |
| Other games' names | never in comments; lineage lives in the asm repositories' `docs/name_map.csv` |

## Status (tools/check_names.py, 2026-11-04)

| ok | other_module | renamed | missing |
|---|---|---|---|
| 838 | 0 | 0 | 0 |

## Learned

(One line per correction, newest last.)
* 2026-11-03: the base is ninety-four v0.3.6 with its `make VANILLA=1` build as the vanilla one, not v0.2.0 and not gens94-c (Abdul). gens94-c is a finished cross-check.
* 2026-11-03: ignore gens94-c (INDIO's Genesis repo) entirely: no work, no cross-checks, not a gate. The base is checked by its own lockstep, captures and golden runs (Abdul).
* 2026-11-04: CONTRIBUTING.md must not contradict History: lineage lives only in the asm repositories' `docs/name_map.csv`, never in source comments. (CONTRIBUTING rewrite vs RULES History row)
* 2026-11-04: `oracle_vdp_get` must not load GPGX `cram`/`vsram` as `uint16_t*` at `-O2`: they are `ALIGNED_(4)`, and GCC 13 emits `movdqa` (SIGSEGV). Copy with `memcpy`; compile the shim with `-fno-tree-vectorize`. (`make vectors` snapshot/lockstep on GCC 13)
* 2026-11-04: after `make CC="gcc --coverage" nhl94`, do not run `make lockstep`: it relinks `build/nhl94` with the default CC and fails (`__gcov_init`). Check that binary with `python3 tools/oracle/vectors.py check`, then rebuild without coverage.
* 2026-11-04: Genesis Original speed is opt-in (`--original`); Smooth stays the default. Do not flip SNES Smooth.
* 2026-11-04: Original's cost model must not charge C helpers, and must not charge inside an extra vblank's handshake: src/ is `-finstrument-functions`, so the Z80 relay during `mdrt_wait_vblank` looped extra vblanks. Charge original 68k routines only; guard `native_vblank_now`.
* 2026-11-04: E6 parked (3 Original lockstep failures). Lockstep game-loop spans all have lag (full state only after lag), so their median L (~7) is the slow passes only; 83% of play waits have L=0. Charging L=7 every pass clears dfok before setvideo's wait and lockstep never checks. A size-weighted 7000-mclk model keeps waits but extras land on L=0 spans (hashes fail). Placing the recording's lag in game logic by port-access count (cost model off during lockstep) checks pause_stats and menus, then work RAM differs on faceoff at oracle frame 243. Smooth still resyncs. Need GPGX event-pair cycle traces, not lockstep L, to fire extras at the cartridge's moment.
* 2026-11-04: C function names follow NHL94Genesis; 1nput forked that disassembly before the cleanup. Apply those names in src/; the names gate's renamed/missing counts should fall. Do not re-cut files this session. Lineage stays in the asm repo's docs/name_map.csv (this C port has none). (Abdul)
* 2026-11-04: one C file per 94G asm file (`src/<module>.c` for `<module>94.asm`). Keep `src/` (already flat; 94G is `src/` too). `start.c` is `main.c`. Generated `*_tables_gen.h` / `*_strings_gen.h` names stay (they match the pin `make gen` still reads). (file recut)
* 2026-11-05 leftover recut: one C file per leftover 94G include (`period.c`, `arena.c`, `shootoutpath.c`, `ui.c`, `helpers.c`, `title.c`, `logos.c`, `chgplayer.c`). The leftover path slice is `shootoutpath.c`, not `shootout.c` (`shootout94` is the $FC320 UI). `goalie.c` is gone (`ManualGoalieMenu` lives in `period.c`). Generated `title_strings_gen.h` stays (ui.c includes it). (94G leftover recut)
* 2026-11-06 leftover order: original routines in leftover C files follow ROM order (`period.c` PeriodStatsScreen/StatsReport before the scroll helpers; `ui.c` PrintPlayerAssists then Goals then PrintParenNumber, PlayedByHome/Text/Exit/None/Away; `logos.c` LeadSong then ClearLeadSong/LeadSongExit then ClearPenalties then clrTmPdst; `chgplayer.c` chgplayer then the thunks). Static C helpers stay with their parent. (94G leftover recut)
* 2026-11-06: `make gen` tables/strings read current 94G files (`logic94`/`video94`/`setup94`/`title94`+`ui94`); generated `*_tables_gen.h` / `*_strings_gen.h` names stay. Oracle pin (`ref/`, `build/asm/symbols.json`) is not rewritten. (gen inputs)
