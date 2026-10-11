# Build from the repository root (shared/ is needed): docker build -f rpg-lua/Dockerfile -t rpg-lua .
FROM gcc:14.4.0 AS build
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends cmake ninja-build && rm -rf /var/lib/apt/lists/*
WORKDIR /src
# cmake/embed.cmake reads ../shared; the tests replay shared/golden.
COPY shared ./shared
COPY rpg-lua ./rpg-lua
WORKDIR /src/rpg-lua
# The Lua 5.5.0 source is downloaded by CMake (pinned by checksum in CMakeLists.txt) and compiled into the binary.
RUN cmake --preset release -DRPG_BUILD_TESTS=ON && cmake --build --preset release
# The image only exists when the four test levels pass on Linux (this is where the POSIX host code is exercised).
RUN ctest --test-dir build/release --output-on-failure

# Same Debian release as gcc:14.4.0 (trixie): the binary needs its glibc.
FROM debian:13.4-slim
COPY --from=build /src/rpg-lua/build/release/rpg-lua /usr/local/bin/rpg-lua
RUN useradd --create-home --uid 1000 rpg && install -d -o rpg /data
USER rpg
ENV RPG_DATA_DIR=/data
VOLUME /data
ENTRYPOINT ["rpg-lua"]
