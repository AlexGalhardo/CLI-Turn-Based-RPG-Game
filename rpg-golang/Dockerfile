# Build from the repository root (shared/ is needed): docker build -f rpg-golang/Dockerfile -t rpg-golang .
FROM golang:1.27.0-bookworm AS build
WORKDIR /src
COPY shared ./shared
COPY rpg-golang ./rpg-golang
WORKDIR /src/rpg-golang
# go generate copies ../shared into internal/assets/shared for go:embed
RUN go generate ./... && CGO_ENABLED=0 go build -trimpath -o /out/rpg-golang ./cmd/rpg

FROM debian:12.14-slim
COPY --from=build /out/rpg-golang /usr/local/bin/rpg-golang
RUN useradd --create-home --uid 1000 rpg && install -d -o rpg /data
USER rpg
ENV RPG_DATA_DIR=/data
VOLUME /data
ENTRYPOINT ["rpg-golang"]
