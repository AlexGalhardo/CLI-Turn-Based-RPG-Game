# Build from the repository root (shared/ is needed): docker build -f rpg-typescript/Dockerfile -t rpg-typescript .
FROM oven/bun:1.4.2-slim AS build
WORKDIR /app/rpg-typescript
COPY rpg-typescript/package.json rpg-typescript/bun.lock ./
RUN bun install --frozen-lockfile
# src/infrastructure/embedded-shared.ts imports ../../../shared statically
COPY shared /app/shared
COPY rpg-typescript/tsconfig.json ./
COPY rpg-typescript/src ./src
RUN bun run build

FROM debian:12.14-slim
COPY --from=build /app/rpg-typescript/dist/rpg-typescript /usr/local/bin/rpg-typescript
RUN useradd --create-home --uid 1000 rpg && install -d -o rpg /data
USER rpg
ENV RPG_DATA_DIR=/data
VOLUME /data
ENTRYPOINT ["rpg-typescript"]
