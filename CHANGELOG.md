# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Every commit to `main` is a release. Its version is computed from the Conventional Commit type: `feat` bumps the
minor version, `fix` and every other type bump the patch version, and a breaking change bumps the major version. The
history before this automation was re-tagged retroactively, one release per commit starting at 0.0.1: the old
milestone tags v0.1.0–v0.6.0 pointed at different commits and were replaced, while v1.0.0 still points at the same
commit.

## [Unreleased]

## [1.4.2] - 2026-10-05

### Added

- `scripts/release-local.sh vX.Y.Z`: builds the release binaries on the developer's machine (host toolchains for
  TypeScript, Go, Elixir and the Windows Rust/C++ builds; Docker for Rust and C++ on linux-x64 and darwin-arm64, the
  latter cross-compiled with zig), writes `SHA256SUMS.txt`, creates the GitHub Release if missing and uploads the
  assets, replacing the disabled `release.yml`. Documented in `docs/ci-cd.md`, the `release` skill and
  `.claude/rules/release.md`.

## [1.4.1] - 2026-10-03

### Changed

- Balance gate: every vocation must also be within target ± `vocationTolerancePct` (5) of each difficulty's win rate
  (`shared/data/balance-targets.json`), so no class is the obvious pick. The current data fails it on the Archer;
  `PLAN.md` M9 records the values found to fix it and the next steps (death tests to update, equipment screen paging).

## [1.4.0] - 2026-10-03

ARPG update (M8), in the six implementations at parity (golden files regenerated, simulator reports byte-identical).

### Added

- **Win the game**: beating Ferumbras in round 100 opens a Victory screen — end the run as a win (history and Hall of
  Fame, won runs first) or continue endlessly. New achievement `conqueror`.
- **Elite enemies**: 20% of non-boss fights; 3× HP, damage, XP and gold; always drop a rare (80%) or legendary (20%)
  item and, one time in three, a potion unlocked at that round.
- **Enemy combat chances** by class (normal 10%, elite 20%, boss 30%): dodge, parry, critical hit (+50%) and a heal of
  20% of max HP that uses their turn. **Parry reflects 20%** of the blocked damage to the attacker, both ways.
- **Item rarities** common, rare, legendary and mythic: 100/150/200/300% stats with 0/1/2/2 extra attributes; drop
  tables per enemy class (normal common 90 / rare 10, boss legendary 80 / mythic 20). Items have a **required level**
  (`1 + tier × itemLevelPerTier`) and an **item score** (weighted stats, like Diablo's item power).
- **Auto-equip** (chosen at new run, default in Settings): after every victory and merchant purchase the best usable
  item per slot is equipped and the replaced one is sold, both reported in the combat log.
- **Auto-battle** in battle (`[5]`): weapon focus (80/20), spell focus (80/20) or balanced (50/50) with need-based
  support turns and an emergency heal; it can't be cancelled; 1x/2x speed in Settings.
- **Settings screen** (language, auto-equip default, battle speed) and an **ARPG equipment screen**: all eight slots
  with empty ones highlighted, total score, bag items with required level and score delta, and a per-stat comparison
  (gains in green, losses in red, affixes gained and lost) before equipping.
- **Balance gate** `bun run balance:check` (`scripts/balance.ts`, targets in `shared/data/balance-targets.json`): plays
  1000 seeded bot runs per vocation and difficulty in parallel with any implementation's simulator. Result: EASY 76.2%,
  NORMAL 50.1%, HARD 25.6% wins (targets 75/50/25 ± 5). The simulator report gained `wins` and `win %`.

### Changed

- Spell levels 2 and 3 (20 and 50 uses) now give 150% and 200% effect.
- Difficulty multipliers rebalanced for the stronger enemies (monster HP and damage 25/30/36%); the HARD rarity bonus is
  gone (drop tables are per enemy class).
- Saves, settings, history and profile use schema 2; schema 1 files migrate on load (`epic` items become `legendary`).
- Selling lists only bag items; selling an equipped item is refused (`invalid_item`), now covered by tests.

## [1.3.6] - 2026-10-03

### Changed

- CI runs locally: the GitHub `CI` and `Release` workflows are kept but disabled, and the husky `pre-push` hook runs
  `scripts/ci-local.sh`, which mirrors `ci.yml` job by job for the projects touched by the pushed commits (`--all`,
  `CI_JOBS`, `SKIP_LOCAL_CI=1`). A `post-commit` hook creates the per-commit annotated tag `vX.Y.Z` with the
  CHANGELOG section, pushed along with `push.followTags`. Documented in `docs/ci-cd.md`, `CLAUDE.md`/`AGENTS.md` and
  the `release` skill.

## [1.3.5] - 2026-10-03

### Changed

- The husky `pre-commit` hook runs `graphify update .` and stages `graphify-out/`, so every commit (and release)
  carries a current knowledge graph; without graphify installed it only prints a reminder. `CLAUDE.md`/`AGENTS.md`
  and the `release` skill describe it.

## [1.3.4] - 2026-10-03

### Added

- graphify knowledge graph of the repository in `graphify-out/` (`GRAPH_REPORT.md`, `graph.json`, `graph.html`):
  AST extraction of the six implementations, `shared/` and scripts, plus semantic extraction of the docs, ADRs and CI
  workflows. `.graphifyignore` leaves out `.claude/`, golden files, ASCII art and lockfiles; machine-local graphify
  state is git-ignored and the generated files are marked `linguist-generated`.
- `CLAUDE.md`/`AGENTS.md`: refresh the graph before every push (`graphify update .`, `/graphify . --update` for docs)
  and use it to navigate the project (`GRAPH_REPORT.md`, `graphify query/path/explain`).

## [1.3.3] - 2026-10-03

### Changed

- README: new tagline and column-aligned Stack and setups tables; the contributing paragraph explains that every commit
  on `main` is a release.
- `CONTRIBUTE.md` asks for `bun run release:prepare` before committing; the Go, Elixir and C++ guides say saves are
  interchangeable with the other five implementations; `PLAN.md` describes the `release` skill's new flow.

## [1.3.2] - 2026-10-03

### Changed

- Documentation describes the six implementations: README (stack, setups, downloads, commands), `CLAUDE.md`/`AGENTS.md`
  (rules, scopes, quick commands, per-commit release flow), `CONTRIBUTE.md`, architecture, testing, TUI, parity, data,
  persistence and game design docs, with a later note in ADRs 0001 and 0004.
- `port-feature` skill covers every port and records the lessons of the Rust, Elixir and C++ ports (including the
  simulator seed-0 finding); `golden-files` and `add-game-content` skills list the six implementations.

## [1.3.1] - 2026-10-03

### Added

- Per-commit releases: `release.yml` now runs on every push to `main`, creates one GitHub Release per pushed commit
  (tag on that commit, notes from its CHANGELOG section) and attaches to the newest one the TypeScript, Go, Rust and
  C++ binaries for linux-x64, darwin-arm64 and windows-x64, the Elixir escript and `SHA256SUMS.txt`. A manual run
  rebuilds the binaries of an existing release (`tag` input) or does a dry run.
- `scripts/release.ts` with `bun run release:prepare "<subject>"` (computes the next version from the commit type,
  bumps the version in every implementation, turns `[Unreleased]` into the new section) and `bun run release:check`;
  tests in `scripts/release.test.ts`. The CI `repo` job runs both.

### Changed

- `CHANGELOG.md` rewritten with one section per commit since the first one (0.0.1), matching the retroactive tags.
- Version set to the per-commit scheme in every implementation (it was still 1.0.0 in the version files).
- `docs/ci-cd.md` and the `release` skill describe the per-commit flow; `PLAN.md` closes M7.
- `docs/cpp.md`: Clang 19 or newer is required (Clang 18 cannot compile libstdc++'s `std::expected`).

## [1.3.0] - 2026-10-03

### Added

- C++23 implementation (`rpg-cpp`) at full parity: deterministic engine, bot, simulator (byte-identical report),
  interchangeable saves (tested against fixtures produced by the Python reference), UI controller and an FTXUI terminal
  UI; FTXUI, nlohmann/json and Catch2 fetched by CMake `FetchContent` at pinned releases with SHA-256 hashes; shared
  content embedded by a CMake script. 112 Catch2 tests (unit, integration, golden replay, bot and save parity, e2e on a
  100×30 FTXUI screen), 99% line coverage on domain + application. Documented in `docs/cpp.md`; setup scripts in
  `setups/`.
- CI `cpp` jobs: clang-format, GCC 14 and Clang 20 builds with warnings as errors, ctest, Clang source-based coverage
  floors, LLVM-MinGW build on Windows, release build and smoke test.

## [1.2.0] - 2026-10-03

### Added

- Rust implementation (`rpg-rust`) at full parity: deterministic engine, bot, simulator (byte-identical report),
  interchangeable saves, UI controller and a ratatui + crossterm terminal UI; shared content embedded by `build.rs`;
  events and commands as serde enums that serialise straight to the golden-file JSON. 134 tests (unit, integration,
  golden replay, bot and save parity, e2e through ratatui's `TestBackend`), 98.8% line coverage on domain +
  application; clippy pedantic clean. Documented in `docs/rust.md`; setup scripts in `setups/`.
- CI `rust` job (Linux and Windows): rustfmt, clippy with warnings denied, tests with `cargo llvm-cov` coverage floors,
  release build and smoke test.

## [1.1.2] - 2026-10-03

### Fixed

- The TypeScript simulator now treats `--seed 0` as base seed 1 like the Python reference (`options.seed or 1`); its
  balance report for seed 0 differed from Python's. Covered by a test that spawns the entry point. Found by the Rust
  port's simulator diff.

## [1.1.1] - 2026-10-03

### Fixed

- The Go simulator now treats `--seed 0` as base seed 1 like the Python reference (`options.seed or 1`); its balance
  report for seed 0 differed from Python's. Covered by a CLI test. Found by the Rust port's simulator diff.

## [1.1.0] - 2026-10-03

### Added

- Elixir implementation (`rpg-elixir`) at full parity with the Python, TypeScript and Go versions, with zero Hex
  dependencies: immutable deterministic engine, bot, simulator (byte-identical report), interchangeable saves, UI
  controller and a hand-written ANSI terminal UI with OTP raw-mode input; shared content embedded at compile time;
  `mix escript.build` executable. 184 ExUnit tests (unit, integration, golden replay, bot and save parity, e2e by keys),
  97% coverage. Documented in `docs/elixir.md`; setup scripts in `setups/`.
- CI `elixir` job (Linux and Windows): `mix format`, warnings as errors, tests with coverage, escript build and smoke
  test. Commit scopes `rust`, `elixir` and `cpp`.

### Changed

- `.editorconfig`: Elixir sources use 2-space indentation, the only style the Elixir formatter supports.

## [1.0.1] - 2026-09-27

### Fixed

- `docs/game-design.md`: added the missing blank line before the item generation list so it renders as a list.

## [1.0.0] - 2026-09-27

First stable release: the same game in Python, TypeScript and Go, proven equivalent by shared golden files.

### Changed

- Version bumped to 1.0.0 in `package.json`, `rpg-python` (`pyproject.toml`, `__version__`), `rpg-typescript` and
  `rpg-golang` (`internal/version`); README badge and title-screen snapshot updated; changelog section published and
  `PLAN.md` milestone M6 marked done. This commit is the same one the original v1.0.0 tag pointed at.

### Added

- README download instructions for the TypeScript and Go release binaries (with `SHA256SUMS.txt` checksums).

## [0.10.7] - 2026-09-27

### Changed

- markdownlint `MD024` uses `siblings_only`, so changelog versions can repeat `### Added` / `### Changed` headings.

## [0.10.6] - 2026-09-27

### Added

- `release.yml` can be started manually (`workflow_dispatch`) as a dry run that builds the binaries without
  publishing a release; documented in `docs/ci-cd.md`.

## [0.10.5] - 2026-09-27

### Added

- README screenshots: text snapshots of the battle, boss and merchant screens.

## [0.10.4] - 2026-09-27

### Added

- `release` project skill describing the version bump and tagging flow, referenced from `AGENTS.md` / `CLAUDE.md`.

### Changed

- `PLAN.md` tracks the progress of milestone M6 (release 1.0).

## [0.10.3] - 2026-09-27

### Added

- `release.yml`: on a `v*` tag, cross-compiles the TypeScript and Go executables for linux-x64, darwin-arm64 and
  windows-x64 and publishes them with SHA-256 checksums in a GitHub Release whose notes come from this changelog.

## [0.10.2] - 2026-09-27

### Changed

- Version bumped to 0.6.0 in `package.json`, `rpg-python` (`pyproject.toml`, `__version__`) and `rpg-typescript`;
  README badge updated; changelog section published and `PLAN.md` milestone M5 (Go at parity) marked done. This was
  the manual milestone release commit for the former v0.6.0; under the per-commit scheme it is 0.10.2.

## [0.10.1] - 2026-09-27

### Added

- CI job for Go on Linux and Windows (gofmt, vet, golangci-lint, race-enabled tests with coverage, build, smoke test).
- `docs/golang.md` documents the Go implementation (layout, commands, libraries, testing).

## [0.10.0] - 2026-09-27

### Added

- Go implementation (`rpg-golang`) at full parity with the Python and TypeScript versions: deterministic engine, bot,
  simulator, persistence (interchangeable saves), UI controller and a Bubble Tea v2 terminal UI; shared content embedded
  with `go generate` + `go:embed`; executable via `go build`.
- Go tests: unit, integration, golden replay, bot parity, save-format parity and end-to-end runs through the Bubble Tea
  model (94.7% coverage); golangci-lint v2 with the recommended configuration and documented exclusions.

## [0.9.5] - 2026-09-27

### Added

- The 46 `golang-*` skills of samber/cc-skills-golang (MIT) in `.claude/skills`.

## [0.9.4] - 2026-09-27

### Changed

- Changelog note on the TypeScript coverage threshold change.

## [0.9.3] - 2026-09-27

### Changed

- TypeScript coverage thresholds are enforced per file by Bun: 90% lines and 80% functions (renderer and CLI files have
  small terminal-only callbacks). Domain and application files remain above 90% functions. Documented in
  `docs/testing.md`.

## [0.9.2] - 2026-09-27

### Changed

- Version bumped to 0.5.0 in `package.json` and `rpg-python` (`pyproject.toml`, `__version__`); README badge updated;
  changelog section published and `PLAN.md` milestone M4 (TypeScript at parity) marked done. This was the manual
  milestone release commit for the former v0.5.0; under the per-commit scheme it is 0.9.2.

## [0.9.1] - 2026-09-27

### Added

- CI job for TypeScript on Linux and Windows (Biome, tsc 7, tests with coverage, build and smoke test of the
  executable).
- `docs/typescript.md` documents the TypeScript port; `port-feature` and `add-game-content` skills updated with the
  TypeScript steps.

## [0.9.0] - 2026-09-27

### Added

- TypeScript persistence (interchangeable saves, history and profile), the framework-independent UI controller ported
  from Python and an Ink terminal UI that renders the same screens as the Textual version; CLI flags, simulator and a
  single-file executable via `bun build --compile`.
- 117 TypeScript tests: unit, integration, golden replay of every Python-recorded scenario, bot parity (the TypeScript
  bot issues exactly the recorded commands) and end-to-end Ink tests including a whole run played by keys.

## [0.8.1] - 2026-09-27

### Added

- Golden files now include the final run state (`finalRun`), proving save-format parity across languages; the Python
  golden test checks it.

## [0.8.0] - 2026-09-27

### Added

- TypeScript implementation (`rpg-typescript`): domain, application (engine, battle, merchant, loot, progression, bot,
  simulator) and the shared data loader ported from the Python reference. Every `shared/golden` scenario replays
  identically, and the TypeScript bot chooses exactly the commands recorded by the Python bot.

## [0.7.2] - 2026-09-27

### Changed

- Version bumped to 0.4.0 in `package.json` and `rpg-python` (`pyproject.toml`, `__version__`); README badge updated;
  changelog section published and `PLAN.md` milestone M3 (Python 1.0 refinement) marked done. This was the manual
  milestone release commit for the former v0.4.0; under the per-commit scheme it is 0.7.2.

## [0.7.1] - 2026-09-27

### Added

- `port-feature` project skill with the porting order and the parity traps for TypeScript and Go.

### Changed

- `docs/python.md` updated; `AGENTS.md` / `CLAUDE.md` list the new skill.

## [0.7.0] - 2026-09-27

### Changed

- Balance pass with the simulator: monster XP ×1.5, bosses with 3× HP and 1.2× damage, EASY at 80% and HARD at 125%,
  sturdier Mage. NORMAL bot medians are now rounds 40–50 for every vocation (table in `docs/game-design.md` §12).
  Golden files regenerated.

## [0.6.2] - 2026-09-27

### Added

- Shared VS Code workspace settings (`.vscode/settings.json`).

### Changed

- Version bumped to 0.3.0 in `package.json`; README badge updated; changelog section published and `PLAN.md` milestone
  M2 (Python Beta) marked done. This was the manual milestone release commit for the former v0.3.0; under the
  per-commit scheme it is 0.6.2.

## [0.6.1] - 2026-09-27

### Changed

- `.gitignore` ignores scratch folders (`.tmp/`, `.playwright-mcp/`) and `TODO.md`; VS Code recommends the Python,
  Pylance and mypy extensions alongside Ruff and Go.

## [0.6.0] - 2026-09-27

### Added

- Python terminal UI built with Textual: title, language selection (first launch and from the title), new run
  (difficulty, name, vocation), battle with animated ASCII monster, HP/MP bars and combat log, spell and potion menus,
  merchant (potions, selling, equipment, stock, character sheet), save & quit, continue, game over, Hall of Fame,
  bestiary and achievements. All texts in English and Brazilian Portuguese.
- Framework-independent UI controller that the TypeScript and Go ports will mirror.
- End-to-end tests driving the real TUI with Textual Pilot, including a whole run played by keys until game over.

### Changed

- `rpg-python` version set to 0.3.0.

## [0.5.0] - 2026-09-27

### Added

- Original ASCII art for 16 monster families and 10 bosses (`idle`, `attack`, `hurt` animations).
- New UI texts in English and Brazilian Portuguese for the terminal UI screens.

## [0.4.0] - 2026-09-27

### Added

- Python persistence: settings, auto-save at the merchant with atomic writes, resume (mid-battle quits resume from the
  last merchant visit), history of finished runs with full statistics and timestamps, profile with bestiary,
  achievements and Hall of Fame, schema version checks. Documented in `docs/persistence.md`.

### Changed

- Golden files regenerated for the new item content.

## [0.3.0] - 2026-09-27

### Added

- 151 items (TibiaWiki names) covering 8 slots in all 10 tiers, with weapons per vocation, shields and spellbooks;
  21 affixes (including one elemental ward per element) and 18 achievements.

## [0.2.3] - 2026-09-27

### Fixed

- CI pins `astral-sh/setup-uv` to its full release tag: the action only publishes full version tags, so `@v10` could
  not be resolved.

## [0.2.2] - 2026-09-27

### Changed

- README badge set to 0.2.0; changelog section published; `PLAN.md` milestones M0 (Foundation) and M1 (Python Alpha)
  marked done. This was the manual milestone release commit for the former v0.2.0; under the per-commit scheme it is
  0.2.2.

## [0.2.1] - 2026-09-27

### Added

- CI job for Python on Linux and Windows (Ruff, mypy, pytest with coverage) and a CI step running
  `bun run check:shared` (schemas, i18n keys, art).
- Project skills `golden-files` and `add-game-content`.

### Changed

- Rules documented as implemented: vocations are Warrior (+15 HP/+5 MP per level), Archer (+10 HP/+15 MP) and Mage
  (+5 HP/+15 MP); the magic level threshold is cumulative mana spent; a consumed stun gives a 2-turn stun cooldown;
  item event fields and i18n event keys refined in the parity and data-format docs.

## [0.2.0] - 2026-09-27

### Added

- Python reference engine (`rpg-python`): mulberry32 PRNG, integer formulas, pure `step(command) → events` state
  machine with battle (melee, spells, potions, defend, crit, dodge, parry, leech, elemental resistances and
  protections, statuses, boss telegraph/charge), spell levels by use, magic level, infinite tiers with cycle
  scaling, merchant (potions, selling, equipment, rotating stock), item factory with rarities and affixes, drops and
  run statistics.
- Greedy bot and balance simulator (`uv run rpg --simulate N`), golden file generator (`uv run rpg-golden`) and the
  first golden files (PRNG vectors, scripted scenarios and 9 bot full runs).
- 131 Python tests (unit, integration, golden) with 95% coverage.

## [0.1.0] - 2026-09-27

### Added

- Shared content: 110 monsters in 10 tiers and 10 bosses (TibiaWiki names, original stats), three vocations
  (Warrior, Archer, Mage) with Tibia spells, potions from Health to Supreme, statuses, balance knobs and starter
  weapons, all validated by JSON Schemas (`bun run check:shared`).
- `shared/i18n` with English (default) and Brazilian Portuguese, with a check that both have the same keys.

## [0.0.1] - 2026-09-27

### Added

- Monorepo for the Python, TypeScript and Go implementations, with a language-agnostic `shared/` folder.
- Root tooling: EditorConfig (tabs, width 4), Biome, commitlint + husky, lint-staged, Dependabot, markdownlint config.
- Documentation written from scratch: architecture, game design (rules and formulas), cross-language parity, shared
  data format, persistence, terminal UI, testing, CI/CD and versioning, per-language guides and ADRs 0001–0005.
- `PLAN.md` with milestones (foundation, Python alpha/beta/1.0, TypeScript and Go ports, release 1.0).
- `AGENTS.md` / `CLAUDE.md`, `README.md`, `CONTRIBUTE.md`, MIT `LICENSE`.
- `setups/` play scripts for Windows (Git Bash) and Linux/macOS for each implementation.
- CI workflow skeleton with per-language jobs.
- General-purpose agent skills, agents and reference checklists in `.claude/`.
- The 2016 Python and 2022 TypeScript code is not carried over; the originals remain in their own repositories.

[Unreleased]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.4.2...HEAD
[1.4.2]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.4.1...v1.4.2
[1.4.1]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.4.0...v1.4.1
[1.4.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.3.6...v1.4.0
[1.3.6]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.3.5...v1.3.6
[1.3.5]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.3.4...v1.3.5
[1.3.4]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.3.3...v1.3.4
[1.3.3]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.3.2...v1.3.3
[1.3.2]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.3.1...v1.3.2
[1.3.1]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.3.0...v1.3.1
[1.3.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.1.2...v1.2.0
[1.1.2]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.1.1...v1.1.2
[1.1.1]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.0.1...v1.1.0
[1.0.1]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.10.7...v1.0.0
[0.10.7]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.10.6...v0.10.7
[0.10.6]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.10.5...v0.10.6
[0.10.5]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.10.4...v0.10.5
[0.10.4]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.10.3...v0.10.4
[0.10.3]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.10.2...v0.10.3
[0.10.2]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.10.1...v0.10.2
[0.10.1]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.10.0...v0.10.1
[0.10.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.9.5...v0.10.0
[0.9.5]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.9.4...v0.9.5
[0.9.4]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.9.3...v0.9.4
[0.9.3]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.9.2...v0.9.3
[0.9.2]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.9.1...v0.9.2
[0.9.1]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.9.0...v0.9.1
[0.9.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.8.1...v0.9.0
[0.8.1]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.8.0...v0.8.1
[0.8.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.7.2...v0.8.0
[0.7.2]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.7.1...v0.7.2
[0.7.1]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.7.0...v0.7.1
[0.7.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.6.2...v0.7.0
[0.6.2]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.6.1...v0.6.2
[0.6.1]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.6.0...v0.6.1
[0.6.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.2.3...v0.3.0
[0.2.3]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.2.2...v0.2.3
[0.2.2]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.0.1...v0.1.0
[0.0.1]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/releases/tag/v0.0.1
