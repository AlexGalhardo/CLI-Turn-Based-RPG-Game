# Records the README gameplay GIFs with VHS. Build from the repository root (shared/ is needed):
#   bash scripts/record-gameplay.sh
FROM golang:1.27.0-bookworm AS build
WORKDIR /src
COPY shared ./shared
COPY rpg-golang ./rpg-golang
WORKDIR /src/rpg-golang
RUN go generate ./... && CGO_ENABLED=0 go build -trimpath -o /out/rpg-golang ./cmd/rpg

FROM ghcr.io/charmbracelet/vhs:v0.12.1
COPY --from=build /out/rpg-golang /usr/local/bin/rpg-golang
