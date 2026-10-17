#!/usr/bin/env bash
# Records the README gameplay GIFs (docs/media/*.gif) with VHS in Docker, from the tapes in scripts/gameplay/.
# Usage: bash scripts/record-gameplay.sh [tape-name ...]   (default: every tape)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

image="rpg-gameplay"
docker build -f scripts/gameplay/Dockerfile -t "$image" .

if [ "$#" -eq 0 ]; then
	set -- start combat boss menus
fi

mkdir -p docs/media
for name in "$@"; do
	echo "recording $name"
	# MSYS_NO_PATHCONV keeps Git Bash on Windows from rewriting the container paths.
	MSYS_NO_PATHCONV=1 docker run --rm -v "$root:/vhs" -w /vhs "$image" "scripts/gameplay/$name.tape"
done
