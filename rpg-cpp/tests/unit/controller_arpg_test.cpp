// M8 screens of the UI controller: settings, auto-equip step, auto-battle, victory and the equipment screen.
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "domain/character.hpp"
#include "presentation/render.hpp"
#include "support/controller_fixture.hpp"

using namespace rpg;
using presentation::Controller;
using presentation::View;
using rpg::testing::make_controller;
using rpg::testing::start_run;

namespace {

std::vector<std::string> labels_of(Controller& controller) {
	std::vector<std::string> labels;
	for (const auto& option : controller.options()) {
		labels.push_back(option.label);
	}
	return labels;
}

std::vector<std::string> keys_of(Controller& controller) {
	std::vector<std::string> keys;
	for (const auto& option : controller.options()) {
		keys.push_back(option.key);
	}
	return keys;
}

std::string key_of(Controller& controller, std::string_view prefix) {
	for (const auto& option : controller.options()) {
		if (option.label.starts_with(prefix)) {
			return option.key;
		}
	}
	FAIL("no option starting with " << prefix);
	return {};
}

Controller battle_controller(const std::filesystem::path& directory) {
	Controller controller = make_controller(directory);
	start_run(controller);
	controller.press("0");
	REQUIRE(controller.view == View::battle);
	return controller;
}

// Plays the final fight (round 100) with calm monsters until the victory screen.
Controller victory_controller(const std::filesystem::path& directory, const domain::GameData& data) {
	Controller controller = make_controller(directory, "en", data);
	start_run(controller);
	controller.session->state().round = data.balance.final_round - 1;
	controller.press("0");
	for (int turn = 0; turn < 50 && controller.session->state().monster.has_value(); ++turn) {
		controller.session->state().monster->hp = 1;
		controller.press("1");
	}
	REQUIRE(controller.view == View::victory);
	return controller;
}

Controller equipment_controller(const std::filesystem::path& directory, const domain::GameData& data) {
	Controller controller = make_controller(directory, "en", data);
	start_run(controller);
	controller.press("3");
	REQUIRE(controller.view == View::equipment);
	return controller;
}

} // namespace

TEST_CASE("settings toggle and persist", "[unit][controller_arpg]") {
	const rpg::testing::TempDir directory;
	auto controller = make_controller(directory.path());
	controller.press("6");
	const auto labels = labels_of(controller);
	REQUIRE(labels[1] == "Auto-equip on new runs: Off");
	REQUIRE(labels[2] == "Auto-battle speed: 1x");
	controller.press("2");
	controller.press("3");
	REQUIRE(controller.settings == infrastructure::Settings{std::nullopt, true, 2});
	REQUIRE(controller.auto_battle_interval_ms() == presentation::auto_battle_base_ms / 2);
	REQUIRE(controller.services.settings->load() == infrastructure::Settings{std::nullopt, true, 2});
	controller.press("3");
	REQUIRE(controller.settings.battle_speed == 1);
	controller.press("1");
	controller.press("1");
	REQUIRE(controller.view == View::settings);
	REQUIRE(controller.services.settings->load() == infrastructure::Settings{"en", true, 1});
}

TEST_CASE("the new-run auto-equip step marks the default", "[unit][controller_arpg]") {
	const rpg::testing::TempDir directory;
	infrastructure::SettingsRepository(directory.path()).save(infrastructure::Settings{"en", true, 1});
	auto controller = make_controller(directory.path());
	start_run(controller, "Zed", "1", "0");
	REQUIRE(controller.view == View::vocation);
	controller.press("1");
	REQUIRE(controller.view == View::auto_equip);
	REQUIRE(controller.title() == controller.t("new_run.auto_equip"));
	const auto labels = labels_of(controller);
	REQUIRE(labels[0].ends_with("(default)"));
	REQUIRE_FALSE(labels[1].ends_with("(default)"));
	controller.press("1");
	REQUIRE(controller.view == View::merchant);
	REQUIRE(controller.session->state().config.auto_equip);
}

TEST_CASE("the auto-battle menu and the instant run", "[unit][controller_arpg]") {
	const rpg::testing::TempDir directory;
	auto controller = battle_controller(directory.path());
	controller.press("5");
	REQUIRE(controller.view == View::auto_battle);
	REQUIRE(controller.title() == controller.t("auto_battle.title"));
	REQUIRE(keys_of(controller) == std::vector<std::string>{"1", "2", "3", "0"});
	controller.press("0");
	REQUIRE(controller.view == View::battle);
	controller.press("5");
	controller.press("3");
	REQUIRE(controller.auto_battle_active());
	REQUIRE(
	    controller.log.back() == controller.t("auto_battle.started", {{"mode", controller.t("auto_battle.balanced")}}));
	const std::int64_t turn = controller.session->state().turn;
	controller.press("1");
	REQUIRE(controller.session->state().turn == turn);
	controller.run_auto_battle();
	REQUIRE_FALSE(controller.auto_battle_active());
	REQUIRE(controller.session->state().phase != domain::Phase::battle);
	REQUIRE((controller.view == View::merchant || controller.view == View::game_over));
	REQUIRE_FALSE(controller.auto_battle_step());
}

TEST_CASE("the auto-battle steps one turn at a time", "[unit][controller_arpg]") {
	const rpg::testing::TempDir directory;
	auto controller = battle_controller(directory.path());
	auto& state = controller.session->state();
	state.monster->hp = 1'000'000;
	state.monster->max_hp = 1'000'000;
	controller.press("5");
	controller.press("1");
	const std::int64_t turn = state.turn;
	state.player.hp = 1'000'000;
	REQUIRE(controller.auto_battle_step());
	REQUIRE(state.turn == turn + 1);
}

TEST_CASE("the victory screen ends the run", "[unit][controller_arpg]") {
	const rpg::testing::TempDir directory;
	const domain::GameData data = rpg::testing::calm(rpg::testing::test_data());
	auto controller = victory_controller(directory.path(), data);
	REQUIRE(controller.title() == controller.t("victory.title"));
	REQUIRE(controller.body_lines().front().find("Ferumbras") != std::string::npos);
	controller.press("9");
	REQUIRE(controller.view == View::victory);
	controller.press("1");
	REQUIRE(controller.view == View::game_over);
	REQUIRE(controller.title() == controller.t("gameover.title_won"));
	REQUIRE(controller.body_lines().front().find("won the run") != std::string::npos);
	controller.press("2");
	controller.press("3");
	REQUIRE(controller.body_lines().front().find("WON") != std::string::npos);
}

TEST_CASE("the victory screen continues the run", "[unit][controller_arpg]") {
	const rpg::testing::TempDir directory;
	const domain::GameData data = rpg::testing::calm(rpg::testing::test_data());
	auto controller = victory_controller(directory.path(), data);
	controller.press("2");
	REQUIRE(controller.view == View::merchant);
	REQUIRE(controller.session->state().won);
}

TEST_CASE("continuing a saved victory returns to the victory screen", "[unit][controller_arpg]") {
	const rpg::testing::TempDir directory;
	const domain::GameData data = rpg::testing::calm(rpg::testing::test_data());
	auto controller = victory_controller(directory.path(), data);
	controller.session.reset();
	controller.view = View::title;
	controller.press("1");
	REQUIRE(controller.view == View::victory);
}

TEST_CASE("the equipment screen lists every slot and the bag", "[unit][controller_arpg]") {
	const rpg::testing::TempDir directory;
	const domain::GameData data = rpg::testing::with_test_items(rpg::testing::test_data());
	auto controller = equipment_controller(directory.path(), data);
	auto& player = controller.session->state().player;
	player.bag.push_back(domain::ItemInstance{900, "test_axe", "rare", 0, {}});
	player.bag.push_back(domain::ItemInstance{901, "test_rod", "common", 0, {}});
	player.bag.push_back(domain::ItemInstance{902, "test_helmet", "legendary", 9, {}});
	const auto lines = controller.body_lines();
	const auto colors = controller.body_colors();
	REQUIRE(lines.size() == colors.size());
	const domain::ItemInstance starter = player.equipment.at("weapon");
	REQUIRE(lines[0] == "EQUIPPED · total score " + std::to_string(domain::item_score(starter, data)));
	REQUIRE(lines[1].starts_with("Weapon: Sword [Common] · Lv 1"));
	REQUIRE(lines[2] == "Shield: - empty -");
	REQUIRE(colors[2] == presentation::style_warning);
	REQUIRE(std::ranges::count_if(
	            lines, [](const std::string& line) { return line.find("- empty -") != std::string::npos; }) == 7);
	REQUIRE(lines.back() == "BAG (usable)");
	const auto options = controller.options();
	REQUIRE(keys_of(controller) == std::vector<std::string>{"1", "2", "3", "0"});
	const auto& axe = options[0];
	const auto& helmet = options[1];
	const auto& slot = options[2];
	const std::int64_t delta = domain::item_score(domain::ItemInstance{900, "test_axe", "rare", 0, {}}, data) -
	                           domain::item_score(starter, data);
	REQUIRE(axe.detail == presentation::format_delta(delta));
	REQUIRE(axe.detail_color == presentation::style_gain);
	REQUIRE(axe.color == "rare");
	REQUIRE(helmet.color == presentation::style_dim);
	REQUIRE(helmet.label.ends_with("requires Lv 37"));
	REQUIRE(slot.label == "Weapon: Sword [Common]");
}

TEST_CASE("the comparison shows stat and score deltas", "[unit][controller_arpg]") {
	const rpg::testing::TempDir directory;
	const domain::GameData data = rpg::testing::with_test_items(rpg::testing::test_data());
	auto controller = equipment_controller(directory.path(), data);
	auto& player = controller.session->state().player;
	player.equipment.insert_or_assign(
	    "helmet", domain::ItemInstance{800, "test_helmet", "common", 0, {domain::AffixRoll{"dodge", 3}}});
	player.bag.push_back(domain::ItemInstance{801, "test_helmet", "rare", 0, {domain::AffixRoll{"critChance", 2}}});
	controller.press("1");
	REQUIRE(controller.view == View::compare);
	REQUIRE(controller.title() == "Helmet: Test Helmet → Test Helmet");
	const auto lines = controller.body_lines();
	const auto line_colors = controller.body_colors();
	std::map<std::string, std::string> colors;
	for (std::size_t i = 0; i < lines.size(); ++i) {
		colors.emplace(lines[i], line_colors[i]);
	}
	REQUIRE(colors.at("Armor: 10 → 15 (+5)") == presentation::style_gain);
	REQUIRE(colors.at("Max HP: 50 → 75 (+25)") == presentation::style_gain);
	REQUIRE(colors.at("Critical chance: 0 → 2 (+2)") == presentation::style_gain);
	REQUIRE(colors.at("Dodge: 3 → 0 (-3)") == presentation::style_loss);
	REQUIRE(colors.at("Affixes gained: +2 Critical chance") == presentation::style_gain);
	REQUIRE(colors.at("Affixes lost: +3 Dodge") == presentation::style_loss);
	REQUIRE(std::ranges::any_of(lines, [](const std::string& line) { return line.starts_with("Score: "); }));
	REQUIRE(keys_of(controller) == std::vector<std::string>{"1", "0"});
	controller.press("1");
	REQUIRE(controller.view == View::equipment);
	REQUIRE(player.equipment.at("helmet").uid == 801);
}

TEST_CASE("the comparison warns about the required level", "[unit][controller_arpg]") {
	const rpg::testing::TempDir directory;
	const domain::GameData data = rpg::testing::with_test_items(rpg::testing::test_data());
	auto controller = equipment_controller(directory.path(), data);
	auto& state = controller.session->state();
	state.player.bag.push_back(domain::ItemInstance{810, "test_axe", "common", 3, {}});
	controller.press("1");
	REQUIRE(controller.body_colors().back() == presentation::style_loss);
	REQUIRE(controller.body_lines().back() == "Requires level 13 (you are level 1).");
	controller.press("1");
	REQUIRE(controller.message == controller.t("error.level_too_low"));
	REQUIRE(controller.view == View::equipment);
	controller.press("0");
	controller.press("3");
	controller.press(key_of(controller, "Weapon"));
	REQUIRE(controller.view == View::equipped_slot);
	REQUIRE(controller.title() == "Weapon");
	REQUIRE(controller.body_lines()[1] == "Attack: 6");
	controller.press("1");
	REQUIRE(controller.view == View::equipment);
	REQUIRE_FALSE(state.player.equipment.contains("weapon"));
	controller.press("0");
}

TEST_CASE("the compare and slot views survive missing items", "[unit][controller_arpg]") {
	const rpg::testing::TempDir directory;
	const domain::GameData data = rpg::testing::with_test_items(rpg::testing::test_data());
	auto controller = equipment_controller(directory.path(), data);
	REQUIRE(controller.body_lines().back() == controller.t("equipment.bag_empty"));
	controller.view = View::compare;
	REQUIRE(controller.body_lines().empty());
	REQUIRE(controller.title() == controller.t("merchant.equipment"));
	controller.view = View::equipped_slot;
	controller.press("0");
	controller.view = View::equipped_slot;
	controller.session->state().player.equipment.clear();
	REQUIRE(controller.body_lines() == std::vector<std::string>{controller.t("equipment.empty")});
}

TEST_CASE("the monster view and the Hall of Fame markers", "[unit][controller_arpg]") {
	const rpg::testing::TempDir directory;
	const domain::GameData data = rpg::testing::all_elites(rpg::testing::test_data());
	auto controller = make_controller(directory.path(), "en", data);
	auto& repository = *controller.services.repositories.profile;
	application::Profile profile = repository.load();
	profile.hall_of_fame.push_back(
	    application::HallOfFameEntry{"r", "Ana", "mage", "hard", 100, 50, "2026-01-01T00:00:00Z", true});
	repository.save(profile);
	controller.press("3");
	REQUIRE(controller.body_lines().front().find("WON") != std::string::npos);
	controller.press("0");
	start_run(controller);
	controller.press("0");
	const auto monster = controller.monster_view();
	REQUIRE(monster.has_value());
	REQUIRE(monster->enemy_class == "elite");
	REQUIRE(std::ranges::any_of(
	    controller.log, [](const std::string& line) { return line.find("ELITE") != std::string::npos; }));
}
