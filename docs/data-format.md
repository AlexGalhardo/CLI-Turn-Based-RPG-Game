# Shared Data Format

Everything tunable lives in [`shared/`](../shared). The three implementations load the same files; the Go and
TypeScript binaries embed them at build time. Every file under `shared/data/` has a JSON Schema in `shared/schemas/`
and CI validates them (`bun run check:shared` at the repository root).

## Conventions

- Ids are `snake_case` ASCII strings and never change once released (saves reference them).
- Display names come from [TibiaWiki](https://tibiawiki.com.br/) (monsters, bosses, items, spells, potions). Stats
  are **original to this game**. Names are the same in every language; descriptions and UI text go through i18n.
- Percentages are integers (`25` = 25%). All numbers are non-negative integers.
- Lists keep file order. Where a rule needs ordering, the engine sorts by `id`.

## Files

| File | Content |
|---|---|
| `balance.json` | Global knobs: difficulties, tiers, scaling, crit, defend, caps, drop chances, rarity tables, spell levels, magic level curve, merchant, starting kit |
| `vocations.json` | Warrior, Archer, Mage: start HP/MP, per-level gains, regen, melee range, spells, starter weapon, equippable weapon types |
| `spells.json` | Attack and healing spells: vocation, incantation, element, mana, min/max, perLevel, perMagicLevel, level3Bonus |
| `monsters.json` | ≥ 100 monsters: tier, family (art), HP, XP, gold range, attacks (element, min/max, weight, status), resistances |
| `bosses.json` | One boss per tier: same shape as a monster + `chargeAttack` |
| `items.json` | Equipment bases: slot, tier, vocations, weapon element, base stats, value |
| `affixes.json` | Random affixes: stat, min/max, perTier, allowed slots |
| `potions.json` | Health/mana potions: resource, min/max, price, unlockRound |
| `statuses.json` | burn, poison, electrify, bleed, freeze, curse, stun |
| `achievements.json` | Achievement ids with condition `type` and `value` |
| `families.json` | Monster families → art file names |

### Example — monster

```json
{
	"id": "dragon",
	"name": "Dragon",
	"tier": 5,
	"family": "dragon",
	"hp": 1000,
	"xp": 700,
	"gold": { "min": 40, "max": 120 },
	"attacks": [
		{ "id": "bite", "element": "physical", "min": 30, "max": 80, "weight": 3 },
		{ "id": "fire_wave", "element": "fire", "min": 60, "max": 140, "weight": 2, "status": { "id": "burn", "chance": 25, "damagePct": 20 } }
	],
	"resistances": { "fire": 0, "ice": 110, "earth": 80 }
}
```

## i18n (`shared/i18n/<locale>.json`)

Flat key → template map. Placeholders use `{name}`. English (`en`) is the default and the fallback for missing keys;
`pt-BR` is the only other locale. A CI check fails if the two files don't have the same key set.

```json
{ "event.player_attacked": "You hit for {damage} {element} damage.", "menu.new_run": "New run" }
```

Event text keys are `event.<type>` plus an optional variant suffix chosen by the presentation: `_crit` (crit),
`_charged` (boss charge), `_boss` (boss round) or `_player`/`_monster` (events with a `target`). Id fields
(`spellId`, `potionId`, `monsterId`, `itemId`) are replaced by display names, and `element`, `status`, `rarity`,
`resource` by their translated labels.

## Art (`shared/art/`)

- `shared/art/families/<family>.txt` and `shared/art/bosses/<bossId>.txt`.
- A file holds **named animations**, each with one or more frames:

```
@idle
  (o.o)
  <| |>
%%
  (-.-)
  <| |>
@attack
  (>.<)
 <|=|>--
@hurt
  (x.x)
  <| |>
```

- `@name` starts an animation, `%%` separates frames. Max 32 columns × 10 rows, ASCII/Unicode box characters only,
  no tabs, no ANSI codes (colour comes from the element/family in the renderer).

## Adding content

1. Add the entry to the JSON file (keep `id` unique, follow the schema).
2. If it needs text, add keys to **both** `en.json` and `pt-BR.json`.
3. Monsters of a new family also need `shared/art/families/<family>.txt`.
4. Run `bun run check:shared`, then each implementation's tests. Balance changes: run the simulator
   (`uv run rpg --simulate 2000`) and note the result in the CHANGELOG entry.
5. If the change alters deterministic outcomes, regenerate the golden files in the same commit.
