#!/usr/bin/env bash
# Builds and plays the Rust version on Linux/macOS.
# Usage: bash setups/play-on-unix-version-rust.sh [game flags, e.g. --seed 42 --lang pt-BR]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

require() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing '$1'. Install it with: $2" >&2
		exit 1
	fi
}

require cargo "curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh   (Rust 1.99 or newer, then reopen the shell)"

PROJECT_DIR="$ROOT_DIR/rpg-rust"
[ -f "$PROJECT_DIR/Cargo.toml" ] || { echo "The Rust version is not available yet (see PLAN.md)." >&2; exit 1; }

cd "$PROJECT_DIR"
cargo build --release --locked
exec ./target/release/rpg-rust "$@"
