---
name: add-game-content
description: Use when adding or changing monsters, bosses, items, affixes, spells, potions, statuses, achievements, i18n strings or ASCII art in shared/ of the CLI Turn-Based RPG monorepo.
---

# Adding game content (shared/)

Content is data, not code (ADR 0002). All three implementations load the same files.

## Checklist

1. Edit the right file in `shared/data/` (see `docs/data-format.md`). Ids are `snake_case` and never change after a
   release (saves reference them). Names come from TibiaWiki; stats are original.
2. New display text → add the key to **both** `shared/i18n/en.json` and `shared/i18n/pt-BR.json` (Portuguese only
   lives in `pt-BR.json`).
3. New monster family → add it to `families.json` and create `shared/art/families/<family>.txt`
   (`@idle`, `@attack`, `@hurt`; frames split by `%%`; max 32 × 10; no tabs). New boss → `shared/art/bosses/<id>.txt`.
4. A new data file needs a schema in `shared/schemas/<name>.schema.json` (the check fails otherwise), and an import in
   `rpg-typescript/src/infrastructure/embedded-shared.ts`. A new family/boss art file also needs an import line there.
5. Validate: `bun run check:shared` (root), then `cd rpg-python && uv run pytest` (cross-reference tests).
6. Balance: `uv run rpg --simulate 30` and compare the median rounds per vocation/difficulty before/after.
7. Content changes alter deterministic outcomes → follow the `golden-files` skill.

## Gotchas learned

- Monsters with 0% resistance to `physical` hard-lock the Warrior (all its damage is physical). Prefer ≥ 40.
- Bosses are the walls of the curve; check the simulator's "top killers" column after touching boss stats.
- Keep `monsters.json` ordering stable; the engine sorts by id where it matters, but diffs stay readable.
