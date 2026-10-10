# Assembly Implementation (`rpg-asm/`)

An eighth implementation written for **didactic purposes**: the same game rules in hand-written x86-64 assembly, so a
reader can see what `step(command) → events` looks like when there is no language underneath. It is not a full port:
the **engine has full rule parity** (it replays every `shared/golden` scenario, its bot issues exactly the commands
recorded by the Python bot and its simulator prints the same report byte for byte), and the interface is a **simple
line-based text UI** instead of the TUI of the other seven implementations.

## Scope

| Covered | Not covered |
|---|---|
| The whole engine: PRNG, formulas, character sheet, spawner, battle, statuses, loot, merchant, auto-equip, progression, run statistics, victory/continue | The full-screen TUI (layout, colours, animation, ASCII art), the controller and its tests |
| `GreedyBot` and `--simulate N` (report byte-identical to Python, `--seed 0` included) | Save/continue, run history, profile, bestiary, achievements, Hall of Fame (no persistence at all) |
| Golden replay of every scenario, comparing every event field and the whole final state | Auto-battle (`docs/game-design.md` §13) and settings |
| A playable text UI: merchant (potions, sell, equipment, stock, character sheet), battle (attack, spells, potions, defend), victory (end/continue), game over | `--lang`, `--no-anim`, `--data-dir`: English only (`shared/i18n/en.json`); pt-BR is not embedded |
| `--seed`, `--simulate`, `--vocation`, `--difficulty`, `--version`, `--help` | Windows/macOS binaries and release assets: x86-64 Linux only, run through Docker elsewhere |

Two limits come from the fixed-size design and are far from normal play: the dropped-item list of the run statistics
keeps the first 4096 drops, and a command may emit at most 1024 events (the program stops with a message otherwise).
There are no unit tests per routine: correctness is checked end to end by the golden replay, which is strict (see
below).

## Stack

- x86-64 Linux, [NASM](https://www.nasm.us/) 2.16.03 syntax, System V AMD64 calling convention
- Linked against glibc with `gcc` as the linker driver (`-no-pie`): libc is used freely for I/O, formatting and
  memory (`printf`, `snprintf`, `fgets`, `fopen`, `strcmp`, `strtol`, `malloc`, `qsort`); the game rules are all assembly
- A plain `Makefile`; every NASM warning is an error (`-w+all -w+error`, minus the `reloc` class that the non-PIE
  addressing triggers on purpose) and the linker runs with `--fatal-warnings`
- Python 3.14 (standard library only) for the two build/test tools; no other dependency
- Docker: `python:3.14.8-slim-trixie` (build: pinned Python + `nasm=2.16.03-1`, `gcc=4:14.2.0-1`, `make=4.4.1-2`) and
  `debian:trixie-20261005-slim` (runtime)

## Commands

```bash
# Play (any system with Docker; the build context is the repository root because shared/ is needed)
docker build -f rpg-asm/Dockerfile -t rpg-asm .      # generates the data, assembles, links, runs every test
docker run --rm -it rpg-asm --seed 42
docker run --rm rpg-asm --simulate 20 --seed 42
bash setups/play-on-unix-version-asm.sh --seed 42    # the two lines above in one script
bash setups/play-on-windows-version-asm.sh --seed 42 # Git Bash + Docker Desktop

# Develop on x86-64 Linux with nasm, gcc, make and python3 installed
cd rpg-asm
make                    # build/rpg-asm
make test               # PRNG vectors + every golden scenario + bot parity
make test-simulator     # simulator report vs the Python reference (reads ../rpg-python/src)
./build/rpg-asm --seed 42

# Develop anywhere else: the same targets inside the pinned toolchain image, with the repository mounted
docker build --target toolchain -t rpg-asm-toolchain -f rpg-asm/Dockerfile .
bash rpg-asm/dev.sh make test
```

Test modes of the binary: `--replay <script>` (golden replay, below) and `--prng <seed> <count>` (PRNG outputs).

## Structure

The four layers and the file names mirror the reference (`rpg-python/src/rpg/`), so `battle.asm` can be read next to
`battle.py`.

```
rpg-asm/
├── Makefile / Dockerfile / Dockerfile.dockerignore / dev.sh
├── tools/
│   ├── gen_data.py               # shared/data + shared/i18n/en.json → build/gamedata.{inc,asm}
│   ├── golden_test.py            # shared/golden/*.json → replay scripts + expected lines, runs and diffs
│   └── simulator_test.py         # `--simulate` output vs the Python reference
├── build/                        # git-ignored: generated data, objects, the binary
└── src/
    ├── version.inc               # %define VERSION "x.y.z"
    ├── common.inc                # conventions, the routine frame, macros (ROW, PCT, EMIT, PRINTF), libc externs
    ├── domain/
    │   ├── enums.inc             # elements, slots, stats, phases... (the generator reads this file)
    │   ├── definitions.inc       # layout of the content tables (Creature, Item, Spell, ...)
    │   ├── entities.inc          # layout of the run state (Player, MonsterInstance, RunState, Stats, Sheet)
    │   ├── rng.asm               # mulberry32: next, roll, chance, weighted
    │   ├── formulas.asm          # pct, xp_for_level, mana_for_magic_level, round_info, ...
    │   └── character.asm         # item stats, item score, build_sheet
    ├── application/
    │   ├── events.inc / events.asm     # event types, the event buffer, the schema of every event and command
    │   ├── commands.inc                # command types
    │   ├── run_state.asm               # the state blocks + bag and status list operations
    │   ├── statistics.asm              # run counters from events (jump table)
    │   ├── loot.asm / spawner.asm / progression.asm / battle.asm / merchant.asm / auto_equip.asm
    │   ├── engine.asm                  # engine_new_run, engine_step, victory, drops
    │   └── bot.asm / simulator.asm / simulator.inc
    ├── infrastructure/
    │   └── i18n.asm              # t(key): lookup in the embedded en.json
    └── presentation/
        ├── cli.asm               # main: flags
        ├── replay.asm            # --replay: script parser, canonical event lines, final-state dump
        ├── event_text.asm        # event → English sentence, from the en.json templates
        ├── simulator_report.asm  # --simulate: the report table
        └── text_ui.asm           # the playable line-based UI
```

## Generated game data (no JSON parser in assembly)

Rule 2 of the project (no balance number in code) holds here too: `tools/gen_data.py` reads the same
`shared/data/*.json` as the other implementations and writes two **build artifacts** (never committed, regenerated by
`make` whenever a JSON file changes):

- `build/gamedata.inc`: table sizes (`ITEM_COUNT`, `MAX_ATTACKS`, ...) and every scalar of `balance.json` as an `equ`
  (`BAL_CRIT_MULTIPLIER_PCT equ 150`), so the engine uses them as immediate operands;
- `build/gamedata.asm`: the tables (`creatures`, `items`, `spells`, `vocations`, `potions`, `statuses`, `affixes`,
  `difficulties`, `rarities`, `enemy_classes`, `spell_levels`, `families`, the enum name tables and `i18n_table`),
  written with `istruc` against the layouts of `src/domain/definitions.inc`. That include file is the only place that
  knows the field offsets; the generator only knows field names.

Conventions of the tables: every field is a qword; a row starts with pointers to its id and display name; references
between tables are row indexes (`-1` = none); maps become dense arrays (an item's `stats` is one qword per stat); sets
become bit masks (the slots of an affix, the item types of a vocation). "Sorted by id" lists are sorted **by the
generator** with Python's code-point string order (`items_by_id`, `affixes_by_id`, `tier_monsters`), so the item
generator filters an already sorted list and never compares strings. The enum numbers come from
`src/domain/enums.inc`, which the generator parses (`NAME equ n ; "json spelling"`).

## Golden tests without JSON

The binary cannot read `shared/golden/*.json`, so `tools/golden_test.py` converts on both sides and the binary gets a
replay mode:

```
seed 7                          [0] run_started seed=7 vocation=warrior difficulty=normal
name Alex                       [0] merchant_entered round=0
vocation warrior                [1] potion_bought potionId=health_potion quantity=1 gold=50
difficulty normal        →      [2] error code=potion_locked
autoEquip false                 ...
begin                           state.round=1                    (finalState, flattened)
buy_potion health_potion 1      run.player.potions.health_potion=6   (finalRun = the whole RunState, flattened)
buy_potion great_health_potion 1
```

- **Events**: one line per event, `[command index] type field=value ...`, with **every field** in the order of
  `docs/cross-language-parity.md` §3. The expected lines are built from the golden JSON in its own key order, and the
  two lists are compared line by line, in order.
- **Final state**: both `finalState` and `finalRun` (the complete run state: player, equipment and bag with affixes,
  statuses, monster with its scaled attacks, merchant stock, statistics) are flattened to `path=value` lines. Lists
  carry their length (`path.#=n`), `null` and booleans are spelled out, and the two sets of lines must be equal.
  Nothing is left out to make a test pass.
- **Bot parity**: for the nine `bot-full-run-*` scenarios a second script replaces the commands with `@bot`. The
  port's `GreedyBot` then plays; each command it chooses is printed (`> equip 35`) and must be the recorded one,
  followed by the same events and the same final state.
- **PRNG**: `--prng <seed> <count>` against `shared/golden/prng.json`.
- **Simulator**: `tools/simulator_test.py` runs the reference (`python -m rpg --simulate 20`, standard library only)
  and the binary with seeds 42 and 0 and one filtered case, and compares the bytes.

`make test` runs 26 checks (5 PRNG vectors, 12 replays, 9 bot runs); the Docker build runs `make test` and
`make test-simulator` and fails if any of them does.

## How to read this code

1. Start with `src/common.inc`: the calling convention, the one stack frame every non-leaf routine uses
   (`ENTER_FRAME` / `LEAVE_FRAME`) and the few macros. Every source file then opens with a header that states its
   role and how its routines use the registers, and every routine documents its inputs, outputs and what it destroys.
2. Read `src/domain/rng.asm` and `formulas.asm` next to `rng.py` and `formulas.py`: short leaf routines, a good
   place to see 32-bit wrapping arithmetic (`imul` on `eax`) and floored division (`div`).
3. `src/domain/definitions.inc` and `entities.inc` show how Python classes, dicts and lists become fixed-size
   records (`struc`), dense arrays and `(count, items[])` lists. Open `build/gamedata.asm` after a build to see the
   tables they describe.
4. `src/application/events.inc` explains the event record and the `EMIT` macro; `events.asm` holds the schema that
   drives every printer.
5. Then follow one command through `engine.asm` → `battle.asm` with `docs/game-design.md` §6 open: the comments carry
   the step numbers of the design document, and each `call rng_*` is one of the contract's random numbers.
6. `statistics.asm` (a jump table), `simulator.asm` (a callback called by libc's `qsort`) and `replay.asm` (table-driven
   printing) each show one classic assembly technique.

Conventions worth knowing before reading: all game values are 64-bit; "a pointer to the row" and "the index of the
row" are different things and the comments always say which; values that must survive a call live in `rbx` and
`r12`-`r15`; `[table + index*8]` uses absolute addressing, which is why the program is linked non-PIE.

## Notes

- `version.inc` holds the version (`%define VERSION "1.5.0"`); `rpg-asm --version` prints `rpg 1.5.0 (asm)`.
- A command script line with an unknown spell or potion id is still sent to the engine (as row `-1`) and answered
  with `unknown_spell` / `unknown_potion`, like the reference does for an unknown string.
- The potions dict of the reference keeps a key at quantity 0 after the last potion is drunk; `Player.potion_known`
  reproduces that in the final-state dump.
