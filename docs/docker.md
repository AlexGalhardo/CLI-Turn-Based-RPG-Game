# Docker

Every implementation runs in Docker, so playing or comparing them needs no toolchain on the host.

## Playing

```bash
docker compose run --rm python                       # python | typescript | golang | rust | elixir | cpp
docker compose run --rm rust --seed 42 --lang pt-BR  # game flags go after the service name
docker compose run --rm -T golang --simulate 100     # -T: no TTY, for the simulator or piped output
docker compose build cpp                             # rebuild one image after a change
docker compose down -v                               # remove the containers and the saves volume
```

Without compose: `docker build -f rpg-rust/Dockerfile -t rpg-rust . && docker run --rm -it rpg-rust --seed 42`.

## How the images are built

- One `Dockerfile` per project (`rpg-*/Dockerfile`), always built **from the repository root**: every implementation
  embeds or reads `shared/`. `.dockerignore` keeps build outputs, `docs/`, `shared/golden` and local files out.
- Multi-stage: a toolchain stage builds, a small runtime stage runs. Exact image tags, never `latest`.

  | Service | Build image | Runtime image | Runs |
  |---|---|---|---|
  | `python` | `python:3.14.7-slim-bookworm` + `ghcr.io/astral-sh/uv:0.12.19` | `python:3.14.7-slim-bookworm` | the venv (`RPG_SHARED_DIR=/app/shared`) |
  | `typescript` | `oven/bun:1.4.2-slim` | `debian:12.14-slim` | the `bun build --compile` binary |
  | `golang` | `golang:1.27.0-bookworm` | `debian:12.14-slim` | static binary (`go generate` + `CGO_ENABLED=0`) |
  | `rust` | `rust:1.99.0-bookworm` | `debian:12.14-slim` | `cargo build --release --locked` binary |
  | `elixir` | `elixir:1.20.4-otp-27-slim` | `erlang:27.3.4.3-slim` | the escript (needs Erlang/OTP) |
  | `cpp` | `gcc:14.4.0` | `debian:13.4-slim` | binary with static libstdc++/libgcc; same Debian release as the build image for glibc |

- The game runs as the non-root user `rpg` (uid 1000) with `RPG_DATA_DIR=/data`.
- `compose.yml` mounts the named volume `saves` on `/data` for every service: save, history and profile formats are
  shared (see [persistence](persistence.md)), so a run started in one language continues in another.
- A new implementation gets its own `Dockerfile` following these rules and a service in `compose.yml`.

## Checking an image

`docker compose run --rm -T <service> --simulate 5 --seed 42` must print exactly what
`cd rpg-python && uv run rpg --simulate 5 --seed 42` prints on the host. Pull or build images one at a time: parallel
pulls of multi-GB images hung Docker Desktop on Windows.
