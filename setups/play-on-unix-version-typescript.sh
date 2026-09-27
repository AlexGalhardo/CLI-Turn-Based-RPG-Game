#!/usr/bin/env bash
# Builds and plays the TypeScript version (Bun single-file executable) on Linux/macOS.
# Usage: bash setups/play-on-unix-version-typescript.sh [game flags, e.g. --seed 42 --lang pt-BR]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

require() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing '$1'. Install it with: $2" >&2
		exit 1
	fi
}

require bun "curl -fsSL https://bun.sh/install | bash -s bun-v1.4.2"

PROJECT_DIR="$ROOT_DIR/rpg-typescript"
[ -f "$PROJECT_DIR/tsconfig.json" ] || { echo "The TypeScript version is not available yet (see PLAN.md)." >&2; exit 1; }

cd "$PROJECT_DIR"
bun install --frozen-lockfile
bun run build
exec ./dist/rpg-typescript "$@"
