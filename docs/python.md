# Python Implementation (`rpg-python/`) — Reference

Phase 1. The Python version is the **reference implementation**: rules are implemented here first and the golden files
in `shared/golden/` are generated from it.

## Stack

- Python 3.14 (managed by [uv](https://docs.astral.sh/uv/), `requires-python = ">=3.14"`)
- [Textual](https://textual.textualize.io/) for the TUI
- Ruff (format + lint, tabs), mypy (strict), pytest + pytest-cov + pytest-asyncio

## Commands

```bash
cd rpg-python
uv sync                       # create .venv and install deps
uv run rpg                    # play
uv run rpg --seed 42 --no-anim
uv run rpg --simulate 2000    # balance report
uv run rpg-golden             # regenerate shared/golden (only for intended rule changes)
uv run ruff format && uv run ruff check && uv run mypy && uv run pytest
```

## Structure

```
rpg-python/
├── pyproject.toml
├── src/rpg/
│   ├── __main__.py            # CLI entry (argparse) → presentation or simulator
│   ├── domain/                # rng, enums, formulas, models, player, monster, items, statuses
│   ├── application/           # engine, commands, events, battle, merchant, loot, progression, stats, profile, bot, simulator
│   ├── infrastructure/        # data loader, repositories, i18n, art parser, paths, clock
│   └── presentation/          # Textual app, screens, widgets
└── tests/{unit,integration,golden,e2e}/
```

## Notes

- Shared files are found through `RPG_SHARED_DIR` or by walking up from the package to the repository `shared/`.
- Domain models are frozen `dataclass(slots=True)` for definitions and mutable dataclasses for run state; everything
  is serialised with explicit `to_dict`/`from_dict` (no pickle) because the save format is shared with TS and Go.
- Python has no executable build (by design); it runs through `uv run`.
