# Contributing

Thanks for your interest! This is a didactic project: the same game in Python, TypeScript, Go, Rust, Elixir and C++.
Contributions that keep the six implementations **identical in behaviour** and easy to read are the most valuable.

## Before you start

1. Read [`AGENTS.md`](AGENTS.md) (short) and the doc of the area you'll touch in [`docs/`](docs).
2. Check [`PLAN.md`](PLAN.md) and the open issues to avoid duplicated work. For big changes, open an issue first.

## Development setup

- [Bun](https://bun.sh) 1.4.2 (root tooling and TypeScript), [uv](https://docs.astral.sh/uv/) (Python 3.14), [Go](https://go.dev) 1.27
- Only for the implementation you touch: [Rust](https://www.rust-lang.org) 1.99,
  [Elixir](https://elixir-lang.org) 1.20 with Erlang/OTP 29, a C++23 compiler (Clang 19+ or GCC 14+) with CMake ≥ 3.28
  and Ninja (details in each `docs/<language>.md`)
- `bun install` at the root installs Biome, commitlint and the git hooks (husky)
- An editor with [EditorConfig](https://editorconfig.org) support (tabs, width 4)

## Making changes

- **Game rules** — change [`docs/game-design.md`](docs/game-design.md) first, implement in Python, regenerate the golden
  files (`cd rpg-python && uv run rpg-golden`), then port to the other five languages in the same PR (or open
  follow-up issues).
- **Content and balance** — edit `shared/data/*.json`, keep `shared/i18n/en.json` and `pt-BR.json` in sync, run
  `bun run check:shared` and the simulator (`uv run rpg --simulate 2000`).
- **One implementation only** (bug fix, refactor) — must not change events/golden output unless the other
  implementations are fixed too.
- Keep the Clean Architecture boundaries (`domain` has no I/O) and the same file/type names across languages.
- Write tests: a bug fix starts with a failing test.

## Checks (CI runs all of them)

```bash
bun run lint:ci && bun run check:shared
cd rpg-python && uv run ruff format --check && uv run ruff check && uv run mypy && uv run pytest
cd rpg-typescript && bun run lint:ci && bun run typecheck && bun test && bun run build
cd rpg-golang && go generate ./... && gofmt -l . && go vet ./... && golangci-lint run && go test -race ./...
cd rpg-rust && cargo fmt --check && cargo clippy --all-targets -- -D warnings && cargo test
cd rpg-elixir && mix format --check-formatted && mix compile --warnings-as-errors && mix test --cover
cd rpg-cpp && cmake --preset debug && cmake --build --preset debug && ctest --preset debug
```

## Commits and pull requests

- [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/): `type(scope): description`, scopes
  `python`, `typescript`, `golang`, `rust`, `elixir`, `cpp`, `shared`, `docs`, `ci`, `setups`, `deps`, `release`,
  `repo`.
- Add an entry under `## [Unreleased]` in [`CHANGELOG.md`](CHANGELOG.md) for user-visible changes.
- Everything in English (code, comments, docs, commits). Portuguese only in `shared/i18n/pt-BR.json`.
- Dependencies: exact, latest **stable** versions only.

## Code of conduct

Be kind and constructive. Harassment or discrimination of any kind is not tolerated.
