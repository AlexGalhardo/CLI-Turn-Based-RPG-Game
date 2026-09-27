# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/releases/tag/v0.1.0
