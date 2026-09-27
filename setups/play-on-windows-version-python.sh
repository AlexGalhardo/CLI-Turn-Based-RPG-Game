#!/usr/bin/env bash
# Plays the Python version (reference implementation) on Windows using Git Bash.
# Usage: bash setups/play-on-windows-version-python.sh [game flags, e.g. --seed 42 --lang pt-BR]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

require() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing '$1'. Install it with: $2" >&2
		exit 1
	fi
}

require uv "winget install --id astral-sh.uv -e   (then reopen Git Bash)"

PROJECT_DIR="$ROOT_DIR/rpg-python"
[ -f "$PROJECT_DIR/pyproject.toml" ] || { echo "The Python version is not available yet (see PLAN.md)." >&2; exit 1; }

# Git Bash's mintty is not a Windows console; winpty gives Textual a real console when available.
RUN=()
if [ -n "${MSYSTEM:-}" ] && command -v winpty >/dev/null 2>&1 && [ -z "${WT_SESSION:-}" ]; then
	RUN=(winpty)
fi

uv sync --project "$PROJECT_DIR" --frozen --no-dev
exec "${RUN[@]}" uv run --project "$PROJECT_DIR" --no-dev rpg "$@"
