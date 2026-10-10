// Item factory: base item + rarity + affixes (docs/game-design.md §8).
#ifndef RPG_APPLICATION_LOOT_H
#define RPG_APPLICATION_LOOT_H

#include "domain/entities.h"
#include "domain/rng.h"

bool can_use(const ItemDef *item, const VocationDef *vocation);
// Weighted roll in the order of `balance.rarities`; zero weights are skipped and a single option is not rolled.
const RarityDef *roll_rarity(const GameData *data, Rng *rng, const RarityWeights *weights);
// Returns false (consuming no randomness) when no item fits the vocation and tier.
bool generate_item(const GameData *data, Rng *rng, const VocationDef *vocation, int64_t tier,
    const RarityWeights *weights, int64_t uid, ItemInstance *out);

#endif
