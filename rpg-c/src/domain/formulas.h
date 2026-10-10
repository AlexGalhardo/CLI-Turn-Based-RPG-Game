// Pure integer formulas from docs/game-design.md. No randomness, no state.
// All game math is int64_t; operands are never negative, so C's truncating division is the floor the rules ask for.
#ifndef RPG_DOMAIN_FORMULAS_H
#define RPG_DOMAIN_FORMULAS_H

#include "domain/definitions.h"

// floor(value * percent / 100) for non-negative operands: the only rounding rule of the engine.
int64_t pct(int64_t value, int64_t percent);
int64_t clamp(int64_t value, int64_t minimum, int64_t maximum);
int64_t min_i64(int64_t a, int64_t b);
int64_t max_i64(int64_t a, int64_t b);

// Total experience needed to reach `level` (Tibia formula).
int64_t xp_for_level(int64_t level);
// Total mana spent (since level 1) needed to advance from `magic_level` to the next one.
int64_t mana_for_magic_level(int64_t magic_level, const Balance *balance);
const SpellLevelDef *spell_level_for_uses(int64_t uses, const Balance *balance);
int64_t armor_mitigation(int64_t damage, int64_t armor);

typedef struct {
	int64_t round;
	int64_t tier;
	int64_t cycle;
	int64_t position;
	bool is_boss;
} RoundInfo;

RoundInfo round_info(int64_t round_number, const Balance *balance, int tier_count);

typedef struct {
	int64_t hp_pct_product;
	int64_t damage_pct_product;
	int64_t reward_xp_pct_product;
	int64_t reward_gold_pct_product;
} Scaling;

Scaling scaling(RoundInfo info, const Balance *balance, const DifficultyDef *difficulty);
// Applies three chained percentages in one floor: value * a * b * c / 1_000_000.
int64_t scale_stat(int64_t value, int64_t pct_product);
// Applies two chained percentages in one floor: value * a * b / 10_000.
int64_t scale_reward(int64_t value, int64_t pct_product);

#endif
