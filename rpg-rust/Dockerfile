# Build from the repository root (shared/ is needed): docker build -f rpg-rust/Dockerfile -t rpg-rust .
FROM rust:1.99.0-bookworm AS build
WORKDIR /src
# build.rs embeds ../shared
COPY shared ./shared
COPY rpg-rust ./rpg-rust
WORKDIR /src/rpg-rust
RUN cargo build --release --locked

FROM debian:12.14-slim
COPY --from=build /src/rpg-rust/target/release/rpg-rust /usr/local/bin/rpg-rust
RUN useradd --create-home --uid 1000 rpg && install -d -o rpg /data
USER rpg
ENV RPG_DATA_DIR=/data
VOLUME /data
ENTRYPOINT ["rpg-rust"]
