#!/usr/bin/env bash
# Runs a command inside the pinned toolchain image with the repository mounted (dev helper).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if command -v cygpath >/dev/null 2>&1; then ROOT="$(cygpath -m "$ROOT")"; fi
MSYS_NO_PATHCONV=1 exec docker run --rm -i -v "$ROOT:/src" -w /src/rpg-asm rpg-asm-toolchain "$@"
