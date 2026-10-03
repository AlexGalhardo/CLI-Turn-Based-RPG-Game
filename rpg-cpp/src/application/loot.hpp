#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "domain/definitions.hpp"
#include "domain/entities.hpp"
#include "domain/rng.hpp"

namespace rpg::application {

// Whether a vocation can equip an item (docs/game-design.md §8).
bool can_use(const domain::ItemDef& item, const domain::VocationDef& vocation);

// Weighted roll in the order of `balance.rarities`; zero weights are skipped and a single option is not rolled.
const domain::RarityDef& roll_rarity(
    const domain::GameData& data, domain::Rng& rng, const domain::RarityWeights& weights);

// The item to generate.
struct ItemRequest {
	const domain::VocationDef* vocation = nullptr;
	std::int64_t tier = 0;
	const domain::RarityWeights* weights = nullptr;
	std::int64_t uid = 0;
};

// Base item + rarity + affixes; std::nullopt (consuming no randomness) when no item fits.
std::optional<domain::ItemInstance> generate_item(
    const domain::GameData& data, domain::Rng& rng, const ItemRequest& request);

} // namespace rpg::application
