#include "application/bot.h"
#include "application/game_session.h"
#include "infrastructure/filesystem.h"
#include "infrastructure/repositories.h"
#include "support/helpers.h"

#include <stdio.h>
#include <stdlib.h>

#define SUITE "integration/persistence"

#define MAX_STEPS 200000

static bool start(GameSession *session, const char *directory, Clock clock, RunConfig config, int64_t seed,
    const char *version, EventList *events) {
	char error[256] = "";
	EventList local = {0};
	bool ok = session_start(session, test_data(), &config, seed, file_repositories(directory), clock, version,
	    events != NULL ? events : &local, error, sizeof(error));
	events_free(&local);
	if (!ok) {
		test_fail(__FILE__, __LINE__, "the session did not start: %s", error);
	}
	return ok;
}

static void session_play(GameSession *session, Command command) {
	EventList events = {0};
	session_step(session, &command, &events, NULL);
	events_free(&events);
}

TEST(SUITE, new_session_autosaves_at_merchant) {
	char directory[TEST_PATH_SIZE];
	temp_dir_create(directory);
	FakeClock clock_state;
	GameSession session;
	EventList events = {0};
	if (start(&session, directory, fake_clock(&clock_state), run_config("Alex", "archer", "normal", false), 5, "9.9.9",
	        &events)) {
		CHECK(events.count > 0 && event_is(&events.items[0], "run_started"));
		JsonError error = {0};
		JsonValue *save = load_json_file(path_in(directory, "save.json"));
		if (save != NULL) {
			const JsonValue *run = json_read_object(save, "run", &error);
			CHECK_INT(json_read_int(save, "schemaVersion", &error), 2);
			CHECK(!json_read_bool(json_read_object(run, "config", &error), "autoEquip", &error));
			CHECK(!json_read_bool(run, "won", &error));
			CHECK_STR(json_read_str(save, "implementation", &error), "c");
			CHECK_STR(json_read_str(save, "gameVersion", &error), "9.9.9");
			CHECK_STR(json_read_str(json_read_object(save, "session", &error), "runId", &error), session.info.run_id);
			CHECK_STR(json_read_str(run, "phase", &error), "merchant");
			json_free(save);
		}
		// The first clock reading names the run.
		CHECK_STR(session.info.run_id, "20260927T120010Z-5");
		CHECK_STR(session.info.started_at, "2026-09-27T12:00:10Z");

		session_play(&session, cmd_buy_potion("health_potion", 1));
		save = load_json_file(path_in(directory, "save.json"));
		if (save != NULL) {
			const JsonValue *player = json_read_object(json_read_object(save, "run", &error), "player", &error);
			CHECK_INT(json_read_int(json_read_object(player, "potions", &error), "health_potion", &error), 6);
			json_free(save);
		}
		CHECK(!error.failed);

		// Written like the reference: tab indentation and a final newline.
		char *text = read_file(path_in(directory, "save.json"));
		if (text != NULL) {
			size_t length = strlen(text);
			CHECK(str_starts_with(text, "{\n\t\"schemaVersion\": 2,"));
			CHECK(length > 2 && str_eq(text + length - 2, "}\n"));
			free(text);
		}
		session_free(&session);
	}
	events_free(&events);
	temp_dir_remove(directory);
}

TEST(SUITE, invalid_config_does_not_start_a_session) {
	char directory[TEST_PATH_SIZE];
	temp_dir_create(directory);
	FakeClock clock_state;
	GameSession session;
	EventList events = {0};
	char error[256] = "";
	RunConfig config = run_config("Alex", "knight", "normal", false);
	CHECK(!session_start(&session, test_data(), &config, 5, file_repositories(directory), fake_clock(&clock_state), "1",
	    &events, error, sizeof(error)));
	CHECK_CONTAINS(error, "invalid run config");
	CHECK(!fs_exists(path_in(directory, "save.json")));
	events_free(&events);
	temp_dir_remove(directory);
}

TEST(SUITE, quit_mid_battle_resumes_from_last_merchant) {
	char directory[TEST_PATH_SIZE];
	temp_dir_create(directory);
	FakeClock clock_state;
	Clock clock = fake_clock(&clock_state);
	GameSession session;
	if (start(&session, directory, clock, run_config("Alex", "warrior", "normal", false), 5, "1", NULL)) {
		session_play(&session, cmd_next_fight());
		if (session.engine.state.has_monster) {
			session.engine.state.monster.hp = 1000000;
			session.engine.state.monster.max_hp = 1000000;
		} else {
			CHECK(session.engine.state.has_monster);
		}
		session_play(&session, cmd_attack());
		CHECK_INT(session.engine.state.phase, PHASE_BATTLE);
		session_save_and_quit(&session);

		GameSession resumed;
		char error[256] = "";
		LoadResult result =
		    session_resume(&resumed, test_data(), file_repositories(directory), clock, "1", error, sizeof(error));
		if (result == LOAD_OK) {
			CHECK_INT(resumed.engine.state.phase, PHASE_MERCHANT);
			CHECK_INT(resumed.engine.state.round, 0);
			CHECK(!resumed.engine.state.has_monster);
			CHECK_INT(resumed.info.sessions, 2);
			CHECK(resumed.info.play_time_seconds > 0);
			CHECK_STR(resumed.info.run_id, session.info.run_id);
			session_free(&resumed);
		} else {
			test_fail(__FILE__, __LINE__, "the save did not resume: %s", error);
		}
		session_free(&session);
	}
	temp_dir_remove(directory);
}

TEST(SUITE, resume_without_save) {
	char directory[TEST_PATH_SIZE];
	temp_dir_create(directory);
	FakeClock clock_state;
	GameSession session;
	char error[256] = "";
	CHECK_INT(session_resume(&session, test_data(), file_repositories(directory), fake_clock(&clock_state), "1", error,
	              sizeof(error)),
	    LOAD_MISSING);
	temp_dir_remove(directory);
}

static bool unlocked_contains(const AchievementList *unlocked, const char *achievement_id) {
	for (size_t i = 0; i < unlocked->count; i++) {
		if (str_eq(unlocked->items[i]->id, achievement_id)) {
			return true;
		}
	}
	return false;
}

TEST(SUITE, death_writes_history_profile_and_deletes_save) {
	char directory[TEST_PATH_SIZE];
	temp_dir_create(directory);
	FakeClock clock_state;
	Repositories repositories = file_repositories(directory);
	GameSession session;
	if (start(&session, directory, fake_clock(&clock_state), run_config("Bot", "mage", "hard", false), 3, "1", NULL)) {
		const RunState *state = &session.engine.state;
		AchievementList unlocked = {0};
		EventList events = {0};
		for (int i = 0; i < MAX_STEPS && state->phase != PHASE_GAME_OVER; i++) {
			Command command = bot_choose(test_data(), state);
			events_clear(&events);
			session_step(&session, &command, &events, &unlocked);
		}
		events_free(&events);
		CHECK_INT(state->phase, PHASE_GAME_OVER);
		CHECK(!fs_exists(path_in(directory, "save.json")));

		RunRecord *records = NULL;
		size_t record_count = 0;
		char error[256] = "";
		CHECK_INT(history_list(directory, &records, &record_count, error, sizeof(error)), LOAD_OK);
		CHECK_INT(record_count, 1);
		Profile profile;
		CHECK_INT(repositories.load_profile(repositories.context, &profile, error, sizeof(error)), LOAD_OK);
		if (record_count == 1) {
			const RunRecord *record = &records[0];
			CHECK_INT(record->round, state->round);
			CHECK_STR(record->death_cause, state->has_death_cause ? state->death_cause : "");
			CHECK_STR(record->implementation, "c");
			// Timestamps have a fixed width, so text order is time order.
			CHECK(strcmp(record->ended_at, record->started_at) > 0);
			CHECK(session.has_finished_record);
			JsonValue *written = run_record_to_json(record);
			JsonValue *kept = run_record_to_json(&session.finished_record);
			CHECK_JSON_EQUAL(written, kept);
			json_free(written);
			json_free(kept);
			CHECK(profile.hall_count > 0 && str_eq(profile.hall_of_fame[0].run_id, record->run_id));
		}
		CHECK(unlocked_contains(&unlocked, "first_blood"));
		CHECK(profile_unlock(&profile, "first_blood") != NULL);
		int64_t kills = 0;
		for (size_t i = 0; i < profile.bestiary_count; i++) {
			kills += profile.bestiary[i].kills;
		}
		CHECK_INT(kills, state->round - 1);
		session_save_and_quit(&session);
		CHECK(!fs_exists(path_in(directory, "save.json")));

		profile_free(&profile);
		for (size_t i = 0; i < record_count; i++) {
			run_record_free(&records[i]);
		}
		free(records);
		achievement_list_free(&unlocked);
		session_free(&session);
	}
	temp_dir_remove(directory);
}

TEST(SUITE, newer_schema_is_refused) {
	char directory[TEST_PATH_SIZE];
	temp_dir_create(directory);
	Repositories repositories = file_repositories(directory);
	const char *newer = "{\"schemaVersion\": 99}";
	char error[256] = "";

	write_file(path_in(directory, "save.json"), newer);
	SaveGame save;
	CHECK_INT(repositories.load_save(repositories.context, &save, error, sizeof(error)), LOAD_FAILED);
	CHECK_CONTAINS(error, "schema 99");
	// A refused file is left alone.
	char *text = read_file(path_in(directory, "save.json"));
	if (text != NULL) {
		CHECK_STR(text, newer);
		free(text);
	}

	error[0] = '\0';
	write_file(path_in(directory, "profile.json"), newer);
	Profile profile;
	CHECK_INT(repositories.load_profile(repositories.context, &profile, error, sizeof(error)), LOAD_FAILED);
	CHECK_CONTAINS(error, "schema 99");

	error[0] = '\0';
	write_file(path_in(directory, "settings.json"), newer);
	Settings settings;
	CHECK_INT(settings_load(directory, &settings, error, sizeof(error)), LOAD_FAILED);
	CHECK_CONTAINS(error, "schema 99");

	error[0] = '\0';
	write_file(path_in(directory, "history/x.json"), newer);
	RunRecord *records = NULL;
	size_t record_count = 0;
	CHECK_INT(history_list(directory, &records, &record_count, error, sizeof(error)), LOAD_FAILED);
	CHECK_CONTAINS(error, "schema 99");
	CHECK_INT(record_count, 0);

	error[0] = '\0';
	write_file(path_in(directory, "save.json"), "{ broken");
	CHECK_INT(repositories.load_save(repositories.context, &save, error, sizeof(error)), LOAD_FAILED);
	CHECK_CONTAINS(error, "save.json");

	// A session cannot resume from it either.
	FakeClock clock_state;
	GameSession session;
	CHECK_INT(session_resume(&session, test_data(), repositories, fake_clock(&clock_state), "1", error, sizeof(error)),
	    LOAD_FAILED);
	temp_dir_remove(directory);
}

static void check_settings(const char *directory, bool has_locale, const char *locale, bool auto_equip, int64_t speed) {
	Settings settings;
	char error[256] = "";
	if (settings_load(directory, &settings, error, sizeof(error)) != LOAD_OK) {
		test_fail(__FILE__, __LINE__, "the settings did not load: %s", error);
		return;
	}
	if (settings.has_locale != has_locale || (has_locale && !str_eq(settings.locale, locale)) ||
	    settings.auto_equip != auto_equip || settings.battle_speed != speed) {
		test_fail(__FILE__, __LINE__, "settings: expected (%s, %d, %lld), got (%s, %d, %lld)",
		    has_locale ? locale : "none", auto_equip, (long long)speed, settings.has_locale ? settings.locale : "none",
		    settings.auto_equip, (long long)settings.battle_speed);
	}
}

TEST(SUITE, settings_round_trip) {
	char directory[TEST_PATH_SIZE];
	temp_dir_create(directory);
	char path[TEST_PATH_SIZE];
	path_join(path, directory, "settings.json");
	check_settings(directory, false, "", false, 1);

	Settings portuguese = {.has_locale = true, .locale = "pt-BR", .auto_equip = true, .battle_speed = 2};
	settings_save(directory, &portuguese);
	check_settings(directory, true, "pt-BR", true, 2);
	JsonValue *saved = load_json_file(path);
	if (saved != NULL) {
		CHECK_JSON(saved, "{\"schemaVersion\": 2, \"locale\": \"pt-BR\", \"autoEquip\": true, \"battleSpeed\": 2}");
		json_free(saved);
	}

	Settings defaults = default_settings();
	settings_save(directory, &defaults);
	check_settings(directory, false, "", false, 1);
	write_file(path, "{\"schemaVersion\": 1, \"locale\": \"fr\"}");
	check_settings(directory, false, "", false, 1);
	write_file(path, "{\"schemaVersion\": 1, \"locale\": \"en\"}");
	check_settings(directory, true, "en", false, 1);
	write_file(path, "{\"schemaVersion\": 2, \"autoEquip\": true, \"battleSpeed\": 7}");
	check_settings(directory, false, "", true, 1);
	temp_dir_remove(directory);
}

static HallOfFameEntry hall_entry(const char *run_id, const char *name, const char *difficulty, int64_t round,
    int64_t level, const char *ended_at, bool won) {
	HallOfFameEntry entry;
	memset(&entry, 0, sizeof(entry));
	str_copy(entry.run_id, RUN_ID_SIZE, run_id);
	str_copy(entry.name, NAME_SIZE, name);
	id_set(entry.vocation, "mage");
	id_set(entry.difficulty, difficulty);
	entry.round = round;
	entry.level = level;
	str_copy(entry.ended_at, TIMESTAMP_SIZE, ended_at);
	entry.won = won;
	return entry;
}

TEST(SUITE, profile_round_trip_and_hall_of_fame_order) {
	char directory[TEST_PATH_SIZE];
	temp_dir_create(directory);
	Profile profile;
	memset(&profile, 0, sizeof(profile));
	for (int index = 0; index < 12; index++) {
		HallOfFameEntry entry = hall_entry(
		    fmt("run%d", index), "A", "normal", index % 5, index, fmt("2026-01-%02dT00:00:00Z", index + 1), false);
		profile_record_finished_run(&profile, &entry);
	}
	REQUIRE_INT(profile.hall_count, 10);
	CHECK_INT(profile.hall_of_fame[0].round, 4);
	CHECK_INT(profile.hall_of_fame[1].round, 4);
	CHECK_INT(profile.hall_of_fame[2].round, 3);
	CHECK(profile.hall_of_fame[0].level > profile.hall_of_fame[1].level);
	HallOfFameEntry winner = hall_entry("winner", "W", "easy", 1, 1, "2026-02-01T00:00:00Z", true);
	profile_record_finished_run(&profile, &winner);
	CHECK_INT(profile.hall_count, 10);
	CHECK_STR(profile.hall_of_fame[0].run_id, "winner");

	Repositories repositories = file_repositories(directory);
	repositories.save_profile(repositories.context, &profile);
	Profile loaded;
	char error[256] = "";
	if (repositories.load_profile(repositories.context, &loaded, error, sizeof(error)) == LOAD_OK) {
		JsonValue *expected = profile_to_json(&profile);
		JsonValue *actual = profile_to_json(&loaded);
		CHECK_JSON_EQUAL(actual, expected);
		json_free(expected);
		json_free(actual);
		CHECK_INT(loaded.hall_count, 10);
		CHECK(loaded.hall_of_fame[0].won);
		profile_free(&loaded);
	} else {
		test_fail(__FILE__, __LINE__, "the profile did not load: %s", error);
	}
	CHECK(!profile_revealed(&profile, "rat"));
	profile_free(&profile);
	temp_dir_remove(directory);
}

TEST(SUITE, run_id_timestamps_and_clock) {
	const int64_t moment = 1767323045; // 2026-01-02T03:04:05Z
	char run_id[RUN_ID_SIZE];
	make_run_id(moment, 42, run_id);
	CHECK_STR(run_id, "20260102T030405Z-42");
	char timestamp[TIMESTAMP_SIZE];
	format_timestamp(moment, timestamp);
	CHECK_STR(timestamp, "2026-01-02T03:04:05Z");
	format_timestamp(0, timestamp);
	CHECK_STR(timestamp, "1970-01-01T00:00:00Z");
	format_timestamp(951868799, timestamp); // the last second of a leap day in a century year
	CHECK_STR(timestamp, "2000-02-29T23:59:59Z");
	Clock clock = system_clock();
	CHECK(clock.now(clock.context) > moment);

	FakeClock clock_state;
	Clock fake = fake_clock(&clock_state);
	format_timestamp(fake.now(fake.context), timestamp);
	CHECK_STR(timestamp, "2026-09-27T12:00:10Z");
	format_timestamp(fake.now(fake.context), timestamp);
	CHECK_STR(timestamp, "2026-09-27T12:00:20Z");
}

// ── saves written by the Python reference ───────────────────────────────────

static void install_fixtures(const char *directory, const char *save, const char *profile) {
	copy_file(path_in(RPG_FIXTURES_DIR, save), path_in(directory, "save.json"));
	copy_file(path_in(RPG_FIXTURES_DIR, profile), path_in(directory, "profile.json"));
}

// Loads the save and the profile through the repositories and compares what they serialise back to.
static void check_loaded_documents(
    const char *directory, const JsonValue *expected_run, const JsonValue *expected_profile, bool auto_equip) {
	Repositories repositories = file_repositories(directory);
	char error[256] = "";
	SaveGame save;
	if (repositories.load_save(repositories.context, &save, error, sizeof(error)) == LOAD_OK) {
		CHECK_STR(save.implementation, "python");
		CHECK(save.run.config.auto_equip == auto_equip);
		JsonValue *run = run_state_to_json(&save.run);
		CHECK_JSON_EQUAL(run, expected_run);
		json_free(run);
		save_game_free(&save);
	} else {
		test_fail(__FILE__, __LINE__, "the Python save did not load: %s", error);
	}
	Profile profile;
	if (repositories.load_profile(repositories.context, &profile, error, sizeof(error)) == LOAD_OK) {
		CHECK(profile.bestiary_count > 0);
		JsonValue *document = profile_to_json(&profile);
		CHECK_JSON_EQUAL(document, expected_profile);
		json_free(document);
		profile_free(&profile);
	} else {
		test_fail(__FILE__, __LINE__, "the Python profile did not load: %s", error);
	}
}

// Plays a resumed session with the bot until the run ends and compares it with the Python continuation.
static void check_python_continuation(const char *directory, const char *expected_file) {
	JsonValue *expected = load_json_file(path_in(RPG_FIXTURES_DIR, expected_file));
	if (expected == NULL) {
		return;
	}
	FakeClock clock_state;
	GameSession session;
	char error[256] = "";
	if (session_resume(&session, test_data(), file_repositories(directory), fake_clock(&clock_state), "1", error,
	        sizeof(error)) != LOAD_OK) {
		test_fail(__FILE__, __LINE__, "%s: the save did not resume: %s", expected_file, error);
		json_free(expected);
		return;
	}
	CHECK_INT(session.info.sessions, 2);
	const RunState *state = &session.engine.state;
	EventList events = {0};
	int64_t steps = 0;
	while (state->phase != PHASE_GAME_OVER && steps < MAX_STEPS) {
		Command command = bot_choose(test_data(), state);
		events_clear(&events);
		session_step(&session, &command, &events, NULL);
		steps++;
	}
	events_free(&events);
	JsonError json_error = {0};
	CHECK_INT(steps, json_read_int(expected, "steps", &json_error));
	CHECK_INT(state->round, json_read_int(expected, "round", &json_error));
	CHECK_INT(state->player.level, json_read_int(expected, "level", &json_error));
	CHECK_INT(state->player.gold, json_read_int(expected, "gold", &json_error));
	CHECK_INT(session.engine.rng.state, json_read_int(expected, "rngState", &json_error));
	CHECK_STR(state->has_death_cause ? state->death_cause : "", json_read_str(expected, "deathCause", &json_error));
	CHECK(state->won == json_read_bool(expected, "won", &json_error));
	JsonValue *stats = statistics_to_json(&state->stats);
	CHECK_JSON_EQUAL(stats, json_get(expected, "stats"));
	json_free(stats);
	CHECK(!json_error.failed);
	// The run ended: the save is gone and the run is in the history.
	CHECK(!fs_exists(path_in(directory, "save.json")));
	size_t history_count = 0;
	char **history = fs_list(path_in(directory, "history"), ".json", &history_count);
	CHECK_INT(history_count, 1);
	fs_free_names(history, history_count);
	session_free(&session);
	json_free(expected);
}

TEST(SUITE, python_save_continues_identically) {
	char directory[TEST_PATH_SIZE];
	temp_dir_create(directory);
	install_fixtures(directory, "python_save.json", "python_profile.json");
	JsonValue *save = load_json_file(path_in(RPG_FIXTURES_DIR, "python_save.json"));
	JsonValue *profile = load_json_file(path_in(RPG_FIXTURES_DIR, "python_profile.json"));
	if (save != NULL && profile != NULL) {
		// The parsed run serialises back to exactly the Python document.
		check_loaded_documents(directory, json_get(save, "run"), profile, true);
		check_python_continuation(directory, "python_save_continued.json");
	}
	json_free(save);
	json_free(profile);
	temp_dir_remove(directory);
}

TEST(SUITE, python_v1_save_is_migrated_and_continues_identically) {
	char directory[TEST_PATH_SIZE];
	temp_dir_create(directory);
	install_fixtures(directory, "python_save_v1.json", "python_profile_v1.json");
	JsonValue *migrated = load_json_file(path_in(RPG_FIXTURES_DIR, "python_save_v1_migrated.json"));
	if (migrated != NULL) {
		const JsonValue *run = json_get(migrated, "run");
		bool auto_equip = false;
		JsonError error = {0};
		auto_equip = json_read_bool(json_read_object(run, "config", &error), "autoEquip", &error);
		CHECK(!error.failed);
		check_loaded_documents(directory, run, json_get(migrated, "profile"), auto_equip);
		check_python_continuation(directory, "python_save_v1_continued.json");
	}
	json_free(migrated);
	temp_dir_remove(directory);
}
