#include <algorithm>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "infrastructure/art.hpp"
#include "presentation/controller.hpp"
#include "presentation/render.hpp"
#include "support/controller_fixture.hpp"

using namespace rpg;
using presentation::View;
using rpg::testing::make_controller;
using rpg::testing::start_run;

namespace {

std::vector<std::string> keys_of(presentation::Controller& controller) {
	std::vector<std::string> keys;
	for (const auto& option : controller.options()) {
		keys.push_back(option.key);
	}
	return keys;
}

std::vector<std::string> labels_of(presentation::Controller& controller) {
	std::vector<std::string> labels;
	for (const auto& option : controller.options()) {
		labels.push_back(option.label);
	}
	return labels;
}

bool any_contains(const std::vector<std::string>& lines, std::string_view text) {
	return std::ranges::any_of(lines, [&](const std::string& line) { return line.find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("name validation and editing", "[unit][controller]") {
	const rpg::testing::TempDir directory;
	auto controller = make_controller(directory.path());
	controller.press("2");
	controller.press("1");
	REQUIRE(controller.view == View::name);
	controller.press("enter");
	REQUIRE(controller.message == controller.t("new_run.name_invalid"));
	for (const char character : std::string("Abcdefghijklmnopqrstuvwxyz")) {
		controller.press(std::string(1, character));
	}
	REQUIRE(controller.input_buffer == "Abcdefghijklmnop");
	controller.press("backspace");
	REQUIRE(controller.input_prompt() == "> Abcdefghijklmno_");
	controller.press("escape");
	REQUIRE(controller.view == View::difficulty);
	controller.press("0");
	REQUIRE(controller.view == View::title);

	// Accented letters count as one character each and are removed whole.
	controller.press("2");
	controller.press("2");
	controller.press("J");
	controller.press("ã");
	REQUIRE(controller.input_buffer == "Jã");
	controller.press("backspace");
	REQUIRE(controller.input_buffer == "J");
	controller.press("\x01");
	REQUIRE(controller.input_buffer == "J");
}

TEST_CASE("the title without a save has no Continue", "[unit][controller]") {
	const rpg::testing::TempDir directory;
	auto controller = make_controller(directory.path());
	REQUIRE_FALSE(std::ranges::contains(keys_of(controller), "1"));
	controller.press("1");
	REQUIRE(controller.view == View::title);
	controller.press("0");
	REQUIRE(controller.exit_requested);
}

TEST_CASE("first launch asks the language; it can be switched from the title", "[unit][controller]") {
	const rpg::testing::TempDir directory;
	auto first = make_controller(directory.path(), "");
	REQUIRE(first.view == View::language);
	REQUIRE(first.header() == first.t("app.subtitle"));

	auto controller = make_controller(directory.path());
	controller.press("6");
	REQUIRE(controller.view == View::language);
	REQUIRE(controller.title() == controller.t("language.title"));
	controller.press("2");
	REQUIRE(controller.view == View::title);
	REQUIRE(controller.locale == "pt-BR");
	REQUIRE(controller.title() == "CLI Turn-Based RPG");
	REQUIRE(std::ranges::contains(labels_of(controller), "Sair"));

	// The choice is saved: the next launch skips the language screen.
	auto relaunched = make_controller(directory.path(), "");
	REQUIRE(relaunched.view == View::title);
	REQUIRE(relaunched.locale == "pt-BR");
}

TEST_CASE("merchant menus", "[unit][controller]") {
	const rpg::testing::TempDir directory;
	auto controller = make_controller(directory.path());
	start_run(controller);
	REQUIRE(controller.session.has_value());
	REQUIRE(controller.title() == controller.t("merchant.title_start"));
	REQUIRE(controller.body_lines().size() == 1);
	auto& player = controller.session->state().player;

	controller.press("2");
	REQUIRE(controller.body_lines() == std::vector<std::string>{controller.t("merchant.empty_bag")});
	controller.press("0");
	player.bag.push_back(domain::ItemInstance{900, "hand_axe", "rare", 0, {}});
	player.bag.push_back(domain::ItemInstance{901, "bow", "common", 0, {}});
	controller.press("3");
	REQUIRE(controller.title() == controller.t("merchant.equipment"));
	const auto labels = labels_of(controller);
	REQUIRE(any_contains(labels, "Hand Axe"));
	REQUIRE_FALSE(any_contains(labels, "Bow"));
	controller.press("1");
	REQUIRE((player.equipment.at("weapon").uid == 900 || player.equipment.at("weapon").uid == 1));
	controller.press("0");

	controller.press("2");
	const auto sell_keys = keys_of(controller);
	REQUIRE(sell_keys.back() == "0");
	controller.press(sell_keys.front());
	controller.press("0");

	controller.press("4");
	REQUIRE(controller.options().size() == controller.session->state().merchant_stock.size() + 1);
	player.gold = 0;
	controller.press("1");
	REQUIRE(controller.message == controller.t("error.not_enough_gold"));
	controller.press("0");

	controller.press("1");
	REQUIRE(controller.title() == controller.t("merchant.buy_potions"));
	controller.press("1");
	REQUIRE(controller.view == View::quantity);
	REQUIRE_THAT(controller.title(), Catch::Matchers::ContainsSubstring("Health Potion"));
	controller.press("x");
	controller.press("enter");
	REQUIRE(controller.view == View::buy_potions);
	controller.press("1");
	controller.press("escape");
	REQUIRE(controller.view == View::buy_potions);
	controller.press("0");

	controller.press("5");
	REQUIRE(controller.view == View::character);
	REQUIRE(any_contains(controller.body_lines(), "Equipment"));
	controller.press("0");
	REQUIRE(controller.view == View::merchant);
}

TEST_CASE("the character sheet lists stats, protections and slots", "[unit][controller]") {
	const rpg::testing::TempDir directory;
	auto controller = make_controller(directory.path());
	start_run(controller);
	auto& player = controller.session->state().player;
	player.equipment.insert_or_assign(
	    "ring", domain::ItemInstance{5, "sword", "common", 0, {{"protFire", 10}, {"dodge", 3}}});
	controller.press("5");
	const auto lines = controller.body_lines();
	REQUIRE(any_contains(lines, "10%"));
	REQUIRE(any_contains(lines, controller.t("stat.dodge")));
}

TEST_CASE("battle submenus and messages", "[unit][controller]") {
	const rpg::testing::TempDir directory;
	auto controller = make_controller(directory.path());
	start_run(controller, "Zed", "3");
	controller.press("0");
	REQUIRE(controller.view == View::battle);
	REQUIRE(controller.title() == controller.t("battle.title"));
	REQUIRE(controller.monster_view().has_value());
	REQUIRE(controller.player_view().has_value());
	REQUIRE_THAT(controller.header(), Catch::Matchers::ContainsSubstring("Seed 7"));

	controller.session->state().player.potions.clear();
	controller.press("3");
	REQUIRE(controller.title() == controller.t("battle.potions"));
	REQUIRE(controller.body_lines() == std::vector<std::string>{controller.t("battle.no_potions")});
	controller.press("0");

	controller.session->state().player.mp = 0;
	controller.press("2");
	REQUIRE(controller.title() == controller.t("battle.spells"));
	controller.press(presentation::list_key(0));
	REQUIRE(controller.message == controller.t("error.not_enough_mana"));
	controller.press("escape");
	controller.press("4");
	REQUIRE((controller.animation_cues == std::vector<std::string>{"attack"} || controller.animation_cues.empty()));
	controller.press("q");
	REQUIRE(controller.view == View::title);
	REQUIRE_FALSE(controller.session.has_value());
}

TEST_CASE("monster and player views describe the fight", "[unit][controller]") {
	const rpg::testing::TempDir directory;
	auto controller = make_controller(directory.path());
	start_run(controller);
	controller.press("0");
	auto& monster = *controller.session->state().monster;
	monster.statuses.push_back({"burn", 2, 3});
	controller.session->state().player.statuses.push_back({"poison", 4, 1});
	const auto monster_view = controller.monster_view();
	REQUIRE_THAT(monster_view->details, Catch::Matchers::ContainsSubstring("(2)"));
	const auto player_view = controller.player_view();
	REQUIRE_THAT(player_view->statuses, Catch::Matchers::ContainsSubstring("(4)"));
	REQUIRE_THAT(player_view->summary, Catch::Matchers::ContainsSubstring("Zed"));
	REQUIRE_THAT(player_view->xp, Catch::Matchers::StartsWith("XP 0"));
}

TEST_CASE("bestiary paging and reveal", "[unit][controller]") {
	const rpg::testing::TempDir directory;
	auto controller = make_controller(directory.path());
	auto& repository = *controller.services.repositories.profile;
	auto profile = repository.load();
	profile.bestiary["rat"] = application::BestiaryEntry{9, "2026-01-01T00:00:00Z"};
	profile.bestiary["bat"] = application::BestiaryEntry{1, "2026-01-01T00:00:00Z"};
	repository.save(profile);

	controller.press("4");
	const auto lines = controller.body_lines();
	REQUIRE(lines.size() == presentation::page_size + 2);
	REQUIRE(std::ranges::any_of(lines,
	    [](const std::string& line) { return line.starts_with("Rat") && line.find("weak") != std::string::npos; }));
	REQUIRE(std::ranges::any_of(lines,
	    [](const std::string& line) { return line.starts_with("Bat") && line.find("weak") == std::string::npos; }));
	controller.press("n");
	REQUIRE(controller.body_lines() != lines);
	for (int i = 0; i < 50; ++i) {
		controller.press("n");
	}
	REQUIRE(controller.body_lines().back().starts_with("Page 12/12"));
	controller.press("p");
	controller.press("0");

	controller.press("3");
	REQUIRE(controller.body_lines() == std::vector<std::string>{controller.t("hall.empty")});
	controller.press("0");
	controller.press("5");
	const auto achievements = controller.body_lines();
	REQUIRE(std::all_of(achievements.begin(), achievements.begin() + presentation::page_size,
	    [](const std::string& line) { return line.starts_with("[ ]"); }));
}

TEST_CASE("render helpers", "[unit][render]") {
	using presentation::bar;
	REQUIRE(bar(0, 100, 10) == "░░░░░░░░░░");
	REQUIRE(bar(1, 100, 10) == "█░░░░░░░░░");
	REQUIRE(bar(100, 100, 10) == "██████████");
	REQUIRE(bar(5, 0, 4) == "░░░░");
	REQUIRE(presentation::hp_color(60, 100) == "#5fd75f");
	REQUIRE(presentation::hp_color(30, 100) == "#ffd75f");
	REQUIRE(presentation::hp_color(10, 100) == "#ff5f5f");
	REQUIRE(presentation::list_key(0) == "1");
	REQUIRE(presentation::list_key(9) == "a");
	REQUIRE(presentation::list_index("a") == 9);
	REQUIRE_FALSE(presentation::list_index("!").has_value());
	REQUIRE_FALSE(presentation::list_index("ab").has_value());
	REQUIRE(presentation::element_color("fire") == "#ff5f5f");
	REQUIRE(presentation::rarity_color("legendary") == "#ffaf00");
	REQUIRE(presentation::rarity_color("mythic").empty());
	REQUIRE(presentation::utf8_length("ação") == 4);
}

TEST_CASE("art parsing", "[unit][art]") {
	using infrastructure::Frame;
	const auto animations = infrastructure::parse_art("@idle\n a\n%%\n b\n@hurt\n x\n");
	REQUIRE(animations == infrastructure::Animations{{"idle", {Frame{" a"}, Frame{" b"}}}, {"hurt", {Frame{" x"}}}});
	REQUIRE(infrastructure::frame_for(animations, "idle", 3) == Frame{" b"});
	REQUIRE(infrastructure::frame_for(animations, "attack", 0) == Frame{" a"});
	REQUIRE(infrastructure::frame_for({}, "idle", 0).empty());
	REQUIRE_THROWS_WITH(infrastructure::parse_art("oops"), Catch::Matchers::ContainsSubstring("before"));
	REQUIRE_THROWS_WITH(infrastructure::parse_art("%%"), Catch::Matchers::ContainsSubstring("separator"));

	infrastructure::ArtLibrary library(assets::embedded_shared());
	const auto& data = rpg::testing::test_data();
	for (const auto& creature : data.monsters) {
		REQUIRE_FALSE(infrastructure::frame_for(library.for_creature(creature), "idle", 0).empty());
	}
	for (const auto& creature : data.bosses) {
		REQUIRE_FALSE(infrastructure::frame_for(library.for_creature(creature), "idle", 0).empty());
	}
	REQUIRE(library.load_file("families", "unknown_family").empty());

	// Malformed art degrades to "no art".
	const assets::SharedFs broken{{"art/families/broken.txt", "no header"}};
	infrastructure::ArtLibrary broken_library(broken);
	REQUIRE(broken_library.load_file("families", "broken").empty());
}
