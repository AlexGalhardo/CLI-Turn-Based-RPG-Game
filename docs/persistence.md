# Persistence — Saves, History and Profile

All three implementations read and write the **same files with the same format**, so a run started in Python can be
continued in Go or TypeScript.

## Location

1. `--data-dir <path>` flag, else
2. `RPG_DATA_DIR` environment variable, else
3. `~/.cli-turn-based-rpg/` (`%USERPROFILE%\.cli-turn-based-rpg\` on Windows)

Tests always point `RPG_DATA_DIR` at a temporary directory.

```
~/.cli-turn-based-rpg/
├── settings.json          # { "schemaVersion": 1, "locale": "en" | "pt-BR" }
├── save.json              # the single active run (absent when there is none)
├── profile.json           # bestiary, achievements, hall of fame
└── history/
    └── <runId>.json       # one file per finished run, full statistics
```

## save.json

```json
{
	"schemaVersion": 1,
	"gameVersion": "0.2.0",
	"implementation": "python",
	"savedAt": "2026-09-27T21:04:11Z",
	"rngState": 2891336453,
	"run": { "id": "…", "seed": 42, "difficulty": "normal", "round": 12, "phase": "merchant", "player": { }, "merchantStock": [ ], "stats": { } }
}
```

- Written when entering the merchant and after every merchant action (auto-save), and on "Save & quit".
- Written atomically: write `save.json.tmp`, then rename.
- `run.stats.startedAt` is kept across sessions; `run.stats.sessions` counts how many times the run was resumed and
  `playTimeSeconds` accumulates time played (so a run can be spread over several days).
- On death the run is moved to `history/<runId>.json` (with `endedAt` and cause of death) and `save.json` is deleted.

## Run id

`<startedAt as yyyyMMddTHHmmssZ>-<seed>` — readable, sortable and unique enough for a single local player.

## profile.json

```json
{
	"schemaVersion": 1,
	"bestiary": { "dragon": { "kills": 12, "firstKilledAt": "…" } },
	"achievements": { "boss_slayer": { "unlockedAt": "…", "runId": "…" } },
	"hallOfFame": [{ "runId": "…", "name": "Alex", "vocation": "knight", "difficulty": "hard", "round": 57, "level": 41, "endedAt": "…" }]
}
```

## Versioning

Every file carries `schemaVersion`. A loader that finds a newer version refuses to overwrite it and tells the player
to update the game. Older versions are migrated in code (`infrastructure/migrations`). Changing a file format is a
**MINOR** bump when migrations keep old files readable, **MAJOR** otherwise.
