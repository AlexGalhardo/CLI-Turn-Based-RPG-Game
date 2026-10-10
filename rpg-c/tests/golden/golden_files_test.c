// Replays every shared/golden scenario; every other implementation's suite runs the same files.
#include "application/bot.h"
#include "application/engine.h"
#include "infrastructure/filesystem.h"
#include "support/helpers.h"

#include <stdio.h>
#include <stdlib.h>

#define SUITE "golden/golden_files"

static JsonValue *final_state(const GameEngine *engine) {
	const RunState *state = &engine->state;
	const Player *player = &state->player;
	JsonValue *object = json_object();
	json_set(object, "phase", json_string(phase_name(state->phase)));
	json_set(object, "round", json_int(state->round));
	json_set(object, "turn", json_int(state->turn));
	json_set(object, "level", json_int(player->level));
	json_set(object, "xp", json_int(player->xp));
	json_set(object, "magicLevel", json_int(player->magic_level));
	json_set(object, "hp", json_int(player->hp));
	json_set(object, "mp", json_int(player->mp));
	json_set(object, "gold", json_int(player->gold));
	json_set(object, "rngState", json_int(engine->rng.state));
	json_set(object, "nextItemUid", json_int(state->next_item_uid));
	json_set(object, "stats", statistics_to_json(&state->stats));
	return object;
}

// Compares the events of one command with the golden list, reporting the first difference as JSON.
static bool events_match(const EventList *events, const JsonValue *expected, const char *name, size_t index) {
	JsonValue *actual = events_to_json(events);
	bool equal = json_equal(actual, expected);
	if (!equal) {
		char *actual_text = json_dump(actual, false);
		char *expected_text = json_dump(expected, false);
		test_fail(__FILE__, __LINE__, "%s: command #%zu\n  expected %s\n  got      %s", name, index, expected_text,
		    actual_text);
		free(actual_text);
		free(expected_text);
	}
	json_free(actual);
	return equal;
}

static void replay(const char *name, const char *path) {
	JsonValue *golden = load_json_file(path);
	if (golden == NULL) {
		return;
	}
	JsonError error = {0};
	RunConfig config;
	run_config_from_json(json_read_object(golden, "config", &error), &config, &error);
	int64_t seed = json_read_int(golden, "seed", &error);
	const JsonValue *commands = json_read_array(golden, "commands", &error);
	const JsonValue *expected_events = json_read_array(golden, "events", &error);
	CHECK(!error.failed);
	CHECK_INT(expected_events->count, commands->count + 1);

	// The bot is part of the contract: in a bot-driven scenario it must issue exactly the recorded commands.
	bool bot_driven = str_starts_with(name, "bot-full-run-");
	GameEngine engine;
	EventList events = {0};
	char run_error[128];
	if (error.failed || !engine_new_run(&engine, test_data(), &config, seed, &events, run_error, sizeof(run_error))) {
		test_fail(__FILE__, __LINE__, "%s: cannot start the run", name);
		json_free(golden);
		return;
	}
	bool ok = events_match(&events, expected_events->items[0], name, 0);
	for (size_t i = 0; ok && i < commands->count; i++) {
		Command command;
		if (!command_from_json(commands->items[i], &command, &error)) {
			test_fail(__FILE__, __LINE__, "%s: command #%zu: %s", name, i + 1, error.message);
			ok = false;
			break;
		}
		if (bot_driven) {
			Command chosen = bot_choose(test_data(), &engine.state);
			if (!command_equal(&chosen, &command)) {
				JsonValue *chosen_json = command_to_json(&chosen);
				char *chosen_text = json_dump(chosen_json, false);
				char *expected_text = json_dump(commands->items[i], false);
				test_fail(__FILE__, __LINE__, "%s: bot command #%zu: expected %s, got %s", name, i + 1, expected_text,
				    chosen_text);
				free(chosen_text);
				free(expected_text);
				json_free(chosen_json);
				ok = false;
				break;
			}
		}
		events_clear(&events);
		engine_step(&engine, &command, &events);
		ok = events_match(&events, expected_events->items[i + 1], name, i + 1);
	}
	if (ok) {
		if (bot_driven) {
			CHECK_INT(engine.state.phase, PHASE_GAME_OVER);
		}
		JsonValue *summary = final_state(&engine);
		CHECK(json_equal(summary, json_get(golden, "finalState")));
		json_free(summary);

		// The whole run state serialises exactly like the reference's, and survives a round trip.
		JsonValue *final_run = run_state_to_json(&engine.state);
		CHECK(json_equal(final_run, json_get(golden, "finalRun")));
		json_free(final_run);
		RunState restored;
		if (run_state_from_json(json_get(golden, "finalRun"), &restored, &error)) {
			JsonValue *round_trip = run_state_to_json(&restored);
			CHECK(json_equal(round_trip, json_get(golden, "finalRun")));
			json_free(round_trip);
			run_state_free(&restored);
		} else {
			test_fail(__FILE__, __LINE__, "%s: finalRun does not load: %s", name, error.message);
		}
	}
	events_free(&events);
	engine_free(&engine);
	json_free(golden);
}

TEST(SUITE, replay_matches_golden) {
	size_t count = 0;
	char **names = fs_list(RPG_GOLDEN_DIR, ".json", &count);
	int scenarios = 0;
	for (size_t i = 0; i < count; i++) {
		if (strcmp(names[i], "prng.json") == 0) {
			continue;
		}
		char path[TEST_PATH_SIZE];
		snprintf(path, sizeof(path), "%s/%s", RPG_GOLDEN_DIR, names[i]);
		char name[128];
		snprintf(name, sizeof(name), "%.*s", (int)(strlen(names[i]) - 5), names[i]);
		replay(name, path);
		scenarios++;
	}
	fs_free_names(names, count);
	CHECK(scenarios >= 11);
}
