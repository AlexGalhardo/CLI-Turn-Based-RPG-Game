#!/usr/bin/env bash
# Builds the downloadable binaries of a release tag on this machine and attaches them to its GitHub Release, with no
# GitHub Actions (release.yml is disabled; see docs/ci-cd.md). Produces the same assets as release.yml:
#   bash scripts/release-local.sh vX.Y.Z              # build, then create the Release if missing and upload
#   bash scripts/release-local.sh vX.Y.Z --no-upload  # build only (assets in $OUT_DIR, default ./dist-release)
# Host toolchains: bun, go, mix (+ cargo and clang++ from LLVM-MinGW for the Windows builds of Rust and C++, built
# only on a Windows host). Docker builds Rust and C++ for linux-x64 and darwin-arm64 (zig cross-compiles macOS).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

for dir in "$HOME/.cargo/bin" "$HOME/go/bin" "$HOME/.local/bin" "$HOME/scoop/shims" \
	"$HOME/scoop/apps/elixir/current/bin" "$HOME/scoop/apps/mingw-mstorsjo-llvm-ucrt/current/bin"; do
	[ -d "$dir" ] && PATH="$dir:$PATH"
done
export PATH

TAG="${1:?usage: bash scripts/release-local.sh vX.Y.Z [--no-upload]}"
UPLOAD=true
[ "${2:-}" = "--no-upload" ] && UPLOAD=false
VERSION="${TAG#v}"
git rev-parse -q --verify "refs/tags/$TAG" >/dev/null || { echo "tag $TAG not found (git fetch --tags)" >&2; exit 1; }

RUST_IMAGE="rust:1.99.0-bookworm"
CPP_IMAGE="gcc:14"
ZIG_IMAGE="ghcr.io/rust-cross/cargo-zigbuild:0.23.4"

OUT_DIR="${OUT_DIR:-$ROOT_DIR/dist-release}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
rm -rf "$OUT_DIR" && mkdir -p "$OUT_DIR"
# Build from the tag itself, never from the working tree.
git archive "$TAG" | tar -x -C "$WORK"
SRC="$WORK"

windows_host=false
case "$(uname -s)" in MINGW* | MSYS* | CYGWIN*) windows_host=true ;; esac
# Docker Desktop on Windows needs Windows paths and no MSYS path mangling of the container paths.
host_path() { if $windows_host; then cygpath -m "$1"; else printf '%s' "$1"; fi; }
docker_run() { MSYS_NO_PATHCONV=1 docker run --rm -v "$(host_path "$SRC"):/src:ro" -v "$(host_path "$OUT_DIR"):/out" "$@"; }

step() { printf '\n== %s\n' "$*"; }
# Cross-builds that may fail without failing the release: the asset is reported as missing at the end.
optional() { "$@" || echo "warning: optional step failed" >&2; }

step "typescript"
(cd "$SRC/rpg-typescript" && bun install --frozen-lockfile >/dev/null &&
	for pair in linux-x64:rpg-typescript-linux-x64 darwin-arm64:rpg-typescript-darwin-arm64 \
		windows-x64:rpg-typescript-windows-x64.exe; do
		bun build --compile --minify --target="bun-${pair%%:*}" ./src/main.tsx --outfile "$OUT_DIR/${pair#*:}" >/dev/null
	done)

step "golang"
(cd "$SRC/rpg-golang" && go generate ./... &&
	for triple in linux:amd64:rpg-golang-linux-x64 darwin:arm64:rpg-golang-darwin-arm64 \
		windows:amd64:rpg-golang-windows-x64.exe; do
		IFS=: read -r os arch asset <<<"$triple"
		CGO_ENABLED=0 GOOS="$os" GOARCH="$arch" go build -trimpath -ldflags="-s -w" -o "$OUT_DIR/$asset" ./cmd/rpg
	done)

step "elixir (escript, needs Erlang/OTP to run)"
(cd "$SRC/rpg-elixir" && MIX_ENV=prod mix escript.build >/dev/null && cp bin/rpg-elixir "$OUT_DIR/rpg-elixir-escript")

if $windows_host; then
	step "rust + cpp windows-x64 (host)"
	(cd "$SRC/rpg-rust" && cargo build --release --locked && cp target/release/rpg-rust.exe "$OUT_DIR/rpg-rust-windows-x64.exe")
	(cd "$SRC/rpg-cpp" && cmake --preset release -DCMAKE_CXX_COMPILER=clang++ >/dev/null && cmake --build --preset release &&
		cp build/release/rpg-cpp.exe "$OUT_DIR/rpg-cpp-windows-x64.exe")
else
	echo "warning: not a Windows host; rpg-rust/rpg-cpp windows-x64 are not built" >&2
fi

# The containers copy shared/ next to the project (build.rs and embed_shared.cmake read ../shared) and drop the build
# outputs left in the tag checkout by the host builds above (a Windows CMake cache or target/ breaks the Linux build).
step "rust linux-x64 (docker)"
optional docker_run "$RUST_IMAGE" bash -c 'set -e; mkdir /w && cp -r /src/shared /src/rpg-rust /w/ && cd /w/rpg-rust && rm -rf target && cargo build --release --locked &&
	cp target/release/rpg-rust /out/rpg-rust-linux-x64'

step "cpp linux-x64 (docker)"
# libstdc++ and libgcc are linked statically so the binary runs on older distributions too.
docker_run "$CPP_IMAGE" bash -c 'set -e; apt-get update -qq >/dev/null && apt-get install -y -qq cmake ninja-build >/dev/null
	mkdir /w && cp -r /src/shared /src/rpg-cpp /w/ && cd /w/rpg-cpp && rm -rf build &&
	cmake --preset release "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc" >/dev/null &&
	cmake --build --preset release && cp build/release/rpg-cpp /out/rpg-cpp-linux-x64'

step "rust darwin-arm64 (docker, cargo-zigbuild)"
optional docker_run "$ZIG_IMAGE" bash -c 'set -e; rustup toolchain install 1.99.0 --profile minimal -t aarch64-apple-darwin >/dev/null
	mkdir /w && cp -r /src/shared /src/rpg-rust /w/ && cd /w/rpg-rust && rm -rf target && cargo +1.99.0 zigbuild --release --locked --target aarch64-apple-darwin &&
	cp target/aarch64-apple-darwin/release/rpg-rust /out/rpg-rust-darwin-arm64'

step "cpp darwin-arm64 (docker, zig c++)"
optional docker_run "$ZIG_IMAGE" bash -c 'set -e; apt-get update -qq >/dev/null && apt-get install -y -qq cmake ninja-build >/dev/null
	printf "#!/bin/sh\nexec zig c++ -target aarch64-macos \"\$@\"\n" > /usr/local/bin/zcxx
	printf "#!/bin/sh\nexec zig cc -target aarch64-macos \"\$@\"\n" > /usr/local/bin/zcc
	chmod +x /usr/local/bin/zcxx /usr/local/bin/zcc
	mkdir /w && cp -r /src/shared /src/rpg-cpp /w/ && cd /w/rpg-cpp && rm -rf build &&
	cmake --preset release -DCMAKE_SYSTEM_NAME=Darwin -DCMAKE_SYSTEM_PROCESSOR=arm64 \
		-DCMAKE_C_COMPILER=zcc -DCMAKE_CXX_COMPILER=zcxx >/dev/null &&
	cmake --build --preset release && cp build/release/rpg-cpp /out/rpg-cpp-darwin-arm64'

step "smoke tests"
# Only the binaries this machine (or a linux container) can execute; darwin ones are checked by file type.
native=(rpg-typescript rpg-golang)
$windows_host && native+=(rpg-rust rpg-cpp)
for name in "${native[@]}"; do
	if $windows_host; then bin="$OUT_DIR/$name-windows-x64.exe"; else bin="$OUT_DIR/$name-linux-x64"; fi
	"$bin" --version | grep -q "$VERSION" || { echo "$bin does not report $VERSION" >&2; exit 1; }
done
docker_run "$CPP_IMAGE" bash -c "for b in /out/*-linux-x64; do
	\$b --version | grep -q '$VERSION' || { echo \"\$b bad\"; exit 1; }; done"
expected=(rpg-typescript-{linux-x64,darwin-arm64,windows-x64.exe} rpg-golang-{linux-x64,darwin-arm64,windows-x64.exe}
	rpg-cpp-linux-x64 rpg-elixir-escript)
for asset in rpg-rust-linux-x64 rpg-rust-darwin-arm64 rpg-cpp-darwin-arm64; do
	[ -s "$OUT_DIR/$asset" ] || echo "warning: $asset not built" >&2
done
$windows_host && expected+=(rpg-rust-windows-x64.exe rpg-cpp-windows-x64.exe)
for asset in "${expected[@]}"; do [ -s "$OUT_DIR/$asset" ] || { echo "missing $asset" >&2; exit 1; }; done

(cd "$OUT_DIR" && sha256sum rpg-* > SHA256SUMS.txt)
ls -la "$OUT_DIR"

$UPLOAD || exit 0
step "upload to the GitHub Release $TAG"
if ! gh release view "$TAG" >/dev/null 2>&1; then
	git show "$TAG:CHANGELOG.md" > "$WORK/changelog.md"
	bun run scripts/release.ts notes "$VERSION" "$WORK/changelog.md" > "$WORK/notes.md"
	gh release create "$TAG" --verify-tag --title "$TAG" --notes-file "$WORK/notes.md"
fi
gh release upload "$TAG" "$OUT_DIR"/* --clobber
gh release view "$TAG" --json assets -q '.assets[].name'
