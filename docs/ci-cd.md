# CI/CD, Versioning and Releases

## Workflows (`.github/workflows/`)

| Workflow | Trigger | Jobs |
|---|---|---|
| `ci.yml` | push / PR to `main` | `repo` (commitlint on PR commits, Biome for root JSON/TS, shared data schemas + i18n key parity) · `python` (ruff format --check, ruff check, mypy, pytest + coverage) · `typescript` (biome ci, tsc --noEmit, bun test, bun build --compile) · `golang` (gofmt check, go vet, golangci-lint, go test -race, go build) |
| `release.yml` | tag `v*` | builds TypeScript and Go binaries for linux-x64, darwin-arm64, windows-x64 and attaches them to the GitHub Release with the CHANGELOG section |

Jobs for a language are skipped until that phase starts (path filters), so the pipeline is green at every phase.

## Tooling per language

| Concern | Python | TypeScript | Go |
|---|---|---|---|
| Formatter | Ruff (`indent-style = "tab"`) | Biome | gofmt (tabs by default) |
| Linter | Ruff | Biome | go vet + golangci-lint |
| Types | mypy (strict) | tsc 7 (`strict`) | compiler |
| Tests | pytest + pytest-cov | bun:test | go test |
| Build | — (runs with `uv run`) | `bun build --compile` | `go build` |

`.editorconfig` at the root: tabs, width 4, LF, final newline, 120 columns (YAML uses 2 spaces as the spec requires).

## Commits

[Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/), checked by commitlint (husky
`commit-msg` hook). Allowed scopes: `python`, `typescript`, `golang`, `shared`, `docs`, `ci`, `setups`, `deps`,
`release`, `repo`.

```
feat(python): add boss telegraph and defend action
fix(golang): floor percentage damage like the reference
test(shared): regenerate golden files after crit rule change
```

## Versioning

[SemVer 2.0.0](https://semver.org/). **One version for the whole monorepo** (root `package.json`, `rpg-python/
pyproject.toml`, `rpg-typescript/package.json`, `rpg-golang/internal/version`), bumped together.

| Version | Milestone |
|---|---|
| 0.1.0 | Monorepo foundation, docs, plan |
| 0.2.0 | Python alpha — clean engine, base game, infinite loop |
| 0.3.0 | Python beta — items, spells levels, statuses, bosses, persistence, TUI |
| 0.4.0 | Python 1.0 candidate — balance, polish, golden files |
| 0.5.0 | TypeScript port at parity |
| 0.6.0 | Go port at parity |
| 1.0.0 | All three at parity, binaries released |

Patch bumps for fixes between milestones.

## Changelog

[Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/). Every relevant change goes under `## [Unreleased]`
(`Added`, `Changed`, `Deprecated`, `Removed`, `Fixed`, `Security`). A release moves it to `## [x.y.z] - YYYY-MM-DD`.

## Release steps

1. Move `[Unreleased]` entries to the new version section; update compare links.
2. Bump the version in the four places listed above.
3. `chore(release): vX.Y.Z` commit, tag `vX.Y.Z`, push with tags → `release.yml` publishes binaries.
