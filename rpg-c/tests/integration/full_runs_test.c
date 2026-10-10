// Whole runs driven by the bot: the engine must always terminate, be deterministic and survive save/restore.
#include "application/bot.h"
#include "support/helpers.h"

#include <stdlib.h>

#define SUITE "integration/full_runs"

#define MAX_STEPS 50000

static GameEngine bot_engine(const char *vocation, const char *difficulty, int64_t seed, JsonValue *log) {
	GameEngine engine;
	EventList events = {0};
	char error[128];
	RunConfig config = run_config("Bot", vocation, difficulty, false);
	if (!engine_new_run(&engine, test_data(), &config, seed, &events, error, sizeof(error))) {
		fatal("%s", error);
	}
	if (log != NULL) {
		json_push(log, events_to_json(&events));
	}
	events_free(&events);
	return engine;
}

// Plays until the run ends, appending the events of every command to `log` (a JSON array, may be NULL).
static bool play_to_death(GameEngine *engine, JsonValue *log) {
	for (int i = 0; i < MAX_STEPS; i++) {
		if (engine->state.phase == PHASE_GAME_OVER) {
			return true;
		}
		const EventList *events = step(engine, bot_choose(test_data(), &engine->state));
		if (has_event(events, "error")) {
			test_fail(__FILE__, __LINE__, "the bot issued an invalid command at step %d: %s", i, event_types(events));
			return false;
		}
		if (log != NULL) {
			json_push(log, events_to_json(events));
		}
	}
	test_fail(__FILE__, __LINE__, "run did not finish");
	return false;
}

TEST(SUITE, bot_plays_until_the_run_ends) {
	const GameData *data = test_data();
	const char *const vocations[] = {"warrior", "archer", "mage"};
	const char *const difficulties[] = {"easy", "normal", "hard"};
	for (size_t v = 0; v < ARRAY_LEN(vocations); v++) {
		for (size_t d = 0; d < ARRAY_LEN(difficulties); d++) {
			GameEngine engine = bot_engine(vocations[v], difficulties[d], 1234, NULL);
			JsonValue *log = json_array();
			if (play_to_death(&engine, log) && log->count >= 2) {
				const RunState *state = &engine.state;
				const JsonValue *last = log->items[log->count - 1];
				CHECK_INT(state->phase, PHASE_GAME_OVER);
				CHECK(state->round >= 1);
				CHECK(state->stats.damage_dealt > 0);
				if (state->won) {
					CHECK_INT(state->round, data->balance.final_round);
					CHECK(!state->has_death_cause);
					const JsonValue *before = log->items[log->count - 2];
					REQUIRE(before->count > 0);
					CHECK_JSON(before->items[before->count - 1],
					    fmt("{\"type\": \"run_won\", \"round\": %lld}", (long long)state->round));
					CHECK_JSON(last, "[{\"type\": \"run_ended\", \"won\": true}]");
					CHECK_INT(counter_total(&state->stats.kills), state->round);
				} else {
					CHECK(state->has_death_cause && state->death_cause[0] != '\0');
					REQUIRE(last->count > 0);
					JsonError error = {0};
					CHECK_STR(json_read_str(last->items[last->count - 1], "type", &error), "player_died");
					CHECK_INT(counter_total(&state->stats.kills), state->round - 1);
				}
			} else {
				test_fail(__FILE__, __LINE__, "%s/%s did not play to the end", vocations[v], difficulties[d]);
			}
			json_free(log);
			engine_free(&engine);
		}
	}
}

// The balance must keep the victory reachable (the balance gate tunes the rates).
TEST(SUITE, some_bot_runs_are_won) {
	int won = 0;
	for (int64_t seed = 2002; seed < 2006; seed++) {
		GameEngine engine = bot_engine("archer", "easy", seed, NULL);
		play_to_death(&engine, NULL);
		won += engine.state.won ? 1 : 0;
		engine_free(&engine);
	}
	CHECK(won > 0);
}

TEST(SUITE, same_seed_same_events) {
	JsonValue *logs[2];
	for (int i = 0; i < 2; i++) {
		logs[i] = json_array();
		GameEngine engine = bot_engine("archer", "normal", 777, logs[i]);
		play_to_death(&engine, logs[i]);
		engine_free(&engine);
	}
	CHECK(logs[0]->count > 1);
	CHECK(json_equal(logs[0], logs[1]));
	json_free(logs[0]);
	json_free(logs[1]);
}

TEST(SUITE, different_seeds_diverge) {
	char *finals[3];
	for (int64_t seed = 1; seed <= 3; seed++) {
		GameEngine engine = bot_engine("warrior", "normal", seed, NULL);
		play_to_death(&engine, NULL);
		JsonValue *stats = statistics_to_json(&engine.state.stats);
		finals[seed - 1] = json_dump(stats, false);
		json_free(stats);
		engine_free(&engine);
	}
	CHECK(strcmp(finals[0], finals[1]) != 0 || strcmp(finals[1], finals[2]) != 0);
	for (int i = 0; i < 3; i++) {
		free(finals[i]);
	}
}

TEST(SUITE, restore_mid_run_continues_identically) {
	const GameData *data = test_data();
	GameEngine reference = bot_engine("mage", "hard", 99, NULL);
	JsonValue *reference_log = json_array();
	play_to_death(&reference, reference_log);
	engine_free(&reference);

	GameEngine engine = bot_engine("mage", "hard", 99, NULL);
	JsonValue *log = json_array();
	int restores = 0;
	for (int i = 0; i < MAX_STEPS && engine.state.phase != PHASE_GAME_OVER; i++) {
		if (engine.state.phase == PHASE_MERCHANT && engine.state.round % 3 == 0) {
			// Through text, like a save file: serialise, parse, rebuild the engine from the copy.
			JsonValue *snapshot = run_state_to_json(&engine.state);
			char *text = json_dump(snapshot, false);
			JsonError error = {0};
			JsonValue *parsed = json_parse(text, &error);
			RunState restored;
			bool ok = parsed != NULL && run_state_from_json(parsed, &restored, &error);
			json_free(parsed);
			free(text);
			json_free(snapshot);
			if (!ok) {
				test_fail(__FILE__, __LINE__, "snapshot does not load: %s", error.message);
				break;
			}
			uint32_t rng_state = engine.rng.state;
			engine_free(&engine);
			engine_restore(&engine, data, restored, rng_state);
			restores++;
		}
		json_push(log, events_to_json(step(&engine, bot_choose(data, &engine.state))));
	}
	CHECK(restores > 1);
	CHECK_INT(log->count, reference_log->count);
	CHECK(json_equal(log, reference_log));
	json_free(log);
	json_free(reference_log);
	engine_free(&engine);
}

TEST(SUITE, new_run_rejects_invalid_config) {
	const RunConfig configs[] = {
	    run_config("X", "knight", "normal", false), run_config("X", "mage", "nightmare", false)};
	for (size_t i = 0; i < ARRAY_LEN(configs); i++) {
		GameEngine engine;
		EventList events = {0};
		char error[128] = "";
		CHECK(!engine_new_run(&engine, test_data(), &configs[i], 1, &events, error, sizeof(error)));
		CHECK_CONTAINS(error, "invalid run config");
		CHECK_INT(events.count, 0);
		events_free(&events);
	}
}
