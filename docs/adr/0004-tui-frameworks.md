# ADR 0004 — Full-screen TUI with Textual, Ink and Bubble Tea

- Status: accepted (2026-09-27)

## Context

The new features need animated monster art, live HP/MP bars and a combat log. The old games only printed lines.

## Decision

Use the most popular full-screen TUI framework of each ecosystem: Textual (Python), Ink (TypeScript/React), Bubble
Tea + Lip Gloss (Go). The layout is specified once in `docs/tui.md` (100 × 30) and all texts come from shared i18n.

## Consequences

- Each framework has a test harness (Pilot, ink-testing-library, teatest) for e2e tests.
- The frameworks differ in paradigm (CSS-like, React, Elm architecture), which is itself useful for learning; exact
  visual equality is checked manually and by comparing key screens in tests.

Later note: the later ports follow the same idea with ratatui (Rust) and FTXUI (C++); Elixir, which has no dependencies,
uses a hand-written ANSI renderer (see [`docs/tui.md`](../tui.md)).
