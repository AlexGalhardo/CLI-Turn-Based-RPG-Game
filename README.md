<h1 align="center">CLI Turn-Based RPG</h1>

<p align="center">An endless turn-based RPG for the terminal, implemented three times with identical rules: Python, TypeScript and Go.</p>

<p align="center">
	<a href="https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/actions/workflows/ci.yml"><img alt="CI" src="https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/actions/workflows/ci.yml/badge.svg?branch=main"></a>
	<a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/badge/license-MIT-blue.svg"></a>
	<a href="CHANGELOG.md"><img alt="Version" src="https://img.shields.io/badge/version-0.2.0-green.svg"></a>
</p>

## About

This project started in 2016 as my first Python program to learn object-oriented programming, got a TypeScript
version in 2022 to learn TypeScript, and in 2026 it became a didactic monorepo: the **same game** written in Python,
TypeScript and Go, side by side, with the same architecture, the same data and tests that prove the three
implementations behave exactly the same.

- Pick a vocation (Warrior, Archer or Mage), a difficulty (Easy, Normal, Hard) and a language (English or
  Português)
- Fight an **endless** sequence of monsters in tiers of increasing power, with a **boss every 10 rounds**
- Visit the merchant after every fight: potions, selling loot, a rotating stock
- Loot **common, rare, epic and legendary** equipment for 8 slots with random affixes: attack, crit, spell power,
  elemental protections, dodge, parry, leech…
- Spells level up the more you use them (level 2 after 20 casts, level 3 after 50)
- Status effects, elemental weaknesses and bosses that telegraph their strongest attacks
- Animated ASCII monsters, HP/MP bars, auto-save, run history, bestiary, achievements and Hall of Fame
- Seeded runs (`--seed`) reproducible across the three languages

Monster, boss, item and spell names are inspired by [Tibia](https://www.tibia.com) (via
[TibiaWiki](https://tibiawiki.com.br/)); all stats and ASCII art are original. This is a non-commercial, educational
project and is not affiliated with CipSoft.

## Stack

| Implementation | Language / runtime | TUI | Quality |
|---|---|---|---|
| [`rpg-python`](rpg-python) (reference) | Python 3.14 + [uv](https://docs.astral.sh/uv/) | [Textual](https://textual.textualize.io/) | Ruff, mypy, pytest |
| [`rpg-typescript`](rpg-typescript) | TypeScript 7 + [Bun](https://bun.sh) 1.4.2 (single-file executable) | [Ink](https://github.com/vadimdemedes/ink) | Biome, tsc, bun:test |
| [`rpg-golang`](rpg-golang) | [Go](https://go.dev) 1.27 (executable) | [Bubble Tea](https://github.com/charmbracelet/bubbletea) | gofmt, golangci-lint, go test |
| [`shared`](shared) | JSON data, i18n, ASCII art, golden files | — | JSON Schema |

## Playing locally

Pick the script for your system and implementation. Each one checks the toolchain, installs dependencies and starts
the game:

| | Python | TypeScript | Go |
|---|---|---|---|
| **Windows** (Git Bash) | [`play-on-windows-version-python.sh`](setups/play-on-windows-version-python.sh) | [`play-on-windows-version-typescript.sh`](setups/play-on-windows-version-typescript.sh) | [`play-on-windows-version-golang.sh`](setups/play-on-windows-version-golang.sh) |
| **Linux/macOS** | [`play-on-unix-version-python.sh`](setups/play-on-unix-version-python.sh) | [`play-on-unix-version-typescript.sh`](setups/play-on-unix-version-typescript.sh) | [`play-on-unix-version-golang.sh`](setups/play-on-unix-version-golang.sh) |

```bash
git clone https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game.git
cd CLI-Turn-Based-RPG-Game
bash setups/play-on-unix-version-python.sh   # or any script from the table above
```

Use a terminal of at least 100 × 30. Useful flags (same in the three versions): `--seed 42`, `--lang pt-BR`,
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
- Implementation guides: [Python](docs/python.md) · [TypeScript](docs/typescript.md) · [Go](docs/golang.md)

## Previous versions

- 2016 — [Python-CLI-Turn-Based-RPG](https://github.com/AlexGalhardo/Python-CLI-Turn-Based-RPG)
- 2022 — [TypeScript-CLI-Turn-Based-RPG](https://github.com/AlexGalhardo/TypeScript-CLI-Turn-Based-RPG)

## Contributing

Contributions are welcome. Read [`CONTRIBUTE.md`](CONTRIBUTE.md): commits follow
[Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) (checked by commitlint), versions follow
[SemVer](https://semver.org/), and relevant changes go into the [`CHANGELOG.md`](CHANGELOG.md)
([Keep a Changelog](https://keepachangelog.com/en/1.1.0/)). CI blocks on formatting, lint, types, tests and builds of
the three implementations.

## Credits and license

Created by [Alex Galhardo](https://github.com/AlexGalhardo). Distributed under the [MIT license](LICENSE).
