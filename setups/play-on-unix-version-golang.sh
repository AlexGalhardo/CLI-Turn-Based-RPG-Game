#!/usr/bin/env bash
# Builds and plays the Go version on Linux/macOS.
# Usage: bash setups/play-on-unix-version-golang.sh [game flags, e.g. --seed 42 --lang pt-BR]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

require() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing '$1'. Install it with: $2" >&2
		exit 1
	fi
}

require go "https://go.dev/doc/install (Go 1.27 or newer)"

PROJECT_DIR="$ROOT_DIR/rpg-golang"
[ -f "$PROJECT_DIR/go.mod" ] || { echo "The Go version is not available yet (see PLAN.md)." >&2; exit 1; }

cd "$PROJECT_DIR"
go generate ./...
go build -o bin/rpg-golang ./cmd/rpg
exec ./bin/rpg-golang "$@"
