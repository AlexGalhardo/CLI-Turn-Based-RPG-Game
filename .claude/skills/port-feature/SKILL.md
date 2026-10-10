---
name: port-feature
description: Use when porting the Python reference implementation (rpg-python) of the CLI Turn-Based RPG to any port (rpg-typescript, rpg-golang, rpg-rust, rpg-elixir, rpg-cpp, rpg-c, rpg-asm), when adding a new language port, or when a rule/UI change made in Python must be replicated in the other languages.
---

# Porting a feature from the Python reference

Python is the reference (ADR 0005). A port is correct when **all `shared/golden/*.json` files replay identically**,
its bot issues the same commands, its `--simulate` report is byte-identical to Python's, saves are interchangeable,
and the UI controller behaves the same. Ports: TypeScript, Go, Rust, Elixir, C++, C (and the `rpg-asm` engine).

## Order of work (per feature or per layer)

1. Read the Python module and its tests; read the matching section of `docs/game-design.md` /
   `docs/cross-language-parity.md`.
2. Port in dependency order: `domain` (rng, formulas, definitions, entities, character) → `application` (events,
   commands, statistics, run state, loot, spawner, progression, battle, merchant, engine, bot, simulator, save game,
   profile, game session) → `infrastructure` (data loader, paths, i18n, art, repositories) → `presentation`
   (event text, render helpers, controller, CLI, TUI renderer).
3. Keep file and type names mirrored: `battle.py` ↔ `battle.ts` ↔ `battle.go` ↔ `battle.rs` ↔ `battle.ex` ↔
   `battle.{hpp,cpp}`, `GameEngine` everywhere.
4. Port the tests with the code (same cases, same fixtures). Golden replay tests come right after the engine.
5. Only then write the framework renderer (Ink / Bubble Tea / ratatui / the Elixir ANSI renderer / FTXUI) on top of
   the ported controller.

## Parity traps (learned)

- Integer math only; `pct(v, p)` floors. TypeScript: `Math.floor(v * p / 100)`; Go/Rust/C++: 64-bit integer division;
  Elixir: `div/2` (values are never negative).
- PRNG: `Math.imul` + `>>> 0` in TS; `uint32` in Go; `u32::wrapping_*` in Rust; `Bitwise` + `band(…, 0xFFFFFFFF)` in
  Elixir; `std::uint32_t` in C++. Check against `shared/golden/prng.json` first.
- `chance(p)` consumes nothing when `p <= 0` or `p >= 100`.
- Every "pick"/"sorted by id" uses code-point string order (`a < b`), never locale compare.
- Iterate JSON arrays in file order; never iterate a map/object to make a game decision (Go maps are random!).
- Event objects must have exactly the fields of `docs/cross-language-parity.md` §3 (no extra `undefined` fields in TS,
  no zero-value fields in Go: use explicit structs → JSON with the same keys).
- Saves use the same camelCase JSON as `RunState.to_dict()`; test that a Python save loads in the port.
- The bot is part of the golden files: port `GreedyBot` decision by decision, including tie-breakers (`max` with
  `(value, id)` keys).
- The reference simulator uses `options.seed or 1`: `--simulate N --seed 0` runs with base seed 1. Go and TypeScript
  diverged here and had to be fixed, so always diff the simulator output for seed 0 too.

## Lessons from the TypeScript port (v0.5.0)

- Mirror the Python module 1:1 and run `tests/golden` right after the engine: the TS port passed every golden file and
  the bot parity check on the first run by following this order.
- Compare simulator reports between languages with `diff --strip-trailing-cr` (Python prints CRLF on Windows).
- Ink: `bun build --compile` must resolve the optional `react-devtools-core` peer → map it to a local stub via
  `tsconfig.json` `paths` (`--external` and `--define` did not work).
- ink-testing-library: a lone `ESC` needs ~100 ms before Ink emits it; long e2e tests need an explicit timeout
  (`bun test` defaults to 5 s).
- Never import helpers from a `*.test.ts` file (bun re-runs that file's tests); keep them in `tests/helpers.ts`.
- `node:util` `parseArgs` throws `TypeError` for usage errors; wrap them to match argparse's exit code 2.

## Lessons from the Rust port

- Events and commands as serde enums with `#[serde(tag = "type", rename_all = "snake_case", rename_all_fields =
  "camelCase")]` produce the golden JSON for free, and exhaustive `match` flags every consumer of a new event.
- Python's `max` keeps the first maximum, Rust's `max_by` the last; the unique id at the end of every bot key makes
  them agree. Check this whenever a key could tie.
- Saves: serde serialises fields in declaration order, so declare them in `to_dict()` order and use `BTreeMap` for
  maps to get the reference key order.
- Mimic argparse exactly: a negative number like `-1` is accepted as a value and then rejected with "must be >= 0";
  usage is printed before `rpg: error: …`; exit code 2.
- `std::env::set_var` is `unsafe` in edition 2024: keep path resolution pure (`resolve_data_dir_from(cli, env, home)`)
  and spawn the binary with `Command::env` in tests.
- `rustfmt.toml` with `use_small_heuristics = "Max"` keeps the repository's 120-column style; use
  `ratatui::try_init()` (returns an error) instead of `init()` (panics).

## Lessons from the Elixir port

- Events as string-keyed maps compare directly with `JSON.decode!` of the golden files; no conversion layer needed.
- An immutable PRNG returning `{value, rng}`, threaded through a `%Battle{}` struct, makes the draw order explicit.
- `max(key=(v, id))` → `Enum.max_by(list, &{v, &1.id})`.
- Maps with more than 32 keys are unordered: keep definitions as lists in file order, maps only as lookup indexes.
- Regexes that touch multibyte characters (box drawing, accents) need the `u` flag.
- `@tag :tmp_dir` creates folders inside the repository; use `System.tmp_dir!()` where that matters. Tests that mutate
  environment variables must be `async: false`.
- Windows: scoop's `.bat` shims for `mix`/`elixir` don't run from Git Bash (put the Elixir `bin` folder on the
  `PATH`), and escripts run as `escript bin/rpg-elixir` (no shebang support).

## Lessons from the C++ port

- Never read a counter with `std::map::operator[]`: it inserts a zero entry that leaks into saves. Use a `count_of()`.
- A temporary in a range-for initializer only gets lifetime extension from C++23 (P2718); keep a named local.
- CMake ≥ 3.28 with C++20/23 scans for modules by default → set `CMAKE_CXX_SCAN_FOR_MODULES OFF`.
- Clang ≤ 18 cannot compile libstdc++'s `std::expected`; use Clang ≥ 19 (or GCC 14+). GCC warns about partial
  designated initializers (`-Wmissing-field-initializers`), which `-Werror` turns into a build failure.
- When embedding `shared/` as raw strings, normalise CRLF and split long literals only at line ends (never inside a
  UTF-8 sequence).
- FTXUI: empty screen cells have `character == ""`, not a space; account for it when reading the screen in e2e tests.

## Lessons from porting a feature to five languages at once (M8, 1.4.0)

- Workflow that worked: finish and balance Python first (`bun run balance:check`), regenerate the golden files once,
  commit on a branch, then one worktree per language at a short path (`~/m8/<lang>`; deep paths break the MSVC linker
  and CMake on Windows) and one agent per worktree, each validated with `CI_JOBS="<lang>" bash scripts/ci-local.sh`.
  All five matched the golden files and the simulator on the first run after their engine port.
- Mirror the reference test helpers before porting tests: `calm()` (a monster that can't kill), `with_enemy_class`,
  `with_balance`. Make definition/balance types copyable with overrides (`with()` in TS, a lambda that edits the row in
  C++ — partial designated initializers break GCC `-Werror`); positional constructors make these ports painful.
- JSON objects whose key order means something (`autoBattle.modes` = menu order) lose it in Go maps and Elixir maps:
  read them with an ordered decoder or keep the order from the reference enum.
- Schema migrations run on the raw JSON (after the version check, before the typed decode), like the reference. Keep
  the old Python fixtures as v1 migration fixtures and regenerate the current ones with the reference.
- Timer-driven UI (auto-battle): the controller exposes one step, the renderer owns the clock, and tests send the tick
  event themselves (FTXUI `Event::Special`, Bubble Tea `tea.Tick` message, Ink effect keyed on a state flag); with
  `--no-anim` the whole fight runs at once.
- Windows tooling: Bash heredocs collapse `\` to `\` (breaks Elixir default arguments) — edit with the Write/Edit
  tools; Python edit scripts need `newline=""` and UTF-8 stdin; never share `/tmp` between parallel agents.
- Tests that must keep a fight going need a huge monster HP: after a balance change monsters often die in one hit.

## Lessons from the C port

- No GC: ids in fixed buffers (`typedef char Id[48]`) and fixed arrays inside structs make almost everything copyable
  by assignment; only `RunState` (counters + drop list) needs `clone`/`free`. Closed-set maps become enum-indexed
  arrays; keep a stat's "present" flag apart from its value (the UI lists present stats even when they are 0).
- Read JSON with a sticky error (read every field, check `failed` once) to keep `from_json` as short as Python's.
- Embed `shared/` as byte arrays generated by CMake (`file(READ … HEX)`): string literals stop at 4095 characters.
- Macro parameters named `count`/`items` collide with `x->count` inside the body: give them distinct names.
- GCC rejects `snprintf("%s/…")` with `-Wformat-truncation` where Clang accepts it: build on Linux/GCC early and
  join paths with a helper that checks the size.
- The reference FakeClock advances 10 s **before** returning; the number and order of clock reads in `GameSession`
  are part of the tested behaviour.
- A framework-less TUI: a pure core (`handle_key`/`tick`/`render`, with and without ANSI) plus a minimal impure shell
  (one single-threaded loop waiting for a key with a timeout) makes e2e tests trivial. Test POSIX raw mode in Docker
  with `script -qec`.
- Never hard-code the version in a test (`"rpg " RPG_VERSION`): every commit bumps it.
- Windows: a Bash heredoc turned `'\0'` into a NUL byte inside a `.c` file (edit sources with Write/Edit only);
  `rmdir` right after deleting files fails intermittently, so remove temp dirs with a retry.
- A `Dockerfile.dockerignore` next to the Dockerfile keeps the host's `build/` out without touching the root file.

## Lessons from the Assembly port (engine only)

- A port without a JSON parser: generate its tables from `shared/data` at build time and give the binary a `--replay`
  mode printing one canonical line per event plus a flattened final state; an external tool turns each golden file
  into script + expected lines and diffs. Compare `finalRun` too: it shows bag, affixes, stock and statuses that
  `finalState` hides. A `@bot` replay mode checks bot parity against the same files.
- When everything passes on the first run, tamper with a copy of a golden file and check the comparison tool fails.
- The reference `potions` dict keeps a key with quantity 0 after the last potion is drunk; an array-based port needs a
  "seen" flag to reproduce `finalRun`.
- "Sorted by id" lists can be pre-sorted by the generator (Python `sorted` is code-point order); only the bot
  tie-breakers need a runtime string compare.
- The reference simulator runs with the standard library alone (`PYTHONPATH=rpg-python/src python -m rpg --simulate …`),
  so the byte-for-byte diff works inside any Docker image with Python.
- Worktrees have no `.husky/_`, so hooks do not run on commits made there: lint and commit checks happen at merge.
- NASM 2.16.03: `alignb` fails under `-w+all -w+error` (use `section .bss align=16`); `%if` cannot evaluate
  `$ - label` (check table sizes with a `times` count that goes negative).

## Done when

- `shared/golden` replay passes, unit/integration/e2e suites pass, coverage floors met (see `docs/testing.md`).
- Bot commands match Python's, the simulator report is byte-identical (including seed 0), and a Python save continues
  identically in the port.
- `bun run check:shared` and the language's CI job(s) in `.github/workflows/ci.yml` are green; PLAN.md boxes ticked;
  CHANGELOG updated.
