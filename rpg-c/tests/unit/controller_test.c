#include "infrastructure/art.h"
#include "presentation/render.h"
#include "support/controller_fixture.h"

#include <stdlib.h>

#define SUITE "unit/controller"

TEST(SUITE, name_validation_and_editing) {
	char dir[TEST_PATH_SIZE];
	Controller controller;
	open_controller(&controller, dir, test_data());
	controller_press(&controller, "2");
	controller_press(&controller, "1");
	CHECK_INT(controller.view, VIEW_NAME);
	controller_press(&controller, "enter");
	CHECK_STR(controller.message, tr(&controller, "new_run.name_invalid"));
	type_text(&controller, "Abcdefghijklmnopqrstuvwxyz");
	CHECK_STR(controller.input_buffer, "Abcdefghijklmnop");
	controller_press(&controller, "backspace");
	CHECK_STR(controller_input_prompt(&controller), "> Abcdefghijklmno_");
	controller_press(&controller, "escape");
	CHECK_INT(controller.view, VIEW_DIFFICULTY);
	controller_press(&controller, "0");
	CHECK_INT(controller.view, VIEW_TITLE);
	close_controller(&controller, dir);
}

TEST(SUITE, title_without_save_has_no_continue) {
	char dir[TEST_PATH_SIZE];
	Controller controller;
	open_controller(&controller, dir, test_data());
	size_t count;
	const MenuOption *options = controller_options(&controller, &count);
	for (size_t i = 0; i < count; i++) {
		CHECK(strcmp(options[i].key, "1") != 0);
	}
	controller_press(&controller, "1");
	CHECK_INT(controller.view, VIEW_TITLE);
	controller_press(&controller, "0");
	CHECK(controller.exit_requested);
	close_controller(&controller, dir);
}

TEST(SUITE, language_switch_from_title) {
	char dir[TEST_PATH_SIZE];
	Controller controller;
	open_controller(&controller, dir, test_data());
	controller_press(&controller, "6");
	CHECK_INT(controller.view, VIEW_SETTINGS);
	controller_press(&controller, "1");
	CHECK_INT(controller.view, VIEW_LANGUAGE);
	controller_press(&controller, "2");
	CHECK_INT(controller.view, VIEW_SETTINGS);
	CHECK_STR(controller.translator.locale, "pt-BR");
	controller_press(&controller, "0");
	CHECK_INT(controller.view, VIEW_TITLE);
	CHECK_STR(controller_title(&controller), "CLI Turn-Based RPG");
	size_t count;
	const MenuOption *options = controller_options(&controller, &count);
	bool has_quit = false;
	for (size_t i = 0; i < count; i++) {
		has_quit = has_quit || strcmp(options[i].label, "Sair") == 0;
	}
	CHECK(has_quit);
	close_controller(&controller, dir);
}

TEST(SUITE, merchant_menus) {
	char dir[TEST_PATH_SIZE];
	Controller controller;
	open_controller(&controller, dir, test_data());
	start_default_run(&controller);
	REQUIRE(controller.has_session);
	RunState *state = &controller.session.engine.state;
	Player *player = &state->player;
	CHECK_STR(controller_title(&controller), tr(&controller, "merchant.title_start"));
	controller_press(&controller, "2");
	CHECK_INT(body_count(&controller), 1);
	CHECK_STR(body_line(&controller, 0), tr(&controller, "merchant.empty_bag"));
	controller_press(&controller, "0");
	ItemInstance axe = make_item(900, "hand_axe", "rare", 0);
	ItemInstance bow = make_item(901, "bow", "common", 0);
	player_bag_push(player, &axe);
	player_bag_push(player, &bow);
	controller_press(&controller, "3");
	size_t count;
	const MenuOption *options = controller_options(&controller, &count);
	bool has_axe = false;
	bool has_bow = false;
	for (size_t i = 0; i < count; i++) {
		has_axe = has_axe || strstr(options[i].label, "Hand Axe") != NULL;
		has_bow = has_bow || strstr(options[i].label, "Bow") != NULL;
	}
	CHECK(has_axe);
	CHECK(!has_bow);
	controller_press(&controller, "1");
	CHECK_INT(controller.view, VIEW_COMPARE);
	controller_press(&controller, "1");
	CHECK_INT(controller.view, VIEW_EQUIPMENT);
	const ItemInstance *first_equipped = NULL;
	for (int slot = 0; slot < SLOT_COUNT && first_equipped == NULL; slot++) {
		first_equipped = player_equipped(player, (Slot)slot);
	}
	REQUIRE(first_equipped != NULL);
	CHECK(first_equipped->uid == 900 || first_equipped->uid == 1);
	controller_press(&controller, "0");
	controller_press(&controller, "2");
	CHECK_STR(option_at(&controller, -1)->key, "0");
	char first_key[sizeof(options->key)];
	str_copy(first_key, sizeof(first_key), option_at(&controller, 0)->key);
	controller_press(&controller, first_key);
	controller_press(&controller, "0");
	controller_press(&controller, "4");
	CHECK_INT(option_count(&controller), state->stock_count + 1);
	player->gold = 0;
	controller_press(&controller, "1");
	CHECK_STR(controller.message, tr(&controller, "error.not_enough_gold"));
	controller_press(&controller, "0");
	controller_press(&controller, "1");
	controller_press(&controller, "1");
	controller_press(&controller, "enter");
	CHECK_INT(controller.view, VIEW_BUY_POTIONS);
	controller_press(&controller, "0");
	controller_press(&controller, "5");
	CHECK_INT(controller.view, VIEW_CHARACTER);
	char *sheet = body_text(&controller);
	CHECK_CONTAINS(sheet, "Equipment");
	free(sheet);
	close_controller(&controller, dir);
}

TEST(SUITE, battle_submenus_and_messages) {
	char dir[TEST_PATH_SIZE];
	Controller controller;
	open_controller(&controller, dir, test_data());
	start_run(&controller, "Zed", "3", "2");
	controller_press(&controller, "0");
	CHECK_INT(controller.view, VIEW_BATTLE);
	CHECK(controller_monster_view(&controller) != NULL);
	CHECK(controller_player_view(&controller) != NULL);
	REQUIRE(controller.has_session);
	Player *player = &controller.session.engine.state.player;
	counter_free(&player->potions);
	controller_press(&controller, "3");
	CHECK_INT(body_count(&controller), 1);
	CHECK_STR(body_line(&controller, 0), tr(&controller, "battle.no_potions"));
	controller_press(&controller, "0");
	player->mp = 0;
	controller_press(&controller, "2");
	char first_spell[2] = {list_key(0), '\0'};
	controller_press(&controller, first_spell);
	CHECK_STR(controller.message, tr(&controller, "error.not_enough_mana"));
	controller_press(&controller, "escape");
	controller_press(&controller, "4");
	CHECK(controller.animation_cue_count <= 1);
	if (controller.animation_cue_count == 1) {
		CHECK_STR(controller.animation_cues[0], "attack");
	}
	controller_press(&controller, "q");
	CHECK_INT(controller.view, VIEW_TITLE);
	CHECK(!controller.has_session);
	close_controller(&controller, dir);
}

static bool starts_with(const char *text, const char *prefix) { return strncmp(text, prefix, strlen(prefix)) == 0; }

TEST(SUITE, bestiary_paging_and_reveal) {
	char dir[TEST_PATH_SIZE];
	Controller controller;
	open_controller(&controller, dir, test_data());
	const Repositories *repositories = &controller.services.repositories;
	Profile profile;
	char error[256];
	REQUIRE(repositories->load_profile(repositories->context, &profile, error, sizeof(error)) == LOAD_OK);
	// Kept sorted by id, as the profile expects.
	BestiaryEntry bat = {.monster_id = "bat", .kills = 1, .first_killed_at = "2026-01-01T00:00:00Z"};
	BestiaryEntry rat = {.monster_id = "rat", .kills = 9, .first_killed_at = "2026-01-01T00:00:00Z"};
	VEC_PUSH(profile.bestiary, profile.bestiary_count, profile.bestiary_capacity, bat);
	VEC_PUSH(profile.bestiary, profile.bestiary_count, profile.bestiary_capacity, rat);
	repositories->save_profile(repositories->context, &profile);
	profile_free(&profile);

	controller_press(&controller, "4");
	size_t count;
	const char *const *lines = controller_body_lines(&controller, &count);
	CHECK_INT(count, LINES_PER_PAGE + 2);
	bool rat_revealed = false;
	bool bat_hidden = false;
	for (size_t i = 0; i < count; i++) {
		rat_revealed = rat_revealed || (starts_with(lines[i], "Rat") && strstr(lines[i], "weak") != NULL);
		bat_hidden = bat_hidden || (starts_with(lines[i], "Bat") && strstr(lines[i], "weak") == NULL);
	}
	CHECK(rat_revealed);
	CHECK(bat_hidden);
	char *first_page = body_text(&controller);
	controller_press(&controller, "n");
	char *second_page = body_text(&controller);
	CHECK(strcmp(first_page, second_page) != 0);
	free(first_page);
	free(second_page);
	for (int i = 0; i < 50; i++) {
		controller_press(&controller, "n");
	}
	CHECK(starts_with(body_line(&controller, -1), "Page 12/12"));
	controller_press(&controller, "p");
	controller_press(&controller, "0");
	controller_press(&controller, "3");
	CHECK_INT(body_count(&controller), 1);
	CHECK_STR(body_line(&controller, 0), tr(&controller, "hall.empty"));
	controller_press(&controller, "0");
	controller_press(&controller, "5");
	lines = controller_body_lines(&controller, &count);
	REQUIRE(count >= LINES_PER_PAGE);
	for (size_t i = 0; i < LINES_PER_PAGE; i++) {
		CHECK(starts_with(lines[i], "[ ]"));
	}
	close_controller(&controller, dir);
}

static void check_bar(int64_t current, int64_t maximum, int width, const char *expected) {
	StrBuf cells = {0};
	bar(&cells, current, maximum, width);
	CHECK_STR(cells.data, expected);
	sb_free(&cells);
}

TEST(SUITE, render_helpers) {
	check_bar(0, 100, 10, "░░░░░░░░░░");
	check_bar(1, 100, 10, "█░░░░░░░░░");
	check_bar(100, 100, 10, "██████████");
	check_bar(5, 0, 4, "░░░░");
	CHECK_STR(hp_color(60, 100), "green");
	CHECK_STR(hp_color(30, 100), "yellow");
	CHECK_STR(hp_color(10, 100), "red");
	CHECK_INT(list_key(0), '1');
	CHECK_INT(list_key(9), 'a');
	CHECK_INT(list_index("a"), 9);
	CHECK_INT(list_index("!"), -1);
}

// True when the frame is exactly the one line `line`.
static bool frame_is(const Frame *frame, const char *line) {
	return frame != NULL && frame->line_count == 1 && strcmp(frame->lines[0], line) == 0;
}

TEST(SUITE, art_parsing) {
	const GameData *data = test_data();
	Animations animations;
	char error[128];
	REQUIRE(parse_art("@idle\n a\n%%\n b\n@hurt\n x\n", &animations, error, sizeof(error)));
	REQUIRE_INT(animations.count, 2);
	const Animation *idle = find_animation(&animations, "idle");
	const Animation *hurt = find_animation(&animations, "hurt");
	REQUIRE(idle != NULL && hurt != NULL);
	REQUIRE_INT(idle->frame_count, 2);
	REQUIRE_INT(hurt->frame_count, 1);
	CHECK(frame_is(&idle->frames[0], " a"));
	CHECK(frame_is(&idle->frames[1], " b"));
	CHECK(frame_is(&hurt->frames[0], " x"));
	CHECK(frame_is(frame_for(&animations, "idle", 3), " b"));
	CHECK(frame_is(frame_for(&animations, "attack", 0), " a"));
	animations_free(&animations);
	Animations empty = {0};
	CHECK(frame_for(&empty, "idle", 0) == NULL);
	CHECK(!parse_art("oops", &animations, error, sizeof(error)));
	CHECK_CONTAINS(error, "before");
	CHECK(!parse_art("%%", &animations, error, sizeof(error)));
	CHECK_CONTAINS(error, "separator");
	for (int i = 0; i < data->monster_count + data->boss_count; i++) {
		const MonsterDef *creature =
		    i < data->monster_count ? &data->monsters[i] : &data->bosses[i - data->monster_count];
		const Frame *frame = frame_for(art_for_creature(creature), "idle", 0);
		CHECK(frame != NULL && frame->line_count > 0);
	}
}
