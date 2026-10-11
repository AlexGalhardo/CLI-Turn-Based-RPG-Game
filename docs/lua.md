# Lua Implementation (`rpg-lua/`)

Written from scratch following the Python reference, for didactic purposes: the same game in a small scripting
language whose whole standard library fits on one page. Same layers, file names and behaviour: it passes every
`shared/golden` scenario, its bot issues exactly the commands recorded by the Python bot, its simulator prints the
same report byte for byte, and its run state serialises to the same JSON (saves are interchangeable with the other
implementations; a save written by Python is continued in a test and must end exactly like the Python continuation).

**The whole game is Lua.** Standard Lua, however, reads the keyboard one line at a time and cannot create or list a
directory, so the port ships as one executable: the official Lua interpreter, a C file of under 500 lines that adds
those missing calls (`host/rpg_host.c`), and the Lua sources and `shared/` compiled in. No LuaRocks package is used.

## Stack

- Lua 5.5.0 (the official source from lua.org, downloaded by CMake and pinned by SHA-256), no third-party Lua library
- The C host (`host/`, C17): `rpg_host.c` (raw keyboard, terminal size, `mkdir`, directory listing, atomic replace)
  and `main.c` (creates the Lua state and runs the game); CMake ≥ 3.28 + Ninja, `CMakePresets.json` (`debug`,
  `release`). Compilers: GCC or Clang; on Windows, LLVM-MinGW UCRT (`scoop install mingw-mstorsjo-llvm-ucrt`), linked
  statically so `rpg-lua.exe` needs no DLLs
- What the other ports take from a library is a small Lua module here: a JSON reader/writer
  (`domain/json_types.lua`), an ANSI-escape renderer (`presentation/tui/app.lua`) and a test harness
  (`tests/support/test.lua`)
- StyLua 2.5.2 (`.stylua.toml`: tabs, 120 columns), luacheck 1.2.0 (`.luacheckrc`), clang-format for `host/`
  (`-Wall -Wextra -Wpedantic -Werror`)

## Commands

```bash
cd rpg-lua
cmake --preset debug                       # configure (downloads the Lua source the first time)
cmake --build --preset debug               # build rpg-lua + rpg-lua-script (the test interpreter)
ctest --preset debug                       # four ctest entries: unit, integration, golden, e2e
./build/debug/rpg-lua-script tests/run.lua golden/     # one level, or any substring of "level/file/test name"
./build/debug/rpg-lua-script tests/run.lua --list      # every test name
cmake --preset release && cmake --build --preset release
./build/release/rpg-lua --seed 42          # play (rpg-lua.exe on Windows)
./build/release/rpg-lua --simulate 20 --seed 42
stylua --check src tests && luacheck src tests
clang-format --dry-run --Werror host/*.c host/*.h

# With a system Lua (5.4 or 5.5) nothing needs to be built: line-based input, shared/ read from disk
lua src/main.lua --simulate 20 --seed 42
lua tests/run.lua unit/

# Docker (from the repository root: shared/ is embedded at build time; the image build runs the tests)
docker build -f rpg-lua/Dockerfile -t rpg-lua .
docker run --rm -it rpg-lua --seed 42
```

On Windows (Git Bash) put LLVM-MinGW first on the `PATH` and pass the compiler to the first configure:
`export PATH="$HOME/scoop/apps/mingw-mstorsjo-llvm-ucrt/current/bin:$PATH"` and
`cmake --preset debug -DCMAKE_C_COMPILER=clang`. Git Bash's own window (mintty) is not a Windows console: run the game
through `winpty` there (`setups/play-on-windows-version-lua.sh` does it), or use Windows Terminal. For development:
`scoop install lua stylua luacheck`.

## Structure

```
rpg-lua/
├── CMakeLists.txt / CMakePresets.json / .stylua.toml / .luacheckrc / .clang-format / Dockerfile (+ .dockerignore)
├── cmake/embed.cmake                 # generates embedded.c from src/rpg/**.lua and ../shared (data, i18n, art)
├── host/                             # the only C: rpg_host.{h,c}, main.c, embedded.h, no_embedded.c
├── src/
│   ├── main.lua                      # entry point under a plain `lua` interpreter
│   └── rpg/
│       ├── main_run.lua              # run(args, out, err): flags → simulator or TUI (testable, like Go's run())
│       ├── version.lua
│       ├── domain/                   # base (classes, typed errors, stable sort), json_types, rng, enums,
│       │                             # definitions, formulas, entities, character
│       ├── application/              # engine, battle, merchant, loot, spawner, progression, statistics, run_state,
│       │                             # save_game, profile, ports, game_session, bot, simulator, commands, events,
│       │                             # auto_equip, auto_battle
│       ├── infrastructure/           # data_loader, i18n, art, repositories (+ system clock), paths, migrations,
│       │                             # filesystem (file calls, on top of the host or of the shell)
│       ├── presentation/             # cli, event_text, render, controller, simulator_report
│       │   └── tui/                  # app (pure ANSI renderer of the controller), terminal (wrapper of the host)
│       └── tools/golden.lua          # records the golden scenarios (compared with shared/golden by the tests)
└── tests/
    ├── run.lua                       # the runner
    ├── unit/ integration/ golden/ e2e/
    ├── support/                      # test.lua (the harness), helpers.lua and fixtures (temp dirs, fake clock)
    └── fixtures/                     # schema 2 and schema 1 saves + profiles written by the Python reference, the
                                      # migrated v1 documents and the Python continuation of each save
```

## Notes (Lua for readers coming from Python, TypeScript or Go)

- **One data structure.** A Lua table is at once the list, the dictionary and the object. Lists count from 1
  (`items[1]` is the first), so the places where the rules count from 0 say so (`BuyStockItem(index)`, the PRNG's
  `pick`, `render.list_key`). `#list` is the length; a table cannot hold `nil`, which is why JSON `null` is the
  value `json.null` and `body_colors()` uses `false` for "no colour".
- **Classes are tables.** `local Rng = base.class("Rng")` makes a table used as the metatable of its instances;
  `function Rng:roll(a, b)` is sugar for `function Rng.roll(self, a, b)` and `rng:roll(1, 6)` passes the instance.
  A Python property becomes a field or a method: `engine.state`, `engine:rng_state()`, `session:state()`.
- **Modules.** Every file ends with `return something`; `require("rpg.domain.rng")` runs the file once and caches
  that value. Names are `local` unless told otherwise: luacheck fails on an accidental global.
- **Integer math and the PRNG.** Since Lua 5.3 numbers are 64-bit integers or floats: `7 // 2` is the integer `3`
  (floor division, exactly what the rules ask for) while `7 / 2` is the float `3.5` and `2 ^ 3` is the float `8.0`,
  so the engine never uses `/` or `^`. Bitwise operators (`&`, `|`, `~` for XOR, `>>`, `<<`) work on the 64 bits;
  mulberry32 masks every intermediate value with `& 0xFFFFFFFF`, like the Python reference.
- **Errors.** There are no exceptions: `error(value)` unwinds to the nearest `pcall`. `base.raise("ValueError", msg)`
  raises a table with `kind` and `message` (the counterpart of a Python exception class) and `base.try(kinds, fn)`
  catches only those kinds. Player mistakes are `error` events, never raised.
- **Deterministic order.** `pairs()` visits a table in no particular order, so it is only used where order cannot
  matter (sums, lookups). Lists from JSON keep the file order; "sorted by id" is a plain `<` on strings (byte order
  in the "C" locale Lua starts with, which is code-point order for the ASCII ids); `table.sort` is not stable, so
  `base.sorted` breaks ties by original position, like Python's `sorted`.
- **JSON in ~400 lines.** `domain/json_types.lua` is a recursive-descent reader and a writer. Lua tables forget the
  order of their keys and cannot tell `[]` from `{}`, so the module remembers both in side tables with weak keys: an
  object keeps the key order of the file or of `json.object({ { key, value }, … })`, and `json.array(list)` marks a
  list. A save written here is byte for byte what Python's `json.dumps(indent="\t")` writes.
- **Events and commands** are plain tables: `{ type = "player_attacked", damage = 12, crit = false, … }` already is
  the flat object the golden files compare; a command is `{ type = "cast", spell_id = "…" }`.
- **Interfaces are duck-typed.** `application/ports.lua` documents `Clock` and the repositories and checks that an
  implementation has the methods; tests pass a fake clock (a table with `now`).
- **Time** is an integer: seconds since the Unix epoch, UTC (`os.time()`). `os.date("!…")` formats it; parsing a
  timestamp back needs a small calendar function because Lua has no `timegm`.
- **The host (C).** `host/rpg_host.c` is a Lua module written in C: each function takes its arguments from the Lua
  stack and pushes its results (`lua_pushstring`, `return 1`). `main.c` registers it in `package.preload` and adds a
  `package.searchers` entry so `require` finds the Lua sources inside the binary. Platform code lives only there:
  `termios` + `poll` on POSIX, the console API on Windows. Under a plain `lua` there is no host:
  `infrastructure/filesystem.lua` falls back to the shell (`mkdir`, `dir`/`ls`) and the TUI to line input.
- **Embedding.** `cmake/embed.cmake` turns every Lua module and every data, i18n and art file into a
  `static const unsigned char[]` (CRLF normalised) inside a generated `embedded.c`. The binary is self-contained;
  golden files and fixtures are read from disk by the tests.
- **The TUI is two files.** `tui/app.lua` is pure: it turns the controller's state into a list of styled spans and
  then into one string of ANSI escapes (`app:render(true)`) or plain text (`app:render(false)`, used by the e2e
  tests), and handles keys and timer ticks as plain method calls. `tui/terminal.lua` wraps the host: a
  single-threaded loop waits for a key or the next timer (500 ms animation, auto-battle pace). Without an interactive
  terminal (a pipe) the game falls back to line input: each line is a sequence of keys followed by Enter.
- **Tests** use a 160-line harness: `T.test("unit/rng", "roll is inclusive", function() T.eq(a, b) end)` registers a
  case, `tests/run.lua <filter>` runs those whose name contains the filter, and ctest runs the four levels with
  `rpg-lua-script` (the same interpreter and host as the game, running files from disk), so the host's filesystem
  functions are tested too. They never touch the real saves: every persistence test uses its own temporary directory.
  A reference test of a Python-only guarantee (a frozen dataclass) has no counterpart.
- **File paths on Windows** go through the C runtime's narrow-character functions, so a data directory with
  characters outside the system code page is not supported (the default `%USERPROFILE%\.cli-turn-based-rpg` usually
  is fine; otherwise pass `--data-dir`).
