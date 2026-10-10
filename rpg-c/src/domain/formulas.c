#include "domain/formulas.h"

int64_t pct(int64_t value, int64_t percent) {
	if (value < 0 || percent < 0) {
		fatal("pct operands must be non-negative");
	}
	return value * percent / 100;
}

int64_t min_i64(int64_t a, int64_t b) { return a < b ? a : b; }

int64_t max_i64(int64_t a, int64_t b) { return a > b ? a : b; }

int64_t clamp(int64_t value, int64_t minimum, int64_t maximum) { return max_i64(minimum, min_i64(maximum, value)); }

int64_t xp_for_level(int64_t level) {
	if (level <= 1) {
		return 0;
	}
	return 50 * (level * level * level - 6 * level * level + 17 * level - 12) / 3;
}

int64_t mana_for_magic_level(int64_t magic_level, const Balance *balance) {
	int64_t step = balance->magic_level_base;
	int64_t total = step;
	for (int64_t i = 1; i < magic_level; i++) {
		step = pct(step, balance->magic_level_growth_pct);
		total += step;
	}
	return total;
}

const SpellLevelDef *spell_level_for_uses(int64_t uses, const Balance *balance) {
	const SpellLevelDef *current = &balance->spell_levels[0];
	for (int i = 0; i < balance->spell_level_count; i++) {
		if (uses >= balance->spell_levels[i].uses) {
			current = &balance->spell_levels[i];
		}
	}
	return current;
}

int64_t armor_mitigation(int64_t damage, int64_t armor) { return damage * 100 / (100 + armor); }

RoundInfo round_info(int64_t round_number, const Balance *balance, int tier_count) {
	int64_t index = round_number - 1;
	int64_t per_tier = balance->rounds_per_tier;
	int64_t position = index % per_tier;
	RoundInfo info = {
	    .round = round_number,
	    .tier = (index / per_tier) % tier_count,
	    .cycle = index / (per_tier * tier_count),
	    .position = position,
	    .is_boss = position == per_tier - 1,
	};
	return info;
}

Scaling scaling(RoundInfo info, const Balance *balance, const DifficultyDef *difficulty) {
	int64_t cycle_pct = 100 + info.cycle * balance->cycle_stat_pct;
	int64_t reward_pct = 100 + info.cycle * balance->cycle_reward_pct;
	int64_t position_pct = info.is_boss ? 100 : 100 + info.position * balance->position_pct;
	Scaling result = {
	    .hp_pct_product = difficulty->hp_pct * cycle_pct * position_pct,
	    .damage_pct_product = difficulty->damage_pct * cycle_pct * position_pct,
	    .reward_xp_pct_product = difficulty->xp_pct * reward_pct,
	    .reward_gold_pct_product = difficulty->gold_pct * reward_pct,
	};
	return result;
}

int64_t scale_stat(int64_t value, int64_t pct_product) { return value * pct_product / 1000000; }

int64_t scale_reward(int64_t value, int64_t pct_product) { return value * pct_product / 10000; }
