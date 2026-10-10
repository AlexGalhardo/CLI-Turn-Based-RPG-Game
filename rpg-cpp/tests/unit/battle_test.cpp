#include <algorithm>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "application/battle.hpp"
#include "application/engine.hpp"
#include "domain/character.hpp"
#include "support/helpers.hpp"

using namespace rpg;
using application::Event;
using rpg::testing::fight;
using rpg::testing::find_event;
using rpg::testing::fixed_attack;
using rpg::testing::new_engine;
using rpg::testing::types_of;

namespace {

std::vector<Event> single(Event event) { return {std::move(event)}; }

std::int64_t count(const std::vector<std::string>& types, std::string_view type) {
	return std::ranges::count(types, type);
}

bool has(const std::vector<std::string>& types, std::string_view type) { return std::ranges::contains(types, type); }

} // namespace

TEST_CASE("melee damage is within the sheet range", "[unit][battle]") {
	const auto& data = rpg::testing::test_data();
	auto engine = new_engine(data);
	fight(engine);
	const auto sheet = domain::build_sheet(engine.state.player, data);
	const auto resistance = data.creature(engine.state.monster->creature_id).resistance("physical");
	const auto events = engine.step(application::Attack{});
	const Event* hit = find_event(events, "player_attacked");
	REQUIRE(hit != nullptr);
	const auto low = std::max<std::int64_t>(1, sheet.melee_min * resistance / 100);
	const auto high = sheet.melee_max * resistance / 100;
	REQUIRE_FALSE(hit->flag("crit"));
	REQUIRE(low <= hit->integer("damage"));
	REQUIRE(hit->integer("damage") <= std::max(high, low));
}

TEST_CASE("invalid battle commands do not consume the RNG", "[unit][battle]") {
	auto engine = new_engine(rpg::testing::test_data());
	REQUIRE(engine.step(application::Attack{}) == single(application::error_event("invalid_phase")));
	fight(engine);
	const auto rng_state = engine.rng_state();
	REQUIRE(engine.step(application::Cast{"flame_strike"}) == single(application::error_event("unknown_spell")));
	engine.state.player.mp = 0;
	REQUIRE(engine.step(application::Cast{"brutal_strike"}) == single(application::error_event("not_enough_mana")));
	engine.state.player.potions.clear();
	REQUIRE(engine.step(application::UsePotion{"health_potion"}) == single(application::error_event("no_potion")));
	REQUIRE(engine.step(application::UsePotion{"elixir"}) == single(application::error_event("unknown_potion")));
	REQUIRE(engine.step(application::NextFight{}) == single(application::error_event("invalid_phase")));
	REQUIRE(
	    engine.step(application::BuyPotion{"health_potion", 1}) == single(application::error_event("invalid_phase")));
	REQUIRE(engine.rng_state() == rng_state);
}

TEST_CASE("defend halves incoming damage", "[unit][battle]") {
	auto engine = new_engine(rpg::testing::test_data());
	fixed_attack(fight(engine), 100);
	engine.state.player.hp = 150;
	const auto events = engine.step(application::Defend{});
	REQUIRE(find_event(events, "monster_attacked")->integer("damage") == 50);
	REQUIRE_FALSE(engine.state.player.defending);
}

TEST_CASE("undefended damage kills the player", "[unit][battle]") {
	auto engine = new_engine(rpg::testing::test_data());
	auto& monster = fight(engine);
	const std::string creature_id = monster.creature_id;
	fixed_attack(monster, 1000);
	const auto events = engine.step(application::Attack{});
	REQUIRE(events.back() == Event{"player_died", {{"monsterId", creature_id}, {"round", std::int64_t{1}}}});
	REQUIRE(engine.state.phase == domain::Phase::game_over);
	REQUIRE(engine.state.death_cause == creature_id);
	REQUIRE(engine.step(application::Attack{}) == single(application::error_event("invalid_phase")));
	REQUIRE(engine.step(application::NextFight{}) == single(application::error_event("invalid_phase")));
}

TEST_CASE("a monster status applies and ticks", "[unit][battle]") {
	auto engine = new_engine(rpg::testing::test_data());
	fixed_attack(fight(engine), 10, "fire", domain::StatusOnHit{"burn", 100, 50});
	const auto events = engine.step(application::Defend{});
	REQUIRE(*find_event(events, "status_applied") ==
	        Event{"status_applied", {{"target", std::string("player")}, {"status", std::string("burn")},
	                                    {"turns", std::int64_t{3}}, {"perTurn", std::int64_t{2}}}});
	REQUIRE(*find_event(events, "status_ticked") ==
	        Event{"status_ticked",
	            {{"target", std::string("player")}, {"status", std::string("burn")}, {"damage", std::int64_t{2}}}});
	REQUIRE(engine.state.player.statuses == std::vector<domain::ActiveStatus>{{"burn", 2, 2}});
}

TEST_CASE("reapplying a status refreshes turns and keeps the higher damage", "[unit][battle]") {
	auto engine = new_engine(rpg::testing::test_data());
	fixed_attack(fight(engine), 10, "fire", domain::StatusOnHit{"burn", 100, 50});
	engine.state.player.statuses.push_back({"burn", 1, 9});
	engine.step(application::Defend{});
	REQUIRE(engine.state.player.statuses == std::vector<domain::ActiveStatus>{{"burn", 2, 9}});
}

TEST_CASE("a status expires", "[unit][battle]") {
	auto engine = new_engine(rpg::testing::test_data());
	fixed_attack(fight(engine), 1);
	engine.state.player.statuses.push_back({"bleed", 1, 1});
	const auto events = engine.step(application::Defend{});
	REQUIRE(has(types_of(events), "status_expired"));
	REQUIRE(engine.state.player.statuses.empty());
}

TEST_CASE("a player stun skips a turn and has a cooldown", "[unit][battle]") {
	auto engine = new_engine(rpg::testing::test_data());
	fixed_attack(fight(engine), 1, "physical", domain::StatusOnHit{"stun", 100, 0});
	auto types = types_of(engine.step(application::Defend{}));
	REQUIRE(count(types, "monster_attacked") == 2);
	REQUIRE(has(types, "player_stunned"));
	REQUIRE(count(types, "status_applied") == 1);
	REQUIRE(engine.state.player.stun_cooldown == 1);
	types = types_of(engine.step(application::Defend{}));
	REQUIRE_FALSE(has(types, "status_applied"));
}

TEST_CASE("a monster stun skips its attack", "[unit][battle]") {
	auto engine = new_engine(rpg::testing::test_data());
	auto& monster = fight(engine);
	fixed_attack(monster, 5);
	monster.statuses.push_back({"stun", 1, 0});
	const auto types = types_of(engine.step(application::Defend{}));
	REQUIRE(has(types, "monster_stunned"));
	REQUIRE_FALSE(has(types, "monster_attacked"));
	REQUIRE(engine.state.monster->stun_cooldown == 1);
}

TEST_CASE("a monster dies from a status tick", "[unit][battle]") {
	auto engine = new_engine(rpg::testing::test_data());
	auto& monster = fight(engine);
	monster.hp = 1;
	monster.statuses.push_back({"bleed", 3, 5});
	const auto types = types_of(engine.step(application::Defend{}));
	REQUIRE(has(types, "monster_killed"));
	REQUIRE(engine.state.phase == domain::Phase::merchant);
}

TEST_CASE("a boss telegraphs then charges", "[unit][battle]") {
	auto engine = new_engine(rpg::testing::test_data());
	engine.state.round = 9;
	auto& boss = fight(engine);
	REQUIRE(boss.is_boss);
	boss.hp = boss.max_hp = 1'000'000;
	for (auto& attack : boss.attacks) {
		attack.status.reset();
	}
	engine.state.player.hp = 1'000'000;
	std::vector<std::string> sequence;
	for (int i = 0; i < 4; ++i) {
		std::string kind = "none";
		for (const auto& event : engine.step(application::Defend{})) {
			if (event.type == "boss_telegraph") {
				kind = "telegraph";
			} else if (event.type == "monster_attacked") {
				kind = event.flag("charged") ? "charged" : "normal";
			} else if (event.type == "attack_dodged" || event.type == "attack_parried") {
				kind = "normal";
			} else {
				continue;
			}
			break;
		}
		sequence.push_back(kind);
	}
	REQUIRE(sequence == std::vector<std::string>{"normal", "normal", "telegraph", "charged"});
}

TEST_CASE("healing is capped and level three cleanses", "[unit][battle]") {
	const auto& data = rpg::testing::test_data();
	auto engine = new_engine(data);
	fixed_attack(fight(engine), 1);
	auto& player = engine.state.player;
	player.spell_uses["wound_cleansing"] = 50;
	player.statuses.push_back({"poison", 5, 1});
	player.hp = domain::build_sheet(player, data).max_hp - 3;
	const auto events = engine.step(application::Cast{"wound_cleansing"});
	const Event* healed = find_event(events, "spell_healed");
	REQUIRE(healed->integer("amount") == 3);
	REQUIRE(healed->integer("mana") == 32);
	REQUIRE(rpg::testing::contains_event(
	    events, Event{"status_expired", {{"target", std::string("player")}, {"status", std::string("poison")}}}));
	REQUIRE(player.spell_uses["wound_cleansing"] == 51);
}

TEST_CASE("a spell levels up after twenty uses", "[unit][battle]") {
	auto engine = new_engine(rpg::testing::test_data());
	fixed_attack(fight(engine), 1);
	auto& player = engine.state.player;
	player.spell_uses["brutal_strike"] = 19;
	player.mp = 1000;
	const auto events = engine.step(application::Cast{"brutal_strike"});
	REQUIRE(rpg::testing::contains_event(
	    events, Event{"spell_level_up", {{"spellId", std::string("brutal_strike")}, {"level", std::int64_t{2}}}}));
	REQUIRE(find_event(events, "spell_cast")->integer("mana") == 20);
}

TEST_CASE("the magic level grows with mana spent", "[unit][battle]") {
	const auto& data = rpg::testing::test_data();
	auto engine = new_engine(data);
	fixed_attack(fight(engine), 1);
	auto& player = engine.state.player;
	player.mana_spent = data.balance.magic_level_base - 1;
	player.mp = 1000;
	const auto events = engine.step(application::Cast{"brutal_strike"});
	REQUIRE(rpg::testing::contains_event(events, Event{"magic_level_up", {{"magicLevel", std::int64_t{2}}}}));
}

TEST_CASE("an immune monster takes no damage", "[unit][battle]") {
	const auto& data = rpg::testing::test_data();
	auto engine = new_engine(data, "mage");
	fixed_attack(fight(engine), 1);
	engine.state.monster->creature_id = "fire_elemental";
	REQUIRE(data.creature("fire_elemental").resistance("fire") == 0);
	engine.state.player.mp = 1000;
	const auto events = engine.step(application::Cast{"flame_strike"});
	REQUIRE(find_event(events, "spell_cast")->integer("damage") == 0);
}

TEST_CASE("items add crit, dodge and leech", "[unit][battle]") {
	// Calm monsters: a monster dodge or parry would skip the leech of the last attack.
	const domain::GameData data = rpg::testing::calm(rpg::testing::with_test_items(rpg::testing::test_data()));
	auto engine = new_engine(data);
	auto& player = engine.state.player;
	player.equipment.insert_or_assign("ring", domain::ItemInstance{99, "test_ring", "common", 0, {}});
	const auto sheet = domain::build_sheet(player, data);
	REQUIRE(sheet.crit_chance == data.balance.caps.crit_chance);
	REQUIRE(sheet.dodge == data.balance.caps.dodge);
	fixed_attack(fight(engine), 5);
	std::int64_t crits = 0;
	std::int64_t dodges = 0;
	for (int i = 0; i < 60; ++i) {
		engine.state.player.hp = 100;
		for (const auto& event : engine.step(application::Attack{})) {
			crits += event.type == "player_attacked" && event.flag("crit") ? 1 : 0;
			dodges += event.type == "attack_dodged" ? 1 : 0;
		}
	}
	REQUIRE(crits > 0);
	REQUIRE(dodges > 0);

	// Leech: an amulet with both leech affixes returns part of the damage dealt.
	engine.state.player.equipment.insert_or_assign(
	    "amulet", domain::ItemInstance{98, "test_helmet", "common", 0, {{"lifeLeech", 25}, {"manaLeech", 25}}});
	engine.state.player.hp = 10;
	engine.state.player.mp = 0;
	const auto events = engine.step(application::Attack{});
	const Event* leeched = find_event(events, "leeched");
	REQUIRE(leeched != nullptr);
	REQUIRE(leeched->integer("hp") + leeched->integer("mp") > 0);
}

TEST_CASE("a potion restores and is consumed", "[unit][battle]") {
	const auto& data = rpg::testing::test_data();
	auto engine = new_engine(data);
	fixed_attack(fight(engine), 1);
	auto& player = engine.state.player;
	player.hp = 10;
	const auto before = player.potion_count("mana_potion");
	player.mp = 0;
	const auto events = engine.step(application::UsePotion{"mana_potion"});
	REQUIRE(find_event(events, "potion_used")->text("resource") == "mp");
	REQUIRE(player.potion_count("mana_potion") == before - 1);
	REQUIRE(player.mp > 0);
	REQUIRE(player.mp <= domain::build_sheet(player, data).max_mp);

	const auto hp_events = engine.step(application::UsePotion{"health_potion"});
	REQUIRE(find_event(hp_events, "potion_used")->text("resource") == "hp");
}

TEST_CASE("every vocation spell can be cast", "[unit][battle]") {
	const std::string vocation = GENERATE("warrior", "archer", "mage");
	const auto& data = rpg::testing::test_data();
	auto engine = new_engine(data, vocation);
	fixed_attack(fight(engine), 1);
	for (const std::string& spell_id : data.vocation(vocation).spells) {
		engine.state.player.mp = 10'000;
		engine.state.player.hp = 5;
		const auto events = engine.step(application::Cast{spell_id});
		REQUIRE(std::ranges::any_of(events, [&](const Event& event) {
			return (event.type == "spell_cast" || event.type == "spell_healed") && event.text("spellId") == spell_id;
		}));
	}
}

TEST_CASE("a level three attack spell can apply its status", "[unit][battle]") {
	const auto& data = rpg::testing::test_data();
	auto engine = new_engine(data);
	fixed_attack(fight(engine), 1);
	engine.state.player.spell_uses["brutal_strike"] = 50;
	bool applied = false;
	for (int i = 0; i < 60 && !applied; ++i) {
		engine.state.player.mp = 1000;
		engine.state.player.hp = 1000;
		const auto events = engine.step(application::Cast{"brutal_strike"});
		const Event* status = find_event(events, "status_applied");
		applied = status != nullptr && status->text("target") == "monster";
	}
	REQUIRE(applied);
	REQUIRE(engine.state.stats.statuses_applied.contains("bleed"));
}

TEST_CASE("a battle without a monster is a programming error", "[unit][battle]") {
	auto engine = new_engine(rpg::testing::test_data());
	fight(engine);
	engine.state.monster.reset();
	domain::Rng rng(1);
	application::Battle battle(rpg::testing::test_data(), rng, engine.state);
	REQUIRE_THROWS(battle.play_turn(application::Attack{}));
}
