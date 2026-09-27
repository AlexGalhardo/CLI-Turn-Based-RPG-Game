# Game Design — Rules and Formulas

This document is the **contract** shared by the three implementations (Python, TypeScript, Go). Numbers live in
[`shared/data/`](../shared/data) (see [data-format.md](data-format.md)); the **formulas and the order of operations**
live here. If an implementation disagrees with this document, the implementation is wrong. If this document is wrong,
fix it here first, then in the Python reference, regenerate the golden files and port the change.

All arithmetic is **integer**. `pct(v, p)` means `floor(v * p / 100)` with `v, p >= 0`. Every random number comes from
the shared PRNG described in [cross-language-parity.md](cross-language-parity.md), consumed in exactly the order written
below.

## 1. Game loop

```
Title ─► (first launch) Language ─► New run: Difficulty ─► Name ─► Vocation
  │                                                      │
  └─ Continue (save exists) ─────────────────────────────┴─► Merchant ◄──────┐
                                                               │ [0] Next fight│
                                                               ▼               │
                                                            Battle ── victory ─┘
                                                               │ defeat
                                                               ▼
                                                  Game Over (run saved to history)
```

- A **run** starts in the Merchant phase with `round = 0`, so the player can shop before fight 1 (the 2016 game did
  the same with its NPC round).
- Each **round** is exactly one fight. Leaving the merchant increments `round` and spawns the monster.
- The run is **auto-saved** every time the Merchant phase is entered and after every merchant action. Quitting in
  battle resumes from the last merchant save (the fight is lost, not the run).
- Death is permanent: the run is written to history, the save slot is cleared.

## 2. Difficulty

| Difficulty | Monster HP | Monster damage | Gold | XP | Non-common drop weight |
|---|---|---|---|---|---|
| EASY | 70% | 70% | 100% | 100% | 100% |
| NORMAL | 100% | 100% | 100% | 100% | 100% |
| HARD | 140% | 140% | 125% | 125% | 150% |

Values come from `balance.json → difficulties`.

## 3. Progression of monsters (infinite loop)

- Monsters are grouped in **tiers** (`monsters.json`, 10 tiers, ≥ 100 monsters named after [TibiaWiki](https://tibiawiki.com.br/)).
  Each tier has one **boss** (`bosses.json`).
- `roundsPerTier = 10`. For round `r >= 1`:
  - `tierIndex = ((r - 1) / roundsPerTier) % tierCount`
  - `cycle = (r - 1) / (roundsPerTier * tierCount)` — after the last tier the loop restarts, stronger ("NG+")
  - `position = (r - 1) % roundsPerTier` — `position == roundsPerTier - 1` is the **boss fight**
- Spawn: boss → `bosses[tierIndex]`. Otherwise `monster = tierMonsters[rng.roll(0, len - 1)]` (list sorted by `id`).
- Scaling percentages:
  - `cyclePct = 100 + cycle * balance.cycleStatPct` (HP and damage)
  - `cycleRewardPct = 100 + cycle * balance.cycleRewardPct` (XP and gold)
  - `positionPct = 100 + position * balance.positionPct` (0 for bosses, i.e. `100`)
  - `scaledHp = floor(baseHp * diff.hpPct * cyclePct * positionPct / 1_000_000)`, minimum 1
  - each attack `min/max` scaled the same way with `diff.damagePct` instead of `diff.hpPct`
  - `xp = floor(baseXp * diff.xpPct * cycleRewardPct / 10_000)`; gold min/max likewise with `diff.goldPct`

## 4. Character

Vocations: **Warrior, Archer, Mage** (`vocations.json`). Per level: Warrior +15 HP / +5 MP, Archer +10 HP / +15 MP,
Mage +5 HP / +15 MP.

- Start: level 1, magic level 1, `vocation.startHp/startMp`, `balance.startingGold`, `balance.startingPotions`, the
  vocation's `starterWeapon` equipped (common rarity, no affixes).
- **Experience** (Tibia formula): total XP needed to *reach* level `L` is
  `xpForLevel(L) = floor(50 * (L³ - 6L² + 17L - 12) / 3)`. After every victory, while `xp >= xpForLevel(level + 1)`:
  `level += 1`, `maxHp += hpPerLevel`, `maxMp += mpPerLevel`, `hp += hpPerLevel`, `mp += mpPerLevel`.
- **Magic level**: grows with mana spent. The cost of each magic level is `cost(1) = balance.magicLevel.base`,
  `cost(n + 1) = pct(cost(n), balance.magicLevel.growthPct)`; `manaForMagicLevel(n) = cost(1) + … + cost(n)` is the
  total mana spent needed to leave level `n`. After each spell cast, while
  `manaSpent >= manaForMagicLevel(magicLevel)`: `magicLevel += 1`.
- **Regeneration** at the end of every turn: `hp += hpRegen`, `mp += mpRegen` (capped at max).

### Derived stats

`stats = vocation base + Σ equipped items (base stats × rarityStatPct + affixes)`. Stat keys (`stats.json`):

| Key | Meaning | Cap |
|---|---|---|
| `attack` | added to both ends of the melee roll | — |
| `armor` | physical mitigation: `dmg = floor(dmg * 100 / (100 + armor))` | — |
| `maxHp`, `maxMp` | added to maximum HP/MP | — |
| `hpRegen`, `mpRegen` | per-turn regeneration | — |
| `critChance` | % chance of critical hit | 50 |
| `critDamage` | extra % on top of `balance.critMultiplierPct` | — |
| `spellPower` | % bonus to spell damage **and** healing | — |
| `physicalDamage` | % bonus to melee damage | — |
| `dodge` | % chance to avoid any monster attack | 40 |
| `parry` | % chance to block a **physical** monster attack | 40 |
| `lifeLeech`, `manaLeech` | % of damage dealt returned as HP/MP | 25 |
| `protPhysical`, `protFire`, `protIce`, `protEnergy`, `protEarth`, `protHoly`, `protDeath` | % damage reduction per element | 75 |

## 5. Elements

`physical, fire, ice, energy, earth, holy, death` (Tibia). Monsters have `resistances: { element: pct }` meaning
*damage taken %* (default 100; 150 = weak, 50 = strong, 0 = immune). The player's defence is `prot<Element>`.

## 6. Battle turn — order of operations

One **player command** resolves one turn. Invalid commands (not enough mana, no potion, unknown spell) emit an
`error` event and change nothing (no turn passes, no RNG consumed).

1. **Player action**
   - `attack` (melee):
     1. `base = rng.roll(voc.meleeMin + (level - 1) * voc.meleePerLevel + attack, voc.meleeMax + (level - 1) * voc.meleePerLevel + attack)`
     2. `dmg = pct(base, 100 + physicalDamage)`
     3. crit: `rng.chance(critChance)` → `dmg = pct(dmg, critMultiplierPct + critDamage)`
     4. element = weapon element (default `physical`); `dmg = pct(dmg, monster.resistance[element])`
     5. `dmg = max(1, dmg)` unless the resistance is 0 (immune → 0)
     6. leech: `hp += pct(dmg, lifeLeech)`, `mp += pct(dmg, manaLeech)` (capped)
   - `cast <spellId>` (attack spell): mana cost `pct(spell.mana, spellLevel.manaPct)` is paid first, then
     1. `base = rng.roll(spell.min + level * spell.perLevel + magicLevel * spell.perMagicLevel, spell.max + <same bonus>)`
     2. `dmg = pct(pct(base, spellLevel.effectPct), 100 + spellPower)`
     3. crit roll as melee; 4. resistance; 5. min 1 / immune; 6. leech
     7. if the spell is at level 3 and has `level3Bonus.status`: `rng.chance(level3Bonus.chance)` → apply status
   - `cast <spellId>` (healing spell): `heal = pct(pct(rng.roll(min', max'), effectPct), 100 + spellPower)` with the same
     level/magic-level bonus; capped at `maxHp`; no crit. Level 3 `cleanse` removes all negative statuses.
   - `potion <potionId>`: `rng.roll(potion.min, potion.max)` restored to HP or MP (capped). One potion per turn.
   - `defend`: incoming damage this turn × `balance.defendDamagePct` (50%).
   - After any successful cast: `spellUses[spellId] += 1`, `manaSpent += cost`, then spell-level and magic-level checks.
2. **Monster dead?** → victory (section 8).
3. **Monster phase**
   1. monster status ticks (DoT) → dead? → victory
   2. monster stunned → consume the stun, emit `monster_stunned`, skip to step 4
   3. boss pattern (bosses only): `pos = bossActions % (telegraphEvery + 1)`; `pos < telegraphEvery - 1` → normal attack,
      `pos == telegraphEvery - 1` → **telegraph** (announce, no damage), `pos == telegraphEvery` → **charged attack**
      (`chargeAttack`, damage × `bossChargeDamagePct`). `bossActions += 1` after each of these.
   4. normal attack: weighted pick `rng.roll(1, totalWeight)` over `attacks` (always rolled, even with one attack)
   5. `rng.chance(dodge)` → dodged, no further rolls
   6. physical attacks only: `rng.chance(parry)` → parried, no further rolls
   7. `raw = rng.roll(attack.min, attack.max)` (already scaled); charged → `pct(raw, bossChargeDamagePct)`
   8. physical → armor mitigation; then `pct(dmg, 100 - prot[element])`; defend → `pct(dmg, defendDamagePct)`; min 1
   9. attack status: `rng.chance(status.chance)` → apply to player
4. **Player dead?** → defeat.
5. **End of turn**: player status ticks (DoT) → dead? → defeat; regeneration; defend flag cleared; stun cooldowns
   decrement; `turn += 1`.
6. **Player stunned?** → consume the stun, emit `player_stunned`, go back to step 3 (the monster acts again).

## 7. Status effects (`statuses.json`)

| Status | Element | Turns | Effect |
|---|---|---|---|
| burn | fire | 3 | DoT |
| poison | earth | 5 | DoT |
| electrify | energy | 3 | DoT |
| bleed | physical | 4 | DoT |
| freeze | ice | 2 | DoT |
| curse | death | 4 | DoT |
| stun | — | 1 | skips the next action |

- DoT per turn = `max(1, pct(damageThatAppliedIt, source.damagePct))`; on the player reduced by `prot[element]`, on
  monsters by `resistance[element]`. Re-applying refreshes `turns` and keeps the higher per-turn value.
- Stun can't be applied while the target is stunned or its `stunCooldown > 0`; consuming a stun sets
  `stunCooldown = 2` (cooldowns decrement at the end of every turn), so a target is never stunned two turns in a row.
- A status whose element the target is immune to (resistance 0) is not applied (the chance is still rolled).
- Spell-applied statuses use `balance.spellStatusDamagePct` as `damagePct`.
- After a victory the player's statuses, stun cooldown and defend flag are cleared.

## 8. Victory, loot and drops

In this order:
1. XP (scaled) → level-up loop.
2. Gold `rng.roll(goldMin, goldMax)` (scaled).
3. Drops: normal monster `rng.chance(balance.dropChancePct)` → 1 item; boss → `balance.bossDrops` items with the boss
   rarity table.
4. Bestiary/statistics updates. Phase → Merchant.

**Item generation** (`items.json`, `affixes.json`, names from TibiaWiki, stats specific to this game):

1. Candidates: items usable by the player's vocation with `tier ∈ [tierIndex - 1, tierIndex]` (clamped ≥ 0), sorted by
   `id` → `rng.roll(0, len - 1)`.
2. Rarity: weighted roll over `common, rare, epic, legendary` (monster, boss or merchant table; HARD multiplies the
   non-common weights by 150%).
3. Affix count `rng.roll(rarityAffixes.min, rarityAffixes.max)` (common 0, rare 1–2, epic 3, legendary 4).
4. For each affix: candidates = affixes allowed for the slot whose stat is not on the item yet, sorted by `id` → pick →
   value `rng.roll(affix.min, affix.max) + tierIndex * affix.perTier`.
5. Base stats × `rarityStatPct` (100/115/130/150). Value (sell price) = `pct(item.value, rarityValuePct)`.
6. Instance id: `run.nextItemUid++` (deterministic). A full bag (20) auto-sells the new item.

## 9. Spells

Three vocations, Tibia spell names and incantations (`spells.json`): three attack spells (light/medium/strong, like
2016) plus healing. Levels by number of uses:

| Level | Uses | Effect | Mana cost | Extra |
|---|---|---|---|---|
| 1 | 0 | 100% | 100% | — |
| 2 | 20 | 125% | 90% | — |
| 3 | 50 | 150% | 80% | `level3Bonus`: status chance (attack) or `cleanse` (heal) |

## 10. Merchant

Entered after every victory and at the start of a run. Options: buy potions (unlocked by round, Tibia names: Health →
Strong → Great → Ultimate → Supreme Health Potion; Mana → Strong → Great → Ultimate Mana Potion), sell bag items (sell
price = value), equip/unequip, buy from a 3-item rotating stock (merchant rarity table, price = value ×
`balance.merchantMarkupPct`), character sheet, next fight, save & quit. Stock is generated with the RNG when the
merchant is entered.

## 11. Run statistics, profile and achievements

- Every run records: start/end timestamps, play time, seed, implementation, version, difficulty, vocation, final
  round/level, cause of death, damage dealt/taken, healing, crits, dodges, parries, defends, attacks, casts per spell,
  potions bought/used per type, gold looted/spent/earned, items dropped by rarity (with names), items sold, kills per
  monster, bosses killed, statuses applied, highest hit. Finished runs go to history (see [persistence.md](persistence.md)).
- The **profile** (cross-run) keeps the bestiary (kills per monster; weaknesses revealed after 5 kills), achievements
  (`achievements.json`) and the Hall of Fame (top 10 by round, then level, then earliest end).
- Achievements and timestamps are **not** part of the deterministic engine; they are computed by the application layer
  from engine events.

## 12. Balance targets (simulator)

`uv run rpg --simulate 30` (greedy bot, seeds 1–30). Target: NORMAL median between rounds 30 and 60, vocations within
±20% of each other. Snapshot for v0.4.0:

| Vocation | EASY median | NORMAL median | HARD median |
|---|---|---|---|
| Warrior | 200 | 50 | 39 |
| Archer | 195 | 40 | 30 |
| Mage | 90 | 44 | 29 |

The bot is a floor, not a ceiling: a human who defends on boss telegraphs and plans potions goes further. Re-run the
simulator after any balance change and update this table in the same commit.
