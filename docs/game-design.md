# Game Design — Rules and Formulas

This document is the **contract** shared by the six implementations (Python, TypeScript, Go, Rust, Elixir, C++). Numbers
live in [`shared/data/`](../shared/data) (see [data-format.md](data-format.md)); the **formulas and the order of
operations** live here. If an implementation disagrees with this document, the implementation is wrong. If this document
is wrong, fix it here first, then in the Python reference, regenerate the golden files and port the change.

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
                                                               │ defeat     │ final boss (round 100) beaten
                                                               ▼            ▼
                                                  Game Over (run saved   Victory ── [1] End run ─► Game Over (won)
                                                  to history)               └──── [2] Continue (endless, NG+) ─► Merchant
```

- The run is **won** by beating the final boss, Ferumbras, in round `balance.finalRound` (100, the last boss of the
  first cycle). The engine enters the `victory` phase: `end_run` closes the run as a win (history + Hall of Fame),
  `continue_run` goes on endlessly (the cycle loop of section 3); the run keeps `won = true` until it ends.

- A **run** starts in the Merchant phase with `round = 0`, so the player can shop before fight 1 (the 2016 game did
  the same with its NPC round).
- Each **round** is exactly one fight. Leaving the merchant increments `round` and spawns the monster.
- The run is **auto-saved** every time the Merchant phase is entered and after every merchant action. Quitting in
  battle resumes from the last merchant save (the fight is lost, not the run).
- Death is permanent: the run is written to history, the save slot is cleared.

## 2. Difficulty

| Difficulty | Monster HP | Monster damage | Gold | XP |
|---|---|---|---|---|
| EASY | `hpPct` | `damagePct` | `goldPct` | `xpPct` |

Values come from `balance.json → difficulties` and are tuned by the balance gate (section 12): the targets are the
win rates, not the multipliers. Drop rarities do not depend on the difficulty.

## 3. Progression of monsters (100 rounds to win, then endless)

- Monsters are grouped in **tiers** (`monsters.json`, 10 tiers, ≥ 100 monsters named after [TibiaWiki](https://tibiawiki.com.br/)).
  Each tier has one **boss** (`bosses.json`).
- `roundsPerTier = 10`. For round `r >= 1`:
  - `tierIndex = ((r - 1) / roundsPerTier) % tierCount`
  - `cycle = (r - 1) / (roundsPerTier * tierCount)` — after the last tier the loop restarts, stronger ("NG+")
  - `position = (r - 1) % roundsPerTier` — `position == roundsPerTier - 1` is the **boss fight**
- Spawn: boss → `bosses[tierIndex]`. Otherwise `monster = tierMonsters[rng.roll(0, len - 1)]` (list sorted by `id`),
  then `elite = rng.chance(balance.eliteChancePct)` (20; bosses roll nothing). Every monster has an **enemy class**:
  `boss`, `elite` or `normal`, whose row in `balance.enemyClasses.<class>` holds its multipliers (`statPct`,
  `rewardPct`), combat chances (`dodge`, `parry`, `crit`, `heal`) and drop table (`dropChancePct`, `drops`,
  `potionDropPct`, `rarityWeights`):

  | Class | `statPct` (HP, damage) | `rewardPct` (XP, gold) | dodge / parry / crit / heal | drops (`dropChancePct` → `drops` items; potion) |
  |---|---|---|---|---|
  | normal | 100 | 100 | 10% each | 30% → 1 item: common 90 / rare 10; no potion |
  | elite | 300 | 300 | 20% each | 100% → 1 item: rare 80 / legendary 20; potion 33% |
  | boss | 100 | 100 | 30% each | 100% → 2 items: legendary 80 / mythic 20; no potion |

- Scaling percentages:
  - `cyclePct = 100 + cycle * balance.cycleStatPct` (HP and damage)
  - `cycleRewardPct = 100 + cycle * balance.cycleRewardPct` (XP and gold)
  - `positionPct = 100 + position * balance.positionPct` (0 for bosses, i.e. `100`). `balance.positionPct` (8) is
    sized so the last normal fight of a tier (+64%) is close to the first fight of the next tier: every round is a
    little tougher than the previous one instead of nine flat rounds and a jump
  - `scaledHp = floor(floor(baseHp * diff.hpPct * cyclePct * positionPct / 1_000_000) * class.statPct / 100)`,
    minimum 1
  - each attack `min/max` scaled the same way with `diff.damagePct` instead of `diff.hpPct`
  - `xp = floor(floor(baseXp * diff.xpPct * cycleRewardPct / 10_000) * class.rewardPct / 100)`; gold min/max likewise
    with `diff.goldPct`

## 4. Character

Vocations: **Warrior, Archer, Mage** (`vocations.json`). Per level: Warrior +15 HP / +5 MP, Archer +10 HP / +15 MP,
Mage +5 HP / +15 MP.

- Start: level 1, magic level 1, `vocation.startHp/startMp`, `balance.startingGold`, `balance.startingPotions`, the
  vocation's `starterWeapon` equipped (common rarity, no affixes).
- Run options chosen at creation (`RunConfig`, defaults from the settings): `autoEquip` (section 8.1).
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
     1. `rng.chance(monster.dodge)` → `monster_dodged`, no damage, no further rolls
     2. `base = rng.roll(voc.meleeMin + (level - 1) * voc.meleePerLevel + attack, voc.meleeMax + (level - 1) * voc.meleePerLevel + attack)`
     3. `dmg = pct(base, 100 + physicalDamage)`
     4. crit: `rng.chance(critChance)` → `dmg = pct(dmg, critMultiplierPct + critDamage)`
     5. element = weapon element (default `physical`); `dmg = pct(dmg, monster.resistance[element])`
     6. `dmg = max(1, dmg)` unless the resistance is 0 (immune → 0)
     7. physical only: `rng.chance(monster.parry)` → **parry**: the monster takes nothing and the player takes
        `reflected = max(1, pct(dmg, balance.parryReflectPct))` (20; no mitigation), `monster_parried`, no leech
        (rolled even when `dmg` is 0 against an immune monster, so the reflect is then 1)
     8. `player_attacked`, then leech: `hp += pct(dmg, lifeLeech)`, `mp += pct(dmg, manaLeech)` (capped)

     A dodged or parried action emits only `monster_dodged` / `monster_parried` (no `player_attacked` /
     `spell_cast`) and ends there: no leech, no level-3 status roll.
   - `cast <spellId>` (attack spell): mana cost `pct(spell.mana, spellLevel.manaPct)` is paid first, then
     1. monster dodge as melee (the mana is spent and the use counts: the after-cast step below still runs)
     2. `base = rng.roll(spell.min + level * spell.perLevel + magicLevel * spell.perMagicLevel, spell.max + <same bonus>)`
     3. `dmg = pct(pct(base, spellLevel.effectPct), 100 + spellPower)`
     4. crit roll as melee
     5. resistance
     6. min 1 / immune
     7. parry as melee when the spell element is `physical`
     8. leech
     9. if the spell is at level 3 and has `level3Bonus.status`: `rng.chance(level3Bonus.chance)` → apply status
   - `cast <spellId>` (healing spell): `heal = pct(pct(rng.roll(min', max'), effectPct), 100 + spellPower)` with the same
     level/magic-level bonus; capped at `maxHp`; no crit, no monster dodge. Level 3 `cleanse` removes all negative
     statuses.
   - `potion <potionId>`: `rng.roll(potion.min, potion.max)` restored to HP or MP (capped). One potion per turn.
   - `defend`: incoming damage this turn × `balance.defendDamagePct` (50%).
   - After any successful cast (dodged and parried casts included): `spellUses[spellId] += 1`, `manaSpent += cost`,
     then spell-level and magic-level checks.
2. **Player dead?** (a parried hit can kill) → defeat. **Monster dead?** → victory (section 8). The player is always
   checked first.
3. **Monster phase**
   1. monster status ticks (DoT) → dead? → victory
   2. monster stunned → consume the stun, emit `monster_stunned`, skip to step 4
   3. heal: only when `hp < maxHp` (no roll at full HP), `rng.chance(class.heal)` →
      `amount = min(pct(maxHp, balance.monsterHealPct), maxHp - hp)` (20), `hp += amount`, `monster_healed { amount }`;
      the monster does nothing else this turn (a boss does not advance its pattern); skip to step 4
   4. boss pattern (bosses only): `pos = bossActions % (telegraphEvery + 1)`; `pos < telegraphEvery - 1` → normal attack,
      `pos == telegraphEvery - 1` → **telegraph** (announce, no damage), `pos == telegraphEvery` → **charged attack**
      (`chargeAttack`, damage × `bossChargeDamagePct`). `bossActions += 1` after each of these.
   5. normal attack: weighted pick `rng.roll(1, totalWeight)` over `attacks` (always rolled, even with one attack)
   6. `rng.chance(dodge)` → dodged, no further rolls
   7. `raw = rng.roll(attack.min, attack.max)` (already scaled); charged → `pct(raw, bossChargeDamagePct)`
   8. physical attacks only: `rng.chance(parry)` → **parry**: the player takes nothing and the monster takes
      `max(1, pct(raw, balance.parryReflectPct))` (`raw` after the charge multiplier; no resistance),
      `attack_parried { attackId, reflected }`; no further rolls (the reflected damage can kill the monster → victory
      after the turn)
   9. monster crit: `rng.chance(class.crit)` → `raw = pct(raw, critMultiplierPct)` (the player's `critDamage` does not
      apply); `monster_attacked.crit` reports it
   10. physical → armor mitigation; then `pct(dmg, 100 - prot[element])`; defend → `pct(dmg, defendDamagePct)`; min 1
   11. attack status: `rng.chance(status.chance)` → apply to player
4. **Player dead?** → defeat. **Monster dead?** (a reflected parry) → victory.
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

1. `monster_killed { monsterId, isBoss, enemyClass }`, XP (scaled) → level-up loop.
2. Gold `rng.roll(goldMin, goldMax)` (scaled).
3. Drops, one rule for the three classes, from the enemy class row (section 3) — `chance(100)` and `chance(0)` consume
   nothing, so a normal monster rolls its drop chance, an elite or a boss doesn't:
   - `rng.chance(class.dropChancePct)` → `class.drops` items generated with `class.rarityWeights` (normal: 30% → 1
     item; elite: 1 item; boss: 2 items)
   - then `rng.chance(class.potionDropPct)` (elite 33, others 0) → one potion picked with `rng.roll(0, len - 1)` among
     the potions unlocked at this round (`unlockRound <= round`, file order), `+1` in the inventory, `potion_dropped`.
     With no unlocked potion nothing drops and nothing more is rolled.
4. Auto-equip (section 8.1) when the run has `autoEquip`.
5. Statuses, stun cooldown and defend flag cleared; HP/MP capped at the (possibly new) maximum.
6. Phase → Merchant (`merchant_entered`, stock generated), or → `victory` when this was the final boss of the first
   cycle (`round == balance.finalRound`): `won = true`, `run_won { round }` is the last event and the merchant is not
   entered (no stock is generated, no RNG consumed). Bestiary and statistics are updated from the events.

**Item generation** (`items.json`, `affixes.json`, names from TibiaWiki, stats specific to this game):

1. Candidates: items usable by the player's vocation with `tier ∈ [tierIndex - 1, tierIndex]` (clamped ≥ 0), sorted by
   `id` → `rng.roll(0, len - 1)`.
2. Rarity: weighted roll over the table's rarities in the order `common, rare, legendary, mythic` (zero or missing
   weights are skipped, no roll when a single rarity has weight).
3. Affix count `rng.roll(rarity.affixMin, rarity.affixMax)`: common 0, rare 1, legendary 2, mythic 2.
4. For each affix: candidates = affixes allowed for the slot whose stat is not on the item yet, sorted by `id` → pick →
   value `rng.roll(affix.min, affix.max) + tierIndex * affix.perTier`.
5. Base stats × `rarity.statPct`: common 100, rare 150, legendary 200, mythic 300 (a Sword with attack 10 is 15 rare,
   20 legendary, 30 mythic). Value (sell price) = `pct(item.value, rarity.valuePct)`.
6. Instance id: `run.nextItemUid++` (deterministic). A full bag (20) auto-sells the new item.

**Level requirement**: `requiredLevel = 1 + item.tier * balance.itemLevelPerTier` (4), where `item.tier` is the
**instance** tier saved with the item (the round tier it was generated for, step 4 above), not the base item's tier.
Equipping an item above the player's level fails with `error: level_too_low` (checked after `invalid_item` and
`cannot_equip`).

**Item score** (like Diablo's item power, shown everywhere an item is listed):
`score = Σ value(stat) * balance.itemScoreWeights[stat]` over the item's final stats (base × rarity, plus affixes;
every stat key has a weight). The equipment screen header shows the sum over the equipped items.
Higher rarities have more and bigger stats, so they score higher; within a rarity, a higher tier scores higher.

### 8.1 Auto-equip

With `config.autoEquip`, after the drops of every victory and after buying a merchant item, for each slot in the order
`weapon, shield, helmet, armor, legs, boots, ring, amulet`: among the bag items usable by the vocation, of that slot and
with `requiredLevel <= level`, take the highest score (ties: lowest `uid`). If it beats the equipped item's score (or
the slot is empty) it is equipped (`item_auto_equipped { uid, itemId, slot, score }`) and the item it replaces is sold
at its value right after (`item_auto_sold`). HP/MP are then capped at the new maximum. No RNG is consumed. Merchant
purchases emit `item_bought` first, then the auto-equip events.

## 9. Spells

Three vocations, Tibia spell names and incantations (`spells.json`): three attack spells (light/medium/strong, like
2016) plus healing. Every spell starts at level 1 and levels up by number of uses (`balance.spellLevels`), so using
spells pays off:

| Level | Uses | Effect (damage or healing) | Mana cost | Extra |
|---|---|---|---|---|
| 1 | 0 | 100% | 100% | — |
| 2 | 20 | 150% | 90% | — |
| 3 | 50 | 200% | 80% | `level3Bonus`: status chance (attack) or `cleanse` (heal) |

## 10. Merchant

Entered after every victory and at the start of a run. Options: buy potions (unlocked by round, Tibia names: Health →
Strong → Great → Ultimate → Supreme Health Potion; Mana → Strong → Great → Ultimate Mana Potion), sell bag items (sell
price = value; equipped items are never in the bag, so they can't be sold — `SellItem` with an equipped `uid` is
`error: invalid_item`), equipment, buy from a 3-item rotating stock (`merchant` rarity table, price = value ×
`balance.merchantMarkupPct`; triggers auto-equip), character sheet, next fight, save & quit. Stock is generated with the
RNG when the merchant is entered.

In the `victory` phase only `end_run` and `continue_run` are valid (`error: invalid_phase` otherwise, and both are
`invalid_phase` in any other phase). `end_run` consumes no RNG: phase → `game_over`, no cause of death,
`run_ended { won: true }`. `continue_run` enters the merchant like a normal victory (`merchant_entered`), so the only
RNG it consumes is the stock generation of that merchant visit; the run stays `won`.

## 11. Run statistics, profile and achievements

- Every run records: start/end timestamps, play time, seed, implementation, version, difficulty, vocation, final
  round/level, whether it was **won**, cause of death (none for a won run ended with `end_run`), damage dealt/taken,
  healing, crits, dodges, parries, defends, attacks, casts per spell, potions bought/used/dropped per type, gold
  looted/spent/earned, items dropped by rarity (with names), items sold and auto-equipped, kills per monster, elites
  and bosses killed, statuses applied, highest hit. Finished runs go to history (see [persistence.md](persistence.md)).
  Counters come from events: `monster_parried.reflected` counts as damage taken, `attack_parried.reflected` as damage
  dealt; `item_auto_sold` counts as an item sold; `monster_killed.enemyClass == "elite"` counts an elite kill.
  Dodged/parried player actions are not attacks or casts in the statistics.
- The **profile** (cross-run) keeps the bestiary (kills per monster; weaknesses revealed after 5 kills), achievements
  (`achievements.json`; type `run_won` unlocks when the final boss is beaten) and the Hall of Fame (top 10: won runs
  first, then by round, then level, then earliest end).
- Achievements and timestamps are **not** part of the deterministic engine; they are computed by the application layer
  from engine events.

## 12. Balance targets (simulator and balance gate)

The simulator plays whole runs with the greedy bot (it ends a won run with `end_run`) and reports, per vocation and
difficulty, the wins, the win rate (`floor(wins * 100 / runs)`, final boss beaten) and the round percentiles (min,
p10, median, p90, max; a won run counts as round 100). The **balance gate**
(`bun run balance:check`, targets in `shared/data/balance-targets.json`) runs thousands of seeded runs per difficulty
with any implementation's simulator (`--impl python|rust|golang|typescript`, Python by default, split over parallel
processes; all give the same numbers thanks to the golden files) and fails when the win rate averaged over the three
vocations is off target ± `tolerancePct`:

| Difficulty | Win rate (round 100 boss beaten) |
|---|---|
| EASY | ~75% |
| NORMAL | ~50% |
| HARD | ~25% |

Each vocation must also stay within target ± `vocationTolerancePct` (5), so no class is the obvious pick.

Last gate run (Python simulator, 1000 seeded runs per vocation and difficulty, 1.4.0; the Archer is still outside
the vocation tolerance, see PLAN.md M9):

| Difficulty | Win rate | Warrior | Archer | Mage |
|---|---|---|---|---|
| EASY | 76.2% | 72.3% | 82.4% | 73.8% |
| NORMAL | 50.1% | 44.5% | 59.1% | 46.7% |
| HARD | 25.6% | 24.8% | 32.0% | 20.1% |

The bot is a floor, not a ceiling: a human who defends on boss telegraphs and plans potions does better. Re-run the
gate after any balance change and record its report in the CHANGELOG of that commit.

## 13. Auto-battle

An application-layer policy (not the engine) that picks the player's commands until the fight ends; the battle can't be
cancelled once it is started. It is deterministic: it only looks at the run state, so the commands it issues replay like
any others, and every command it returns is valid. Modes (chosen when starting it; `balance.autoBattle.modes.<mode>`
holds `offense` and `supportEvery`, and a support turn is `state.turn % supportEvery == supportEvery - 1`, where
`state.turn` starts at 1 in every fight):

| Mode | `offense` | Offensive action | `supportEvery` |
|---|---|---|---|
| `melee` (weapon focus) | `attack` | `attack` | 5 (`turn % 5 == 4`) |
| `spells` (spell focus) | `spell` | strongest affordable attack spell, else `attack` | 5 |
| `balanced` | `spell` | `spells`' offensive action | 2 (`turn % 2 == 1`) |

"Strongest" = highest `max`, ties to the lowest `id`; "affordable" = level-adjusted mana cost ≤ MP. The policy, in this
order:

1. Any turn: HP below `balance.autoBattle.emergencyHealBelowPct` (25) → the **heal step** (strongest affordable
   healing spell, else the best health potion) when one is available.
2. Support turn: HP below `healBelowPct` (50) → the heal step when available; else MP below `manaBelowPct` (30) → the
   best mana potion when owned; else a boss telegraph pending (`isBoss` and `bossActions % (telegraphEvery + 1) ==
   telegraphEvery`, i.e. the next boss action is the charge) → `defend`.
3. Otherwise the offensive action.

"Best potion" = the owned potion of that resource with the highest `max`, ties to the lowest `id`. Comparisons use
integers: "HP below p%" is `hp * 100 < maxHp * p`.
