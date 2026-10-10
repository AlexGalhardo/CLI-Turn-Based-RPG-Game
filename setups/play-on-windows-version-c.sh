#!/usr/bin/env bash
# Builds and plays the C version on Windows using Git Bash.
# Usage: bash setups/play-on-windows-version-c.sh [game flags, e.g. --seed 42 --lang pt-BR]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# The LLVM-MinGW toolchain installed by scoop is not always on the Git Bash PATH.
LLVM_MINGW="$HOME/scoop/apps/mingw-mstorsjo-llvm-ucrt/current/bin"
[ -d "$LLVM_MINGW" ] && export PATH="$LLVM_MINGW:$HOME/scoop/shims:$PATH"

require() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing '$1'. Install it with: $2" >&2
		exit 1
	fi
}

require clang "scoop install mingw-mstorsjo-llvm-ucrt   (https://scoop.sh, then reopen Git Bash)"
require cmake "scoop install cmake"
require ninja "scoop install ninja"

PROJECT_DIR="$ROOT_DIR/rpg-c"
[ -f "$PROJECT_DIR/CMakeLists.txt" ] || { echo "The C version is not available yet (see PLAN.md)." >&2; exit 1; }

# Git Bash's own window (mintty) is not a Windows console: winpty gives the game a real one.
RUN=()
if [ -n "${MSYSTEM:-}" ] && command -v winpty >/dev/null 2>&1 && [ -z "${WT_SESSION:-}" ]; then
	RUN=(winpty)
fi

cd "$PROJECT_DIR"
cmake --preset release -DCMAKE_C_COMPILER=clang
cmake --build --preset release
exec "${RUN[@]}" ./build/release/rpg-c.exe "$@"
