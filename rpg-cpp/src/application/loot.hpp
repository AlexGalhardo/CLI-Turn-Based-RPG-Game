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

// The rarity weights of a drop table, with the difficulty bonus on non-common rarities.
std::vector<std::int64_t> rarity_weights(
    const domain::GameData& data, const std::string& table, const domain::DifficultyDef& difficulty);

// The item to generate.
struct ItemRequest {
	const domain::VocationDef* vocation = nullptr;
	std::int64_t tier = 0;
	std::string table;
	const domain::DifficultyDef* difficulty = nullptr;
	std::int64_t uid = 0;
};

// Base item + rarity + affixes; std::nullopt (consuming no randomness) when no item fits.
std::optional<domain::ItemInstance> generate_item(
    const domain::GameData& data, domain::Rng& rng, const ItemRequest& request);

} // namespace rpg::application
