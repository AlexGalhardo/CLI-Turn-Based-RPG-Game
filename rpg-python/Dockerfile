# Build from the repository root (shared/ is needed): docker build -f rpg-python/Dockerfile -t rpg-python .
FROM python:3.14.7-slim-bookworm AS build
COPY --from=ghcr.io/astral-sh/uv:0.12.19 /uv /usr/local/bin/uv
WORKDIR /app/rpg-python
# pyproject.toml points its readme at ../README.md
COPY README.md /app/README.md
COPY rpg-python/pyproject.toml rpg-python/uv.lock ./
RUN uv sync --frozen --no-dev --no-install-project
COPY rpg-python/src ./src
RUN uv sync --frozen --no-dev --no-editable

FROM python:3.14.7-slim-bookworm
COPY --from=build /app/rpg-python/.venv /app/rpg-python/.venv
# Installed as a package, so it cannot find shared/ by walking up from the source tree.
COPY shared /app/shared
ENV RPG_SHARED_DIR=/app/shared
RUN useradd --create-home --uid 1000 rpg && install -d -o rpg /data
USER rpg
ENV RPG_DATA_DIR=/data
VOLUME /data
ENTRYPOINT ["/app/rpg-python/.venv/bin/rpg"]
