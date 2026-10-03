<h1 align="center">CLI Turn-Based RPG</h1>

<p align="center">An endless turn-based RPG for the terminal CLI, implemented six times with identical rules: Python, TypeScript, Rust, Elixir, C++ and Go - Built with ClaudeCode Opus 5.5</p>

<p align="center">
	<a href="https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/actions/workflows/ci.yml"><img alt="CI" src="https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/actions/workflows/ci.yml/badge.svg?branch=main"></a>
	<a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/badge/license-MIT-blue.svg"></a>
	<a href="CHANGELOG.md"><img alt="Version" src="https://img.shields.io/badge/version-1.4.0-green.svg"></a>
</p>

## About

This project started in 2016 as my first Python program to learn object-oriented programming, got a TypeScript
version in 2022 to learn TypeScript, and in 2026 it became a didactic monorepo: the **same game** written in Python,
TypeScript, Go, Rust, Elixir and C++, side by side, with the same architecture, the same data and tests that prove the
six implementations behave exactly the same.

- Pick a vocation (Warrior, Archer or Mage), a difficulty (Easy, Normal, Hard) and a language (English or
  Português)
- Fight an **endless** sequence of monsters in tiers of increasing power, with a **boss every 10 rounds**
- Visit the merchant after every fight: potions, selling loot, a rotating stock
- Loot **common, rare, epic and legendary** equipment for 8 slots with random affixes: attack, crit, spell power,
  elemental protections, dodge, parry, leech…
- Spells level up the more you use them (level 2 after 20 casts, level 3 after 50)
- Status effects, elemental weaknesses and bosses that telegraph their strongest attacks
- Animated ASCII monsters, HP/MP bars, auto-save, run history, bestiary, achievements and Hall of Fame
- Seeded runs (`--seed`) reproducible across the six languages

Monster, boss, item and spell names are inspired by [Tibia](https://www.tibia.com) (via
[TibiaWiki](https://tibiawiki.com.br/)); all stats and ASCII art are original. This is a non-commercial, educational
project and is not affiliated with CipSoft.

## Screenshots

Captured from the Go version (`--seed 42`, Warrior, Normal); the other five versions render the same layout from the
same UI controller. A regular fight:

```text
╭─ Round 1 · Tier 1 · NORMAL · Seed 42 ────────────────────────────────────────────────────────────╮
│     (\,/)                       RAT                                                              │
│     (o.o)                       HP █████░░░░░░░░░░░░░░░░░░░░  13/64                              │
│   ~~(   )                       physical · weak: earth                                           │
│      " "                                                                                         │
│                                                                                                  │
│                                                                                                  │
╰──────────────────────────────────────────────────────────────────────────────────────────────────╯
╭──────────────────────────────────────────────────────────────────────────────────────────────────╮
│ Hero · Warrior · Lv 1 · ML 1   Gold 100                                                          │
│ HP ███████████████████████░░  167/180                                                            │
│ MP █████████████████████████  40/40   XP 0 / 100                                                 │
╰──────────────────────────────────────────────────────────────────────────────────────────────────╯
╭──────────────────────────────────────────────────────────────────────────────────────────────────╮
│ Rat hits you for 9 physical damage.                                                              │
│ You regenerate 3 HP and 1 MP.                                                                    │
│ Mana Potion restores 38 MP.                                                                      │
│ Rat hits you for 8 physical damage.                                                              │
│ You regenerate 3 HP and 0 MP.                                                                    │
╰──────────────────────────────────────────────────────────────────────────────────────────────────╯
╭──────────────────────────────────────────────────────────────────────────────────────────────────╮
│ Your turn                                                                                        │
│                                                                                                  │
│ [1] Attack                                      [2] Spells                                       │
│ [3] Potions                                     [4] Defend                                       │
│ [Q] Save & quit                                                                                  │
│                                                                                                  │
│                                                                                                  │
│                                                                                                  │
╰──────────────────────────────────────────────────────────────────────────────────────────────────╯
```

<details>
<summary>Round 10 boss and the merchant</summary>

```text
╭─ Round 10 · Tier 1 · NORMAL · Seed 42 ───────────────────────────────────────────────────────────╮
│      _/\/\_                     BOSS MUNSTER                                                     │
│     ( o  o )  ~                 HP █████████████████████████  240/240                            │
│   ~~(  ww  )~~                  physical · earth · weak: energy                                  │
│      (    )                                                                                      │
│       "  "                                                                                       │
│                                                                                                  │
╰──────────────────────────────────────────────────────────────────────────────────────────────────╯
╭──────────────────────────────────────────────────────────────────────────────────────────────────╮
│ Hero · Warrior · Lv 4 · ML 3   Gold 57                                                           │
│ HP ████████████░░░░░░░░░░░░░  111/230                                                            │
│ MP ███░░░░░░░░░░░░░░░░░░░░░░  8/55   XP 480 / 800                                                │
╰──────────────────────────────────────────────────────────────────────────────────────────────────╯
╭──────────────────────────────────────────────────────────────────────────────────────────────────╮
│ You looted 18 gold.                                                                              │
│ The merchant welcomes you.                                                                       │
│ You bought 1 Mana Potion for 50 gold.                                                            │
│ Round 10: the boss Munster challenges you! (240 HP)                                              │
│ Achievement unlocked: Survivor!                                                                  │
╰──────────────────────────────────────────────────────────────────────────────────────────────────╯
╭──────────────────────────────────────────────────────────────────────────────────────────────────╮
│ Your turn                                                                                        │
│                                                                                                  │
│ [1] Attack                                      [2] Spells                                       │
│ [3] Potions                                     [4] Defend                                       │
│ [Q] Save & quit                                                                                  │
│                                                                                                  │
│                                                                                                  │
│                                                                                                  │
╰──────────────────────────────────────────────────────────────────────────────────────────────────╯
```

```text
╭─ Round 0 · Tier 1 · NORMAL · Seed 42 ────────────────────────────────────────────────────────────╮
│           __/>                  CLI Turn-Based RPG                                               │
│     /\___/ o \__                An endless journey through Tibia's monsters                      │
│    <  ___      _>                                                                                │
│     \/   \ vv /                 v1.0.0 · Go                                                      │
│      /\  /\  /\                                                                                  │
│                                                                                                  │
╰──────────────────────────────────────────────────────────────────────────────────────────────────╯
╭──────────────────────────────────────────────────────────────────────────────────────────────────╮
│ Hero · Warrior · Lv 1 · ML 1   Gold 100                                                          │
│ HP █████████████████████████  180/180                                                            │
│ MP █████████████████████████  40/40   XP 0 / 100                                                 │
╰──────────────────────────────────────────────────────────────────────────────────────────────────╯
╭──────────────────────────────────────────────────────────────────────────────────────────────────╮
│ A new adventure begins!                                                                          │
│ The merchant welcomes you.                                                                       │
│                                                                                                  │
│                                                                                                  │
│                                                                                                  │
╰──────────────────────────────────────────────────────────────────────────────────────────────────╯
╭──────────────────────────────────────────────────────────────────────────────────────────────────╮
│ Merchant — prepare for your journey                                                              │
│ Welcome, Hero! You have 100 gold.                                                                │
│                                                                                                  │
│ [1] Buy potions                                 [2] Sell items                                   │
│ [3] Equipment                                   [4] Merchant stock                               │
│ [5] Character                                   [0] Next fight                                   │
│ [Q] Save & quit                                                                                  │
│                                                                                                  │
╰──────────────────────────────────────────────────────────────────────────────────────────────────╯
```

</details>

## Stack

| Implementation                         | Language / runtime                                                                    | TUI                                                      | Quality                                           |
| -------------------------------------- | ------------------------------------------------------------------------------------- | -------------------------------------------------------- | ------------------------------------------------- |
| [`rpg-python`](rpg-python) (reference) | Python 3.14 + [uv](https://docs.astral.sh/uv/)                                        | [Textual](https://textual.textualize.io/)                | Ruff, mypy, pytest                                |
| [`rpg-typescript`](rpg-typescript)     | TypeScript 7 + [Bun](https://bun.sh) 1.4.2 (single-file executable)                   | [Ink](https://github.com/vadimdemedes/ink)               | Biome, tsc, bun:test                              |
| [`rpg-golang`](rpg-golang)             | [Go](https://go.dev) 1.27 (executable)                                                | [Bubble Tea](https://github.com/charmbracelet/bubbletea) | gofmt, golangci-lint, go test                     |
| [`rpg-rust`](rpg-rust)                 | [Rust](https://www.rust-lang.org) 1.99, edition 2024 (executable)                     | [ratatui](https://ratatui.rs) + crossterm                | rustfmt, clippy, cargo test, cargo-llvm-cov       |
| [`rpg-elixir`](rpg-elixir)             | [Elixir](https://elixir-lang.org) 1.20 + Erlang/OTP 29 (escript), no Hex dependencies | hand-written ANSI renderer                               | mix format, ExUnit, `mix test --cover`            |
| [`rpg-cpp`](rpg-cpp)                   | C++23 + [CMake](https://cmake.org) and Ninja (executable)                             | [FTXUI](https://github.com/ArthurSonzogni/FTXUI)         | clang-format, `-Werror`, Catch2 + ctest, llvm-cov |
| [`shared`](shared)                     | JSON data, i18n, ASCII art, golden files                                              | —                                                        | JSON Schema                                       |

## Playing locally

Pick the script for your system and implementation. Each one checks the toolchain, installs dependencies and starts
the game:

| Implementation | Windows (Git Bash)                                                                      | Linux/macOS                                                                       |
| -------------- | --------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| Python         | [`play-on-windows-version-python.sh`](setups/play-on-windows-version-python.sh)         | [`play-on-unix-version-python.sh`](setups/play-on-unix-version-python.sh)         |
| TypeScript     | [`play-on-windows-version-typescript.sh`](setups/play-on-windows-version-typescript.sh) | [`play-on-unix-version-typescript.sh`](setups/play-on-unix-version-typescript.sh) |
| Go             | [`play-on-windows-version-golang.sh`](setups/play-on-windows-version-golang.sh)         | [`play-on-unix-version-golang.sh`](setups/play-on-unix-version-golang.sh)         |
| Rust           | [`play-on-windows-version-rust.sh`](setups/play-on-windows-version-rust.sh)             | [`play-on-unix-version-rust.sh`](setups/play-on-unix-version-rust.sh)             |
| Elixir         | [`play-on-windows-version-elixir.sh`](setups/play-on-windows-version-elixir.sh)         | [`play-on-unix-version-elixir.sh`](setups/play-on-unix-version-elixir.sh)         |
| C++            | [`play-on-windows-version-cpp.sh`](setups/play-on-windows-version-cpp.sh)               | [`play-on-unix-version-cpp.sh`](setups/play-on-unix-version-cpp.sh)               |

```bash
git clone https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game.git
cd CLI-Turn-Based-RPG-Game
bash setups/play-on-unix-version-python.sh   # or any script from the table above
```

No toolchain? Download a ready-to-run TypeScript, Go, Rust or C++ executable for Linux x64, macOS arm64 or Windows x64
from the [latest release](https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/releases/latest) (checksums in
`SHA256SUMS.txt`). On macOS/Linux, `chmod +x` it first. The release also ships the Elixir escript, a single file that
runs anywhere Erlang/OTP is installed (`escript rpg-elixir` on Windows).

Use a terminal of at least 100 × 30. Useful flags (same in the six versions): `--seed 42`, `--lang pt-BR`,
`--no-anim`, `--simulate 1000`.

## Commands

```bash
# Python (reference)
cd rpg-python && uv sync && uv run rpg
uv run ruff format && uv run ruff check && uv run mypy && uv run pytest

# TypeScript
cd rpg-typescript && bun install && bun run dev
bun run lint && bun run typecheck && bun test && bun run build

# Go
cd rpg-golang && go generate ./... && go run ./cmd/rpg
go vet ./... && go test -race ./... && go build -o bin/rpg-golang ./cmd/rpg

# Rust
cd rpg-rust && cargo run --release
cargo fmt --check && cargo clippy --all-targets -- -D warnings && cargo test && cargo build --release

# Elixir
cd rpg-elixir && mix run -e 'System.halt(Rpg.Main.run(System.argv()))'
mix format --check-formatted && mix compile --warnings-as-errors && mix test --cover && mix escript.build

# C++
cd rpg-cpp && cmake --preset release && cmake --build --preset release && ./build/release/rpg-cpp
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
```

## Documentation

The [`docs/`](docs) folder explains the project by area (it is also the context used by AI agents, through
[`AGENTS.md`](AGENTS.md)). The roadmap is in [`PLAN.md`](PLAN.md).

- [Architecture](docs/architecture.md)
- [Game design — rules and formulas](docs/game-design.md)
- [Cross-language parity](docs/cross-language-parity.md)
- [Shared data format](docs/data-format.md)
- [Persistence](docs/persistence.md)
- [Terminal UI](docs/tui.md)
- [Testing](docs/testing.md)
- [CI/CD and versioning](docs/ci-cd.md)
- [Architecture decision records](docs/adr)
- Implementation guides: [Python](docs/python.md) · [TypeScript](docs/typescript.md) · [Go](docs/golang.md) ·
  [Rust](docs/rust.md) · [Elixir](docs/elixir.md) · [C++](docs/cpp.md)

## Previous versions

- 2016 — [Python-CLI-Turn-Based-RPG](https://github.com/AlexGalhardo/Python-CLI-Turn-Based-RPG)
- 2022 — [TypeScript-CLI-Turn-Based-RPG](https://github.com/AlexGalhardo/TypeScript-CLI-Turn-Based-RPG)

## Contributing

Contributions are welcome. Read [`CONTRIBUTE.md`](CONTRIBUTE.md): commits follow
[Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) (checked by commitlint), versions follow
[SemVer](https://semver.org/) and every commit on `main` is its own release, described in
[`CHANGELOG.md`](CHANGELOG.md) ([Keep a Changelog](https://keepachangelog.com/en/1.1.0/)). CI blocks on formatting, lint, types, tests and builds of
the six implementations.

## Credits and license

Created by [Alex Galhardo](https://github.com/AlexGalhardo). Distributed under the [MIT license](LICENSE).
