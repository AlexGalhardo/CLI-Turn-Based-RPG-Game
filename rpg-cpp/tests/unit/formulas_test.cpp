#include <tuple>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "domain/formulas.hpp"
#include "support/helpers.hpp"

using namespace rpg::domain;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("pct floors", "[unit][formulas]") {
	REQUIRE(pct(99, 50) == 49);
	REQUIRE(pct(0, 150) == 0);
	REQUIRE(pct(7, 100) == 7);
}

TEST_CASE("pct rejects negative operands", "[unit][formulas]") {
	REQUIRE_THROWS_WITH(pct(-1, 10), ContainsSubstring("non-negative"));
	REQUIRE_THROWS(pct(1, -10));
}

TEST_CASE("clamp", "[unit][formulas]") {
	REQUIRE(clamp(5, 0, 3) == 3);
	REQUIRE(clamp(-1, 0, 3) == 0);
	REQUIRE(clamp(2, 0, 3) == 2);
}

TEST_CASE("xp_for_level matches the Tibia table", "[unit][formulas]") {
	const auto [level, xp] = GENERATE(table<std::int64_t, std::int64_t>(
	    {{1, 0}, {2, 100}, {3, 200}, {4, 400}, {5, 800}, {10, 9300}, {20, 98800}, {100, 15694800}}));
	REQUIRE(xp_for_level(level) == xp);
}

TEST_CASE("mana_for_magic_level is cumulative", "[unit][formulas]") {
	const Balance& balance = rpg::testing::test_data().balance;
	const auto base = balance.magic_level_base;
	REQUIRE(mana_for_magic_level(1, balance) == base);
	REQUIRE(mana_for_magic_level(2, balance) == base + base * balance.magic_level_growth_pct / 100);
	REQUIRE(mana_for_magic_level(5, balance) > mana_for_magic_level(4, balance));
}

TEST_CASE("spell_level_for_uses", "[unit][formulas]") {
	const auto& levels = rpg::testing::test_data().balance.spell_levels;
	REQUIRE(spell_level_for_uses(0, levels).level == 1);
	REQUIRE(spell_level_for_uses(19, levels).level == 1);
	REQUIRE(spell_level_for_uses(20, levels).level == 2);
	REQUIRE(spell_level_for_uses(49, levels).level == 2);
	REQUIRE(spell_level_for_uses(50, levels).level == 3);
	REQUIRE(spell_level_for_uses(5000, levels).level == 3);
}

TEST_CASE("armor mitigation", "[unit][formulas]") {
	REQUIRE(armor_mitigation(100, 0) == 100);
	REQUIRE(armor_mitigation(100, 100) == 50);
	REQUIRE(armor_mitigation(10, 3) == 9);
}

TEST_CASE("round_info locates a round in the infinite loop", "[unit][formulas]") {
	const auto& data = rpg::testing::test_data();
	using Row = std::tuple<std::int64_t, std::int64_t, std::int64_t, std::int64_t, bool>;
	const auto [round, tier, cycle, position, is_boss] =
	    GENERATE(table<std::int64_t, std::int64_t, std::int64_t, std::int64_t, bool>(
	        {Row{1, 0, 0, 0, false}, Row{9, 0, 0, 8, false}, Row{10, 0, 0, 9, true}, Row{11, 1, 0, 0, false},
	            Row{100, 9, 0, 9, true}, Row{101, 0, 1, 0, false}, Row{250, 4, 2, 9, true}}));
	const RoundInfo info = round_info(round, data.balance, data.tier_count());
	REQUIRE(info.tier == tier);
	REQUIRE(info.cycle == cycle);
	REQUIRE(info.position == position);
	REQUIRE(info.is_boss == is_boss);
}

TEST_CASE("scaling combines difficulty, cycle and position", "[unit][formulas]") {
	const auto& data = rpg::testing::test_data();
	const Balance& balance = data.balance;
	const DifficultyDef& hard = balance.difficulty("hard");
	const RoundInfo info = round_info(103, balance, data.tier_count());
	const Scaling factors = scaling(info, balance, hard);
	const auto cycle_pct = 100 + balance.cycle_stat_pct;
	const auto position_pct = 100 + 2 * balance.position_pct;
	REQUIRE(factors.hp_pct_product == hard.hp_pct * cycle_pct * position_pct);
	REQUIRE(scale_stat(1000, factors.hp_pct_product) == 1000 * hard.hp_pct * cycle_pct * position_pct / 1'000'000);
	REQUIRE(scale_reward(100, factors.reward_xp_pct_product) ==
	        100 * hard.xp_pct * (100 + balance.cycle_reward_pct) / 10'000);
}

TEST_CASE("bosses ignore position scaling", "[unit][formulas]") {
	const auto& data = rpg::testing::test_data();
	const RoundInfo info = round_info(10, data.balance, data.tier_count());
	const DifficultyDef& normal = data.balance.difficulty("normal");
	REQUIRE(scaling(info, data.balance, normal).hp_pct_product == normal.hp_pct * 100 * 100);
}
