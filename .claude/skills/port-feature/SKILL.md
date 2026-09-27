---
name: port-feature
description: Use when porting the Python reference implementation (rpg-python) of the CLI Turn-Based RPG to TypeScript (rpg-typescript) or Go (rpg-golang), or when a rule/UI change made in Python must be replicated in the other languages.
---

# Porting a feature from the Python reference

Python is the reference (ADR 0005). A port is correct when **all `shared/golden/*.json` files replay identically**
and the UI controller behaves the same.

## Order of work (per feature or per layer)

1. Read the Python module and its tests; read the matching section of `docs/game-design.md` /
   `docs/cross-language-parity.md`.
2. Port in dependency order: `domain` (rng, formulas, definitions, entities, character) → `application` (events,
   commands, statistics, run state, loot, spawner, progression, battle, merchant, engine, bot, simulator, save game,
   profile, game session) → `infrastructure` (data loader, paths, i18n, art, repositories) → `presentation`
   (event text, render helpers, controller, CLI, TUI renderer).
3. Keep file and type names mirrored: `battle.py` ↔ `battle.ts` ↔ `battle.go`, `GameEngine` everywhere.
4. Port the tests with the code (same cases, same fixtures). Golden replay tests come right after the engine.
5. Only then write the framework renderer (Ink / Bubble Tea) on top of the ported controller.

## Parity traps (learned)

- Integer math only; `pct(v, p)` floors. TypeScript: `Math.floor(v * p / 100)`; Go: `int` division (values are never
  negative).
- PRNG: `Math.imul` + `>>> 0` in TS; `uint32` arithmetic in Go. Check against `shared/golden/prng.json` first.
- `chance(p)` consumes nothing when `p <= 0` or `p >= 100`.
- Every "pick"/"sorted by id" uses code-point string order (`a < b`), never locale compare.
- Iterate JSON arrays in file order; never iterate a map/object to make a game decision (Go maps are random!).
- Event objects must have exactly the fields of `docs/cross-language-parity.md` §3 (no extra `undefined` fields in TS,
  no zero-value fields in Go: use explicit structs → JSON with the same keys).
- Saves use the same camelCase JSON as `RunState.to_dict()`; test that a Python save loads in the port.
- The bot is part of the golden files: port `GreedyBot` decision by decision, including tie-breakers (`max` with
  `(value, id)` keys).

## Lessons from the TypeScript port (v0.5.0)

- Mirror the Python module 1:1 and run `tests/golden` right after the engine: the TS port passed every golden file and
  the bot parity check on the first run by following this order.
- Compare simulator reports between languages with `diff --strip-trailing-cr` (Python prints CRLF on Windows).
- Ink: `bun build --compile` must resolve the optional `react-devtools-core` peer → map it to a local stub via
  `tsconfig.json` `paths` (`--external` and `--define` did not work).
- ink-testing-library: a lone `ESC` needs ~100 ms before Ink emits it; long e2e tests need an explicit timeout
  (`bun test` defaults to 5 s).
- Never import helpers from a `*.test.ts` file (bun re-runs that file's tests); keep them in `tests/helpers.ts`.
- `node:util` `parseArgs` throws `TypeError` for usage errors; wrap them to match argparse's exit code 2.

## Done when

- `shared/golden` replay passes, unit/integration/e2e suites pass, coverage ≥ 90% on domain/application.
- `bun run check:shared` and the language's CI job are green; PLAN.md boxes ticked; CHANGELOG updated.
