# ADR 0005 — Python is built completely first and is the reference

- Status: accepted (2026-09-27)

## Context

Designing new features in three languages at once multiplies every design change by three.

## Decision

Phase 1 delivers the full game (alpha, beta, 1.0 candidate) in Python. Phases 2 and 3 port the stable design to
TypeScript and Go, validated by the golden files produced in Phase 1.

## Consequences

- Design churn happens once. TypeScript and Go ports are mostly mechanical and test-driven.
- The Python version is the tie-breaker when the docs are ambiguous (after which the docs are fixed).
