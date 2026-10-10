# CI/CD, Versioning and Releases

## Where CI runs: locally, in the pre-push hook

The GitHub workflows below are kept in the repository but **disabled** on GitHub (`gh workflow disable ci.yml` /
`release.yml`; re-enable with `gh workflow enable`). The same checks run on the developer's machine:

- `.husky/pre-push` runs `scripts/ci-local.sh`, which mirrors `ci.yml` job by job (repo, python, typescript, golang,
  rust, elixir, cpp) for the projects touched by the commits being pushed (`shared/`, root tooling or workflow changes
  run every job). A failing job blocks the push. `bash scripts/ci-local.sh --all` runs everything;
  `SKIP_LOCAL_CI=1 git push` skips it once. Differences from GitHub: one OS (the local one), no `-race` for Go, no
  C++ coverage gate. Known flake on Windows: a Go test can fail with `TempDir RemoveAll cleanup: ... directory not
  empty` (a different test each time; something outside the code still holds a freshly written save) — push again.
- `.husky/post-commit` creates the per-commit release tag `vX.Y.Z` (annotated, message = the commit's CHANGELOG
  section) when the commit bumped the version; with `git config push.followTags true` the tag is pushed with the
  commit. `release.yml` is disabled, so the GitHub Release page and its binaries are made locally (next section).

## Release binaries: built locally

`bash scripts/release-local.sh vX.Y.Z` builds every downloadable asset of `release.yml` from the tag (`git archive`,
never the working tree), smoke-tests them, writes `SHA256SUMS.txt`, creates the GitHub Release when it does not exist
(notes = the tag's CHANGELOG section, via `scripts/release.ts notes`) and uploads the files with `--clobber`.
`--no-upload` only builds (into `dist-release/`, git-ignored; `OUT_DIR` overrides). Run it after pushing every
release commit, so the latest release always has binaries.

| Asset | Built with |
|---|---|
| `rpg-typescript-{linux-x64,darwin-arm64,windows-x64.exe}` | host `bun build --compile --target=bun-…` (cross-compiles) |
| `rpg-golang-{linux-x64,darwin-arm64,windows-x64.exe}` | host `go build`, `CGO_ENABLED=0` with `GOOS`/`GOARCH` |
| `rpg-elixir-escript` | host `MIX_ENV=prod mix escript.build` (portable; players need Erlang/OTP) |
| `rpg-rust-windows-x64.exe`, `rpg-cpp-windows-x64.exe` | host `cargo` and LLVM-MinGW `clang++` (**Windows host only**) |
| `rpg-rust-linux-x64`, `rpg-cpp-linux-x64` | Docker `rust:1.99.0-bookworm` and `gcc:14` (static libstdc++/libgcc) |
| `rpg-rust-darwin-arm64`, `rpg-cpp-darwin-arm64` | Docker `ghcr.io/rust-cross/cargo-zigbuild:0.23.4` (`cargo zigbuild`, `zig c++ -target aarch64-macos`) |

The Docker cross-builds of `rpg-rust-linux-x64`, `rpg-rust-darwin-arm64` and `rpg-cpp-darwin-arm64` are optional: a failure only warns and the asset is missing from the release (they were not validated yet; first verified build: `rpg-cpp-linux-x64`). Docker Desktop can hang when several multi-GB images are pulled at once: pull them one by one (`docker pull -q <image>`) before the first run.

Requirements: bun, go, mix, Docker running, `gh` authenticated; on Windows also rustup and LLVM-MinGW (scoop
`mingw-mstorsjo-llvm-ucrt`). The darwin binaries cannot run here; they are checked only by being produced. The
first run pulls the images and downloads bun's cross-compile runtimes (several minutes); later runs reuse them.

## Workflows (`.github/workflows/`)

| Workflow | Trigger | Jobs |
|---|---|---|
| `ci.yml` | push / PR to `main` | `repo` (commitlint on PR commits, Biome for root JSON/TS, shared data schemas + i18n key parity, release tooling tests, version files ↔ CHANGELOG check) · `python` (ruff format --check, ruff check, mypy, pytest + coverage) · `typescript` (biome ci, tsc --noEmit, bun test, bun build --compile) · `golang` (gofmt check, go vet, golangci-lint, go test -race, go build) · `rust` (rustfmt, clippy `-D warnings`, cargo llvm-cov floors, release build) · `elixir` (mix format, warnings as errors, mix test --cover, escript) · `cpp` (clang-format, GCC 14 and Clang 20 with `-Werror`, ctest, llvm-cov floors, release build) · `cpp-windows` (LLVM-MinGW build + ctest) |
| `release.yml` (disabled; replaced by `scripts/release-local.sh`) | push to `main` | one GitHub Release per pushed commit (tag `vX.Y.Z` on that commit, notes = its CHANGELOG section); the newest commit gets the binaries: TypeScript, Go, Rust and C++ for linux-x64, darwin-arm64, windows-x64, the Elixir escript and `SHA256SUMS.txt`. Manual run (`workflow_dispatch`): with `tag`, rebuilds and attaches the binaries of an existing release; without, a dry run that only builds |

Every implementation job runs on Linux and Windows (C++ on Windows is the separate `cpp-windows` job), each with
smoke tests of the executable. A language job is skipped while its project does not exist (`detect` job).

## Tooling per language

| Concern | Python | TypeScript | Go | Rust | Elixir | C++ |
|---|---|---|---|---|---|---|
| Formatter | Ruff (`indent-style = "tab"`) | Biome | gofmt (tabs by default) | rustfmt (`hard_tabs`) | `mix format` (2 spaces) | clang-format (tabs) |
| Linter | Ruff | Biome | go vet + golangci-lint | clippy (pedantic) | compiler warnings as errors | `-Wall -Wextra -Wpedantic -Werror` |
| Types | mypy (strict) | tsc 7 (`strict`) | compiler | compiler | — (dynamic) | compiler |
| Tests | pytest + pytest-cov | bun:test | go test | cargo test + cargo-llvm-cov | ExUnit (`--cover`) | Catch2 + ctest + llvm-cov |
| Build | — (runs with `uv run`) | `bun build --compile` | `go build` | `cargo build --release` | `mix escript.build` | CMake presets + Ninja |

`.editorconfig` at the root: tabs, width 4, LF, final newline, 120 columns (YAML uses 2 spaces as the spec requires,
Elixir 2 spaces because `mix format` supports nothing else).

## Commits

[Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/), checked by commitlint (husky
`commit-msg` hook). Allowed scopes: `python`, `typescript`, `golang`, `rust`, `elixir`, `cpp`, `shared`, `docs`, `ci`,
`setups`, `deps`, `release`, `repo`.

```
feat(python): add boss telegraph and defend action
fix(golang): floor percentage damage like the reference
test(shared): regenerate golden files after crit rule change
```

## Versioning: every commit on `main` is a release

[SemVer 2.0.0](https://semver.org/), **one version for the whole monorepo**, and **one release per commit**. The commit
type decides the bump:

| Commit | Bump | Example |
|---|---|---|
| `type!:` or a `BREAKING CHANGE:` footer | major | `1.3.2` → `2.0.0` |
| `feat` | minor | `1.3.2` → `1.4.0` |
| anything else (`fix`, `docs`, `ci`, `chore`, `test`, …) | patch | `1.3.2` → `1.3.3` |

The commit itself carries its version, so the tag points at the commit that made the change (no bot commits):

1. Describe the change under `## [Unreleased]` in `CHANGELOG.md` (`Added`, `Changed`, `Deprecated`, `Removed`,
   `Fixed`, `Security`).
2. Run `bun run release:prepare "<commit subject>"`. It computes the next version from the current one and the
   subject, writes it in every version file (root and README badge, Python `pyproject.toml` + `__init__.py` +
   `uv.lock`, TypeScript `package.json`, Go `version.go`, Rust `Cargo.toml` + `Cargo.lock` + `version.rs`, Elixir
   `mix.exs` + `version.ex`, C++ `CMakeLists.txt` + `version.hpp`) and turns `[Unreleased]` into
   `## [X.Y.Z] - YYYY-MM-DD` with its compare link.
3. Commit with exactly that subject and push. CI (`bun run release:check`) fails if the version files disagree or the
   section is missing; `release.yml` tags the commit `vX.Y.Z` and publishes the release.

Several commits pushed together get one release each, oldest first; only the newest gets binaries (rebuild an older
one with a manual run and its `tag`). A commit that did not bump the version is skipped with a warning.

History before this scheme was re-tagged retroactively from `v0.0.1` (first commit) with the same rules; the
`chore(release): v1.0.0` commit kept `v1.0.0`. The former milestone tags (`v0.1.0`–`v0.6.0`) pointed at other commits
and were replaced. `scripts/release.ts` holds the version logic and is tested by `bun run test:scripts`.

## Changelog

[Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/), one `## [X.Y.Z] - YYYY-MM-DD` section per commit,
newest first, with compare links at the bottom. The release notes on GitHub are that section verbatim.
