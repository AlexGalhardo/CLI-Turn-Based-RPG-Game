#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include "application/commands.hpp"
#include "application/events.hpp"
#include "application/run_state.hpp"
#include "support/helpers.hpp"

using namespace rpg;
using Catch::Matchers::ContainsSubstring;
using nlohmann::json;

TEST_CASE("every command round-trips through JSON", "[unit][serialization]") {
	const application::Command command = GENERATE(as<application::Command>{}, application::Attack{},
	    application::Cast{"brutal_strike"}, application::UsePotion{"health_potion"}, application::Defend{},
	    application::NextFight{}, application::BuyPotion{"mana_potion", 3}, application::SellItem{4},
	    application::Equip{5}, application::Unequip{"ring"}, application::BuyStockItem{1});
	const json document = json::parse(application::command_to_json(command).dump());
	REQUIRE(application::command_from_json(document) == command);
}

TEST_CASE("commands render exactly like the reference", "[unit][serialization]") {
	REQUIRE(
	    application::command_to_json(application::BuyStockItem{0}) == json{{"type", "buy_stock_item"}, {"index", 0}});
	REQUIRE(application::command_to_json(application::Unequip{"ring"}) == json{{"type", "unequip"}, {"slot", "ring"}});
	REQUIRE(application::is_battle(application::Defend{}));
	REQUIRE_FALSE(application::is_battle(application::NextFight{}));
}

TEST_CASE("an unknown command type is rejected", "[unit][serialization]") {
	REQUIRE_THROWS_WITH(application::command_from_json(json{{"type", "dance"}}), ContainsSubstring("unknown command"));
}

TEST_CASE("the run state round-trips through JSON", "[unit][serialization]") {
	auto engine = rpg::testing::new_engine(rpg::testing::test_data());
	engine.step(application::NextFight{});
	for (int i = 0; i < 3; ++i) {
		engine.step(application::Attack{});
	}
	auto& state = engine.state;
	state.player.bag.push_back(domain::ItemInstance{90, "sword", "epic", 2, {domain::AffixRoll{"dodge", 3}}});
	state.player.statuses.push_back({"burn", 2, 4});
	if (state.monster.has_value()) {
		state.monster->statuses.push_back({"stun", 1, 0});
		state.monster->attacks.front().status = domain::StatusOnHit{"bleed", 10, 20};
	}
	state.death_cause = "rat";
	const json raw = json::parse(application::to_json(state).dump());
	const application::RunState restored = application::run_state_from_json(raw);
	REQUIRE(restored == state);
	REQUIRE(application::to_json(restored) == application::to_json(state));
}

TEST_CASE("wrong JSON types are rejected", "[unit][serialization]") {
	REQUIRE_THROWS_AS(application::command_from_json(json{{"type", 1}}), json::exception);
	REQUIRE_THROWS_AS(application::command_from_json(json{{"type", "sell_item"}, {"uid", "1"}}), json::exception);
	REQUIRE_THROWS_AS(application::run_state_from_json(json::array()), json::exception);
}

TEST_CASE("event accessors read typed fields with defaults", "[unit][serialization]") {
	const application::Event event{"x", {{"n", std::int64_t{3}}, {"s", std::string("a")}, {"b", true}}};
	REQUIRE(event.integer("n") == 3);
	REQUIRE(event.text("s") == "a");
	REQUIRE(event.flag("b"));
	REQUIRE(event.integer("s") == 0);
	REQUIRE(event.text("missing").empty());
	REQUIRE_FALSE(event.flag("n"));
	REQUIRE(event.has("n"));
	REQUIRE_FALSE(event.has("missing"));
}
