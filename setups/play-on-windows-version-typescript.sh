#!/usr/bin/env bash
# Builds and plays the TypeScript version (Bun single-file executable) on Windows using Git Bash.
# Usage: bash setups/play-on-windows-version-typescript.sh [game flags, e.g. --seed 42 --lang pt-BR]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

require() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing '$1'. Install it with: $2" >&2
		exit 1
	fi
}

require bun "powershell -c \"irm bun.sh/install.ps1 | iex\"   (then reopen Git Bash)"

PROJECT_DIR="$ROOT_DIR/rpg-typescript"
[ -f "$PROJECT_DIR/tsconfig.json" ] || { echo "The TypeScript version is not available yet (see PLAN.md)." >&2; exit 1; }

RUN=()
if [ -n "${MSYSTEM:-}" ] && command -v winpty >/dev/null 2>&1 && [ -z "${WT_SESSION:-}" ]; then
	RUN=(winpty)
fi

cd "$PROJECT_DIR"
bun install --frozen-lockfile
bun run build
exec "${RUN[@]}" ./dist/rpg-typescript.exe "$@"
