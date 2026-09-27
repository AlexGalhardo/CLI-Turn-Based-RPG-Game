# ADR 0002 — Game content and balance in shared JSON

- Status: accepted (2026-09-27)

## Context

Duplicated constants per language (as in the old games) diverge silently.

## Decision

All content and numbers live in `shared/data/*.json` validated by JSON Schema; formulas live in
`docs/game-design.md`. Implementations contain no balance numbers.

## Consequences

- Adding a monster or item is a data change (Open/Closed principle).
- Go needs a `go generate` copy step because `go:embed` can't read parent directories.
