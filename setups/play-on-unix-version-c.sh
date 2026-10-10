#!/usr/bin/env bash
# Builds and plays the C version on Linux/macOS.
# Usage: bash setups/play-on-unix-version-c.sh [game flags, e.g. --seed 42 --lang pt-BR]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

require() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing '$1'. Install it with: $2" >&2
		exit 1
	fi
}

require cmake "https://cmake.org/download/ (CMake 3.28 or newer; e.g. 'sudo apt install cmake' or 'brew install cmake')"
require ninja "'sudo apt install ninja-build' or 'brew install ninja'"
if ! command -v cc >/dev/null 2>&1 && ! command -v gcc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1; then
	echo "Missing a C17 compiler. Install GCC ('sudo apt install gcc') or Clang (Xcode on macOS)." >&2
	exit 1
fi

PROJECT_DIR="$ROOT_DIR/rpg-c"
[ -f "$PROJECT_DIR/CMakeLists.txt" ] || { echo "The C version is not available yet (see PLAN.md)." >&2; exit 1; }

cd "$PROJECT_DIR"
cmake --preset release
cmake --build --preset release
exec ./build/release/rpg-c "$@"
