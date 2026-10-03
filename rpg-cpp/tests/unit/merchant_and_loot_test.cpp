#include <algorithm>
#include <set>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "application/engine.hpp"
#include "application/loot.hpp"
#include "application/merchant.hpp"
#include "domain/character.hpp"
#include "support/helpers.hpp"

using namespace rpg;
using application::error_event;
using application::Event;
using Catch::Matchers::ContainsSubstring;
using rpg::testing::new_engine;

namespace {

std::vector<Event> single(Event event) { return {std::move(event)}; }

domain::GameData loot_data() {
	domain::GameData data = rpg::testing::with_test_items(rpg::testing::test_data());
	data.affixes = {
	    domain::AffixDef{"of_power", "attack", 1, 3, 2, {"weapon"}},
	    domain::AffixDef{"of_the_bear", "maxHp", 5, 10, 5, {"weapon", "helmet"}},
	    domain::AffixDef{"of_speed", "dodge", 1, 2, 0, {"weapon", "ring"}},
	    domain::AffixDef{"of_speed_2", "dodge", 1, 2, 0, {"weapon"}},
	};
	return data;
}

application::ItemRequest request(const domain::GameData& data, std::int64_t tier, std::string_view enemy_class,
    std::string_view vocation, std::int64_t uid) {
	return application::ItemRequest{
	    &data.vocation(vocation), tier, &data.balance.enemy_class(enemy_class).rarity_weights, uid};
}

} // namespace

TEST_CASE("buying potions follows the rules", "[unit][merchant]") {
	auto engine = new_engine(rpg::testing::test_data());
	auto& player = engine.state.player;
	const auto gold = player.gold;
	REQUIRE(engine.step(application::BuyPotion{"health_potion", 2}) ==
	        single(Event{"potion_bought", {{"potionId", std::string("health_potion")}, {"quantity", std::int64_t{2}},
	                                          {"gold", std::int64_t{100}}}}));
	REQUIRE(player.gold == gold - 100);
	REQUIRE(engine.step(application::BuyPotion{"health_potion", 1}) == single(error_event("not_enough_gold")));
	REQUIRE(engine.step(application::BuyPotion{"health_potion", 0}) == single(error_event("invalid_quantity")));
	REQUIRE(engine.step(application::BuyPotion{"health_potion", 100}) == single(error_event("invalid_quantity")));
	REQUIRE(engine.step(application::BuyPotion{"great_health_potion", 1}) == single(error_event("potion_locked")));
	REQUIRE(engine.step(application::BuyPotion{"elixir", 1}) == single(error_event("unknown_potion")));
	REQUIRE(engine.state.stats.potions_bought.at("health_potion") == 2);
	REQUIRE(engine.state.stats.gold_spent == 100);
}

TEST_CASE("potions unlock by round", "[unit][merchant]") {
	const auto& data = rpg::testing::test_data();
	auto engine = new_engine(data);
	REQUIRE(
	    application::available_potions(engine.state, data) == std::vector<std::string>{"health_potion", "mana_potion"});
	engine.state.round = 80;
	REQUIRE(application::available_potions(engine.state, data).size() == data.potions.size());
}

TEST_CASE("equip swaps, sell and unequip", "[unit][merchant]") {
	const domain::GameData data = rpg::testing::with_test_items(rpg::testing::test_data());
	auto engine = new_engine(data);
	auto& player = engine.state.player;
	const domain::ItemInstance axe{50, "test_axe", "common", 0, {}};
	const domain::ItemInstance rod{51, "test_rod", "rare", 0, {}};
	player.bag.push_back(axe);
	player.bag.push_back(rod);
	const auto starter_uid = player.equipment.at("weapon").uid;

	REQUIRE(engine.step(application::Equip{51}) == single(error_event("cannot_equip")));
	REQUIRE(engine.step(application::Equip{50}) ==
	        std::vector<Event>{
	            Event{"item_unequipped",
	                {{"uid", starter_uid}, {"itemId", std::string("sword")}, {"slot", std::string("weapon")}}},
	            Event{"item_equipped",
	                {{"uid", std::int64_t{50}}, {"itemId", std::string("test_axe")}, {"slot", std::string("weapon")}}},
	        });
	REQUIRE(domain::build_sheet(player, data).melee_min == 8 + 20);

	const auto gold = player.gold;
	REQUIRE(engine.step(application::SellItem{51}) ==
	        single(Event{"item_sold", {{"uid", std::int64_t{51}}, {"itemId", std::string("test_rod")},
	                                      {"gold", domain::item_value(rod, data)}}}));
	REQUIRE(player.gold == gold + 250);
	REQUIRE(engine.step(application::SellItem{51}) == single(error_event("invalid_item")));
	REQUIRE(engine.step(application::Unequip{"weapon"}) ==
	        single(Event{"item_unequipped",
	            {{"uid", std::int64_t{50}}, {"itemId", std::string("test_axe")}, {"slot", std::string("weapon")}}}));
	REQUIRE(engine.step(application::Unequip{"weapon"}) == single(error_event("invalid_item")));
	REQUIRE(engine.step(application::Equip{999}) == single(error_event("invalid_item")));
}

TEST_CASE("unequip with a full bag, and HP clamps", "[unit][merchant]") {
	const domain::GameData data = rpg::testing::with_test_items(rpg::testing::test_data());
	auto engine = new_engine(data);
	auto& player = engine.state.player;
	player.equipment.insert_or_assign("helmet", domain::ItemInstance{70, "test_helmet", "common", 0, {}});
	player.hp = domain::build_sheet(player, data).max_hp;
	for (std::int64_t i = 0; i < data.balance.bag_capacity; ++i) {
		player.bag.push_back(domain::ItemInstance{100 + i, "test_ring", "common", 0, {}});
	}
	REQUIRE(engine.step(application::Unequip{"helmet"}) == single(error_event("bag_full")));
	player.bag.pop_back();
	engine.step(application::Unequip{"helmet"});
	REQUIRE(player.hp == domain::build_sheet(player, data).max_hp);
}

TEST_CASE("buying from the merchant stock", "[unit][merchant]") {
	const domain::GameData data = loot_data();
	auto engine = new_engine(data);
	auto& state = engine.state;
	REQUIRE(std::cmp_equal(state.merchant_stock.size(), data.balance.merchant_stock_size));
	const domain::ItemInstance item = state.merchant_stock.front();
	const auto price = application::stock_price(item, data);
	state.player.gold = price;
	REQUIRE(engine.step(application::BuyStockItem{0}) ==
	        single(Event{"item_bought", {{"uid", item.uid}, {"itemId", item.item_id}, {"gold", price}}}));
	REQUIRE(std::ranges::contains(state.player.bag, item));
	REQUIRE(engine.step(application::BuyStockItem{0}) == single(error_event("not_enough_gold")));
	REQUIRE(engine.step(application::BuyStockItem{9}) == single(error_event("invalid_item")));
	REQUIRE(engine.step(application::BuyStockItem{-1}) == single(error_event("invalid_item")));
	for (std::int64_t i = 0; i < 30; ++i) {
		state.player.bag.push_back(domain::ItemInstance{200 + i, "test_ring", "common", 0, {}});
	}
	REQUIRE(engine.step(application::BuyStockItem{0}) == single(error_event("bag_full")));
	engine.step(application::NextFight{});
	REQUIRE(state.merchant_stock.empty());
}

TEST_CASE("the merchant rejects battle commands it does not handle", "[unit][merchant]") {
	auto engine = new_engine(rpg::testing::test_data());
	domain::Rng rng(1);
	application::Merchant merchant(rpg::testing::test_data(), rng, engine.state);
	REQUIRE(merchant.handle(application::Attack{}) == single(error_event("invalid_phase")));
}

TEST_CASE("item generation is deterministic with unique affix stats", "[unit][loot]") {
	const domain::GameData data = loot_data();
	const auto& warrior = data.vocation("warrior");
	std::vector<std::optional<domain::ItemInstance>> first;
	std::vector<std::optional<domain::ItemInstance>> second;
	for (std::int64_t uid = 0; uid < 20; ++uid) {
		domain::Rng a(5);
		domain::Rng b(5);
		first.push_back(application::generate_item(data, a, request(data, 0, "boss", "warrior", uid)));
		second.push_back(application::generate_item(data, b, request(data, 0, "boss", "warrior", uid)));
	}
	REQUIRE(first == second);

	domain::Rng rng(11);
	for (std::int64_t uid = 0; uid < 200; ++uid) {
		const auto item = application::generate_item(data, rng, request(data, 1, "boss", "warrior", uid));
		REQUIRE(item.has_value());
		REQUIRE((item->rarity == "legendary" || item->rarity == "mythic"));
		std::set<std::string> stats;
		for (const auto& affix : item->affixes) {
			stats.insert(affix.stat);
		}
		REQUIRE(stats.size() == item->affixes.size());
		REQUIRE(application::can_use(data.item(item->item_id), warrior));
	}
}

TEST_CASE("item generation without candidates consumes nothing", "[unit][loot]") {
	domain::GameData empty = rpg::testing::test_data();
	empty.items.clear();
	empty.index();
	domain::Rng rng(3);
	const auto result = application::generate_item(empty, rng, request(empty, 9, "normal", "mage", 1));
	REQUIRE_FALSE(result.has_value());
	REQUIRE(rng.state() == 3);
}

TEST_CASE("the rarity roll skips zero weights and single options", "[unit][loot]") {
	const auto& data = rpg::testing::test_data();
	domain::Rng rng(1);
	REQUIRE(application::roll_rarity(data, rng, {{"rare", 5}}).id == "rare");
	REQUIRE(application::roll_rarity(data, rng, {{"common", 0}, {"mythic", 3}}).id == "mythic");
	REQUIRE(rng.state() == 1);
	std::set<std::string> rolled;
	for (int i = 0; i < 40; ++i) {
		rolled.insert(application::roll_rarity(data, rng, {{"common", 1}, {"legendary", 1}}).id);
	}
	REQUIRE(rolled == std::set<std::string>{"common", "legendary"});
	REQUIRE(rng.state() != 1);
	REQUIRE_THROWS_WITH(application::roll_rarity(data, rng, {{"common", 0}}), ContainsSubstring("positive"));
}

TEST_CASE("equip rejects items above the player level", "[unit][merchant]") {
	const domain::GameData data = rpg::testing::with_test_items(rpg::testing::test_data());
	auto engine = new_engine(data);
	auto& player = engine.state.player;
	const domain::ItemInstance axe{60, "test_axe", "common", 5, {}};
	player.bag.push_back(axe);
	const auto rng_state = engine.rng_state();
	REQUIRE(engine.step(application::Equip{60}) == single(error_event("level_too_low")));
	REQUIRE(std::ranges::contains(player.bag, axe));
	REQUIRE(engine.rng_state() == rng_state);
	player.level = domain::required_level(axe, data);
	REQUIRE(engine.step(application::Equip{60}).back() ==
	        Event{"item_equipped",
	            {{"uid", std::int64_t{60}}, {"itemId", std::string("test_axe")}, {"slot", std::string("weapon")}}});
}

TEST_CASE("selling an equipped uid is rejected", "[unit][merchant]") {
	auto engine = new_engine(rpg::testing::test_data());
	auto& player = engine.state.player;
	const domain::ItemInstance weapon = player.equipment.at("weapon");
	const auto gold = player.gold;
	REQUIRE(engine.step(application::SellItem{weapon.uid}) == single(error_event("invalid_item")));
	REQUIRE(player.equipment.at("weapon") == weapon);
	REQUIRE(player.gold == gold);
}

TEST_CASE("shields follow the vocation's shield types", "[unit][loot]") {
	const auto& data = rpg::testing::test_data();
	const domain::ItemDef shield{"test_shield", "Test Shield", "shield", "shield", 0, std::nullopt, {}, 1};
	REQUIRE(application::can_use(shield, data.vocation("warrior")));
	const domain::ItemDef armor{"test_armor", "Test Armor", "armor", "armor", 0, std::nullopt, {}, 1};
	REQUIRE(application::can_use(armor, data.vocation("mage")));
}

TEST_CASE("a full bag auto-sells drops", "[unit][loot]") {
	const auto& data = rpg::testing::test_data();
	auto engine = new_engine(data);
	engine.state.round = 9; // the next fight is a boss: it always drops items
	for (std::int64_t i = 0; i < data.balance.bag_capacity; ++i) {
		engine.state.player.bag.push_back(domain::ItemInstance{500 + i, "sword", "common", 0, {}});
	}
	auto& boss = rpg::testing::fight(engine);
	boss.hp = 1;
	engine.state.player.hp = 100'000;
	std::vector<Event> events;
	while (engine.state.phase == domain::Phase::battle) {
		engine.state.player.hp = 100'000;
		events = engine.step(application::Attack{});
	}
	REQUIRE(std::ranges::contains(rpg::testing::types_of(events), "item_auto_sold"));
	REQUIRE(engine.state.stats.items_sold > 0);
}
