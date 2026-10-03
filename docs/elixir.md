# Elixir Implementation (`rpg-elixir/`)

Written from scratch following the Python reference: same layers, file names and behaviour. It passes every
`shared/golden` scenario, its bot issues exactly the commands recorded by the Python bot, its simulator report is
byte-identical to Python's, and its run state serialises to the same JSON (saves are interchangeable with the
other five implementations).

## Stack

- Erlang/OTP 29 + Elixir 1.20 (`mix.exs` requires `~> 1.18` for the built-in `JSON` module)
- **No Hex dependencies**: JSON with Elixir's `JSON` module, tests with ExUnit, coverage with `mix test --cover`,
  CLI flags with `OptionParser`
- TUI: a hand-written ANSI renderer (box drawing, 24-bit colours) and raw keyboard input through OTP 28+
  `:shell.start_interactive({:noshell, :raw})`
- Distribution: an [escript](https://hexdocs.pm/mix/Mix.Tasks.Escript.Build.html) (`bin/rpg-elixir`, ~1.7 MB). It
  embeds `shared/{data,i18n,art}` at compile time, but **needs Erlang/OTP installed** on the machine that runs it
  (`escript bin/rpg-elixir` on Windows, `./bin/rpg-elixir` elsewhere)
- `mix format` (the Elixir formatter only supports 2-space indentation: an accepted exception to the repository's tabs
  rule) and `mix compile --warnings-as-errors`

## Commands

```bash
cd rpg-elixir
mix compile --warnings-as-errors
mix format --check-formatted
mix test --cover                       # all suites; fails below 80% total coverage
mix test test/golden                   # parity only
mix run -e 'System.halt(Rpg.Main.run(System.argv()))' -- --simulate 20 --seed 42
mix escript.build                      # → bin/rpg-elixir
escript bin/rpg-elixir --seed 42 --lang pt-BR
```

## Structure

```
rpg-elixir/
├── mix.exs / .formatter.exs
├── lib/rpg/
│   ├── main.ex                      # escript entry point: flags → simulator or TUI (run/1 is testable)
│   ├── version.ex
│   ├── domain/                      # rng, enums, json_types, definitions, formulas, entities, character
│   ├── application/                 # engine (GameEngine), battle, merchant, loot, spawner, progression,
│   │                                # statistics, run_state, save_game, profile, game_session, ports, bot
│   │                                # (GreedyBot), simulator, commands, events
│   ├── infrastructure/              # assets (embedded shared/), data_loader, i18n, art, paths, repositories
│   └── presentation/                # cli, event_text, render, controller, simulator_report
│       └── tui/app.ex               # pure renderer (App) + the terminal loop (Terminal)
└── test/{unit,integration,golden,e2e}/   # + test/support/helpers.ex (fixtures, FakeClock)
```

File names mirror Python (`battle.py` ↔ `battle.ex`); module names follow the file
(`lib/rpg/application/battle.ex` → `Rpg.Application.Battle`), except where the reference names a type that other
ports keep too: `GameEngine` (engine.ex), `GreedyBot` (bot.ex), `RunStatistics` (statistics.ex), `ProfileService`
(profile.ex).

## Elixir notes (for side-by-side reading)

- **Immutability.** Python, TypeScript and Go mutate the run state in place; here every value is immutable.
  `GameEngine.step(engine, command)` returns `{new_engine, events}`, `Controller.press(controller, key)` returns a new
  controller, and the merchant snapshot kept for auto-saves is just the old state (no deep copy needed). Tests set up
  situations with small helpers (`update_player/2`, `update_monster/2`) instead of assigning fields.
- **Explicit PRNG threading.** `Rng.roll(rng, min, max)` returns `{value, next_rng}`. The order of random draws is the
  parity contract, so it is visible in the code: whatever consumes randomness receives the generator and returns the
  next one. Inside a battle a `%Battle{}` value carries the data, the PRNG, the state and the emitted events.
- **Pattern matching** replaces `isinstance`/`switch`: commands are one struct each (`%Cast{spell_id: id}`) and the
  engine dispatches on the struct in function heads; events are maps with string keys, so they compare directly with
  the decoded golden files.
- **Behaviours vs interfaces.** Ports (`Clock`, `SaveRepository`, `HistoryRepository`, `ProfileRepository`) are
  behaviours. An adapter is a struct whose module declares `@behaviour` and `@impl true` callbacks; the port module
  dispatches with `adapter.__struct__.callback(adapter, ...)`. The compiler warns about a missing callback, much like a
  Go type that does not satisfy an interface. The test `FakeClock` keeps its moving time in an `Agent`, since values
  cannot change.
- **Maps are unordered** (above 32 keys), so game decisions never iterate a map: definitions keep lists in JSON file
  order, maps are only lookup indexes, and "sorted by id" uses `Enum.sort_by(& &1.id)` (byte order = code-point order).
- **Integer math** uses `div/2` and `rem/2` on non-negative integers; `/` (float division) never appears in the engine.
  The PRNG masks every intermediate with `Bitwise.band(…, 0xFFFFFFFF)`.
- **Saves** are written with sorted keys and tab indentation (`Repositories.encode_pretty/1`), the same layout as the Go
  port; `implementation` is `"elixir"`. A save written by Python loads and continues identically (tested).
- **Embedding.** `Rpg.Infrastructure.Assets` reads `../shared` at compile time into a module attribute
  (`@external_resource` recompiles on edits, `__mix_recompile__?/0` on added/removed files). `RPG_SHARED_DIR` switches
  to a directory at runtime; golden files are read from `../shared/golden` by the tests.
- **TUI.** `Rpg.Presentation.Tui.App` is pure (`handle_key/2`, `tick/1`, `resize/3`, `render/1`), the Elm-style split
  the Go port gets from Bubble Tea. `Tui.Terminal` is the only impure part: a reader process sends keys as messages, a
  500 ms `:timer` drives the animation, and a lone `ESC` is told apart from arrow-key sequences with a 30 ms
  `receive ... after`. Without a TTY (piped input) it falls back to line input. E2E tests drive `App` with keys and
  assert on `render_plain/1`, including a whole run to game over.
- Problems caused by the player become `error` events; exceptions are reserved for inconsistent data or invalid run
  configs, as in the other ports.
