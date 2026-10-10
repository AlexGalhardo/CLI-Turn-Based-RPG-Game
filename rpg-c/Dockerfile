# Build from the repository root (shared/ is needed): docker build -f rpg-c/Dockerfile -t rpg-c .
FROM gcc:14.4.0 AS build
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends cmake ninja-build && rm -rf /var/lib/apt/lists/*
WORKDIR /src
# cmake/embed_shared.cmake reads ../shared
COPY shared ./shared
COPY rpg-c ./rpg-c
WORKDIR /src/rpg-c
RUN cmake --preset release && cmake --build --preset release

# Same Debian release as gcc:14.4.0 (trixie): the binary needs its glibc.
FROM debian:13.4-slim
COPY --from=build /src/rpg-c/build/release/rpg-c /usr/local/bin/rpg-c
RUN useradd --create-home --uid 1000 rpg && install -d -o rpg /data
USER rpg
ENV RPG_DATA_DIR=/data
VOLUME /data
ENTRYPOINT ["rpg-c"]
