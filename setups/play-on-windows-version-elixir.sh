#!/usr/bin/env bash
# Builds and plays the Elixir version on Windows using Git Bash.
# Usage: bash setups/play-on-windows-version-elixir.sh [game flags, e.g. --seed 42 --lang pt-BR]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Scoop's .bat shims for mix/elixir do not run from Git Bash; the scripts in the Elixir bin folder do.
if [ -d "$HOME/scoop/apps/elixir/current/bin" ]; then
	PATH="$HOME/scoop/apps/elixir/current/bin:$HOME/scoop/shims:$PATH"
fi

require() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing '$1'. Install it with: $2" >&2
		exit 1
	fi
}

require escript "scoop install erlang   (Erlang/OTP 28 or newer, then reopen Git Bash)"
require mix "scoop install elixir   (Elixir 1.18 or newer, then reopen Git Bash)"

PROJECT_DIR="$ROOT_DIR/rpg-elixir"
[ -f "$PROJECT_DIR/mix.exs" ] || { echo "The Elixir version is not available yet (see PLAN.md)." >&2; exit 1; }

RUN=()
if [ -n "${MSYSTEM:-}" ] && command -v winpty >/dev/null 2>&1 && [ -z "${WT_SESSION:-}" ]; then
	RUN=(winpty)
fi

cd "$PROJECT_DIR"
MIX_ENV=prod mix escript.build
# An escript is not a Windows executable: run it through escript.exe.
exec "${RUN[@]}" escript bin/rpg-elixir "$@"
