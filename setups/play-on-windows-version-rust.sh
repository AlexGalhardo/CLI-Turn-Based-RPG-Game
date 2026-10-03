#!/usr/bin/env bash
# Builds and plays the Rust version on Windows using Git Bash.
# Usage: bash setups/play-on-windows-version-rust.sh [game flags, e.g. --seed 42 --lang pt-BR]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v cargo >/dev/null 2>&1 && [ -x "$HOME/.cargo/bin/cargo" ]; then
	export PATH="$HOME/.cargo/bin:$PATH"
fi

require() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing '$1'. Install it with: $2" >&2
		exit 1
	fi
}

require cargo "winget install --id Rustlang.Rustup -e   (then reopen Git Bash; the MSVC toolchain needs the Visual Studio C++ Build Tools)"

PROJECT_DIR="$ROOT_DIR/rpg-rust"
[ -f "$PROJECT_DIR/Cargo.toml" ] || { echo "The Rust version is not available yet (see PLAN.md)." >&2; exit 1; }

RUN=()
if [ -n "${MSYSTEM:-}" ] && command -v winpty >/dev/null 2>&1 && [ -z "${WT_SESSION:-}" ]; then
	RUN=(winpty)
fi

cd "$PROJECT_DIR"
cargo build --release --locked
exec "${RUN[@]}" ./target/release/rpg-rust.exe "$@"
