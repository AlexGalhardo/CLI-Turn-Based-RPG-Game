# Build from the repository root (shared/ is needed): docker build -f rpg-cpp/Dockerfile -t rpg-cpp .
FROM gcc:14.4.0 AS build
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends cmake ninja-build && rm -rf /var/lib/apt/lists/*
WORKDIR /src
# cmake/embed_shared.cmake reads ../shared
COPY shared ./shared
COPY rpg-cpp ./rpg-cpp
WORKDIR /src/rpg-cpp
# libstdc++ and libgcc are linked statically: the runtime image ships an older libstdc++ than GCC 14.
RUN cmake --preset release "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc" && cmake --build --preset release

# Same Debian release as gcc:14.4.0 (trixie): the binary needs its glibc.
FROM debian:13.4-slim
COPY --from=build /src/rpg-cpp/build/release/rpg-cpp /usr/local/bin/rpg-cpp
RUN useradd --create-home --uid 1000 rpg && install -d -o rpg /data
USER rpg
ENV RPG_DATA_DIR=/data
VOLUME /data
ENTRYPOINT ["rpg-cpp"]
