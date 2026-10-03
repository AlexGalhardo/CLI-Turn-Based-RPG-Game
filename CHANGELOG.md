# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Elixir implementation (`rpg-elixir`) at full parity with the Python, TypeScript and Go versions, with zero Hex
  dependencies: immutable deterministic engine, bot, simulator (byte-identical report), interchangeable saves, UI
  controller and a hand-written ANSI terminal UI with OTP raw-mode input; shared content embedded at compile time;
  `mix escript.build` executable. 184 ExUnit tests (unit, integration, golden replay, bot and save parity, e2e by keys),
  97% coverage. Documented in `docs/elixir.md`; setup scripts in `setups/`.
- CI `elixir` job (Linux and Windows): `mix format`, warnings as errors, tests with coverage, escript build and smoke
  test. Commit scopes `rust`, `elixir` and `cpp`.

- Rust implementation (`rpg-rust`) at full parity: deterministic engine, bot, simulator (byte-identical report),
  interchangeable saves, UI controller and a ratatui + crossterm terminal UI; shared content embedded by `build.rs`;
  events and commands as serde enums that serialise straight to the golden-file JSON. 134 tests (unit, integration,
  golden replay, bot and save parity, e2e through ratatui's `TestBackend`), 98.8% line coverage on domain +
  application; clippy pedantic clean. Documented in `docs/rust.md`; setup scripts in `setups/`.
- CI `rust` job (Linux and Windows): rustfmt, clippy with warnings denied, tests with `cargo llvm-cov` coverage floors,
  release build and smoke test.

### Fixed

- Go and TypeScript simulators now treat `--seed 0` as base seed 1 like the Python reference (`options.seed or 1`);
  their balance reports for seed 0 differed from Python's. Found by the Rust port's simulator diff.

### Changed

- `.editorconfig`: Elixir sources use 2-space indentation, the only style the Elixir formatter supports.

## [1.0.0] - 2026-09-27

First stable release: the same game in Python, TypeScript and Go, proven equivalent by shared golden files.

### Added

- `release.yml`: on a `v*` tag, cross-compiles the TypeScript and Go executables for linux-x64, darwin-arm64 and
  windows-x64 and publishes them with SHA-256 checksums in a GitHub Release whose notes come from this changelog.
- `release` project skill describing the version bump and tagging flow.
- README screenshots (text snapshots of the battle, boss and merchant screens) and download instructions for the
  release binaries.

## [0.6.0] - 2026-09-27

### Added

- Go implementation (`rpg-golang`) at full parity with the Python and TypeScript versions: deterministic engine, bot,
  simulator, persistence (interchangeable saves), UI controller and a Bubble Tea v2 terminal UI; shared content embedded
  with `go generate` + `go:embed`; executable via `go build`.
- Go tests: unit, integration, golden replay, bot parity, save-format parity and end-to-end runs through the Bubble Tea
  model (94.7% coverage); golangci-lint v2 with the recommended configuration and documented exclusions.
- CI job for Go on Linux and Windows (gofmt, vet, golangci-lint, race-enabled tests with coverage, build, smoke test).
- The 46 `golang-*` skills of samber/cc-skills-golang (MIT) in `.claude/skills`.

### Changed

- TypeScript coverage thresholds are enforced per file by Bun: 90% lines and 80% functions (renderer and CLI files have
  small terminal-only callbacks). Domain and application files remain above 90% functions.

## [0.5.0] - 2026-09-27

### Added

- TypeScript implementation (`rpg-typescript`) at full parity with the Python reference: deterministic engine, bot,
  simulator, persistence (interchangeable saves), UI controller and an Ink terminal UI that renders the same screens
  as the Textual version. Single-file executable via `bun build --compile`.
- 117 TypeScript tests: unit, integration, golden replay of every Python-recorded scenario, bot parity (the TypeScript
  bot issues exactly the recorded commands) and end-to-end Ink tests including a whole run played by keys.
- Golden files now include the final run state (`finalRun`), proving save-format parity across languages.
- CI job for TypeScript on Linux and Windows (Biome, tsc 7, tests with coverage, build and smoke test).

## [0.4.0] - 2026-09-27

### Changed

- Balance pass with the simulator: monster XP ×1.5, bosses with 3× HP and 1.2× damage, EASY at 80% and HARD at 125%,
  sturdier Mage. NORMAL bot medians are now rounds 40–50 for every vocation (table in `docs/game-design.md` §12).

### Added

- `port-feature` project skill with the porting order and the parity traps for TypeScript and Go.

## [0.3.0] - 2026-09-27

### Added

- Python terminal UI built with Textual: title, language selection (first launch and from the title), new run
  (difficulty, name, vocation), battle with animated ASCII monster, HP/MP bars and combat log, spell and potion menus,
  merchant (potions, selling, equipment, stock, character sheet), save & quit, continue, game over, Hall of Fame,
  bestiary and achievements. All texts in English and Brazilian Portuguese.
- Framework-independent UI controller that the TypeScript and Go ports will mirror.
- Original ASCII art for 16 monster families and 10 bosses (`idle`, `attack`, `hurt` animations).
- End-to-end tests driving the real TUI with Textual Pilot, including a whole run played by keys until game over.

- 151 items (TibiaWiki names) covering 8 slots in all 10 tiers, with weapons per vocation, shields and spellbooks;
  21 affixes (including one elemental ward per element) and 18 achievements.
- Python persistence: settings, auto-save at the merchant with atomic writes, resume (mid-battle quits resume from the
  last merchant visit), history of finished runs with full statistics and timestamps, profile with bestiary,
  achievements and Hall of Fame, schema version checks.

## [0.2.0] - 2026-09-27

### Added

- Shared content: 110 monsters in 10 tiers and 10 bosses (TibiaWiki names, original stats), three vocations
  (Warrior, Archer, Mage) with Tibia spells, potions from Health to Supreme, statuses, balance knobs and starter
  weapons, all validated by JSON Schemas (`bun run check:shared`).
- `shared/i18n` with English (default) and Brazilian Portuguese, with a CI check that both have the same keys.
- Python reference engine (`rpg-python`): mulberry32 PRNG, integer formulas, pure `step(command) → events` state
  machine with battle (melee, spells, potions, defend, crit, dodge, parry, leech, elemental resistances and
  protections, statuses, boss telegraph/charge), spell levels by use, magic level, infinite tiers with cycle
  scaling, merchant (potions, selling, equipment, rotating stock), item factory with rarities and affixes, drops and
  run statistics.
- Greedy bot and balance simulator (`uv run rpg --simulate N`), golden file generator (`uv run rpg-golden`) and the
  first golden files (PRNG vectors, scripted scenarios and 9 bot full runs).
- 131 Python tests (unit, integration, golden) with 95% coverage; CI job for Python on Linux and Windows.
- Project skills `golden-files` and `add-game-content`.

### Changed

- Vocations are Warrior (+15 HP/+5 MP per level), Archer (+10 HP/+15 MP) and Mage (+5 HP/+15 MP).
- The magic level threshold is cumulative mana spent; a consumed stun gives a 2-turn stun cooldown.

## [0.1.0] - 2026-09-27

### Added

- Monorepo for the Python, TypeScript and Go implementations, with a language-agnostic `shared/` folder.
- Root tooling: EditorConfig (tabs, width 4), Biome, commitlint + husky, lint-staged, Dependabot, markdownlint config.
- Documentation written from scratch: architecture, game design (rules and formulas), cross-language parity, shared
  data format, persistence, terminal UI, testing, CI/CD and versioning, per-language guides and ADRs 0001–0005.
- `PLAN.md` with milestones (foundation, Python alpha/beta/1.0, TypeScript and Go ports, release 1.0).
- `AGENTS.md` / `CLAUDE.md`, `README.md`, `CONTRIBUTE.md`, MIT `LICENSE`.
- `setups/` play scripts for Windows (Git Bash) and Linux/macOS for each implementation.
- CI workflow skeleton with per-language jobs.
- The 2016 Python and 2022 TypeScript code is not carried over; the originals remain in their own repositories.

[Unreleased]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.6.0...v1.0.0
[0.6.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/releases/tag/v0.1.0
