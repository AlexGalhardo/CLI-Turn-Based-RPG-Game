# C Implementation (`rpg-c/`)

Written from scratch following the Python reference, for didactic purposes: the same game in the language the other
six are (directly or indirectly) built on, with **no third-party library at all**. Same layers, file names and
behaviour: it passes every `shared/golden` scenario, its bot issues exactly the commands recorded by the Python bot,
its simulator prints the same report byte for byte, and its run state serialises to the same JSON (saves are
interchangeable with the other implementations; a save written by Python is continued in a test and must end exactly
like the Python continuation).

## Stack

- C17 (ISO C plus POSIX where the standard library ends), CMake ≥ 3.28 + Ninja, `CMakePresets.json` (`debug`,
  `release`)
- Compilers: GCC or Clang; on Windows, LLVM-MinGW UCRT (`scoop install mingw-mstorsjo-llvm-ucrt`), linked statically
  so `rpg-c.exe` needs no DLLs
- Zero dependencies. What the other ports take from a library is a small module here: a JSON reader/writer
  (`domain/json_types`), an ANSI-escape renderer (`presentation/tui/app`), raw keyboard input
  (`presentation/tui/terminal`) and an assert-based test harness (`tests/support/test`)
- `-Wall -Wextra -Wpedantic -Werror`, clang-format (`.clang-format`: LLVM base, tabs, 120 columns)

## Commands

```bash
cd rpg-c
cmake --preset debug                       # configure
cmake --build --preset debug               # build rpg-c + rpg_tests (warnings are errors)
ctest --preset debug                       # four ctest entries: unit, integration, golden, e2e
./build/debug/rpg_tests golden/            # one level, or any substring of "level/file/test_name"
./build/debug/rpg_tests --list             # every test name
cmake --preset release && cmake --build --preset release
./build/release/rpg-c --seed 42            # play (rpg-c.exe on Windows)
./build/release/rpg-c --simulate 20 --seed 42
clang-format --dry-run --Werror $(find src tests -name '*.c' -o -name '*.h')

# Docker (from the repository root: shared/ is embedded at build time)
docker build -f rpg-c/Dockerfile -t rpg-c .
docker run --rm -it rpg-c --seed 42
```

On Windows (Git Bash) put LLVM-MinGW first on the `PATH` and pass the compiler to the first configure:
`export PATH="$HOME/scoop/apps/mingw-mstorsjo-llvm-ucrt/current/bin:$PATH"` and
`cmake --preset debug -DCMAKE_C_COMPILER=clang`. Git Bash's own window (mintty) is not a Windows console: run the game
through `winpty` there (`setups/play-on-windows-version-c.sh` does it), or use Windows Terminal.

## Structure

```
rpg-c/
├── CMakeLists.txt / CMakePresets.json / .clang-format / Dockerfile (+ Dockerfile.dockerignore)
├── cmake/embed_shared.cmake          # generates shared_files.c from ../shared (data, i18n, art)
├── src/
│   ├── main.c                        # prints what run() produced
│   ├── main_run.{h,c}                # run(): flags → simulator or TUI (testable, like Go's run())
│   ├── version.h
│   ├── assets/shared_files.h         # the embedded shared/ tree (the generated .c lives in build/)
│   ├── domain/                       # base (ids, StrBuf, fatal), json_types, rng, enums, definitions, formulas,
│   │                                 # entities, character
│   ├── application/                  # engine, battle, merchant, loot, spawner, progression, statistics, run_state,
│   │                                 # save_game, profile, ports, game_session, bot, simulator, commands, events,
│   │                                 # auto_equip, auto_battle
│   ├── infrastructure/               # data_loader, i18n, art, repositories (+ system clock), paths, migrations,
│   │                                 # filesystem (the POSIX/Windows file calls)
│   └── presentation/                 # cli, event_text, render, controller, simulator_report
│       └── tui/                      # app (pure ANSI renderer of the controller), terminal (raw input, platform code)
└── tests/                            # one executable (rpg_tests), four ctest entries
    ├── unit/ integration/ golden/ e2e/
    ├── support/                      # test.{h,c} (the harness) and helpers (temp dirs, fake clock, fixtures)
    └── fixtures/                     # schema 2 and schema 1 saves + profiles written by the Python reference, the
                                      # migrated v1 documents and the Python continuation of each save
```

Every layer is compiled into one static library, `rpg_core`, linked by the binary and by the tests. Each `.h` declares
a module's public interface (with a header comment explaining its role) and the matching `.c` holds the definitions;
helpers private to a file are `static`.

## Notes (C for readers coming from Python, TypeScript or Go)

- **No classes, no methods.** A Python class becomes a struct plus functions that take it as their first argument:
  `engine.step(command)` → `engine_step(&engine, &command, &events)`. A module's "private" functions are `static`.
- **Values vs. owners.** Most game types (`Player`'s bag, `MonsterInstance`, `ItemInstance`, `Event`, `Command`) are
  plain structs with fixed-size arrays inside, so assignment copies them and nothing needs freeing. The few types
  that own heap memory say so in their header and come with `*_free()` and `*_clone()` (`RunState`, `GameEngine`,
  `GameSession`, `Profile`, `JsonValue`). The merchant snapshot that Go and TypeScript build by serialising the state
  is `run_state_clone()` here.
- **Ids are fixed buffers.** `typedef char Id[48]` keeps content ids inside the structs that use them (no pointers
  into the data, no lifetime questions) and makes structs trivially copyable. Closed sets (`Element`, `Slot`, `Stat`,
  `Phase`…) are enums with `*_name()`/`*_parse()` for the JSON strings, and maps keyed by them are arrays indexed by
  the enum (`resistances[ELEMENT_FIRE]`, `stats[STAT_ARMOR]`).
- **Implementation limits are not balance numbers.** `MAX_ATTACKS`, `MAX_BAG`, `MAX_AFFIXES`… size the fixed arrays;
  the data loader and the save reader reject files that do not fit instead of overflowing.
- **Errors without exceptions.** Expected failures are return values: `bool` + an error buffer, `LoadResult`
  (`LOAD_OK` / `LOAD_MISSING` / `LOAD_FAILED`), or the sticky `JsonError` (read every field, check `failed` once at
  the end). Player mistakes are `error` events. A bug — an unknown data id, a negative `pct` operand — stops the
  program through `fatal()`, the counterpart of an uncaught exception.
- **Integer math and the PRNG.** All game math is `int64_t`; operands are never negative, so C's truncating division
  is the floor the rules ask for (`pct()` rejects negative operands). The PRNG is five lines of `uint32_t`
  arithmetic, which wraps exactly like `Math.imul`.
- **Deterministic order.** Lists keep file order; "sorted by id" is `qsort` with `strcmp`, which orders ASCII ids by
  code point. `Counter` (the string → integer map of potions, spell uses and statistics) keeps its entries sorted by
  key, the order saves are written in. There is no hash map anywhere.
- **JSON in ~500 lines.** `domain/json_types` is a recursive-descent parser and a writer over one `JsonValue` struct.
  Objects keep their key order (saves are built in the reference's order and written with tab indentation, so a C
  save looks like a Python one), numbers are 64-bit integers only (the data files and saves hold no fractions).
- **Events** are a type plus up to seven named fields (`int`, text or `bool`) in a fixed struct; `event_to_json()`
  turns them into the flat objects the golden files compare. **Commands** are a tagged struct (`type` + `id`,
  `number`, `slot`).
- **Interfaces are structs of function pointers.** `application/ports.h` declares `Clock` and `Repositories` (load/
  write save, history, profile) with a `context` pointer; `infrastructure/repositories.c` fills them with the file
  implementations and tests pass a fake clock.
- **Embedding `shared/`.** C has no `go:embed` and no raw strings, and a string literal is only guaranteed 4095
  characters: `cmake/embed_shared.cmake` turns every data, i18n and art file into a `static const unsigned char[]`
  (CRLF normalised) inside a generated `shared_files.c`. The binary is self-contained; golden files and fixtures are
  read by the tests at runtime through compile definitions (`RPG_GOLDEN_DIR`, `RPG_FIXTURES_DIR`).
- **The TUI is two files.** `tui/app.c` is pure: it turns the controller's state into a list of styled spans and then
  into one string of ANSI escapes (`app_render`) or plain text (`app_render_plain`, used by the e2e tests), and
  handles keys and timer ticks as plain function calls, like the Elixir renderer. `tui/terminal.c` is the only
  impure part: raw mode with `termios` + `poll` on POSIX, the Win32 console API on Windows (one `#ifdef`), a
  single-threaded loop that waits for a key or the next timer (500 ms animation, auto-battle pace). Without an
  interactive terminal (a pipe) the game falls back to line input: each line is a sequence of keys followed by Enter.
- **Platform code lives in two files only**: `infrastructure/filesystem.c` (directories, atomic rename) and
  `presentation/tui/terminal.c`. `_POSIX_C_SOURCE` is defined by CMake so the POSIX declarations are visible under
  `-std=c17`.
- **Migrations.** `infrastructure/migrations` upgrades the raw JSON (version 1 → 2) before the application layer
  parses it, exactly like the reference. To refresh the Python fixtures after a save-format change, run the
  reference with the same seeds (archer, hard, seed 2024, auto-equip on, saved at the round 12 merchant) and keep the
  v1 files: they guard the migration.
- **Tests** use a 100-line harness: `TEST("unit/rng", name) { CHECK_INT(a, b); }` registers itself before `main`
  (a constructor attribute, supported by GCC and Clang), `rpg_tests <filter>` runs the tests whose name contains the
  filter, and ctest runs the four levels. They never touch the real saves: the runner points `RPG_DATA_DIR` at a
  temporary directory and sets `RPG_NO_ANIM=1`; every persistence test uses its own temporary directory. A reference
  assertion that expects an exception for a *bug* (`pytest.raises` on a negative `pct`) has no in-process
  counterpart, because `fatal()` ends the process.
- **File paths on Windows** go through the C runtime's narrow-character functions, so a data directory with
  characters outside the system code page is not supported (the default `%USERPROFILE%\.cli-turn-based-rpg` usually
  is fine; otherwise pass `--data-dir`).
