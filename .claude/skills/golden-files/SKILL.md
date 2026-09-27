---
name: golden-files
description: Use when a change alters deterministic game outcomes (rules, formulas, RNG consumption order, event shapes, bot decisions, balance numbers in shared/data) in the CLI Turn-Based RPG monorepo — explains when and how to regenerate shared/golden and keep Python, TypeScript and Go in parity.
---

# Golden files (cross-language parity)

`shared/golden/*.json` are recorded by the **Python reference** and replayed by the Python, TypeScript and Go test
suites. They are the proof that the three implementations are the same game.

## When a regeneration is required

Any change that alters, for a fixed seed and command list, the events or the final state:

- formulas or order of operations (`docs/game-design.md`)
- RNG consumption (a new `roll`/`chance`, a changed call order, a `chance(0)`→`chance(5)` change)
- event names or fields (`docs/cross-language-parity.md` §3)
- balance/content numbers in `shared/data/*.json` (monsters, spells, balance, potions, items, affixes)
- bot heuristics (`application/bot.py` in each language) — bot full runs record the bot's commands

`test_golden_files_are_up_to_date` (Python) fails when you forgot to regenerate.

## Steps

1. Update the rule in `docs/game-design.md` / `docs/cross-language-parity.md` first.
2. Implement it in `rpg-python` and make its unit/integration tests pass.
3. Regenerate: `cd rpg-python && uv run rpg-golden` (uses `uv`; on Windows Git Bash put uv on PATH first).
4. Run `uv run pytest` — golden replay + up-to-date tests must pass.
5. Port the change to `rpg-typescript` and `rpg-golang`; their golden suites must pass with the new files.
6. Commit rule + code + golden files together, e.g. `feat(python): …` then `test(shared): regenerate golden files`,
   and note balance changes in `CHANGELOG.md`.

## Gotchas learned

- Never hand-edit golden files; always regenerate.
- `rng.chance(p)` with `p <= 0` or `p >= 100` consumes **no** number — changing a 0% effect to a small % shifts every
  later roll.
- Item generation with no candidate items consumes no randomness; adding a new item to a tier changes drops.
- Biome ignores `shared/golden` (the files are single-line JSON on purpose).
