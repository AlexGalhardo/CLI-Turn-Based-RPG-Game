# ADR 0001 — Single monorepo for the three implementations

- Status: accepted (2026-09-27)

## Context

The 2016 Python and 2022 TypeScript games lived in separate repositories and drifted apart (different vocations,
monsters and rules). A Go version is being added and the goal is a side-by-side comparison of the same game.

## Decision

One repository with `rpg-python/`, `rpg-typescript/`, `rpg-golang/` and a language-agnostic `shared/` folder. One
CHANGELOG and one SemVer version for the whole repository. The old code is not carried over; the original repositories
remain on GitHub for history.

## Consequences

- A rule change and its three ports can land and be reviewed together; CI enforces parity.
- CI must run per-language jobs; path filters keep it fast.

Later note: the Rust, Elixir and C++ ports (`rpg-rust/`, `rpg-elixir/`, `rpg-cpp/`) joined under the same decision.
