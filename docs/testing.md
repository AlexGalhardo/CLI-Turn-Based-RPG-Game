# Testing

Every implementation has the same four levels of tests. The engine is pure, so most of the game is covered without a
terminal.

| Level | What it covers | Python (pytest) | TypeScript (bun:test) | Go (testing) |
|---|---|---|---|---|
| Unit | PRNG vectors, formulas, stats aggregation, status effects, item generation, spell/magic levels, XP curve, i18n lookup, art parsing | `tests/unit/` | `tests/unit/` | `*_test.go` next to the code |
| Integration | Engine with real shared data: battles, victory/defeat, merchant, drops, saves round-trip, profile/achievements, data loader + schemas | `tests/integration/` | `tests/integration/` | `internal/.../*_integration_test.go` |
| Golden (parity) | Replays every `shared/golden/*.json` and compares events + final state | `tests/golden/` | `tests/golden/` | `internal/golden/` |
| E2E (TUI) | Drives the real app with key presses: title → new run → battle → merchant → save & quit → continue; language switch; game over screen; full bot run to death | Textual `App.run_test()` (Pilot) | `ink-testing-library` | `teatest` |

## Commands

```bash
# Python
cd rpg-python && uv run pytest                 # all tests + coverage
# TypeScript
cd rpg-typescript && bun test --coverage
# Go
cd rpg-golang && go test -race -cover ./...
# Shared data (root)
bun run check:shared
```

## Rules

- Tests never touch the real save directory: `RPG_DATA_DIR` points at a temp dir (fixtures do this).
- Tests always use a fixed seed and `--no-anim`.
- Coverage floor: **90% lines** on `domain` + `application`, **80%** overall. CI fails below it. Lowering a threshold
  requires a documented reason in the CHANGELOG.
- Never delete or skip a failing test to get green; fix the cause or open an issue and mark it `xfail`/`skip` with the
  issue link.
- A bug fix starts with a failing test that reproduces it.
- Golden files are regenerated only by the Python reference and only when a rule change is intended.
