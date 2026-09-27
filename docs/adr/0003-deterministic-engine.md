# ADR 0003 — Deterministic, pure engine with a shared PRNG and golden tests

- Status: accepted (2026-09-27)

## Context

"Same game in three languages" must be verifiable, not just claimed. Language RNGs and float math differ.

## Decision

- The engine is a pure state machine: `step(command) → events`. No I/O, clock or global state.
- mulberry32 PRNG implemented identically in the three languages; integer-only math with explicit floor.
- The Python reference generates golden files (seed + commands → events); TS and Go replay them in their tests.

## Consequences

- Seeded runs are reproducible across languages (and saves are portable).
- Every rule change must specify RNG consumption order; golden files are regenerated only on intended changes.
