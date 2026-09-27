#!/usr/bin/env bash
# Builds and plays the Go version on Windows using Git Bash.
# Usage: bash setups/play-on-windows-version-golang.sh [game flags, e.g. --seed 42 --lang pt-BR]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

require() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing '$1'. Install it with: $2" >&2
		exit 1
	fi
}

require go "winget install --id GoLang.Go -e   (then reopen Git Bash)"

PROJECT_DIR="$ROOT_DIR/rpg-golang"
[ -f "$PROJECT_DIR/go.mod" ] || { echo "The Go version is not available yet (see PLAN.md)." >&2; exit 1; }

RUN=()
if [ -n "${MSYSTEM:-}" ] && command -v winpty >/dev/null 2>&1 && [ -z "${WT_SESSION:-}" ]; then
	RUN=(winpty)
fi

cd "$PROJECT_DIR"
go generate ./...
go build -o bin/rpg-golang.exe ./cmd/rpg
exec "${RUN[@]}" ./bin/rpg-golang.exe "$@"
