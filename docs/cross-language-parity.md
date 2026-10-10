# Cross-Language Parity

The seven implementations must produce the **same game**: same seed + same commands ⇒ same events, byte for byte
after JSON normalisation. This is what makes the project a fair side-by-side comparison of Python, TypeScript, Go,
Rust, Elixir and C++, and it is enforced by the golden tests.

## 1. Rules that make parity possible

1. **One source of numbers**: everything tunable lives in `shared/data/*.json`. No magic numbers in code.
2. **Integer math only** in the engine. Percentages use `pct(v, p) = floor(v * p / 100)` with non-negative operands.
   - Python: `v * p // 100` · TypeScript: `Math.floor((v * p) / 100)` · Go: `v * p / 100` on `int` (64-bit)
   - Rust: `v * p / 100` on `i64` · Elixir: `div(v * p, 100)` · C++: `v * p / 100` on `std::int64_t` (`pct()`
     rejects negative operands, since C++ division truncates towards zero)
   - Never use floats, `round()` or language-specific division of negative numbers.
3. **One PRNG** (below), never the language's own random generator (`random`, `Math.random`, `math/rand`, the `rand`
   crate, `:rand`, `<random>`) in the engine.
4. **Deterministic iteration**: lists from JSON keep file order; whenever a rule says "sorted by id", sort by the
   string `id` with a plain code-point comparison. Never iterate over a hash map to make a game decision.
5. **Engine is pure**: no clock, no I/O, no global state. Timestamps and file access live outside the engine.

## 2. PRNG — mulberry32

State: one unsigned 32-bit integer. `seed` is taken as `seed mod 2³²`.

```
next():
    state = (state + 0x6D2B79F5) mod 2³²
    t = state
    t = imul(t XOR (t >>> 15), t OR 1)
    t = t XOR (t + imul(t XOR (t >>> 7), t OR 61))
    return (t XOR (t >>> 14)) as unsigned 32-bit
```

`imul` is 32-bit wrapping multiplication (`Math.imul` in JS; `uint32` multiply in Go; `(a * b) & 0xFFFFFFFF` in
Python; `u32::wrapping_mul` / `wrapping_add` in Rust; `Bitwise` operators with every intermediate masked by
`band(…, 0xFFFFFFFF)` in Elixir; `std::uint32_t` arithmetic, which wraps, in C++). All intermediate values are masked
to 32 bits.

Derived helpers (the only ones the engine may use):

| Helper | Definition |
|---|---|
| `roll(min, max)` | `min + next() % (max - min + 1)`; requires `min <= max` |
| `chance(pct)` | `pct <= 0` → `false` **without consuming**; `pct >= 100` → `true` **without consuming**; else `roll(1, 100) <= pct` |
| `weighted(weights)` | `r = roll(1, sum)`; first index whose cumulative weight `>= r` |
| `pick(list)` | `list[roll(0, len - 1)]` |

Reference vectors (`shared/golden/prng.json`): `seed = 42` → first outputs must match in all implementations.

The PRNG state is part of the save file, so a continued run keeps its determinism.

## 3. Events

The engine returns a list of events for each command. An event is a flat JSON object with a `type` and fields in
`camelCase`. The presentation layer turns events into text through i18n keys `event.<type>`.

| Type | Fields |
|---|---|
| `run_started` | `seed`, `vocation`, `difficulty` |
| `round_started` | `round`, `tier`, `cycle`, `monsterId`, `isBoss`, `enemyClass` (`normal`/`elite`/`boss`), `hp` |
| `player_attacked` | `damage`, `crit`, `element` |
| `spell_cast` | `spellId`, `damage`, `crit`, `element`, `mana` |
| `spell_healed` | `spellId`, `amount`, `mana` |
| `potion_used` | `potionId`, `amount`, `resource` (`hp`/`mp`) |
| `player_defended` | — |
| `leeched` | `hp`, `mp` |
| `monster_attacked` | `attackId`, `damage`, `element`, `charged`, `crit` |
| `monster_dodged` | — (the player's attack or spell missed) |
| `monster_parried` | `reflected` (damage taken by the player) |
| `monster_healed` | `amount` |
| `attack_dodged` | `attackId` |
| `attack_parried` | `attackId`, `reflected` (damage taken by the monster) |
| `boss_telegraph` | `attackId`, `element` |
| `status_applied` | `target` (`player`/`monster`), `status`, `turns`, `perTurn` |
| `status_ticked` | `target`, `status`, `damage` |
| `status_expired` | `target`, `status` |
| `player_stunned` / `monster_stunned` | — |
| `regenerated` | `hp`, `mp` |
| `monster_killed` | `monsterId`, `isBoss`, `enemyClass` |
| `xp_gained` | `amount`, `total` |
| `level_up` | `level`, `maxHp`, `maxMp` |
| `magic_level_up` | `magicLevel` |
| `spell_level_up` | `spellId`, `level` |
| `gold_looted` | `amount` |
| `item_dropped` | `uid`, `itemId`, `rarity` |
| `item_auto_sold` | `uid`, `itemId`, `gold` (a drop sold because the bag is full, or the item replaced by auto-equip) |
| `item_auto_equipped` | `uid`, `itemId`, `slot`, `score` |
| `potion_dropped` | `potionId` |
| `run_won` | `round` (last event of the final boss victory; the phase becomes `victory`) |
| `run_ended` | `won` (emitted by `end_run`, always `true` today) |
| `merchant_entered` | `round` |
| `potion_bought` | `potionId`, `quantity`, `gold` |
| `item_bought` / `item_sold` | `uid`, `itemId`, `gold` |
| `item_equipped` / `item_unequipped` | `uid`, `itemId`, `slot` |
| `player_died` | `monsterId`, `round` |
| `error` | `code` (`not_enough_mana`, `not_enough_gold`, `no_potion`, `unknown_spell`, `unknown_potion`, `potion_locked`, `invalid_phase`, `invalid_quantity`, `bag_full`, `cannot_equip`, `invalid_item`, `level_too_low`) |

## 4. Golden tests

`shared/golden/*.json` are generated by the **Python reference** (`uv run rpg-golden`) and committed:

```json
{
	"name": "knight-normal-seed-42",
	"seed": 42,
	"config": { "name": "Alex", "vocation": "knight", "difficulty": "normal", "autoEquip": false },
	"commands": [{ "type": "next_fight" }, { "type": "attack" }, { "type": "cast", "spellId": "brutal_strike" }],
	"events": [[{ "type": "run_started" }], [{ "type": "merchant_entered", "round": 0 }]],
	"finalState": { "round": 1, "hp": 700, "gold": 100 }
}
```

Commands (`type` + fields): `attack`, `cast` (`spellId`), `potion` (`potionId`), `defend`, `next_fight`, `buy_potion`
(`potionId`, `quantity`), `sell_item` (`uid`), `equip` (`uid`), `unequip` (`slot`), `buy_stock_item` (`index`), and in the
`victory` phase `end_run` and `continue_run`. The run config carries `autoEquip` (boolean).

`events[i]` is the list returned by command `i` (index 0 is run creation). Each implementation loads every golden
file, replays the commands and compares events and the final-state summary structurally. Golden files are
regenerated **only** when a rule change is intended, in the same commit as the rule change (`test(shared): regenerate
golden files`).

The bot-driven "full run" golden (`bot-full-run-*.json`) plays whole runs with the simulator bot until the run ends
(death, or `end_run` after beating the final boss) and compares the final summary; it is the end-to-end parity check.
`bot-victory-continue-archer-easy.json` (auto-equip on) wins the run, sends invalid commands in the `victory` phase,
continues with `continue_run` and stops at the merchant three rounds later; `mage-spells.json` starts a new fight
whenever the scripted spells kill the monster.
