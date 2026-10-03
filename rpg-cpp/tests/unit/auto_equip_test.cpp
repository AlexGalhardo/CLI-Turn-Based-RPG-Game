// Auto-equip with auto-sell (docs/game-design.md §8.1).
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "application/auto_equip.hpp"
#include "application/engine.hpp"
#include "domain/character.hpp"
#include "support/helpers.hpp"

using namespace rpg;
using application::Event;
using rpg::testing::types_of;

namespace {

application::GameEngine engine_for(const domain::GameData& data, bool auto_equip = true) {
	return rpg::testing::new_engine(data, "warrior", "normal", 42, auto_equip);
}

} // namespace

TEST_CASE("a better item is equipped and the old one sold", "[unit][auto_equip]") {
	const domain::GameData data = rpg::testing::with_test_items(rpg::testing::test_data());
	auto engine = engine_for(data);
	auto& state = engine.state;
	const domain::ItemInstance starter = state.player.equipment.at("weapon");
	const domain::ItemInstance axe{50, "test_axe", "common", 0, {}};
	state.player.bag.push_back(axe);
	const auto gold = state.player.gold;
	const auto rng_state = engine.rng_state();
	const auto events = application::auto_equip(state, data);
	REQUIRE(events == std::vector<Event>{
	                      Event{"item_auto_equipped",
	                          {{"uid", std::int64_t{50}}, {"itemId", std::string("test_axe")},
	                              {"slot", std::string("weapon")}, {"score", domain::item_score(axe, data)}}},
	                      Event{"item_auto_sold", {{"uid", starter.uid}, {"itemId", std::string("sword")},
	                                                  {"gold", domain::item_value(starter, data)}}},
	                  });
	REQUIRE(state.player.equipment.at("weapon") == axe);
	REQUIRE(state.player.gold == gold + domain::item_value(starter, data));
	REQUIRE(state.player.bag.empty());
	REQUIRE(engine.rng_state() == rng_state);
}

TEST_CASE("empty slots are filled without selling", "[unit][auto_equip]") {
	const domain::GameData data = rpg::testing::with_test_items(rpg::testing::test_data());
	auto engine = engine_for(data);
	engine.state.player.bag.push_back(domain::ItemInstance{60, "test_helmet", "common", 0, {}});
	REQUIRE(types_of(application::auto_equip(engine.state, data)) == std::vector<std::string>{"item_auto_equipped"});
	REQUIRE(engine.state.player.equipment.at("helmet").uid == 60);
}

TEST_CASE("ties go to the lowest uid and worse items stay", "[unit][auto_equip]") {
	const domain::GameData data = rpg::testing::with_test_items(rpg::testing::test_data());
	auto engine = engine_for(data);
	auto& player = engine.state.player;
	player.bag.push_back(domain::ItemInstance{72, "test_helmet", "common", 0, {}});
	player.bag.push_back(domain::ItemInstance{71, "test_helmet", "common", 0, {}});
	player.bag.push_back(domain::ItemInstance{73, "test_rod", "common", 0, {}});
	application::auto_equip(engine.state, data);
	REQUIRE(player.equipment.at("helmet").uid == 71);
	std::vector<std::int64_t> uids;
	for (const auto& item : player.bag) {
		uids.push_back(item.uid);
	}
	REQUIRE(uids == std::vector<std::int64_t>{72, 73});
	REQUIRE(application::auto_equip(engine.state, data).empty());
}

TEST_CASE("items above the player level are skipped", "[unit][auto_equip]") {
	const domain::GameData data = rpg::testing::with_test_items(rpg::testing::test_data());
	auto engine = engine_for(data);
	engine.state.player.bag.push_back(domain::ItemInstance{80, "test_axe", "mythic", 9, {}});
	REQUIRE(application::auto_equip(engine.state, data).empty());
	REQUIRE_FALSE(application::best_bag_item(engine.state, data, "weapon").has_value());
	engine.state.player.level = 1 + 9 * data.balance.item_level_per_tier;
	REQUIRE(application::auto_equip(engine.state, data).front().integer("uid") == 80);
}

TEST_CASE("a victory triggers auto-equip only when enabled", "[unit][auto_equip]") {
	domain::GameData data =
	    rpg::testing::with_enemy_class(rpg::testing::calm(rpg::testing::with_test_items(rpg::testing::test_data())),
	        "normal", [](domain::EnemyClassDef& row) { row.drop_chance_pct = 100; });
	for (domain::ItemDef& item : data.items) {
		if (item.id == "sword") {
			item.stats.clear();
		}
	}
	for (const bool enabled : {true, false}) {
		CAPTURE(enabled);
		auto engine = engine_for(data, enabled);
		engine.step(application::NextFight{});
		engine.state.monster->hp = 1;
		const auto events = engine.step(application::Attack{});
		REQUIRE(engine.state.phase == domain::Phase::merchant);
		REQUIRE(std::ranges::contains(types_of(events), "item_auto_equipped") == enabled);
		REQUIRE(engine.state.stats.items_auto_equipped == (enabled ? 1 : 0));
	}
}

TEST_CASE("buying a stock item triggers auto-equip", "[unit][auto_equip]") {
	const domain::GameData data = rpg::testing::with_test_items(rpg::testing::test_data());
	const domain::ItemInstance axe{90, "test_axe", "common", 0, {}};
	auto engine = engine_for(data);
	engine.state.merchant_stock = {axe};
	engine.state.player.gold = 10'000;
	REQUIRE(types_of(engine.step(application::BuyStockItem{0})) ==
	        std::vector<std::string>{"item_bought", "item_auto_equipped", "item_auto_sold"});
	REQUIRE(engine.state.player.equipment.at("weapon") == axe);

	auto manual = engine_for(data, false);
	manual.state.merchant_stock = {axe};
	manual.state.player.gold = 10'000;
	REQUIRE(types_of(manual.step(application::BuyStockItem{0})) == std::vector<std::string>{"item_bought"});
}
