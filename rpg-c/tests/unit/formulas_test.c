#include "domain/formulas.h"
#include "support/helpers.h"

#define SUITE "unit/formulas"

TEST(SUITE, pct_floors) {
	CHECK_INT(pct(99, 50), 49);
	CHECK_INT(pct(0, 150), 0);
	CHECK_INT(pct(7, 100), 7);
}

// The reference also asserts that pct(-1, 10) raises: here a negative operand is a bug that stops the program
// (fatal), so that single assertion cannot run in-process.

TEST(SUITE, clamp) {
	CHECK_INT(clamp(5, 0, 3), 3);
	CHECK_INT(clamp(-1, 0, 3), 0);
	CHECK_INT(clamp(2, 0, 3), 2);
}

TEST(SUITE, xp_for_level_matches_tibia_table) {
	const int64_t table[][2] = {
	    {1, 0}, {2, 100}, {3, 200}, {4, 400}, {5, 800}, {10, 9300}, {20, 98800}, {100, 15694800}};
	for (size_t i = 0; i < ARRAY_LEN(table); i++) {
		CHECK_INT(xp_for_level(table[i][0]), table[i][1]);
	}
}

TEST(SUITE, mana_for_magic_level_is_cumulative) {
	const Balance *balance = &test_data()->balance;
	int64_t base = balance->magic_level_base;
	int64_t growth = balance->magic_level_growth_pct;
	CHECK_INT(mana_for_magic_level(1, balance), base);
	CHECK_INT(mana_for_magic_level(2, balance), base + base * growth / 100);
	CHECK(mana_for_magic_level(5, balance) > mana_for_magic_level(4, balance));
}

TEST(SUITE, spell_level_for_uses) {
	const Balance *balance = &test_data()->balance;
	const int64_t table[][2] = {{0, 1}, {19, 1}, {20, 2}, {49, 2}, {50, 3}, {5000, 3}};
	for (size_t i = 0; i < ARRAY_LEN(table); i++) {
		CHECK_INT(spell_level_for_uses(table[i][0], balance)->level, table[i][1]);
	}
}

TEST(SUITE, armor_mitigation) {
	CHECK_INT(armor_mitigation(100, 0), 100);
	CHECK_INT(armor_mitigation(100, 100), 50);
	CHECK_INT(armor_mitigation(10, 3), 9);
}

TEST(SUITE, round_info) {
	const GameData *data = test_data();
	const struct {
		int64_t round;
		int64_t tier;
		int64_t cycle;
		int64_t position;
		bool is_boss;
	} cases[] = {
	    {1, 0, 0, 0, false},
	    {9, 0, 0, 8, false},
	    {10, 0, 0, 9, true},
	    {11, 1, 0, 0, false},
	    {100, 9, 0, 9, true},
	    {101, 0, 1, 0, false},
	    {250, 4, 2, 9, true},
	};
	for (size_t i = 0; i < ARRAY_LEN(cases); i++) {
		RoundInfo info = round_info(cases[i].round, &data->balance, data_tier_count(data));
		CHECK_INT(info.tier, cases[i].tier);
		CHECK_INT(info.cycle, cases[i].cycle);
		CHECK_INT(info.position, cases[i].position);
		CHECK(info.is_boss == cases[i].is_boss);
	}
}

TEST(SUITE, scaling_combines_difficulty_cycle_and_position) {
	const GameData *data = test_data();
	const Balance *balance = &data->balance;
	const DifficultyDef *hard = balance_difficulty(balance, "hard");
	RoundInfo info = round_info(103, balance, data_tier_count(data));
	Scaling factors = scaling(info, balance, hard);
	int64_t cycle_pct = 100 + balance->cycle_stat_pct;
	int64_t position_pct = 100 + 2 * balance->position_pct;
	CHECK_INT(factors.hp_pct_product, hard->hp_pct * cycle_pct * position_pct);
	CHECK_INT(scale_stat(1000, factors.hp_pct_product), 1000 * hard->hp_pct * cycle_pct * position_pct / 1000000);
	CHECK_INT(scale_reward(100, factors.reward_xp_pct_product),
	    100 * hard->xp_pct * (100 + balance->cycle_reward_pct) / 10000);
}

TEST(SUITE, boss_ignores_position_scaling) {
	const GameData *data = test_data();
	const DifficultyDef *normal = balance_difficulty(&data->balance, "normal");
	RoundInfo info = round_info(10, &data->balance, data_tier_count(data));
	CHECK_INT(scaling(info, &data->balance, normal).hp_pct_product, normal->hp_pct * 100 * 100);
}
