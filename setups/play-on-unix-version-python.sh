#!/usr/bin/env bash
# Plays the Python version (reference implementation) on Linux/macOS.
# Usage: bash setups/play-on-unix-version-python.sh [game flags, e.g. --seed 42 --lang pt-BR]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

require() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing '$1'. Install it with: $2" >&2
		exit 1
	fi
}

require uv "curl -LsSf https://astral.sh/uv/install.sh | sh"

PROJECT_DIR="$ROOT_DIR/rpg-python"
[ -f "$PROJECT_DIR/pyproject.toml" ] || { echo "The Python version is not available yet (see PLAN.md)." >&2; exit 1; }

# uv downloads Python 3.14 automatically when it is not installed.
uv sync --project "$PROJECT_DIR" --frozen --no-dev
exec uv run --project "$PROJECT_DIR" --no-dev rpg "$@"
