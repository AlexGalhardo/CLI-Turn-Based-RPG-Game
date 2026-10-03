#pragma once

#include <cstdint>
#include <map>
#include <string_view>

#include "domain/definitions.hpp"
#include "domain/entities.hpp"

namespace rpg::domain {

using StatTotals = std::map<Stat, std::int64_t, std::less<>>;

// The stats of an item: base stats × rarity + affixes.
StatTotals item_stats(const ItemInstance& item, const GameData& data);

// The sell price of an item.
std::int64_t item_value(const ItemInstance& item, const GameData& data);

// Sum of the item's final stats weighted by `balance.itemScoreWeights` (like Diablo's item power).
std::int64_t item_score(const ItemInstance& item, const GameData& data);

// Uses the instance tier: the round tier the item was generated for (docs/game-design.md §8).
std::int64_t required_level(const ItemInstance& item, const GameData& data);

// The total score of the equipped items.
std::int64_t equipment_score(const Player& player, const GameData& data);

// The derived stats of the player (docs/game-design.md §4).
struct CharacterSheet {
	std::int64_t max_hp = 0;
	std::int64_t max_mp = 0;
	std::int64_t hp_regen = 0;
	std::int64_t mp_regen = 0;
	std::int64_t melee_min = 0;
	std::int64_t melee_max = 0;
	Element weapon_element;
	std::int64_t armor = 0;
	std::int64_t crit_chance = 0;
	std::int64_t crit_damage = 0;
	std::int64_t spell_power = 0;
	std::int64_t physical_damage = 0;
	std::int64_t dodge = 0;
	std::int64_t parry = 0;
	std::int64_t life_leech = 0;
	std::int64_t mana_leech = 0;
	StatTotals protections;

	// The protection percentage of an element.
	[[nodiscard]] std::int64_t protection(std::string_view element) const;
};

CharacterSheet build_sheet(const Player& player, const GameData& data);

} // namespace rpg::domain
