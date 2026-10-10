# Builds, tests and runs the Assembly port. The build context is the REPOSITORY ROOT (shared/ is needed):
#
#   docker build -f rpg-asm/Dockerfile -t rpg-asm .
#   docker run --rm -it rpg-asm --seed 42
#
# Every tag and tool version is pinned (no `latest`).

# ── toolchain: pinned Python (data generator, test tools) + NASM + gcc as linker driver ────────────────────────
FROM python:3.14.8-slim-trixie AS toolchain
RUN apt-get update \
	&& apt-get install -y --no-install-recommends nasm=2.16.03-1 gcc=4:14.2.0-1 libc6-dev make=4.4.1-2 \
	&& rm -rf /var/lib/apt/lists/*
WORKDIR /src/rpg-asm

# ── build: generate the game data, assemble, link and run the parity tests ─────────────────────────────────────
FROM toolchain AS build
COPY shared /src/shared
COPY rpg-python/src /src/rpg-python/src
COPY rpg-asm /src/rpg-asm
RUN make clean && make && make test && make test-simulator

# ── runtime: the binary only (dynamically linked against the glibc of the same Debian release) ─────────────────
FROM debian:trixie-20261005-slim AS runtime
COPY --from=build /src/rpg-asm/build/rpg-asm /usr/local/bin/rpg-asm
RUN useradd --create-home --uid 1000 rpg
USER rpg
ENTRYPOINT ["rpg-asm"]
