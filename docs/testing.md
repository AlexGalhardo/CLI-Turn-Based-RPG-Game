# Testing

Every implementation has the same four levels of tests. The engine is pure, so most of the game is covered without a
terminal.

| Level | What it covers |
|---|---|
| Unit | PRNG vectors, formulas, stats aggregation, status effects, item generation, spell/magic levels, XP curve, i18n lookup, art parsing |
| Integration | Engine with real shared data: battles, victory/defeat, merchant, drops, saves round-trip, profile/achievements, data loader + schemas |
| Golden (parity) | Replays every `shared/golden/*.json` and compares events + final state |
| E2E (TUI) | Drives the real app with key presses: title → new run → battle → merchant → save & quit → continue; language switch; game over screen; full bot run to death |

Where each level lives:

| Implementation | Unit | Integration | Golden (parity) | E2E (TUI) |
|---|---|---|---|---|
| Python (pytest) | `tests/unit/` | `tests/integration/` | `tests/golden/` | Textual `App.run_test()` (Pilot) |
| TypeScript (bun:test) | `tests/unit/` | `tests/integration/` | `tests/golden/` | `ink-testing-library` |
| Go (testing) | `*_test.go` next to the code | `internal/.../*_integration_test.go` | `internal/golden/` | `teatest` |
| Rust (cargo test) | `#[cfg(test)] mod tests` next to the code | `tests/integration_*.rs` | `tests/golden.rs` | `tests/e2e_tui.rs` (ratatui `TestBackend`) |
| Elixir (ExUnit) | `test/unit/` | `test/integration/` | `test/golden/` | `test/e2e/` (pure `App` + `render_plain/1`) |
| C++ (Catch2 + ctest) | `tests/unit/` (`[unit]`) | `tests/integration/` (`[integration]`) | `tests/golden/` (`[golden]`) | `tests/e2e/` (`[e2e]`, FTXUI component rendered into a `Screen`) |

## Commands

```bash
# Python
cd rpg-python && uv run pytest                 # all tests + coverage
# TypeScript
cd rpg-typescript && bun test --coverage
# Go
cd rpg-golang && go test -race -cover ./...
# Rust
cd rpg-rust && cargo test                      # coverage: cargo llvm-cov --summary-only
# Elixir
cd rpg-elixir && mix test --cover
# C++ (coverage needs Clang: see docs/cpp.md)
cd rpg-cpp && cmake --preset debug && cmake --build --preset debug && ctest --preset debug
cmake --preset coverage && cmake --build --preset coverage && ctest --preset coverage   # then llvm-profdata + llvm-cov
# Shared data (root)
bun run check:shared
```

## Rules

- Tests never touch the real save directory: `RPG_DATA_DIR` points at a temp dir (fixtures do this).
- Tests always use a fixed seed and `--no-anim`.
- Coverage floor: **90% lines** on `domain` + `application`, **80%** overall. CI fails below it. Lowering a threshold
  requires a documented reason in the CHANGELOG.
- TypeScript: Bun applies thresholds per file, so `bunfig.toml` uses 90% lines and 80% functions for every file.
- Rust: CI runs `cargo llvm-cov --fail-under-lines 80`, then the report again with `presentation`, `infrastructure`
  and the entry files ignored and `--fail-under-lines 90`.
- Elixir: `mix test --cover` fails below **80% total** (`test_coverage: [summary: [threshold: 80]]` in `mix.exs`);
  there is no separate domain/application floor.
- C++: CI builds the `coverage` preset with Clang, merges the profiles with `llvm-profdata` and checks the
  `llvm-cov export` line totals: ≥ 80% for `src`, ≥ 90% for `src/domain` + `src/application`.
- Never delete or skip a failing test to get green; fix the cause or open an issue and mark it `xfail`/`skip` with the
  issue link.
- A bug fix starts with a failing test that reproduces it.
- Golden files are regenerated only by the Python reference and only when a rule change is intended.
