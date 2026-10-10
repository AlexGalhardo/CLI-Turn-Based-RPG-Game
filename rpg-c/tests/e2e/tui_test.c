// End-to-end tests: the real App (controller + renderer) driven by key presses and read back as plain text.
// No terminal is involved: no animation unless a test asks for it, fixed seed, temporary data directory.
#include "application/bot.h"
#include "application/loot.h"
#include "application/merchant.h"
#include "infrastructure/filesystem.h"
#include "presentation/render.h"
#include "presentation/tui/app.h"
#include "support/helpers.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>

#define SUITE "e2e/tui"

typedef struct {
	char directory[TEST_PATH_SIZE];
	Controller controller;
	App app;
	StrBuf screen;
} Game;

// `lang` NULL keeps the saved language (the language screen on a first launch).
static void game_open(Game *game, const char *lang, bool animate) {
	Services services = {
	    .data = test_data(),
	    .data_dir = game->directory,
	    .repositories = file_repositories(game->directory),
	    .clock = system_clock(),
	    .version = RPG_VERSION,
	    .has_seed = true,
	    .seed = 42,
	    .locale_override = lang,
	};
	controller_init(&game->controller, &services);
	app_init(&game->app, &game->controller, animate);
	game->screen = (StrBuf){0};
}

static void game_close(Game *game) {
	controller_free(&game->controller);
	sb_free(&game->screen);
	temp_dir_remove(game->directory);
}

static bool press(Game *game, const char *key) { return app_handle_key(&game->app, key); }

// Presses every character of `keys` as its own key.
static void type(Game *game, const char *keys) {
	for (const char *c = keys; *c != '\0'; c++) {
		char key[2] = {*c, '\0'};
		press(game, key);
	}
}

static const char *screen_text(Game *game) {
	sb_clear(&game->screen);
	app_render(&game->app, &game->screen, false);
	return game->screen.data;
}

static RunState *state_of(Game *game) { return &game->controller.session.engine.state; }

static bool file_exists(const Game *game, const char *name) {
	char path[PATH_SIZE];
	path_join(path, game->directory, name);
	return fs_exists(path);
}

static size_t history_count(const Game *game) {
	char path[PATH_SIZE];
	path_join(path, game->directory, "history");
	size_t count = 0;
	fs_free_names(fs_list(path, ".json", &count), count);
	return count;
}

TEST(SUITE, first_launch_language_then_new_run_flow) {
	Game game;
	temp_dir_create(game.directory);
	game_open(&game, NULL, false);
	CHECK_INT(game.controller.view, VIEW_LANGUAGE);
	press(&game, "2");
	CHECK_INT(game.controller.view, VIEW_TITLE);
	CHECK_CONTAINS(screen_text(&game), "Nova jornada");
	press(&game, "2");
	CHECK_INT(game.controller.view, VIEW_DIFFICULTY);
	press(&game, "3");
	type(&game, "Ana");
	press(&game, "enter");
	CHECK_INT(game.controller.view, VIEW_VOCATION);
	press(&game, "3");
	CHECK_INT(game.controller.view, VIEW_AUTO_EQUIP);
	press(&game, "2");
	CHECK_INT(game.controller.view, VIEW_MERCHANT);
	CHECK_CONTAINS(screen_text(&game), "Ana");
	CHECK_CONTAINS(screen_text(&game), "Mago");

	char path[PATH_SIZE];
	path_join(path, game.directory, "settings.json");
	JsonValue *settings = load_json_file(path);
	REQUIRE(settings != NULL);
	JsonError error = {0};
	CHECK_STR(json_read_str(settings, "locale", &error), "pt-BR");
	json_free(settings);
	CHECK(file_exists(&game, "save.json"));
	game_close(&game);
}

TEST(SUITE, screen_is_100_by_30_and_reports_small_terminals) {
	Game game;
	temp_dir_create(game.directory);
	game_open(&game, "en", false);
	const char *text = screen_text(&game);
	int rows = 1;
	size_t first_row_bytes = strcspn(text, "\n");
	for (const char *c = text; *c != '\0'; c++) {
		rows += *c == '\n' ? 1 : 0;
	}
	CHECK_INT(rows, MIN_ROWS);
	char first_row[512];
	snprintf(first_row, sizeof(first_row), "%.*s", (int)first_row_bytes, text);
	CHECK_INT(utf8_length(first_row), MIN_COLUMNS);
	CHECK_CONTAINS(text, "v" RPG_VERSION " · C");

	game.app.width = 80;
	CHECK_CONTAINS(screen_text(&game), "100");
	CHECK_NOT_CONTAINS(screen_text(&game), "╭");
	game_close(&game);
}

TEST(SUITE, battle_merchant_save_quit_and_continue) {
	Game game;
	temp_dir_create(game.directory);
	game_open(&game, "en", false);
	type(&game, "22");
	type(&game, "Bo");
	press(&game, "enter");
	type(&game, "12");
	CHECK_INT(game.controller.view, VIEW_MERCHANT);
	press(&game, "1");
	CHECK_INT(game.controller.view, VIEW_BUY_POTIONS);
	press(&game, "1");
	CHECK_INT(game.controller.view, VIEW_QUANTITY);
	press(&game, "1");
	press(&game, "enter");
	REQUIRE(game.controller.has_session);
	CHECK_INT(counter_get(&state_of(&game)->player.potions, "health_potion"), 6);
	type(&game, "05");
	CHECK_CONTAINS(screen_text(&game), "Equipment");
	type(&game, "00");
	CHECK_INT(game.controller.view, VIEW_BATTLE);
	CHECK_CONTAINS(screen_text(&game), "HP");
	REQUIRE(state_of(&game)->has_monster);
	state_of(&game)->monster.hp = 1000000;
	state_of(&game)->monster.max_hp = 1000000;
	press(&game, "1");
	press(&game, "2");
	CHECK_INT(game.controller.view, VIEW_SPELLS);
	char first[2] = {list_key(0), '\0'};
	press(&game, first);
	press(&game, "3");
	CHECK_INT(game.controller.view, VIEW_POTIONS);
	press(&game, "escape");
	CHECK_INT(game.controller.view, VIEW_BATTLE);
	press(&game, "q");
	CHECK_INT(game.controller.view, VIEW_TITLE);
	CHECK_CONTAINS(screen_text(&game), "Continue");
	press(&game, "1");
	CHECK_INT(game.controller.view, VIEW_MERCHANT);
	REQUIRE(game.controller.has_session);
	CHECK_INT(game.controller.session.info.sessions, 2);
	game_close(&game);
}

// The keys a player presses to issue `command` from the merchant or battle screen.
static void press_command(Game *game, const Command *command) {
	const GameData *data = test_data();
	const RunState *state = state_of(game);
	const Player *player = &state->player;
	char keys[16] = "";
	size_t index = 0;
	switch (command->type) {
	case CMD_ATTACK:
		type(game, "1");
		return;
	case CMD_DEFEND:
		type(game, "4");
		return;
	case CMD_NEXT_FIGHT:
		type(game, "0");
		return;
	case CMD_END_RUN:
		type(game, "1");
		return;
	case CMD_CAST: {
		const VocationDef *vocation = data_vocation(data, player->vocation_id);
		while (!str_eq(vocation->spells[index], command->id)) {
			index++;
		}
		snprintf(keys, sizeof(keys), "2%c", list_key(index));
		break;
	}
	case CMD_USE_POTION:
		// The potion menu lists the owned potions in data order.
		for (int i = 0; i < data->potion_count && !str_eq(data->potions[i].id, command->id); i++) {
			index += counter_get(&player->potions, data->potions[i].id) > 0 ? 1 : 0;
		}
		snprintf(keys, sizeof(keys), "3%c", list_key(index));
		break;
	case CMD_BUY_POTION:
		for (int i = 0; i < data->potion_count && !str_eq(data->potions[i].id, command->id); i++) {
			index += potion_available(state, &data->potions[i]) ? 1 : 0;
		}
		snprintf(keys, sizeof(keys), "1%c%lld", list_key(index), (long long)command->number);
		type(game, keys);
		press(game, "enter");
		type(game, "0");
		return;
	case CMD_SELL_ITEM:
		snprintf(keys, sizeof(keys), "2%c0", list_key((size_t)player_bag_index(player, command->number)));
		break;
	case CMD_EQUIP: {
		// The equipment menu lists the usable bag items first, in bag order.
		const VocationDef *vocation = data_vocation(data, player->vocation_id);
		for (int i = 0; i < player->bag_count && player->bag[i].uid != command->number; i++) {
			index += can_use(data_item(data, player->bag[i].item_id), vocation) ? 1 : 0;
		}
		snprintf(keys, sizeof(keys), "3%c10", list_key(index));
		break;
	}
	case CMD_BUY_STOCK_ITEM:
		snprintf(keys, sizeof(keys), "4%c0", list_key((size_t)command->number));
		break;
	default:
		test_fail(__FILE__, __LINE__, "unexpected bot command");
		return;
	}
	type(game, keys);
}

// Plays a whole run through the UI keys, choosing what the bot would do, until death.
TEST(SUITE, full_run_until_game_over) {
	Game game;
	temp_dir_create(game.directory);
	game_open(&game, "en", false);
	type(&game, "23");
	type(&game, "Hero");
	press(&game, "enter");
	type(&game, "12");
	REQUIRE(game.controller.has_session);
	bool jumped = false;
	for (int i = 0; i < 5000 && state_of(&game)->phase != PHASE_GAME_OVER; i++) {
		RunState *state = state_of(&game);
		if (!jumped && state->phase == PHASE_MERCHANT && state->stats.kills.count > 0) {
			// After the first kill, skip ahead so the run ends quickly (each fight costs many key presses).
			state->round = 95;
			jumped = true;
		}
		Command command = bot_choose(test_data(), state);
		press_command(&game, &command);
	}
	CHECK_INT(game.controller.view, VIEW_GAME_OVER);
	CHECK_CONTAINS(screen_text(&game), state_of(&game)->won ? "RUN COMPLETE" : "GAME OVER");
	press(&game, "2");
	press(&game, "3");
	CHECK_CONTAINS(screen_text(&game), "Hero");
	type(&game, "05");
	CHECK_CONTAINS(screen_text(&game), "[x] First Blood");
	type(&game, "04");
	CHECK_INT(game.controller.view, VIEW_BESTIARY);
	type(&game, "np0");
	CHECK(!press(&game, "0"));
	CHECK(!file_exists(&game, "save.json"));
	CHECK_INT(history_count(&game), 1);
	game_close(&game);
}

// Every fight is played by the auto-battle (instant without animation) until the run ends.
TEST(SUITE, full_run_with_auto_battle) {
	Game game;
	temp_dir_create(game.directory);
	game_open(&game, "en", false);
	type(&game, "21");
	type(&game, "Auto");
	press(&game, "enter");
	type(&game, "21");
	REQUIRE(game.controller.has_session);
	CHECK(state_of(&game)->config.auto_equip);
	const char *const modes[] = {"1", "2", "3"};
	for (int fight = 0; fight < 5000 && state_of(&game)->phase != PHASE_GAME_OVER; fight++) {
		if (state_of(&game)->phase == PHASE_VICTORY) {
			CHECK_CONTAINS(screen_text(&game), "VICTORY");
			press(&game, "1");
			continue;
		}
		type(&game, "05");
		press(&game, modes[fight % 3]);
		CHECK(!game.controller.auto_battle_active);
	}
	CHECK_INT(game.controller.view, VIEW_GAME_OVER);
	CHECK(state_of(&game)->round >= 1);
	CHECK_INT(history_count(&game), 1);
	game_close(&game);
}

// With animation on, the renderer's timer drives the fight: the test sends the timer ticks itself.
TEST(SUITE, auto_battle_is_paced_by_a_timer) {
	Game game;
	temp_dir_create(game.directory);
	Settings settings = default_settings();
	settings.has_locale = true;
	str_copy(settings.locale, LOCALE_SIZE, "en");
	settings.battle_speed = 2;
	settings_save(game.directory, &settings);
	game_open(&game, NULL, true);
	type(&game, "21");
	type(&game, "Tim");
	press(&game, "enter");
	type(&game, "120");
	CHECK_INT(game.controller.view, VIEW_BATTLE);
	REQUIRE(game.controller.has_session && state_of(&game)->has_monster);
	RunState *state = state_of(&game);
	state->monster.hp = 1000000;
	state->monster.max_hp = 1000000;
	state->player.hp = 1000000;
	type(&game, "51");
	CHECK(game.controller.auto_battle_active);
	CHECK_INT(controller_auto_battle_interval_ms(&game.controller), AUTO_BATTLE_BASE_MS / 2);
	int64_t turn = state->turn;
	for (int i = 0; i < 3; i++) {
		CHECK(app_auto_battle_tick(&game.app));
	}
	CHECK(state->turn > turn);
	// Keys are ignored while the fight plays itself.
	press(&game, "4");
	CHECK(game.controller.auto_battle_active);
	state->monster.hp = 1;
	for (int i = 0; i < 20 && game.controller.auto_battle_active; i++) {
		app_auto_battle_tick(&game.app);
	}
	CHECK(!game.controller.auto_battle_active);
	CHECK(game.controller.view != VIEW_BATTLE);
	game_close(&game);
}

TEST(SUITE, animation_cues_are_shown_one_per_tick) {
	Game game;
	temp_dir_create(game.directory);
	game_open(&game, "en", true);
	type(&game, "22");
	type(&game, "Cue");
	press(&game, "enter");
	type(&game, "120");
	REQUIRE(game.controller.has_session && state_of(&game)->has_monster);
	state_of(&game)->monster.hp = 1000000;
	state_of(&game)->monster.max_hp = 1000000;
	press(&game, "4");
	// Defending makes the monster act: its attack is queued as a one-shot animation unless it healed or missed.
	size_t cues = game.app.cue_count;
	CHECK_INT(game.controller.animation_cue_count, 0);
	int64_t tick = game.app.tick;
	app_tick(&game.app);
	CHECK_INT(game.app.tick, tick + 1);
	CHECK_INT(game.app.cue_count, cues > 0 ? cues - 1 : 0);
	CHECK_CONTAINS(screen_text(&game), "HP");
	game_close(&game);
}
