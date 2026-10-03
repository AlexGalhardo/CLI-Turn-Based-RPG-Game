// Auto-battle policy decisions (docs/game-design.md §13).
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "application/auto_battle.hpp"
#include "application/engine.hpp"
#include "domain/character.hpp"
#include "support/helpers.hpp"

using namespace rpg;
using application::AutoBattleMode;
using application::AutoBattlePolicy;
using application::Command;

namespace {

constexpr std::int64_t offense_turn = 1;

application::GameEngine battle(std::string_view vocation = "warrior", std::int64_t round = 0) {
	auto engine = rpg::testing::new_engine(rpg::testing::test_data(), vocation, "normal", 11);
	engine.state.round = round;
	engine.step(application::NextFight{});
	return engine;
}

Command choose(application::GameEngine& engine, AutoBattleMode mode, std::int64_t turn) {
	engine.state.turn = turn;
	return AutoBattlePolicy(rpg::testing::test_data(), mode).choose(engine.state);
}

std::int64_t support_turn(AutoBattleMode mode) {
	return rpg::testing::test_data().balance.auto_battle.mode(application::to_string(mode)).support_every - 1;
}

std::int64_t max_hp(const application::GameEngine& engine) {
	return domain::build_sheet(engine.state.player, rpg::testing::test_data()).max_hp;
}

} // namespace

TEST_CASE("the offensive action of each mode", "[unit][auto_battle]") {
	const auto& data = rpg::testing::test_data();
	auto engine = battle();
	engine.state.player.mp = 10'000;
	REQUIRE(choose(engine, AutoBattleMode::melee, offense_turn + 1) == Command{application::Attack{}});
	REQUIRE(choose(engine, AutoBattleMode::spells, offense_turn) == Command{application::Cast{"annihilation"}});
	REQUIRE(choose(engine, AutoBattleMode::balanced, 2) == Command{application::Cast{"annihilation"}});
	engine.state.player.mp = data.spell("brutal_strike").mana;
	REQUIRE(choose(engine, AutoBattleMode::spells, offense_turn) == Command{application::Cast{"brutal_strike"}});
	engine.state.player.mp = 0;
	REQUIRE(choose(engine, AutoBattleMode::spells, offense_turn) == Command{application::Attack{}});
}

TEST_CASE("the support turn cadence of each mode", "[unit][auto_battle]") {
	std::vector<std::int64_t> turns;
	for (const AutoBattleMode mode : application::kAutoBattleModes) {
		turns.push_back(support_turn(mode));
	}
	REQUIRE(turns == std::vector<std::int64_t>{4, 4, 1});
	REQUIRE(application::to_string(AutoBattleMode::balanced) == "balanced");
}

TEST_CASE("a support turn heals below half HP", "[unit][auto_battle]") {
	const AutoBattleMode mode = GENERATE(AutoBattleMode::melee, AutoBattleMode::spells, AutoBattleMode::balanced);
	auto engine = battle();
	auto& player = engine.state.player;
	player.hp = max_hp(engine) * 40 / 100;
	player.mp = 10'000;
	REQUIRE(choose(engine, mode, support_turn(mode)) == Command{application::Cast{"wound_cleansing"}});
	player.mp = 0;
	REQUIRE(choose(engine, mode, support_turn(mode)) == Command{application::UsePotion{"health_potion"}});
	player.potions.clear();
	REQUIRE(choose(engine, mode, support_turn(mode)) == Command{application::Attack{}});
}

TEST_CASE("the heal waits for the support turn above the emergency line", "[unit][auto_battle]") {
	auto engine = battle();
	engine.state.player.hp = max_hp(engine) * 40 / 100;
	REQUIRE(choose(engine, AutoBattleMode::melee, offense_turn) == Command{application::Attack{}});
}

TEST_CASE("the emergency heal happens on any turn", "[unit][auto_battle]") {
	auto engine = battle();
	engine.state.player.hp = max_hp(engine) * 20 / 100;
	engine.state.player.mp = 10'000;
	REQUIRE(choose(engine, AutoBattleMode::melee, offense_turn) == Command{application::Cast{"wound_cleansing"}});
}

TEST_CASE("the best potion is the strongest owned", "[unit][auto_battle]") {
	auto engine = battle();
	auto& player = engine.state.player;
	player.hp = 1;
	player.mp = 0;
	player.potions["strong_health_potion"] = 1;
	REQUIRE(
	    choose(engine, AutoBattleMode::melee, offense_turn) == Command{application::UsePotion{"strong_health_potion"}});
}

TEST_CASE("a support turn drinks mana when low", "[unit][auto_battle]") {
	auto engine = battle("mage");
	engine.state.player.mp = 0;
	const std::int64_t turn = support_turn(AutoBattleMode::spells);
	REQUIRE(choose(engine, AutoBattleMode::spells, turn) == Command{application::UsePotion{"mana_potion"}});
	engine.state.player.potions.clear();
	REQUIRE(choose(engine, AutoBattleMode::spells, turn) == Command{application::Attack{}});
}

TEST_CASE("a support turn defends against a telegraphed charge", "[unit][auto_battle]") {
	auto engine = battle("warrior", 9);
	REQUIRE(engine.state.monster->is_boss);
	engine.state.monster->boss_actions = rpg::testing::test_data().balance.boss_telegraph_every;
	const std::int64_t turn = support_turn(AutoBattleMode::melee);
	REQUIRE(choose(engine, AutoBattleMode::melee, turn) == Command{application::Defend{}});
	REQUIRE(choose(engine, AutoBattleMode::melee, turn + 1) == Command{application::Attack{}});
	engine.state.monster->boss_actions = 0;
	REQUIRE(choose(engine, AutoBattleMode::melee, turn) == Command{application::Attack{}});
}

TEST_CASE("the policy finishes fights with valid commands", "[unit][auto_battle]") {
	const AutoBattleMode mode = GENERATE(AutoBattleMode::melee, AutoBattleMode::spells, AutoBattleMode::balanced);
	auto engine = battle("archer");
	const AutoBattlePolicy policy(rpg::testing::test_data(), mode);
	for (int turn = 0; turn < 500 && engine.state.phase == domain::Phase::battle; ++turn) {
		for (const auto& event : engine.step(policy.choose(engine.state))) {
			REQUIRE(event.type != "error");
		}
	}
	REQUIRE(engine.state.phase != domain::Phase::battle);
}

TEST_CASE("the policy is deterministic", "[unit][auto_battle]") {
	auto engine = battle("mage");
	const auto rng_state = engine.rng_state();
	const auto decisions = [&] {
		std::vector<Command> result;
		for (const AutoBattleMode mode : application::kAutoBattleModes) {
			for (std::int64_t turn = 0; turn < 6; ++turn) {
				result.push_back(choose(engine, mode, turn));
			}
		}
		return result;
	};
	REQUIRE(decisions() == decisions());
	REQUIRE(engine.rng_state() == rng_state);
}
