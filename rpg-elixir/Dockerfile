# Build from the repository root (shared/ is needed): docker build -f rpg-elixir/Dockerfile -t rpg-elixir .
FROM elixir:1.20.4-otp-27-slim AS build
WORKDIR /app
# Rpg.Infrastructure.Assets embeds ../shared at compile time
COPY shared ./shared
COPY rpg-elixir/mix.exs ./rpg-elixir/
COPY rpg-elixir/lib ./rpg-elixir/lib
WORKDIR /app/rpg-elixir
RUN MIX_ENV=prod mix escript.build

# The escript needs Erlang/OTP at run time (same OTP 27 as the build image).
FROM erlang:27.3.4.3-slim
COPY --from=build /app/rpg-elixir/bin/rpg-elixir /usr/local/bin/rpg-elixir
RUN useradd --create-home --uid 1000 rpg && install -d -o rpg /data
USER rpg
ENV RPG_DATA_DIR=/data
VOLUME /data
ENTRYPOINT ["escript", "/usr/local/bin/rpg-elixir"]
