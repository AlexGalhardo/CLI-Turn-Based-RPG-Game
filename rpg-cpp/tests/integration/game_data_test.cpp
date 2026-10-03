#include <algorithm>
#include <set>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include "assets/shared_files.hpp"
#include "infrastructure/data_loader.hpp"
#include "infrastructure/paths.hpp"
#include "support/helpers.hpp"

using namespace rpg;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("the content meets the requirements", "[integration][game_data]") {
	const auto& data = rpg::testing::test_data();
	REQUIRE(data.monsters.size() >= 100);
	REQUIRE(data.tier_count() == 10);
	std::set<std::string> vocations;
	for (const auto& vocation : data.vocations) {
		vocations.insert(vocation.id);
	}
	REQUIRE(vocations == std::set<std::string>{"warrior", "archer", "mage"});
	for (std::int64_t tier = 0; tier < data.tier_count(); ++tier) {
		REQUIRE(data.monsters_in_tier(tier).size() >= 9);
		REQUIRE(data.boss_of_tier(tier).is_boss);
	}
}

TEST_CASE("cross references are valid", "[integration][game_data]") {
	const auto& data = rpg::testing::test_data();
	for (const auto& vocation : data.vocations) {
		REQUIRE_NOTHROW(data.item(vocation.starter_weapon));
		for (const auto& spell_id : vocation.spells) {
			REQUIRE_NOTHROW(data.spell(spell_id));
		}
	}
	std::vector<const domain::MonsterDef*> creatures;
	for (const auto& monster : data.monsters) {
		creatures.push_back(&monster);
	}
	for (const auto& boss : data.bosses) {
		creatures.push_back(&boss);
	}
	std::set<std::string> ids;
	for (const domain::MonsterDef* creature : creatures) {
		ids.insert(creature->id);
		REQUIRE(std::ranges::contains(data.families, creature->family));
		for (const auto& attack : creature->attacks) {
			REQUIRE(attack.min <= attack.max);
			if (attack.status.has_value()) {
				REQUIRE_NOTHROW(data.status(attack.status->status));
			}
		}
		if (creature->is_boss) {
			REQUIRE(creature->charge_attack.has_value());
			REQUIRE_NOTHROW(creature->attack(*creature->charge_attack));
		}
	}
	REQUIRE(ids.size() == creatures.size());
	for (const auto& spell : data.spells) {
		if (spell.level3_bonus.status.has_value()) {
			const auto& kind = data.status(*spell.level3_bonus.status).kind;
			REQUIRE((kind == "dot" || kind == "stun"));
		}
	}
	for (const auto& stack : data.balance.starting_potions) {
		REQUIRE_NOTHROW(data.potion(stack.potion_id));
	}
}

TEST_CASE("lookup errors", "[integration][game_data]") {
	const auto& data = rpg::testing::test_data();
	REQUIRE_THROWS_AS(data.spell("avada_kedavra"), domain::UnknownIdError);
	REQUIRE_THROWS_AS(data.balance.difficulty("nightmare"), domain::UnknownIdError);
	REQUIRE_THROWS_AS(data.balance.rarity("mythic"), domain::UnknownIdError);
	REQUIRE_THROWS_AS(data.creature("rat").attack("laser"), domain::UnknownIdError);
}

TEST_CASE("invalid data raises DataError naming the problem", "[integration][game_data]") {
	assets::SharedFs copy = assets::embedded_shared();
	auto document = nlohmann::ordered_json::parse(copy.at("data/vocations.json"));
	document["vocations"][0].erase("startHp");
	const std::string missing_field = document.dump();
	copy.insert_or_assign("data/vocations.json", missing_field);
	REQUIRE_THROWS_AS(infrastructure::load_game_data(copy), infrastructure::DataError);
	REQUIRE_THROWS_WITH(infrastructure::load_game_data(copy), ContainsSubstring("startHp"));

	const std::string not_json = "{ not json";
	copy.insert_or_assign("data/vocations.json", not_json);
	REQUIRE_THROWS_WITH(infrastructure::load_game_data(copy), ContainsSubstring("vocations.json"));

	copy.erase("data/vocations.json");
	REQUIRE_THROWS_WITH(infrastructure::load_game_data(copy), ContainsSubstring("missing"));

	assets::SharedFs no_bosses = assets::embedded_shared();
	const std::string empty_bosses = R"({"bosses": []})";
	no_bosses.insert_or_assign("data/bosses.json", empty_bosses);
	REQUIRE_THROWS_WITH(infrastructure::load_game_data(no_bosses), ContainsSubstring("required"));
}

TEST_CASE("the data directory resolves flag, environment, then home", "[integration][paths]") {
	REQUIRE(infrastructure::resolve_data_dir("custom") == std::filesystem::path("custom"));
	const std::string previous = infrastructure::get_env("RPG_DATA_DIR");
	rpg::testing::set_env("RPG_DATA_DIR", "from-env");
	REQUIRE(infrastructure::resolve_data_dir() == std::filesystem::path("from-env"));
	rpg::testing::unset_env("RPG_DATA_DIR");
	REQUIRE(infrastructure::resolve_data_dir().filename() == ".cli-turn-based-rpg");
	rpg::testing::set_env("RPG_DATA_DIR", previous);
}
