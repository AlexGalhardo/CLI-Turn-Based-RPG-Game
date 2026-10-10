#!/usr/bin/env bash
# Local CI, run by the husky pre-push hook (the GitHub workflows are kept but disabled; see docs/ci-cd.md).
# Mirrors .github/workflows/ci.yml job by job, but only for the projects touched by the commits being pushed:
#   bash scripts/ci-local.sh [base-ref]     # default base: the upstream branch (origin/main)
#   bash scripts/ci-local.sh --all          # every job
#   CI_JOBS="rust cpp" bash scripts/ci-local.sh   # chosen jobs only
# Skip once with SKIP_LOCAL_CI=1 git push (or git push --no-verify).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# Toolchains installed per user (rustup, scoop on Windows) are often missing from a hook's PATH.
for dir in "$HOME/.cargo/bin" "$HOME/go/bin" "$HOME/.local/bin" "$HOME/scoop/shims" \
	"$HOME/scoop/apps/elixir/current/bin" "$HOME/scoop/apps/mingw-mstorsjo-llvm-ucrt/current/bin"; do
	[ -d "$dir" ] && PATH="$dir:$PATH"
done
export PATH

ALL_JOBS=(repo python typescript golang rust elixir cpp c asm)

changed_jobs() {
	local base="$1" files
	files="$(git diff --name-only "$base" HEAD)"
	local jobs=(repo)
	# Shared content and root tooling affect every implementation (golden files, embedded data).
	if grep -qE '^(shared/|package\.json|bun\.lock|\.github/)' <<<"$files"; then
		printf '%s\n' "${ALL_JOBS[@]}"
		return
	fi
	for job in python typescript golang rust elixir cpp c asm; do
		grep -q "^rpg-$job/" <<<"$files" && jobs+=("$job")
	done
	printf '%s\n' "${jobs[@]}"
}

require() {
	command -v "$1" >/dev/null 2>&1 || { echo "local CI: '$1' is not installed (needed by the $2 job)" >&2; exit 1; }
}

run() {
	echo "  \$ $*"
	"$@"
}

job_repo() {
	require bun repo
	run bun install --frozen-lockfile
	run bun run lint:ci
	run bun run check:shared
	run bun run test:scripts
	run bun run release:check
}

job_python() {
	require uv python
	cd rpg-python
	run uv sync --frozen
	run uv run ruff format --check
	run uv run ruff check
	run uv run mypy
	run uv run pytest -q
}

job_typescript() {
	require bun typescript
	cd rpg-typescript
	run bun install --frozen-lockfile
	run bun run lint:ci
	run bun run typecheck
	run bun run test:coverage
	run bun run build
	run ./dist/rpg-typescript --simulate 1 --vocation mage --difficulty easy >/dev/null
}

job_golang() {
	require go golang
	cd rpg-golang
	run go generate ./...
	test -z "$(gofmt -l .)" || { gofmt -l .; exit 1; }
	run go vet ./...
	if command -v golangci-lint >/dev/null 2>&1; then run golangci-lint run ./...; fi
	run go test -count=1 ./...
	run go build -o bin/rpg-golang ./cmd/rpg
	run ./bin/rpg-golang --simulate 1 --vocation mage --difficulty easy >/dev/null
}

job_rust() {
	require cargo rust
	cd rpg-rust
	run cargo fmt --check
	run cargo clippy --all-targets --locked -- -D warnings
	if command -v cargo-llvm-cov >/dev/null 2>&1; then
		run cargo llvm-cov --locked --summary-only --fail-under-lines 80
		run cargo llvm-cov report --summary-only \
			--ignore-filename-regex '(presentation|infrastructure|main\.rs|assets\.rs|version\.rs)' --fail-under-lines 90
	else
		run cargo test --locked
	fi
	run cargo build --release --locked
	run ./target/release/rpg-rust --simulate 1 --vocation mage --difficulty easy >/dev/null
}

job_elixir() {
	require mix elixir
	cd rpg-elixir
	run mix format --check-formatted
	run mix compile --warnings-as-errors
	run mix test --cover --warnings-as-errors
	MIX_ENV=prod run mix escript.build
	run escript bin/rpg-elixir --simulate 1 --vocation mage --difficulty easy >/dev/null
}

job_cpp() {
	require cmake cpp
	# A bare name: CMake on Windows can't use the MSYS-style path `command -v` prints.
	local cxx=g++
	command -v clang++ >/dev/null 2>&1 && cxx=clang++
	cd rpg-cpp
	# shellcheck disable=SC2046
	run clang-format --dry-run --Werror $(find src tests -name '*.cpp' -o -name '*.hpp')
	run cmake --preset debug -DCMAKE_CXX_COMPILER="$cxx"
	run cmake --build --preset debug
	run ctest --preset debug
	run cmake --preset release -DCMAKE_CXX_COMPILER="$cxx"
	run cmake --build --preset release
	run ./build/release/rpg-cpp --simulate 1 --vocation mage --difficulty easy >/dev/null
}

job_c() {
	require cmake c
	# A bare name: CMake on Windows can't use the MSYS-style path `command -v` prints.
	local cc=gcc
	command -v clang >/dev/null 2>&1 && cc=clang
	cd rpg-c
	# shellcheck disable=SC2046
	run clang-format --dry-run --Werror $(find src tests -name '*.c' -o -name '*.h')
	run cmake --preset debug -DCMAKE_C_COMPILER="$cc"
	run cmake --build --preset debug
	run ctest --preset debug
	run cmake --preset release -DCMAKE_C_COMPILER="$cc"
	run cmake --build --preset release
	run ./build/release/rpg-c --simulate 1 --vocation mage --difficulty easy >/dev/null
}

job_asm() {
	require docker asm
	# The image build assembles, links and runs the golden replay, bot parity and simulator tests (x86-64 Linux only).
	run docker build -q -f rpg-asm/Dockerfile -t rpg-asm .
	run docker run --rm rpg-asm --simulate 1 --vocation mage --difficulty easy >/dev/null
}

main() {
	local jobs
	if [ -n "${CI_JOBS:-}" ]; then
		jobs="$CI_JOBS"
	elif [ "${1:-}" = "--all" ]; then
		jobs="$(printf '%s\n' "${ALL_JOBS[@]}")"
	else
		local base="${1:-$(git rev-parse --verify -q '@{upstream}' || echo origin/main)}"
		jobs="$(changed_jobs "$base")"
	fi
	local start failed=()
	for job in $jobs; do
		echo "== local CI: $job"
		start=$SECONDS
		# Not inside `if`: there `set -e` would be ignored and a failing step would not stop its job.
		set +e
		(
			set -e
			"job_$job"
		)
		local status=$?
		set -e
		if [ "$status" -eq 0 ]; then
			echo "== local CI: $job ok ($((SECONDS - start)) s)"
		else
			echo "== local CI: $job FAILED" >&2
			failed+=("$job")
		fi
	done
	if [ ${#failed[@]} -gt 0 ]; then
		echo "local CI failed: ${failed[*]} (push blocked; SKIP_LOCAL_CI=1 git push to override)" >&2
		exit 1
	fi
	echo "local CI passed: $(echo $jobs | tr '\n' ' ')"
}

main "$@"
