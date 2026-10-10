#include "application/commands.h"
#include "application/run_state.h"
#include "support/helpers.h"

#include <stdlib.h>

#define SUITE "unit/serialization"

// Through text and back, like the reference's json.loads(json.dumps(...)). Takes ownership of `value`.
static JsonValue *through_text(JsonValue *value) {
	char *text = json_dump(value, false);
	JsonError error = {0};
	JsonValue *parsed = json_parse(text, &error);
	if (parsed == NULL) {
		test_fail(__FILE__, __LINE__, "%s: %s", error.message, text);
	}
	free(text);
	json_free(value);
	return parsed;
}

TEST(SUITE, command_round_trip) {
	const Command commands[] = {
	    cmd_attack(),
	    cmd_cast("brutal_strike"),
	    cmd_use_potion("health_potion"),
	    cmd_defend(),
	    cmd_next_fight(),
	    cmd_buy_potion("mana_potion", 3),
	    cmd_sell_item(4),
	    cmd_equip(5),
	    cmd_unequip(SLOT_RING),
	    cmd_buy_stock_item(1),
	    cmd_end_run(),
	    cmd_continue_run(),
	};
	for (size_t i = 0; i < ARRAY_LEN(commands); i++) {
		JsonValue *raw = through_text(command_to_json(&commands[i]));
		REQUIRE(raw != NULL);
		Command restored;
		JsonError error = {0};
		if (command_from_json(raw, &restored, &error)) {
			CHECK_COMMAND(restored, commands[i]);
		} else {
			test_fail(__FILE__, __LINE__, "command #%zu does not load: %s", i, error.message);
		}
		json_free(raw);
	}
}

TEST(SUITE, unknown_command_type) {
	JsonError error = {0};
	JsonValue *raw = json_parse("{\"type\": \"dance\"}", &error);
	REQUIRE(raw != NULL);
	Command command;
	CHECK(!command_from_json(raw, &command, &error));
	CHECK(error.failed);
	CHECK_CONTAINS(error.message, "unknown command");
	json_free(raw);
}

TEST(SUITE, run_state_round_trip_through_json) {
	GameEngine engine = new_engine(test_data(), "warrior", "normal", 42);
	step(&engine, cmd_next_fight());
	for (int i = 0; i < 3; i++) {
		step(&engine, cmd_attack());
	}
	RunState *state = &engine.state;
	ItemInstance sword = make_item(90, "sword", "mythic", 2);
	sword.affix_count = 1;
	sword.affixes[0] = (AffixRoll){STAT_DODGE, 3};
	player_bag_push(&state->player, &sword);
	status_list_push(&state->player.statuses, make_status("burn", 2, 4));
	if (state->has_monster) {
		status_list_push(&state->monster.statuses, make_status("stun", 1, 0));
	}

	JsonValue *original = run_state_to_json(state);
	JsonValue *raw = through_text(run_state_to_json(state));
	REQUIRE(raw != NULL);
	RunState restored;
	JsonError error = {0};
	if (run_state_from_json(raw, &restored, &error)) {
		// Field by field for what the test added, then the whole state through its document.
		CHECK_INT(restored.round, state->round);
		CHECK_INT(restored.phase, state->phase);
		CHECK(restored.has_monster == state->has_monster);
		int index = player_bag_index(&restored.player, 90);
		CHECK(index >= 0 && items_equal(&restored.player.bag[index], &sword));
		int last = restored.player.statuses.count - 1;
		CHECK(last >= 0 && str_eq(restored.player.statuses.items[last].status_id, "burn") &&
		      restored.player.statuses.items[last].turns == 2 && restored.player.statuses.items[last].per_turn == 4);
		JsonValue *again = run_state_to_json(&restored);
		CHECK_JSON_EQUAL(again, original);
		json_free(again);
		run_state_free(&restored);
	} else {
		test_fail(__FILE__, __LINE__, "the run state does not load: %s", error.message);
	}
	json_free(raw);
	json_free(original);
	engine_free(&engine);
}

TEST(SUITE, json_readers_reject_wrong_types) {
	JsonError parse_error = {0};
	JsonValue *holder = json_parse(
	    "{\"list\": [], \"object\": {}, \"bool\": true, \"text\": \"1\", \"int\": 1, \"zero\": 0}", &parse_error);
	REQUIRE(holder != NULL);

	JsonError error = {0};
	CHECK_INT(json_read_object(holder, "list", &error)->count, 0);
	CHECK(error.failed);

	error = (JsonError){0};
	CHECK_INT(json_read_array(holder, "object", &error)->count, 0);
	CHECK(error.failed);

	error = (JsonError){0};
	CHECK_INT(json_as_int(json_get(holder, "bool"), &error), 0);
	CHECK(error.failed);

	error = (JsonError){0};
	CHECK_INT(json_as_int(json_get(holder, "text"), &error), 0);
	CHECK(error.failed);

	error = (JsonError){0};
	CHECK_STR(json_as_str(json_get(holder, "int"), &error), "");
	CHECK(error.failed);

	error = (JsonError){0};
	CHECK(!json_as_bool(json_get(holder, "zero"), &error));
	CHECK(error.failed);

	json_free(holder);
}
