#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "domain/character.hpp"
#include "domain/enums.hpp"
#include "support/helpers.hpp"

using namespace rpg::domain;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("item stats apply rarity and affixes; value applies the rarity", "[unit][character]") {
	const GameData data = rpg::testing::with_test_items(rpg::testing::test_data());
	const ItemInstance item{1, "test_helmet", "legendary", 0, {AffixRoll{"maxHp", 7}}};
	REQUIRE(item_stats(item, data) == StatTotals{{"armor", 20}, {"maxHp", 107}});
	REQUIRE(item_value(item, data) == 600);
}

TEST_CASE("rarities scale base stats and affix counts", "[unit][character]") {
	const GameData data = rpg::testing::with_test_items(rpg::testing::test_data());
	const std::vector<std::tuple<std::string, std::int64_t, std::int64_t>> cases{
	    {"common", 20, 0}, {"rare", 30, 1}, {"legendary", 40, 2}, {"mythic", 60, 2}};
	for (const auto& [rarity, attack, affixes] : cases) {
		CAPTURE(rarity);
		REQUIRE(item_stats(ItemInstance{1, "test_axe", rarity, 0, {}}, data) == StatTotals{{"attack", attack}});
		const RarityDef& definition = data.balance.rarity(rarity);
		REQUIRE(definition.affix_min == affixes);
		REQUIRE(definition.affix_max == affixes);
	}
}

TEST_CASE("the item score weights the final stats", "[unit][character]") {
	const GameData data = rpg::testing::with_test_items(rpg::testing::test_data());
	const auto& weights = data.balance.item_score_weights;
	const ItemInstance common{1, "test_helmet", "common", 0, {}};
	REQUIRE(item_score(common, data) == 10 * weights.at("armor") + 50 * weights.at("maxHp"));
	std::vector<std::int64_t> scores;
	for (const std::string rarity : {"common", "rare", "legendary"}) {
		scores.push_back(item_score(ItemInstance{1, "test_helmet", rarity, 0, {}}, data));
	}
	REQUIRE(std::ranges::is_sorted(scores));
	REQUIRE(scores[0] < scores[1]);
	REQUIRE(scores[1] < scores[2]);
	const ItemInstance with_affix{1, "test_helmet", "common", 0, {AffixRoll{"dodge", 2}}};
	REQUIRE(item_score(with_affix, data) == item_score(common, data) + 2 * weights.at("dodge"));
}

TEST_CASE("the required level grows with the item tier", "[unit][character]") {
	const GameData& data = rpg::testing::test_data();
	const std::int64_t per_tier = data.balance.item_level_per_tier;
	REQUIRE(required_level(ItemInstance{1, "sword", "common", 0, {}}, data) == 1);
	REQUIRE(required_level(ItemInstance{1, "sword", "common", 3, {}}, data) == 1 + 3 * per_tier);
}

TEST_CASE("the sheet combines vocation, level and equipment with caps", "[unit][character]") {
	const GameData data = rpg::testing::with_test_items(rpg::testing::test_data());
	Player player;
	player.name = "A";
	player.vocation_id = "warrior";
	const VocationDef& warrior = data.vocation("warrior");

	CharacterSheet sheet = build_sheet(player, data);
	REQUIRE(sheet.max_hp == warrior.start_hp);
	REQUIRE(sheet.melee_min == warrior.melee_min);
	REQUIRE(sheet.weapon_element == "physical");
	REQUIRE(sheet.protection("fire") == 0);

	player.level = 3;
	player.equipment.emplace("ring", ItemInstance{9, "test_ring", "common", 0, {}});
	player.equipment.emplace("helmet", ItemInstance{10, "test_helmet", "common", 0, {AffixRoll{"protFire", 500}}});
	sheet = build_sheet(player, data);
	REQUIRE(sheet.max_hp == warrior.start_hp + 2 * warrior.hp_per_level + 50);
	REQUIRE(sheet.melee_max == warrior.melee_max + 2 * warrior.melee_per_level);
	REQUIRE(sheet.armor == 10);
	REQUIRE(sheet.crit_chance == data.balance.caps.crit_chance);
	REQUIRE(sheet.dodge == data.balance.caps.dodge);
	REQUIRE(sheet.protection("fire") == data.balance.caps.protection);
}

TEST_CASE("a weapon with an element changes the melee element", "[unit][character]") {
	const auto& data = rpg::testing::test_data();
	const auto elemental = std::ranges::find_if(data.items, [](const ItemDef& item) {
		return item.slot == "weapon" && item.element.has_value() && *item.element != "physical";
	});
	REQUIRE(elemental != data.items.end());
	Player player;
	player.name = "A";
	player.vocation_id = "mage";
	player.equipment.emplace("weapon", ItemInstance{1, elemental->id, "common", 0, {}});
	REQUIRE(build_sheet(player, data).weapon_element == *elemental->element);
}

TEST_CASE("game data lookups", "[unit][character]") {
	const auto& data = rpg::testing::test_data();
	REQUIRE(data.has_vocation("mage"));
	REQUIRE_FALSE(data.has_vocation("knight"));
	REQUIRE(data.has_spell("brutal_strike"));
	REQUIRE(data.has_creature("rat"));
	REQUIRE(data.has_potion("mana_potion"));
	REQUIRE(data.has_item("sword"));
	REQUIRE(data.creature("munster").is_boss);
	REQUIRE(data.creature("rat").resistance("holy") == 100);
	REQUIRE_THROWS_AS(data.vocation("knight"), UnknownIdError);
	REQUIRE_THROWS_AS(data.potion("elixir"), UnknownIdError);
	REQUIRE_THROWS_AS(data.status("frozen"), UnknownIdError);
	REQUIRE_THROWS_AS(data.item("excalibur"), UnknownIdError);
	REQUIRE_THROWS_WITH(data.boss_of_tier(99), ContainsSubstring("boss of tier 99"));

	const auto tier = data.monsters_in_tier(0);
	REQUIRE(std::ranges::is_sorted(tier, {}, &MonsterDef::id));
	REQUIRE(std::ranges::all_of(tier, [](const MonsterDef* monster) { return monster->tier == 0; }));
}

TEST_CASE("enums and counters", "[unit][character]") {
	REQUIRE(protection_stat("fire") == "protFire");
	REQUIRE(protection_stat("physical") == "protPhysical");
	REQUIRE(to_string(Phase::game_over) == "game_over");
	REQUIRE(phase_from_string("battle") == Phase::battle);
	REQUIRE(phase_from_string("merchant") == Phase::merchant);
	REQUIRE(phase_from_string(to_string(Phase::game_over)) == Phase::game_over);
	REQUIRE_THROWS(phase_from_string("lobby"));

	const CountMap counts{{"a", 2}};
	REQUIRE(count_of(counts, "a") == 2);
	REQUIRE(count_of(counts, "b") == 0);

	MonsterInstance monster;
	monster.attacks = {MonsterAttack{"bite", "physical", 1, 2, 1, std::nullopt}};
	REQUIRE(monster.attack("bite").max == 2);
	REQUIRE_THROWS_AS(monster.attack("laser"), UnknownIdError);
}
