// M8 rules (docs/game-design.md §3, §6, §8, §9): enemy classes, monster dodge/parry/heal/crit, parry reflects, class
// drop tables, potion drops, spell level effects and the victory phase.
#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "application/engine.hpp"
#include "application/spawner.hpp"
#include "domain/character.hpp"
#include "domain/formulas.hpp"
#include "domain/rng.hpp"
#include "support/helpers.hpp"

using namespace rpg;
using application::Event;
using rpg::testing::calm;
using rpg::testing::new_engine;
using rpg::testing::types_of;
using rpg::testing::with_enemy_class;

namespace {

constexpr int max_turns = 300;

bool has_type(const std::vector<Event>& events, std::string_view type) {
	return std::ranges::contains(types_of(events), type);
}

std::vector<Event> single(Event event) { return {std::move(event)}; }

// Leaves the merchant and gives the monster one fixed attack and a lot of HP.
domain::MonsterInstance& fight(
    application::GameEngine& engine, std::int64_t damage = 1, std::string_view element = domain::element::physical) {
	domain::MonsterInstance& monster = rpg::testing::fight(engine);
	rpg::testing::fixed_attack(monster, damage, element);
	return monster;
}

// Finishes the current fight with melee hits (the monster is left with 1 HP before each hit).
std::vector<Event> kill(application::GameEngine& engine) {
	for (int turn = 0; turn < max_turns; ++turn) {
		if (!engine.state.monster.has_value()) {
			break;
		}
		engine.state.monster->hp = 1;
		engine.state.player.hp = 1'000'000;
		std::vector<Event> events = engine.step(application::Attack{});
		if (engine.state.phase != domain::Phase::battle) {
			return events;
		}
	}
	FAIL("the fight did not end");
	return {};
}

std::vector<Event> final_victory(application::GameEngine& engine) {
	engine.state.round = engine.data().balance.final_round - 1;
	fight(engine);
	return kill(engine);
}

std::string physical_resistant_100(const domain::GameData& data) {
	const auto found = std::ranges::find_if(
	    data.monsters, [](const domain::MonsterDef& monster) { return monster.resistance("physical") == 100; });
	REQUIRE(found != data.monsters.end());
	return found->id;
}

} // namespace

// ── spawn ─────────────────────────────────────────────────────────────────────────────────────────────────────────

TEST_CASE("an elite spawn scales stats and rewards", "[unit][arpg]") {
	const auto& data = rpg::testing::test_data();
	const domain::GameData normal_data = calm(data);
	const domain::GameData elite_data = rpg::testing::all_elites(normal_data);
	const auto& difficulty = data.balance.difficulty("normal");
	domain::Rng first(5);
	domain::Rng second(5);
	const auto [normal, normal_info] = application::spawn_monster(normal_data, first, 3, difficulty);
	const auto [elite, elite_info] = application::spawn_monster(elite_data, second, 3, difficulty);
	const auto& row = data.balance.enemy_class("elite");
	REQUIRE(normal.enemy_class == "normal");
	REQUIRE(elite.enemy_class == "elite");
	REQUIRE(elite.creature_id == normal.creature_id);
	REQUIRE(elite.max_hp == domain::pct(normal.max_hp, row.stat_pct));
	REQUIRE(elite.attacks.front().max == domain::pct(normal.attacks.front().max, row.stat_pct));
	REQUIRE(elite.xp == domain::pct(normal.xp, row.reward_pct));
	REQUIRE(elite.gold_max == domain::pct(normal.gold_max, row.reward_pct));
}

TEST_CASE("the elite roll follows the monster pick", "[unit][arpg]") {
	const auto& data = rpg::testing::test_data();
	const auto& difficulty = data.balance.difficulty("normal");
	domain::Rng rng(77);
	std::vector<std::string> classes;
	for (int i = 0; i < 300; ++i) {
		classes.push_back(application::spawn_monster(data, rng, 1 + i % 9, difficulty).first.enemy_class);
	}
	REQUIRE(std::set<std::string>(classes.begin(), classes.end()) == std::set<std::string>{"normal", "elite"});
	const auto elites = std::ranges::count(classes, "elite");
	REQUIRE(elites >= 30);
	REQUIRE(elites <= 90);
	domain::Rng boss_rng(1);
	REQUIRE(application::spawn_monster(data, boss_rng, 10, difficulty).first.enemy_class == "boss");
}

TEST_CASE("round_started reports the enemy class", "[unit][arpg]") {
	const domain::GameData data = rpg::testing::all_elites(rpg::testing::test_data());
	auto engine = new_engine(data);
	const Event started = engine.step(application::NextFight{}).front();
	REQUIRE(started.text("enemyClass") == "elite");
	REQUIRE(started.has("isBoss"));
	REQUIRE_FALSE(started.flag("isBoss"));
}

// ── monster dodge, parry, heal and crit ───────────────────────────────────────────────────────────────────────────

TEST_CASE("a monster dodge stops melee and spells", "[unit][arpg]") {
	const domain::GameData data = with_enemy_class(
	    calm(rpg::testing::test_data()), "normal", [&](domain::EnemyClassDef& row) { row.dodge = 100; });
	auto engine = new_engine(data);
	const domain::MonsterInstance& monster = fight(engine);
	auto events = engine.step(application::Attack{});
	REQUIRE(events.front() == Event{"monster_dodged"});
	REQUIRE_FALSE(has_type(events, "player_attacked"));
	REQUIRE(monster.hp == monster.max_hp);
	auto& player = engine.state.player;
	player.mp = 1000;
	events = engine.step(application::Cast{"brutal_strike"});
	REQUIRE(events.front() == Event{"monster_dodged"});
	REQUIRE(player.mp < 1000);
	REQUIRE(domain::count_of(player.spell_uses, "brutal_strike") == 1);
}

TEST_CASE("a monster parry reflects physical hits", "[unit][arpg]") {
	const domain::GameData data = with_enemy_class(
	    calm(rpg::testing::test_data()), "normal", [&](domain::EnemyClassDef& row) { row.parry = 100; });
	auto engine = new_engine(data);
	const domain::MonsterInstance& monster = fight(engine);
	auto& player = engine.state.player;
	REQUIRE_FALSE(has_type(engine.step(application::Defend{}), "monster_parried"));
	const std::int64_t hp = player.hp;
	const auto events = engine.step(application::Attack{});
	const Event& parried = events.front();
	REQUIRE(parried.type == "monster_parried");
	REQUIRE(parried.integer("reflected") >= 1);
	REQUIRE_FALSE(has_type(events, "leeched"));
	REQUIRE(monster.hp == monster.max_hp);
	std::int64_t hits = 0;
	std::int64_t regen = 0;
	for (const Event& event : events) {
		hits += event.type == "monster_attacked" ? event.integer("damage") : 0;
		regen += event.type == "regenerated" ? event.integer("hp") : 0;
	}
	REQUIRE(player.hp == hp - parried.integer("reflected") - hits + regen);
}

TEST_CASE("a monster parry reflect can kill the player", "[unit][arpg]") {
	const domain::GameData data = with_enemy_class(
	    calm(rpg::testing::test_data()), "normal", [&](domain::EnemyClassDef& row) { row.parry = 100; });
	auto engine = new_engine(data);
	const domain::MonsterInstance& monster = fight(engine);
	engine.state.player.hp = 1;
	const auto events = engine.step(application::Attack{});
	REQUIRE(types_of(events) == std::vector<std::string>{"monster_parried", "player_died"});
	REQUIRE(engine.state.phase == domain::Phase::game_over);
	REQUIRE(monster.hp == monster.max_hp);
}

TEST_CASE("a monster parry ignores non-physical spells", "[unit][arpg]") {
	const domain::GameData data = with_enemy_class(
	    calm(rpg::testing::test_data()), "normal", [&](domain::EnemyClassDef& row) { row.parry = 100; });
	auto engine = new_engine(data, "mage");
	fight(engine);
	engine.state.player.mp = 1000;
	const auto events = engine.step(application::Cast{"flame_strike"});
	REQUIRE_FALSE(has_type(events, "monster_parried"));
	REQUIRE(has_type(events, "spell_cast"));
}

TEST_CASE("a monster heals instead of attacking", "[unit][arpg]") {
	const domain::GameData data = with_enemy_class(
	    calm(rpg::testing::test_data()), "normal", [&](domain::EnemyClassDef& row) { row.heal = 100; });
	auto engine = new_engine(data);
	domain::MonsterInstance& monster = fight(engine);
	auto events = engine.step(application::Defend{});
	REQUIRE_FALSE(has_type(events, "monster_healed"));
	REQUIRE(has_type(events, "monster_attacked"));
	monster.hp = monster.max_hp - 5;
	events = engine.step(application::Defend{});
	REQUIRE(rpg::testing::contains_event(events, Event{"monster_healed", {{"amount", std::int64_t{5}}}}));
	REQUIRE_FALSE(has_type(events, "monster_attacked"));
	monster.hp = 100;
	events = engine.step(application::Defend{});
	const std::int64_t amount = domain::pct(monster.max_hp, data.balance.monster_heal_pct);
	REQUIRE(rpg::testing::contains_event(events, Event{"monster_healed", {{"amount", amount}}}));
}

TEST_CASE("a healing boss does not advance its pattern", "[unit][arpg]") {
	const domain::GameData data =
	    with_enemy_class(calm(rpg::testing::test_data()), "boss", [&](domain::EnemyClassDef& row) { row.heal = 100; });
	auto engine = new_engine(data);
	engine.state.round = 9;
	domain::MonsterInstance& boss = rpg::testing::fight(engine);
	boss.hp = boss.max_hp - 1;
	engine.state.player.hp = 1'000'000;
	engine.step(application::Defend{});
	REQUIRE(engine.state.monster->boss_actions == 0);
}

TEST_CASE("a monster crit multiplies the raw damage", "[unit][arpg]") {
	const auto [crit, expected] = GENERATE(table<std::int64_t, std::int64_t>({{0, 100}, {100, 150}}));
	const domain::GameData data = with_enemy_class(
	    calm(rpg::testing::test_data()), "normal", [&](domain::EnemyClassDef& row) { row.crit = crit; });
	auto engine = new_engine(data);
	fight(engine, 100, domain::element::fire);
	engine.state.player.hp = 1000;
	const auto events = engine.step(application::Attack{});
	const Event* hit = rpg::testing::find_event(events, "monster_attacked");
	REQUIRE(hit != nullptr);
	REQUIRE(hit->flag("crit") == (crit == 100));
	REQUIRE(hit->integer("damage") == expected);
}

TEST_CASE("a player parry reflects damage to the monster", "[unit][arpg]") {
	domain::GameData data = calm(rpg::testing::test_data());
	data.items.push_back(
	    domain::ItemDef{"test_parry_shield", "Test Shield", "shield", "shield", 0, std::nullopt, {{"parry", 100}}, 10});
	data.index();
	data.balance.caps = domain::Caps{data.balance.caps.crit_chance, 0, 100, 25, 75};
	auto engine = new_engine(data);
	engine.state.player.equipment.insert_or_assign(
	    "shield", domain::ItemInstance{90, "test_parry_shield", "common", 0, {}});
	REQUIRE(domain::build_sheet(engine.state.player, data).parry == 100);
	domain::MonsterInstance& monster = fight(engine, 50);
	auto events = engine.step(application::Defend{});
	REQUIRE(rpg::testing::contains_event(
	    events, Event{"attack_parried", {{"attackId", std::string("test_hit")}, {"reflected", std::int64_t{10}}}}));
	REQUIRE(monster.hp == monster.max_hp - 10);
	REQUIRE(engine.state.stats.parries == 1);
	monster.hp = 5;
	events = engine.step(application::Defend{});
	REQUIRE(has_type(events, "monster_killed"));
	REQUIRE(engine.state.phase == domain::Phase::merchant);
}

// ── victory, drops and potions ────────────────────────────────────────────────────────────────────────────────────

TEST_CASE("the normal drop table", "[unit][arpg]") {
	const domain::GameData always = with_enemy_class(
	    calm(rpg::testing::test_data()), "normal", [&](domain::EnemyClassDef& row) { row.drop_chance_pct = 100; });
	auto engine = new_engine(always);
	fight(engine);
	auto events = kill(engine);
	std::vector<Event> drops;
	std::ranges::copy_if(events, std::back_inserter(drops), [](const Event& e) { return e.type == "item_dropped"; });
	REQUIRE(drops.size() == 1);
	REQUIRE((drops.front().text("rarity") == "common" || drops.front().text("rarity") == "rare"));
	REQUIRE_FALSE(has_type(events, "potion_dropped"));

	const domain::GameData never = with_enemy_class(
	    calm(rpg::testing::test_data()), "normal", [&](domain::EnemyClassDef& row) { row.drop_chance_pct = 0; });
	auto other = new_engine(never);
	fight(other);
	REQUIRE_FALSE(has_type(kill(other), "item_dropped"));
}

TEST_CASE("an elite drops a good item and maybe a potion", "[unit][arpg]") {
	const domain::GameData data = rpg::testing::all_elites(with_enemy_class(
	    calm(rpg::testing::test_data()), "elite", [&](domain::EnemyClassDef& row) { row.potion_drop_pct = 100; }));
	auto engine = new_engine(data);
	fight(engine);
	const domain::CountMap before = engine.state.player.potions;
	const auto events = kill(engine);
	std::vector<Event> drops;
	std::ranges::copy_if(events, std::back_inserter(drops), [](const Event& e) { return e.type == "item_dropped"; });
	REQUIRE(drops.size() == 1);
	REQUIRE((drops.front().text("rarity") == "rare" || drops.front().text("rarity") == "legendary"));
	const Event* potion = rpg::testing::find_event(events, "potion_dropped");
	REQUIRE(potion != nullptr);
	const std::string potion_id = potion->text("potionId");
	REQUIRE(data.potion(potion_id).unlock_round <= 1);
	REQUIRE(engine.state.player.potion_count(potion_id) == domain::count_of(before, potion_id) + 1);
	REQUIRE(domain::count_of(engine.state.stats.potions_dropped, potion_id) == 1);
	REQUIRE(engine.state.stats.elites_killed == 1);
	REQUIRE(rpg::testing::find_event(events, "monster_killed")->text("enemyClass") == "elite");
}

TEST_CASE("a potion drop without unlocked potions consumes nothing", "[unit][arpg]") {
	domain::GameData data =
	    with_enemy_class(calm(rpg::testing::test_data()), "normal", [&](domain::EnemyClassDef& row) {
		    row.drop_chance_pct = 0;
		    row.potion_drop_pct = 100;
	    });
	for (domain::PotionDef& potion : data.potions) {
		potion.unlock_round = 99;
	}
	auto engine = new_engine(data);
	fight(engine);
	REQUIRE_FALSE(has_type(kill(engine), "potion_dropped"));
}

TEST_CASE("a boss drops several top items", "[unit][arpg]") {
	const domain::GameData data = calm(rpg::testing::test_data());
	auto engine = new_engine(data);
	engine.state.round = 9;
	fight(engine);
	const auto events = kill(engine);
	std::int64_t drops = 0;
	for (const Event& event : events) {
		if (event.type == "item_dropped") {
			drops += 1;
			REQUIRE((event.text("rarity") == "legendary" || event.text("rarity") == "mythic"));
		}
	}
	REQUIRE(drops == data.balance.enemy_class("boss").drops);
}

TEST_CASE("a full bag auto-sells drops with the class table", "[unit][arpg]") {
	const domain::GameData data = with_enemy_class(
	    calm(rpg::testing::test_data()), "normal", [&](domain::EnemyClassDef& row) { row.drop_chance_pct = 100; });
	auto engine = new_engine(data);
	auto& player = engine.state.player;
	for (std::int64_t i = 0; i < data.balance.bag_capacity; ++i) {
		player.bag.push_back(domain::ItemInstance{500 + i, "sword", "common", 0, {}});
	}
	fight(engine);
	REQUIRE(has_type(kill(engine), "item_auto_sold"));
	REQUIRE(std::cmp_equal(player.bag.size(), data.balance.bag_capacity));
}

TEST_CASE("beating the final boss enters the victory phase", "[unit][arpg]") {
	const domain::GameData data = calm(rpg::testing::test_data());
	auto engine = new_engine(data);
	const auto events = final_victory(engine);
	REQUIRE(events.back() == Event{"run_won", {{"round", data.balance.final_round}}});
	REQUIRE_FALSE(has_type(events, "merchant_entered"));
	REQUIRE(engine.state.phase == domain::Phase::victory);
	REQUIRE(engine.state.won);
	const auto rng_state = engine.rng_state();
	const auto stock = engine.state.merchant_stock;
	const std::vector<application::Command> rejected{application::Attack{}, application::Defend{},
	    application::NextFight{}, application::BuyPotion{"health_potion", 1}, application::Equip{1},
	    application::SellItem{1}};
	for (const application::Command& command : rejected) {
		REQUIRE(engine.step(command) == single(application::error_event("invalid_phase")));
	}
	REQUIRE(engine.rng_state() == rng_state);
	REQUIRE(engine.state.merchant_stock == stock);
	REQUIRE(engine.step(application::EndRun{}) == single(Event{"run_ended", {{"won", true}}}));
	REQUIRE(engine.state.phase == domain::Phase::game_over);
	REQUIRE_FALSE(engine.state.death_cause.has_value());
	REQUIRE(engine.step(application::ContinueRun{}) == single(application::error_event("invalid_phase")));
}

TEST_CASE("continue_run enters the merchant", "[unit][arpg]") {
	const domain::GameData data = calm(rpg::testing::test_data());
	auto engine = new_engine(data);
	final_victory(engine);
	REQUIRE(engine.step(application::ContinueRun{}) ==
	        single(Event{"merchant_entered", {{"round", data.balance.final_round}}}));
	REQUIRE(engine.state.phase == domain::Phase::merchant);
	REQUIRE(std::cmp_equal(engine.state.merchant_stock.size(), data.balance.merchant_stock_size));
	engine.step(application::NextFight{});
	REQUIRE(engine.state.round == data.balance.final_round + 1);
	REQUIRE(engine.state.won);
}

TEST_CASE("victory commands are rejected outside the victory phase", "[unit][arpg]") {
	auto engine = new_engine(rpg::testing::test_data());
	REQUIRE(engine.step(application::EndRun{}) == single(application::error_event("invalid_phase")));
	REQUIRE(engine.step(application::ContinueRun{}) == single(application::error_event("invalid_phase")));
}

// ── spell levels ──────────────────────────────────────────────────────────────────────────────────────────────────

TEST_CASE("the spell level effect scales damage", "[unit][arpg]") {
	const auto [uses, effect] = GENERATE(table<std::int64_t, std::int64_t>({{0, 100}, {20, 150}, {50, 200}}));
	const domain::GameData data = calm(rpg::testing::test_data());
	auto engine = new_engine(data);
	fight(engine);
	engine.state.monster->creature_id = physical_resistant_100(data);
	auto& player = engine.state.player;
	player.spell_uses["brutal_strike"] = uses;
	player.mp = 1000;
	domain::Rng rng(engine.rng_state());
	const domain::SpellDef& spell = data.spell("brutal_strike");
	const std::int64_t bonus = player.level * spell.per_level + player.magic_level * spell.per_magic_level;
	const std::int64_t base = rng.roll(spell.min + bonus, spell.max + bonus);
	const auto events = engine.step(application::Cast{"brutal_strike"});
	const Event* cast = rpg::testing::find_event(events, "spell_cast");
	REQUIRE(cast != nullptr);
	REQUIRE(cast->integer("damage") == std::max<std::int64_t>(1, domain::pct(base, effect)));
}

TEST_CASE("the spell level effect scales healing", "[unit][arpg]") {
	const domain::GameData data = calm(rpg::testing::test_data());
	auto engine = new_engine(data);
	fight(engine);
	auto& player = engine.state.player;
	player.spell_uses["wound_cleansing"] = 20;
	player.mp = 1000;
	player.hp = 1;
	domain::Rng rng(engine.rng_state());
	const domain::SpellDef& spell = data.spell("wound_cleansing");
	const std::int64_t bonus = player.level * spell.per_level + player.magic_level * spell.per_magic_level;
	const std::int64_t expected = domain::pct(rng.roll(spell.min + bonus, spell.max + bonus), 150);
	const std::int64_t room = domain::build_sheet(player, data).max_hp - player.hp;
	const auto events = engine.step(application::Cast{"wound_cleansing"});
	const Event* healed = rpg::testing::find_event(events, "spell_healed");
	REQUIRE(healed != nullptr);
	REQUIRE(healed->integer("amount") == std::min(expected, room));
}
