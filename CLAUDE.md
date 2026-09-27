# Agent Guide (AGENTS.md = CLAUDE.md)

Open-source endless turn-based RPG for the terminal, implemented **three times with identical rules**: Python
(reference), TypeScript (Bun) and Go. Content lives in `shared/`; the plan and progress live in [`PLAN.md`](PLAN.md).

## Read before changing anything

- [Architecture](docs/architecture.md) — monorepo layout, Clean Architecture layers, engine `step(command) → events`
- [Game design](docs/game-design.md) — rules, formulas and order of operations (the contract)
- [Cross-language parity](docs/cross-language-parity.md) — PRNG, integer math, events, golden tests
- [Shared data](docs/data-format.md) — JSON files, i18n, ASCII art, adding content
- [Persistence](docs/persistence.md) — save, history, profile formats (shared by the three implementations)
- [Terminal UI](docs/tui.md) — layout, screens, keys, animation, CLI flags
- [Testing](docs/testing.md) · [CI/CD & versioning](docs/ci-cd.md) · [ADRs](docs/adr)
- Per language: [Python](docs/python.md) · [TypeScript](docs/typescript.md) · [Go](docs/golang.md)

## Non-negotiable rules

1. English everywhere (code, comments, docs, commits). Portuguese only in `shared/i18n/pt-BR.json`.
2. No balance numbers in code: everything tunable goes in `shared/data/*.json`.
3. Engine is pure and deterministic: shared mulberry32 PRNG, integer math with explicit floor, no clock/I/O.
4. Rule change order: `docs/game-design.md` → Python → regenerate `shared/golden` → port to TypeScript and Go.
5. Same names and layers in the three languages (`domain`, `application`, `infrastructure`, `presentation`).
6. Formatting: `.editorconfig` tabs width 4 (YAML 2 spaces). Biome (TS/JSON), Ruff (Python), gofmt (Go).
7. TypeScript: strict, no `any` (use `unknown` + narrowing), explicit return types on exports.
8. Dependencies pinned to exact latest **stable** versions (never beta/rc/canary).
9. Tests never touch real saves (`RPG_DATA_DIR` = temp dir) and run with a fixed seed and `--no-anim`.

## Workflow

- Commits: Conventional Commits with scopes `python|typescript|golang|shared|docs|ci|setups|deps|release|repo`.
- Before committing: format, lint, type-check and test the touched project (commands in the per-language docs).
- Record relevant changes in [`CHANGELOG.md`](CHANGELOG.md) (Keep a Changelog) and tick `PLAN.md` checkboxes.
- Version: one SemVer for the monorepo, bumped in four places (see [CI/CD](docs/ci-cd.md)).
- Recurring flows become project skills in `.claude/skills/` (`golden-files`, `add-game-content`, `port-feature`).

## Quick commands

```bash
bun install && bun run lint:ci            # root: Biome + shared data checks
cd rpg-python && uv run pytest            # Python
cd rpg-typescript && bun test             # TypeScript
cd rpg-golang && go test ./...            # Go
```
