#pragma once

#include <cstdint>
#include <span>

#include "domain/definitions.hpp"

namespace rpg::domain {

// floor(value * percent / 100) for non-negative operands — the only rounding rule of the engine.
std::int64_t pct(std::int64_t value, std::int64_t percent);

std::int64_t clamp(std::int64_t value, std::int64_t minimum, std::int64_t maximum);

// Total experience needed to reach a level (Tibia formula).
std::int64_t xp_for_level(std::int64_t level);

// Total mana spent (since level 1) needed to advance from magic_level to the next one.
std::int64_t mana_for_magic_level(std::int64_t magic_level, const Balance& balance);

// The level reached with a number of uses.
const SpellLevelDef& spell_level_for_uses(std::int64_t uses, std::span<const SpellLevelDef> levels);

// Physical damage reduced by armor.
std::int64_t armor_mitigation(std::int64_t damage, std::int64_t armor);

// Where a round sits in the infinite loop.
struct RoundInfo {
	std::int64_t round = 0;
	std::int64_t tier = 0;
	std::int64_t cycle = 0;
	std::int64_t position = 0;
	bool is_boss = false;
};

RoundInfo round_info(std::int64_t round, const Balance& balance, std::int64_t tier_count);

// The chained percentage products of a round.
struct Scaling {
	std::int64_t hp_pct_product = 0;
	std::int64_t damage_pct_product = 0;
	std::int64_t reward_xp_pct_product = 0;
	std::int64_t reward_gold_pct_product = 0;
};

Scaling scaling(const RoundInfo& info, const Balance& balance, const DifficultyDef& difficulty);

// Three chained percentages in one floor: value * a * b * c / 1_000_000.
std::int64_t scale_stat(std::int64_t value, std::int64_t pct_product);

// Two chained percentages in one floor: value * a * b / 10_000.
std::int64_t scale_reward(std::int64_t value, std::int64_t pct_product);

} // namespace rpg::domain
