# Agent Guide (AGENTS.md = CLAUDE.md)

Open-source endless turn-based RPG for the terminal, implemented **six times with identical rules**: Python
(reference), TypeScript, Go, Rust, Elixir and C++. Content is in `shared/`; plan and progress in [`PLAN.md`](PLAN.md).

## Read before changing anything

- [Architecture](docs/architecture.md) — monorepo layout, Clean Architecture layers, engine `step(command) → events`
- [Game design](docs/game-design.md) — rules, formulas and order of operations (the contract)
- [Cross-language parity](docs/cross-language-parity.md) — PRNG, integer math, events, golden tests
- [Shared data](docs/data-format.md) — JSON files, i18n, ASCII art, adding content
- [Persistence](docs/persistence.md) — save, history, profile formats (shared by the six implementations)
- [Terminal UI](docs/tui.md) — layout, screens, keys, animation, CLI flags
- [Testing](docs/testing.md) · [CI/CD & versioning](docs/ci-cd.md) · [Docker](docs/docker.md) · [ADRs](docs/adr)
- Per language: [Python](docs/python.md) · [TypeScript](docs/typescript.md) · [Go](docs/golang.md) ·
  [Rust](docs/rust.md) · [Elixir](docs/elixir.md) · [C++](docs/cpp.md)

## Non-negotiable rules

1. English everywhere (code, comments, docs, commits). Portuguese only in `shared/i18n/pt-BR.json`.
2. No balance numbers in code: everything tunable goes in `shared/data/*.json`.
3. Engine is pure and deterministic: shared mulberry32 PRNG, integer math with explicit floor, no clock/I/O.
4. Rule change order: `docs/game-design.md` → Python → regenerate `shared/golden` → port to the other five languages.
5. Same names and layers in the six languages (`domain`, `application`, `infrastructure`, `presentation`).
6. Formatting: `.editorconfig` tabs width 4 (YAML and Elixir 2 spaces: `mix format` only supports spaces). Biome
   (TS/JSON), Ruff (Python), gofmt (Go), rustfmt (Rust), `mix format` (Elixir), clang-format (C++).
7. TypeScript: strict, no `any` (use `unknown` + narrowing), explicit return types on exports.
8. Dependencies pinned to exact latest **stable** versions (never beta/rc/canary).
   Docker: every `rpg-*/Dockerfile` builds from the repository root (it needs `shared/`), multi-stage, exact image
   tags, non-root user, saves in `/data`; a new implementation also gets a service in `compose.yml`.
9. Tests never touch real saves (`RPG_DATA_DIR` = temp dir) and run with a fixed seed and `--no-anim`.

## Workflow

- Conventional Commits, scopes `python|typescript|golang|rust|elixir|cpp|shared|docs|ci|setups|deps|release|repo`.
- Before committing: format, lint, type-check and test the touched project (commands in the per-language docs).
- Record relevant changes in [`CHANGELOG.md`](CHANGELOG.md) (Keep a Changelog) and tick `PLAN.md` checkboxes.
- Each commit on `main` is a release: fill `[Unreleased]`, run `bun run release:prepare "<subject>"` (`release` skill).
- Release binaries are built locally, not by GitHub Actions: after pushing a release commit, run
  `bash scripts/release-local.sh vX.Y.Z` (creates the GitHub Release if missing, uploads the binaries; needs Docker).
- CI runs locally: the husky `pre-push` hook runs `scripts/ci-local.sh` (GitHub workflows are disabled); a post-commit
  hook tags `vX.Y.Z` and `git config push.followTags true` pushes the tag.
- Recurring flows become project skills in `.claude/skills/` (`golden-files`, `add-game-content`, `port-feature`, `release`).
- Go work: always load the `golang-how-to` skill first (samber/cc-skills-golang); it routes to the other Go skills.
- The husky `pre-commit` hook runs `graphify update .` (code, no LLM, ~15 s) and stages `graphify-out/`, so every
  commit (= release) carries a current graph; when docs changed, run `/graphify . --update` before committing.

## Navigating the project (graphify)

`graphify-out/` holds a knowledge graph of the six implementations, `shared/`, docs and CI (`.graphifyignore` leaves out
`.claude/`, golden files and ASCII art). Use it as working context before broad searches:

- Read `graphify-out/GRAPH_REPORT.md` first: community hubs, god nodes, surprising cross-file connections.
- Ask the graph: `graphify query "<question>"`, `graphify path "A" "B"`, `graphify explain "X"`; open
  `graphify-out/graph.html` for the visual map. Cite `source_location` from answers, then read only those files.
- Install once: `uv tool install graphifyy==0.9.74`. The `/graphify` skill rebuilds everything from scratch.

## Quick commands

```bash
bun install && bun run lint:ci            # root: Biome + shared data checks
cd rpg-python && uv run pytest            # Python
cd rpg-typescript && bun test             # TypeScript
cd rpg-golang && go test ./...            # Go
cd rpg-rust && cargo test                 # Rust
cd rpg-elixir && mix test                 # Elixir
cd rpg-cpp && cmake --preset debug && cmake --build --preset debug && ctest --preset debug   # C++
docker compose run --rm python            # any implementation in Docker (services in compose.yml)
```
