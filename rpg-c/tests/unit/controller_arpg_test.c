// M8 screens of the UI controller: settings, auto-equip step, auto-battle, victory and the equipment screen.

#include "domain/character.h"
#include "presentation/render.h"
#include "support/controller_fixture.h"

#include <stdio.h>
#include <stdlib.h>

#define SUITE "unit/controller_arpg"

static bool starts_with(const char *text, const char *prefix) { return strncmp(text, prefix, strlen(prefix)) == 0; }

static bool ends_with(const char *text, const char *suffix) {
	size_t length = strlen(text);
	size_t suffix_length = strlen(suffix);
	return length >= suffix_length && strcmp(text + length - suffix_length, suffix) == 0;
}

static Settings load_settings(const char *dir) {
	Settings settings;
	char error[256];
	if (settings_load(dir, &settings, error, sizeof(error)) != LOAD_OK) {
		fatal("%s", error);
	}
	return settings;
}

static void check_settings(Settings actual, const char *locale, bool auto_equip, int64_t battle_speed) {
	CHECK(actual.has_locale == (locale != NULL));
	if (locale != NULL && actual.has_locale) {
		CHECK_STR(actual.locale, locale);
	}
	CHECK(actual.auto_equip == auto_equip);
	CHECK_INT(actual.battle_speed, battle_speed);
}

TEST(SUITE, settings_toggle_and_persist) {
	char dir[TEST_PATH_SIZE];
	Controller controller;
	open_controller(&controller, dir, test_data());
	controller_press(&controller, "6");
	CHECK_STR(option_at(&controller, 1)->label, "Auto-equip on new runs: Off");
	CHECK_STR(option_at(&controller, 2)->label, "Auto-battle speed: 1x");
	controller_press(&controller, "2");
	controller_press(&controller, "3");
	check_settings(controller.settings, NULL, true, 2);
	CHECK_INT(controller_auto_battle_interval_ms(&controller), AUTO_BATTLE_BASE_MS / 2);
	check_settings(load_settings(dir), NULL, true, 2);
	controller_press(&controller, "3");
	CHECK_INT(controller.settings.battle_speed, 1);
	controller_press(&controller, "1");
	controller_press(&controller, "1");
	CHECK_INT(controller.view, VIEW_SETTINGS);
	check_settings(load_settings(dir), "en", true, 1);
	close_controller(&controller, dir);
}

TEST(SUITE, new_run_auto_equip_step_marks_the_default) {
	char dir[TEST_PATH_SIZE];
	Controller controller;
	temp_dir_create(dir);
	Settings saved = {.has_locale = true, .locale = "en", .auto_equip = true, .battle_speed = 1};
	settings_save(dir, &saved);
	make_controller(&controller, test_data(), dir, "en");
	start_run(&controller, "Zed", "1", "0");
	CHECK_INT(controller.view, VIEW_VOCATION);
	controller_press(&controller, "1");
	CHECK_INT(controller.view, VIEW_AUTO_EQUIP);
	CHECK_STR(controller_title(&controller), tr(&controller, "new_run.auto_equip"));
	CHECK(ends_with(option_at(&controller, 0)->label, "(default)"));
	CHECK(!ends_with(option_at(&controller, 1)->label, "(default)"));
	controller_press(&controller, "1");
	CHECK_INT(controller.view, VIEW_MERCHANT);
	REQUIRE(controller.has_session);
	CHECK(controller.session.engine.state.config.auto_equip);
	close_controller(&controller, dir);
}

// A new run taken to its first fight.
static void open_battle(Controller *controller, char dir[TEST_PATH_SIZE]) {
	open_controller(controller, dir, test_data());
	start_default_run(controller);
	controller_press(controller, "0");
	CHECK_INT(controller->view, VIEW_BATTLE);
}

TEST(SUITE, auto_battle_menu_and_instant_run) {
	char dir[TEST_PATH_SIZE];
	Controller controller;
	open_battle(&controller, dir);
	controller_press(&controller, "5");
	CHECK_INT(controller.view, VIEW_AUTO_BATTLE);
	char *keys = option_keys(&controller);
	CHECK_STR(keys, "1 2 3 0");
	free(keys);
	controller_press(&controller, "0");
	CHECK_INT(controller.view, VIEW_BATTLE);
	controller_press(&controller, "5");
	controller_press(&controller, "3");
	CHECK(controller.auto_battle_active);
	const char *mode = tr(&controller, "auto_battle.balanced");
	CHECK_STR(last_log_line(&controller), CONTROLLER_T(&controller, "auto_battle.started", P_STR("mode", mode)));
	REQUIRE(controller.has_session);
	const RunState *state = &controller.session.engine.state;
	int64_t turn = state->turn;
	controller_press(&controller, "1");
	CHECK_INT(state->turn, turn);
	controller_run_auto_battle(&controller);
	CHECK(!controller.auto_battle_active);
	CHECK(state->phase != PHASE_BATTLE);
	CHECK(controller.view == VIEW_MERCHANT || controller.view == VIEW_GAME_OVER);
	CHECK(!controller_auto_battle_step(&controller));
	close_controller(&controller, dir);
}

TEST(SUITE, auto_battle_steps_one_turn_at_a_time) {
	char dir[TEST_PATH_SIZE];
	Controller controller;
	open_battle(&controller, dir);
	REQUIRE(controller.has_session);
	RunState *state = &controller.session.engine.state;
	REQUIRE(state->has_monster);
	state->monster.hp = 1000000;
	state->monster.max_hp = 1000000;
	controller_press(&controller, "5");
	controller_press(&controller, "1");
	int64_t turn = state->turn;
	state->player.hp = 1000000;
	CHECK(controller_auto_battle_step(&controller));
	CHECK_INT(state->turn, turn + 1);
	close_controller(&controller, dir);
}

// A calm run that just beat the final boss. `data` receives the copy the controller reads (release it last).
static void open_victory(Controller *controller, char dir[TEST_PATH_SIZE], GameData *data) {
	*data = data_copy();
	calm(data);
	open_controller(controller, dir, data);
	start_default_run(controller);
	if (!controller->has_session) {
		return;
	}
	RunState *state = &controller->session.engine.state;
	state->round = data->balance.final_round - 1;
	controller_press(controller, "0");
	for (int i = 0; i < 50 && state->has_monster; i++) {
		state->monster.hp = 1;
		controller_press(controller, "1");
	}
	CHECK_INT(controller->view, VIEW_VICTORY);
}

TEST(SUITE, victory_screen_end_run) {
	char dir[TEST_PATH_SIZE];
	Controller controller;
	GameData data;
	open_victory(&controller, dir, &data);
	CHECK_STR(controller_title(&controller), tr(&controller, "victory.title"));
	CHECK_CONTAINS(body_line(&controller, 0), "Ferumbras");
	controller_press(&controller, "9");
	CHECK_INT(controller.view, VIEW_VICTORY);
	controller_press(&controller, "1");
	CHECK_INT(controller.view, VIEW_GAME_OVER);
	CHECK_STR(controller_title(&controller), tr(&controller, "gameover.title_won"));
	CHECK_CONTAINS(body_line(&controller, 0), "won the run");
	controller_press(&controller, "2");
	controller_press(&controller, "3");
	CHECK_CONTAINS(body_line(&controller, 0), "WON");
	close_controller(&controller, dir);
	game_data_free(&data);
}

TEST(SUITE, victory_screen_continue) {
	char dir[TEST_PATH_SIZE];
	Controller controller;
	GameData data;
	open_victory(&controller, dir, &data);
	controller_press(&controller, "2");
	CHECK_INT(controller.view, VIEW_MERCHANT);
	REQUIRE(controller.has_session);
	CHECK(controller.session.engine.state.won);
	close_controller(&controller, dir);
	game_data_free(&data);
}

TEST(SUITE, continue_saved_victory_returns_to_the_victory_screen) {
	char dir[TEST_PATH_SIZE];
	Controller controller;
	GameData data;
	open_victory(&controller, dir, &data);
	controller_close_session(&controller);
	controller.view = VIEW_TITLE;
	controller_press(&controller, "1");
	CHECK_INT(controller.view, VIEW_VICTORY);
	close_controller(&controller, dir);
	game_data_free(&data);
}

// A new run on the equipment screen, with the test items in the data.
static void open_equipment(Controller *controller, char dir[TEST_PATH_SIZE], GameData *data) {
	*data = data_copy();
	add_test_items(data);
	open_controller(controller, dir, data);
	start_default_run(controller);
	controller_press(controller, "3");
	CHECK_INT(controller->view, VIEW_EQUIPMENT);
}

static void equip(Player *player, Slot slot, ItemInstance item) {
	player->equipped[slot] = true;
	player->equipment[slot] = item;
}

TEST(SUITE, equipment_screen_lists_every_slot_and_the_bag) {
	char dir[TEST_PATH_SIZE];
	Controller controller;
	GameData data;
	open_equipment(&controller, dir, &data);
	REQUIRE(controller.has_session);
	Player *player = &controller.session.engine.state.player;
	ItemInstance axe = make_item(900, "test_axe", "rare", 0);
	ItemInstance rod = make_item(901, "test_rod", "common", 0);
	ItemInstance helmet = make_item(902, "test_helmet", "legendary", 9);
	player_bag_push(player, &axe);
	player_bag_push(player, &rod);
	player_bag_push(player, &helmet);
	const ItemInstance *starter = player_equipped(player, SLOT_WEAPON);
	REQUIRE(starter != NULL);
	char header[64];
	snprintf(header, sizeof(header), "EQUIPPED · total score %lld", (long long)item_score(starter, &data));
	CHECK_STR(body_line(&controller, 0), header);
	CHECK(starts_with(body_line(&controller, 1), "Weapon: Sword [Common] · Lv 1"));
	CHECK_STR(body_line(&controller, 2), "Shield: - empty -");
	CHECK_STR(body_color(&controller, 2), STYLE_WARNING);
	size_t count;
	const char *const *lines = controller_body_lines(&controller, &count);
	int empty_slots = 0;
	for (size_t i = 0; i < count; i++) {
		empty_slots += strstr(lines[i], "- empty -") != NULL ? 1 : 0;
	}
	CHECK_INT(empty_slots, 7);
	CHECK_STR(body_line(&controller, -1), "BAG (usable)");
	char *keys = option_keys(&controller);
	CHECK_STR(keys, "1 2 3 0");
	free(keys);

	const MenuOption *options = controller_options(&controller, &count);
	REQUIRE_INT(count, 4);
	char delta[DELTA_SIZE];
	format_delta(item_score(&axe, &data) - item_score(starter, &data), delta);
	CHECK_STR(options[0].detail, delta);
	CHECK_STR(options[0].detail_color, STYLE_GAIN);
	CHECK_STR(options[0].color, "rare");
	CHECK_STR(options[1].color, STYLE_DIM);
	CHECK(ends_with(options[1].label, "requires Lv 37"));
	CHECK_STR(options[2].label, "Weapon: Sword [Common]");
	close_controller(&controller, dir);
	game_data_free(&data);
}

// Colour of the body line whose text is exactly `line`; "(missing)" when there is no such line.
static const char *color_of(Controller *controller, const char *line) {
	size_t count;
	const char *const *lines = controller_body_lines(controller, &count);
	for (size_t i = 0; i < count; i++) {
		if (strcmp(lines[i], line) == 0) {
			return body_color(controller, (int)i);
		}
	}
	return "(missing)";
}

TEST(SUITE, comparison_shows_stat_and_score_deltas) {
	char dir[TEST_PATH_SIZE];
	Controller controller;
	GameData data;
	open_equipment(&controller, dir, &data);
	REQUIRE(controller.has_session);
	Player *player = &controller.session.engine.state.player;
	ItemInstance worn = make_item(800, "test_helmet", "common", 0);
	worn.affix_count = 1;
	worn.affixes[0] = (AffixRoll){.stat = STAT_DODGE, .value = 3};
	equip(player, SLOT_HELMET, worn);
	ItemInstance found = make_item(801, "test_helmet", "rare", 0);
	found.affix_count = 1;
	found.affixes[0] = (AffixRoll){.stat = STAT_CRIT_CHANCE, .value = 2};
	player_bag_push(player, &found);
	controller_press(&controller, "1");
	CHECK_INT(controller.view, VIEW_COMPARE);
	CHECK_STR(controller_title(&controller), "Helmet: Test Helmet → Test Helmet");
	CHECK_STR(color_of(&controller, "Armor: 10 → 15 (+5)"), STYLE_GAIN);
	CHECK_STR(color_of(&controller, "Max HP: 50 → 75 (+25)"), STYLE_GAIN);
	CHECK_STR(color_of(&controller, "Critical chance: 0 → 2 (+2)"), STYLE_GAIN);
	CHECK_STR(color_of(&controller, "Dodge: 3 → 0 (-3)"), STYLE_LOSS);
	CHECK_STR(color_of(&controller, "Affixes gained: +2 Critical chance"), STYLE_GAIN);
	CHECK_STR(color_of(&controller, "Affixes lost: +3 Dodge"), STYLE_LOSS);
	size_t count;
	const char *const *lines = controller_body_lines(&controller, &count);
	bool has_score = false;
	for (size_t i = 0; i < count; i++) {
		has_score = has_score || starts_with(lines[i], "Score: ");
	}
	CHECK(has_score);
	char *keys = option_keys(&controller);
	CHECK_STR(keys, "1 0");
	free(keys);
	controller_press(&controller, "1");
	CHECK_INT(controller.view, VIEW_EQUIPMENT);
	const ItemInstance *helmet = player_equipped(player, SLOT_HELMET);
	REQUIRE(helmet != NULL);
	CHECK_INT(helmet->uid, 801);
	close_controller(&controller, dir);
	game_data_free(&data);
}

// The key of the first option whose label starts with `prefix` ("" when there is none).
static void list_key_of(Controller *controller, const char *prefix, char key[4]) {
	size_t count;
	const MenuOption *options = controller_options(controller, &count);
	key[0] = '\0';
	for (size_t i = 0; i < count; i++) {
		if (starts_with(options[i].label, prefix)) {
			str_copy(key, 4, options[i].key);
			return;
		}
	}
}

TEST(SUITE, comparison_warns_about_the_required_level) {
	char dir[TEST_PATH_SIZE];
	Controller controller;
	GameData data;
	open_equipment(&controller, dir, &data);
	REQUIRE(controller.has_session);
	Player *player = &controller.session.engine.state.player;
	ItemInstance axe = make_item(810, "test_axe", "common", 3);
	player_bag_push(player, &axe);
	controller_press(&controller, "1");
	CHECK_STR(body_color(&controller, -1), STYLE_LOSS);
	CHECK_STR(body_line(&controller, -1), "Requires level 13 (you are level 1).");
	controller_press(&controller, "1");
	CHECK_STR(controller.message, tr(&controller, "error.level_too_low"));
	CHECK_INT(controller.view, VIEW_EQUIPMENT);
	controller_press(&controller, "0");
	controller_press(&controller, "3");
	char key[4];
	list_key_of(&controller, "Weapon", key);
	controller_press(&controller, key);
	CHECK_INT(controller.view, VIEW_EQUIPPED_SLOT);
	CHECK_STR(controller_title(&controller), "Weapon");
	CHECK_STR(body_line(&controller, 1), "Attack: 6");
	controller_press(&controller, "1");
	CHECK_INT(controller.view, VIEW_EQUIPMENT);
	CHECK(player_equipped(player, SLOT_WEAPON) == NULL);
	controller_press(&controller, "0");
	close_controller(&controller, dir);
	game_data_free(&data);
}

TEST(SUITE, compare_and_slot_views_survive_missing_items) {
	char dir[TEST_PATH_SIZE];
	Controller controller;
	GameData data;
	open_equipment(&controller, dir, &data);
	REQUIRE(controller.has_session);
	Player *player = &controller.session.engine.state.player;
	CHECK_STR(body_line(&controller, -1), tr(&controller, "equipment.bag_empty"));
	controller.view = VIEW_COMPARE;
	CHECK_INT(body_count(&controller), 0);
	CHECK_STR(controller_title(&controller), tr(&controller, "merchant.equipment"));
	controller.view = VIEW_EQUIPPED_SLOT;
	controller_press(&controller, "0");
	controller.view = VIEW_EQUIPPED_SLOT;
	memset(player->equipped, 0, sizeof(player->equipped));
	CHECK_INT(body_count(&controller), 1);
	CHECK_STR(body_line(&controller, 0), tr(&controller, "equipment.empty"));
	close_controller(&controller, dir);
	game_data_free(&data);
}

TEST(SUITE, monster_view_and_hall_of_fame_markers) {
	char dir[TEST_PATH_SIZE];
	Controller controller;
	GameData data = data_copy();
	all_elites(&data);
	open_controller(&controller, dir, &data);
	const Repositories *repositories = &controller.services.repositories;
	Profile profile;
	char error[256];
	REQUIRE(repositories->load_profile(repositories->context, &profile, error, sizeof(error)) == LOAD_OK);
	REQUIRE(profile.hall_count < MAX_HALL_OF_FAME);
	profile.hall_of_fame[profile.hall_count++] = (HallOfFameEntry){
	    .run_id = "r",
	    .name = "Ana",
	    .vocation = "mage",
	    .difficulty = "hard",
	    .round = 100,
	    .level = 50,
	    .ended_at = "2026-01-01T00:00:00Z",
	    .won = true,
	};
	repositories->save_profile(repositories->context, &profile);
	profile_free(&profile);
	controller_press(&controller, "3");
	CHECK_CONTAINS(body_line(&controller, 0), "WON");
	controller_press(&controller, "0");
	start_default_run(&controller);
	controller_press(&controller, "0");
	const MonsterView *monster = controller_monster_view(&controller);
	REQUIRE(monster != NULL);
	CHECK_STR(monster->enemy_class, "elite");
	bool has_elite_line = false;
	for (size_t i = 0; i < controller.log_count; i++) {
		has_elite_line = has_elite_line || strstr(controller.log[i], "ELITE") != NULL;
	}
	CHECK(has_elite_line);
	close_controller(&controller, dir);
	game_data_free(&data);
}
