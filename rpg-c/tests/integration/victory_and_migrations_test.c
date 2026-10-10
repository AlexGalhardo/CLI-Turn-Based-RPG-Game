// Victory phase through the session (save, resume, history, Hall of Fame) and schema 1 → 2 migrations.
#include "application/game_session.h"
#include "infrastructure/filesystem.h"
#include "infrastructure/repositories.h"
#include "support/helpers.h"

#include <stdio.h>
#include <stdlib.h>

#define SUITE "integration/victory_and_migrations"

#define MAX_SWINGS 200

static bool start(GameSession *session, const char *directory, Clock clock, RunConfig config, int64_t seed) {
	char error[256] = "";
	EventList events = {0};
	bool ok = session_start(
	    session, test_data(), &config, seed, file_repositories(directory), clock, "1", &events, error, sizeof(error));
	events_free(&events);
	if (!ok) {
		test_fail(__FILE__, __LINE__, "the session did not start: %s", error);
	}
	return ok;
}

// The events of one command, in a list owned by the helper (valid until the next call).
static const EventList *session_play(GameSession *session, Command command) {
	static EventList events;
	events_clear(&events);
	session_step(session, &command, &events, NULL);
	return &events;
}

// Starts a run and beats the final boss; the session is left in the victory phase.
static bool win_final_fight(GameSession *session, const char *directory, Clock clock) {
	const GameData *data = test_data();
	if (!start(session, directory, clock, run_config("Vic", "warrior", "easy", true), 8)) {
		return false;
	}
	RunState *state = &session->engine.state;
	state->round = data->balance.final_round - 1;
	session_play(session, cmd_next_fight());
	CHECK(state->has_monster);
	CHECK_STR(state->monster.creature_id, "ferumbras");
	CHECK_INT(state->monster.enemy_class, ENEMY_BOSS);
	for (int i = 0; i < MAX_SWINGS && state->phase == PHASE_BATTLE; i++) {
		state->player.hp = 1000000;
		state->monster.hp = 1;
		session_play(session, cmd_attack());
	}
	if (!CHECK_INT(state->phase, PHASE_VICTORY)) {
		session_free(session);
		return false;
	}
	return true;
}

TEST(SUITE, victory_is_saved_resumed_and_ended_as_won) {
	char directory[TEST_PATH_SIZE];
	temp_dir_create(directory);
	FakeClock clock_state;
	Clock clock = fake_clock(&clock_state);
	Repositories repositories = file_repositories(directory);
	GameSession session;
	if (win_final_fight(&session, directory, clock)) {
		JsonError json_error = {0};
		JsonValue *save = load_json_file(path_in(directory, "save.json"));
		if (save != NULL) {
			const JsonValue *run = json_read_object(save, "run", &json_error);
			CHECK_STR(json_read_str(run, "phase", &json_error), "victory");
			CHECK(json_read_bool(run, "won", &json_error));
			CHECK(!json_error.failed);
			json_free(save);
		}

		GameSession resumed;
		char error[256] = "";
		if (session_resume(&resumed, test_data(), repositories, clock, "1", error, sizeof(error)) == LOAD_OK) {
			CHECK_INT(resumed.engine.state.phase, PHASE_VICTORY);
			CHECK_EVENTS(session_play(&resumed, cmd_end_run()), "[{\"type\": \"run_ended\", \"won\": true}]");
			Profile profile;
			if (repositories.load_profile(repositories.context, &profile, error, sizeof(error)) == LOAD_OK) {
				CHECK(profile_unlock(&profile, "conqueror") != NULL);
				CHECK(profile.hall_count > 0 && profile.hall_of_fame[0].won);
				profile_free(&profile);
			} else {
				test_fail(__FILE__, __LINE__, "the profile did not load: %s", error);
			}
			CHECK(!fs_exists(path_in(directory, "save.json")));
			RunRecord *records = NULL;
			size_t count = 0;
			CHECK_INT(history_list(directory, &records, &count, error, sizeof(error)), LOAD_OK);
			if (count > 0) {
				CHECK(records[0].won);
				CHECK_STR(records[0].death_cause, "");
			} else {
				CHECK(count > 0);
			}
			for (size_t i = 0; i < count; i++) {
				run_record_free(&records[i]);
			}
			free(records);
			session_free(&resumed);
		} else {
			test_fail(__FILE__, __LINE__, "the victory save did not resume: %s", error);
		}
		session_free(&session);
	}
	temp_dir_remove(directory);
}

TEST(SUITE, continue_after_victory_keeps_the_run_won) {
	char directory[TEST_PATH_SIZE];
	temp_dir_create(directory);
	FakeClock clock_state;
	GameSession session;
	if (win_final_fight(&session, directory, fake_clock(&clock_state))) {
		int64_t final_round = test_data()->balance.final_round;
		CHECK_EVENTS(session_play(&session, cmd_continue_run()),
		    fmt("[{\"type\": \"merchant_entered\", \"round\": %lld}]", (long long)final_round));
		CHECK_INT(session.engine.state.phase, PHASE_MERCHANT);
		CHECK(session.engine.state.won);
		session_play(&session, cmd_next_fight());
		CHECK_INT(session.engine.state.round, final_round + 1);
		session_free(&session);
	}
	temp_dir_remove(directory);
}

static void remove_m8_stats(JsonValue *stats) {
	json_remove(stats, "itemsAutoEquipped");
	json_remove(stats, "elitesKilled");
	json_remove(stats, "potionsDropped");
}

// Turns a current save into what version 1 wrote: no M8 fields and the old `epic` rarity.
static void make_v1(JsonValue *document) {
	json_set(document, "schemaVersion", json_int(1));
	JsonValue *run = json_get(document, "run");
	json_remove(json_get(run, "config"), "autoEquip");
	json_remove(run, "won");
	JsonValue *weapon = json_get(json_get(json_get(run, "player"), "equipment"), "weapon");
	json_set(weapon, "rarity", json_string("epic"));
	JsonValue *stats = json_get(run, "stats");
	remove_m8_stats(stats);
	JsonError error = {0};
	json_set(stats, "itemsDropped", json_parse("{\"epic\": 2, \"legendary\": 1}", &error));
	json_set(
	    stats, "droppedItems", json_parse("[{\"itemId\": \"sword\", \"rarity\": \"epic\", \"round\": 3}]", &error));
}

TEST(SUITE, v1_save_is_migrated) {
	char directory[TEST_PATH_SIZE];
	temp_dir_create(directory);
	FakeClock clock_state;
	GameSession session;
	if (start(&session, directory, fake_clock(&clock_state), run_config("Old", "warrior", "normal", false), 4)) {
		session_free(&session);
		JsonValue *current = load_json_file(path_in(directory, "save.json"));
		REQUIRE(current != NULL);
		// The current format has the fields the migration must restore.
		CHECK(json_has(json_get(json_get(current, "run"), "config"), "autoEquip"));
		CHECK(json_has(json_get(json_get(current, "run"), "stats"), "elitesKilled"));
		make_v1(current);
		write_json_file(path_in(directory, "save.json"), current);
		json_free(current);

		Repositories repositories = file_repositories(directory);
		SaveGame loaded;
		char error[256] = "";
		if (repositories.load_save(repositories.context, &loaded, error, sizeof(error)) == LOAD_OK) {
			const RunState *run = &loaded.run;
			CHECK(!run->config.auto_equip);
			CHECK(!run->won);
			CHECK(run->player.equipped[SLOT_WEAPON]);
			CHECK_STR(run->player.equipment[SLOT_WEAPON].rarity, "legendary");
			CHECK_INT(run->stats.items_dropped.count, 1);
			CHECK_INT(counter_get(&run->stats.items_dropped, "legendary"), 3);
			CHECK(run->stats.dropped_count == 1 && str_eq(run->stats.dropped_items[0].rarity, "legendary"));
			CHECK_INT(run->stats.elites_killed, 0);
			save_game_free(&loaded);
		} else {
			test_fail(__FILE__, __LINE__, "the version 1 save did not load: %s", error);
		}
	}
	temp_dir_remove(directory);
}

TEST(SUITE, v1_monster_gets_its_class_from_is_boss) {
	char directory[TEST_PATH_SIZE];
	temp_dir_create(directory);
	FakeClock clock_state;
	GameSession session;
	if (start(&session, directory, fake_clock(&clock_state), run_config("Old", "mage", "normal", false), 4)) {
		session.engine.state.round = 9;
		session_play(&session, cmd_next_fight());
		JsonValue *document = load_json_file(path_in(directory, "save.json"));
		if (document != NULL && session.engine.state.has_monster) {
			json_set(json_get(document, "run"), "monster", monster_instance_to_json(&session.engine.state.monster));
			make_v1(document);
			JsonValue *monster = json_get(json_get(document, "run"), "monster");
			CHECK(json_has(monster, "enemyClass"));
			json_remove(monster, "enemyClass");
			write_json_file(path_in(directory, "save.json"), document);

			Repositories repositories = file_repositories(directory);
			SaveGame loaded;
			char error[256] = "";
			if (repositories.load_save(repositories.context, &loaded, error, sizeof(error)) == LOAD_OK) {
				CHECK(loaded.run.has_monster);
				CHECK(loaded.run.monster.is_boss);
				CHECK_INT(loaded.run.monster.enemy_class, ENEMY_BOSS);
				save_game_free(&loaded);
			} else {
				test_fail(__FILE__, __LINE__, "the version 1 save did not load: %s", error);
			}
		} else {
			CHECK(document != NULL && session.engine.state.has_monster);
		}
		json_free(document);
		session_free(&session);
	}
	temp_dir_remove(directory);
}

TEST(SUITE, v1_history_and_profile_are_migrated) {
	char directory[TEST_PATH_SIZE];
	char won_directory[TEST_PATH_SIZE];
	char old_directory[TEST_PATH_SIZE];
	temp_dir_create(directory);
	path_join(won_directory, directory, "won");
	path_join(old_directory, directory, "old");
	FakeClock clock_state;
	GameSession session;
	if (win_final_fight(&session, won_directory, fake_clock(&clock_state))) {
		session_play(&session, cmd_end_run());
		session_free(&session);

		size_t name_count = 0;
		char **names = fs_list(path_in(won_directory, "history"), ".json", &name_count);
		if (name_count == 1) {
			JsonValue *record = load_json_file(fmt("%s/history/%s", won_directory, names[0]));
			if (record != NULL) {
				CHECK(json_has(record, "won"));
				json_set(record, "schemaVersion", json_int(1));
				json_remove(record, "won");
				remove_m8_stats(json_get(record, "stats"));
				write_json_file(fmt("%s/history/%s", old_directory, names[0]), record);
				json_free(record);
			}
			RunRecord *records = NULL;
			size_t count = 0;
			char error[256] = "";
			CHECK_INT(history_list(old_directory, &records, &count, error, sizeof(error)), LOAD_OK);
			if (count == 1) {
				CHECK(!records[0].won);
				CHECK_INT(records[0].stats.elites_killed, 0);
				CHECK_INT(records[0].round, test_data()->balance.final_round);
			} else {
				test_fail(__FILE__, __LINE__, "expected one migrated record, got %zu (%s)", count, error);
			}
			for (size_t i = 0; i < count; i++) {
				run_record_free(&records[i]);
			}
			free(records);
		} else {
			CHECK_INT(name_count, 1);
		}
		fs_free_names(names, name_count);

		JsonValue *profile = load_json_file(path_in(won_directory, "profile.json"));
		if (profile != NULL) {
			json_set(profile, "schemaVersion", json_int(1));
			JsonError json_error = {0};
			const JsonValue *hall = json_read_array(profile, "hallOfFame", &json_error);
			CHECK(hall->count > 0);
			for (size_t i = 0; i < hall->count; i++) {
				CHECK(json_read_bool(hall->items[i], "won", &json_error));
				json_remove(hall->items[i], "won");
			}
			CHECK(!json_error.failed);
			write_json_file(path_in(old_directory, "profile.json"), profile);
			json_free(profile);

			Repositories repositories = file_repositories(old_directory);
			Profile loaded;
			char error[256] = "";
			if (repositories.load_profile(repositories.context, &loaded, error, sizeof(error)) == LOAD_OK) {
				CHECK(loaded.hall_count > 0 && !loaded.hall_of_fame[0].won);
				profile_free(&loaded);
			} else {
				test_fail(__FILE__, __LINE__, "the version 1 profile did not load: %s", error);
			}
		}
	}
	temp_dir_remove(directory);
}
