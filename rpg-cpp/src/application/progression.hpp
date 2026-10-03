#pragma once

#include <cstdint>
#include <vector>

#include "application/events.hpp"
#include "domain/definitions.hpp"
#include "domain/entities.hpp"

namespace rpg::application {

// Experience, levels, magic levels and spell levels (docs/game-design.md §4 and §9).
class Progression {
public:
	explicit Progression(const domain::GameData& data) : data_(&data) {}

	// Adds XP and applies every level up it reaches.
	std::vector<Event> gain_experience(domain::Player& player, std::int64_t amount) const;

	// Counts a spell use and grows spell level and magic level.
	std::vector<Event> after_cast(domain::Player& player, const domain::SpellDef& spell, std::int64_t mana_cost) const;

private:
	const domain::GameData* data_;
};

} // namespace rpg::application
