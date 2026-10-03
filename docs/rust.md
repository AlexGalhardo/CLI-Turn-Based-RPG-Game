# Rust Implementation (`rpg-rust/`)

Written from scratch following the Python reference: same layers, file names and behaviour. It passes every
`shared/golden` scenario, its bot issues exactly the commands recorded by the Python bot, its `--simulate` report is
byte-identical to Python's, and its run state serialises to the same JSON (saves are interchangeable with the other
implementations, in both directions).

## Stack

- Rust stable 1.99, edition 2024 (`Cargo.toml` → `rust-version`)
- [ratatui](https://ratatui.rs) 0.30 + [crossterm](https://github.com/crossterm-rs/crossterm) 0.29 for the TUI
- [serde](https://serde.rs) + [serde_json](https://github.com/serde-rs/json) for data files, saves and golden files
- Nothing else: CLI parsing, timestamps, temp dirs and the random seed use the standard library
- `rustfmt` (tabs, 120 columns, `rustfmt.toml`) and `cargo clippy` with the `pedantic` group (`[lints]` in
  `Cargo.toml`, with documented exclusions)
- Tests: built-in `cargo test`; coverage with [cargo-llvm-cov](https://github.com/taiki-e/cargo-llvm-cov)

## Commands

```bash
cd rpg-rust
cargo run --release -- --seed 42                  # play
cargo build --release                             # → target/release/rpg-rust(.exe), self-contained
cargo fmt --check && cargo clippy --all-targets -- -D warnings
cargo test                                        # unit + integration + golden + e2e
cargo llvm-cov --summary-only                     # coverage (cargo install cargo-llvm-cov)
cargo run --release -- --simulate 30 --seed 1     # balance report (same output as uv run rpg --simulate 30)
```

## Structure

```
rpg-rust/
├── build.rs                        # embeds ../shared/{data,i18n,art} (generated include_str! table)
├── src/
│   ├── main.rs                     # flags → simulator or TUI (run() is testable)
│   ├── lib.rs · version.rs · assets.rs (SharedFs: the embedded files)
│   ├── domain/                     # rng, enums, definitions, formulas, entities, character
│   ├── application/                # engine, battle, merchant, loot, spawner, progression, statistics, run_state,
│   │                               # save_game, profile, game_session, ports, bot, simulator, commands, events
│   ├── infrastructure/             # data_loader, i18n, art, repositories (+ clock), paths
│   └── presentation/               # cli, event_text, render, controller, simulator_report
│       └── tui/app.rs              # ratatui renderer + event loop
└── tests/                          # golden, integration_*, e2e_tui (+ common/ fixtures, like conftest.py)
```

Unit tests that need no game data live next to the code in `#[cfg(test)] mod tests`; everything that loads the
shared data is an integration test in `tests/` (each file is its own crate that only sees the public API).

## Notes for side-by-side reading

- **Embedding.** `include_str!` needs literal paths and Cargo can't embed a parent directory, so `build.rs` walks
  `../shared` and generates a table of `include_str!` calls (Go copies the folder with `go generate` instead). It emits
  `cargo:rerun-if-changed`, so editing shared data rebuilds the binary. Golden files are read from disk by the tests.
- **PRNG.** `u32::wrapping_add` / `wrapping_mul` give mulberry32's modulo-2³² arithmetic explicitly; game math uses
  `i64`. In debug builds a plain `+` that overflows panics, which is a useful guard everywhere else.
- **Events and commands are enums.** Python and Go use loose maps; here `Event` and `Command` are enums with data, and
  serde's internally tagged representation (`#[serde(tag = "type", rename_all_fields = "camelCase")]`) produces the
  exact flat JSON of the golden files. A missing or misspelled field is a compile error, and `match` must be
  exhaustive, so adding an event type forces every consumer (statistics, UI cues) to be revisited.
- **Saves.** Entities derive `Serialize`/`Deserialize`; field order follows the reference `to_dict()` and maps are
  `BTreeMap`s, so the files come out in the same key order as Python (tab-indented through serde_json's
  `PrettyFormatter`). `Slot` implements `Ord` by its string so equipment is sorted like Python's `sorted()`.
- **No hash-map decisions.** Definitions keep `Vec`s in file order; `HashMap` indexes are private to `GameData` and
  only used for lookups by id. Every "pick" sorts by `id` with `str::cmp` (byte order = code-point order).
- **`max(key=…)` ties.** Python's `max` keeps the first maximum and Rust's `Iterator::max_by` the last; the bot's keys
  always end with a unique id, so both pick the same element.
- **Ownership instead of shared mutable objects.** `GameEngine` owns the run state and the PRNG; `Battle` and `Merchant`
  borrow them (`&mut`) for one command, so the compiler guarantees nothing else touches the state mid-turn. Long-lived
  shared values (game data, repositories, clock) are `Rc<…>`; ports are traits used as `Rc<dyn SaveRepository>`.
- **Errors.** Lookups of ids that the data itself references panic (`data.spell(id)`), like Go; ids coming from outside
  use `find_*` → `Option`. Player mistakes are `error` events; persistence problems are `Result<_, PersistenceError>`
  and the controller shows them (`controller.error`) instead of crashing.
- **Menus are data.** The reference controller stores closures per option. Closures that capture `&mut self` fight the
  borrow checker, so each option carries an `Action` enum value and `Controller::run(action)` performs it.
- **Environment in tests.** `std::env::set_var` is `unsafe` since edition 2024, so the crate forbids `unsafe` and tests
  call `resolve_data_dir_from(cli, env, home)` (the pure part) or spawn the binary with `Command::env`. Every test uses
  its own temp directory (`tests/common::TempDir`).
- **TUI.** ratatui is immediate-mode: `App::render` rebuilds the widgets from the controller on every frame. E2E tests
  render to `ratatui::backend::TestBackend` and press keys through `App::on_key`, including a whole run to death.
- **Simulator seed.** `--simulate N --seed 0` uses base seed 1, exactly like Python's `options.seed or 1`.
