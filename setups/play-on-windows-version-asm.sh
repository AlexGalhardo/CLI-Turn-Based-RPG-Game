#!/usr/bin/env bash
# Builds and plays the Assembly version on Windows using Git Bash. The port targets x86-64 Linux, so it runs in a
# Linux container (Docker Desktop with Linux containers).
# Usage: bash setups/play-on-windows-version-asm.sh [game flags, e.g. --seed 42]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

require() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing '$1'. Install it with: $2" >&2
		exit 1
	fi
}

require docker "scoop install docker (or Docker Desktop: https://docs.docker.com/desktop/), then start Docker"

PROJECT_DIR="$ROOT_DIR/rpg-asm"
[ -f "$PROJECT_DIR/Dockerfile" ] || { echo "The Assembly version is not available yet (see PLAN.md)." >&2; exit 1; }

# The build context is the repository root: the game data is generated from shared/.
docker build -f "$PROJECT_DIR/Dockerfile" -t rpg-asm "$ROOT_DIR"

# mintty (the default Git Bash window) is not a Windows console: `docker run -t` needs winpty there.
RUN=()
RUN_FLAGS=(--rm -i)
if [ -t 0 ]; then
	RUN_FLAGS+=(-t)
	if [ -n "${MSYSTEM:-}" ] && command -v winpty >/dev/null 2>&1 && [ -z "${WT_SESSION:-}" ]; then
		RUN=(winpty)
	fi
fi
exec "${RUN[@]}" docker run "${RUN_FLAGS[@]}" rpg-asm "$@"
