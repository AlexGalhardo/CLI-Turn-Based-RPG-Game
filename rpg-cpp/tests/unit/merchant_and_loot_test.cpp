#include <algorithm>
#include <set>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "application/engine.hpp"
#include "application/loot.hpp"
#include "application/merchant.hpp"
#include "domain/character.hpp"
#include "support/helpers.hpp"

using namespace rpg;
using application::error_event;
using application::Event;
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

application::ItemRequest request(
    const domain::GameData& data, std::int64_t tier, std::string table, std::string_view vocation, std::int64_t uid) {
	return application::ItemRequest{
	    &data.vocation(vocation), tier, std::move(table), &data.balance.difficulty("normal"), uid};
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
		REQUIRE(item->rarity != "common");
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
	const auto result = application::generate_item(empty, rng, request(empty, 9, "monster", "mage", 1));
	REQUIRE_FALSE(result.has_value());
	REQUIRE(rng.state() == 3);
}

TEST_CASE("hard difficulty boosts non-common weights", "[unit][loot]") {
	const auto& data = rpg::testing::test_data();
	const auto normal = application::rarity_weights(data, "monster", data.balance.difficulty("normal"));
	const auto hard = application::rarity_weights(data, "monster", data.balance.difficulty("hard"));
	REQUIRE(hard.front() == normal.front());
	for (std::size_t i = 1; i < hard.size(); ++i) {
		REQUIRE(hard[i] >= normal[i]);
	}
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
