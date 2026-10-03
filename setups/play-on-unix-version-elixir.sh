#!/usr/bin/env bash
# Builds and plays the Elixir version on Linux/macOS.
# Usage: bash setups/play-on-unix-version-elixir.sh [game flags, e.g. --seed 42 --lang pt-BR]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

require() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing '$1'. Install it with: $2" >&2
		exit 1
	fi
}

require escript "https://elixir-lang.org/install.html (Erlang/OTP 28 or newer)"
require mix "https://elixir-lang.org/install.html (Elixir 1.18 or newer)"

PROJECT_DIR="$ROOT_DIR/rpg-elixir"
[ -f "$PROJECT_DIR/mix.exs" ] || { echo "The Elixir version is not available yet (see PLAN.md)." >&2; exit 1; }

cd "$PROJECT_DIR"
MIX_ENV=prod mix escript.build
exec ./bin/rpg-elixir "$@"
