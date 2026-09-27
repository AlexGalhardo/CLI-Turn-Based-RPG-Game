# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.4.0...HEAD
[0.4.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/releases/tag/v0.1.0
