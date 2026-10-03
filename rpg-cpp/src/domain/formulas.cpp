#include "domain/formulas.hpp"

#include <algorithm>
#include <stdexcept>

namespace rpg::domain {

std::int64_t pct(std::int64_t value, std::int64_t percent) {
	// Integer division truncates towards zero in C++, which is only a floor for non-negative operands.
	if (value < 0 || percent < 0) {
		throw std::invalid_argument("pct operands must be non-negative");
	}
	return value * percent / 100;
}

std::int64_t clamp(std::int64_t value, std::int64_t minimum, std::int64_t maximum) {
	return std::max(minimum, std::min(maximum, value));
}

std::int64_t xp_for_level(std::int64_t level) {
	if (level <= 1) {
		return 0;
	}
	return 50 * (level * level * level - 6 * level * level + 17 * level - 12) / 3;
}

std::int64_t mana_for_magic_level(std::int64_t magic_level, const Balance& balance) {
	std::int64_t step = balance.magic_level_base;
	std::int64_t total = step;
	for (std::int64_t level = 1; level < magic_level; ++level) {
		step = pct(step, balance.magic_level_growth_pct);
		total += step;
	}
	return total;
}

const SpellLevelDef& spell_level_for_uses(std::int64_t uses, std::span<const SpellLevelDef> levels) {
	const SpellLevelDef* current = &levels.front();
	for (const SpellLevelDef& level : levels) {
		if (uses >= level.uses) {
			current = &level;
		}
	}
	return *current;
}

std::int64_t armor_mitigation(std::int64_t damage, std::int64_t armor) { return damage * 100 / (100 + armor); }

RoundInfo round_info(std::int64_t round, const Balance& balance, std::int64_t tier_count) {
	const std::int64_t index = round - 1;
	const std::int64_t per_tier = balance.rounds_per_tier;
	const std::int64_t position = index % per_tier;
	return RoundInfo{
	    .round = round,
	    .tier = (index / per_tier) % tier_count,
	    .cycle = index / (per_tier * tier_count),
	    .position = position,
	    .is_boss = position == per_tier - 1,
	};
}

Scaling scaling(const RoundInfo& info, const Balance& balance, const DifficultyDef& difficulty) {
	const std::int64_t cycle_pct = 100 + info.cycle * balance.cycle_stat_pct;
	const std::int64_t reward_pct = 100 + info.cycle * balance.cycle_reward_pct;
	const std::int64_t position_pct = info.is_boss ? 100 : 100 + info.position * balance.position_pct;
	return Scaling{
	    .hp_pct_product = difficulty.hp_pct * cycle_pct * position_pct,
	    .damage_pct_product = difficulty.damage_pct * cycle_pct * position_pct,
	    .reward_xp_pct_product = difficulty.xp_pct * reward_pct,
	    .reward_gold_pct_product = difficulty.gold_pct * reward_pct,
	};
}

std::int64_t scale_stat(std::int64_t value, std::int64_t pct_product) { return value * pct_product / 1'000'000; }

std::int64_t scale_reward(std::int64_t value, std::int64_t pct_product) { return value * pct_product / 10'000; }

} // namespace rpg::domain
