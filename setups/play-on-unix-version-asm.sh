#!/usr/bin/env bash
# Builds and plays the Assembly version on Linux/macOS. The port targets x86-64 Linux, so it always runs through
# Docker (on Apple Silicon the image is emulated).
# Usage: bash setups/play-on-unix-version-asm.sh [game flags, e.g. --seed 42]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

require() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing '$1'. Install it with: $2" >&2
		exit 1
	fi
}

require docker "https://docs.docker.com/get-docker/ (Docker Engine or Docker Desktop, running)"

PROJECT_DIR="$ROOT_DIR/rpg-asm"
[ -f "$PROJECT_DIR/Dockerfile" ] || { echo "The Assembly version is not available yet (see PLAN.md)." >&2; exit 1; }

# The build context is the repository root: the game data is generated from shared/.
docker build --platform linux/amd64 -f "$PROJECT_DIR/Dockerfile" -t rpg-asm "$ROOT_DIR"

RUN_FLAGS=(--rm -i --platform linux/amd64)
[ -t 0 ] && RUN_FLAGS+=(-t)
exec docker run "${RUN_FLAGS[@]}" rpg-asm "$@"
