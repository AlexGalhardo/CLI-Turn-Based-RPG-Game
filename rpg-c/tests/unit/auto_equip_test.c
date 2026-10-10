// Auto-equip with auto-sell (docs/game-design.md §8.1).
#include "application/auto_equip.h"
#include "domain/character.h"
#include "support/helpers.h"

#define SUITE "unit/auto_equip"

static GameData items_data(void) {
	GameData data = data_copy();
	add_test_items(&data);
	return data;
}

static GameEngine auto_engine(const GameData *data, bool enabled) {
	GameEngine engine;
	EventList events = {0};
	char error[128];
	RunConfig config = run_config("Auto", "warrior", "normal", enabled);
	if (!engine_new_run(&engine, data, &config, 42, &events, error, sizeof(error))) {
		fatal("%s", error);
	}
	events_free(&events);
	return engine;
}

TEST(SUITE, better_item_is_equipped_and_the_old_one_sold) {
	GameData data = items_data();
	GameEngine engine = auto_engine(&data, true);
	RunState *state = &engine.state;
	REQUIRE(state->player.equipped[SLOT_WEAPON]);
	ItemInstance starter = state->player.equipment[SLOT_WEAPON];
	ItemInstance axe = make_item(50, "test_axe", "common", 0);
	player_bag_push(&state->player, &axe);
	int64_t gold = state->player.gold;
	uint32_t rng_state = engine.rng.state;
	EventList events = {0};
	auto_equip(state, &data, &events);
	CHECK_EVENTS(&events,
	    fmt("[{\"type\": \"item_auto_equipped\", \"uid\": 50, \"itemId\": \"test_axe\", \"slot\": \"weapon\","
	        " \"score\": %lld},"
	        " {\"type\": \"item_auto_sold\", \"uid\": %lld, \"itemId\": \"sword\", \"gold\": %lld}]",
	        (long long)item_score(&axe, &data), (long long)starter.uid, (long long)item_value(&starter, &data)));
	CHECK(state->player.equipped[SLOT_WEAPON]);
	CHECK(items_equal(&state->player.equipment[SLOT_WEAPON], &axe));
	CHECK_INT(state->player.gold, gold + item_value(&starter, &data));
	CHECK_INT(state->player.bag_count, 0);
	CHECK_INT(engine.rng.state, rng_state);
	events_free(&events);
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, empty_slots_are_filled_without_selling) {
	GameData data = items_data();
	GameEngine engine = auto_engine(&data, true);
	RunState *state = &engine.state;
	ItemInstance helmet = make_item(60, "test_helmet", "common", 0);
	player_bag_push(&state->player, &helmet);
	EventList events = {0};
	auto_equip(state, &data, &events);
	CHECK_STR(event_types(&events), "item_auto_equipped");
	CHECK(state->player.equipped[SLOT_HELMET]);
	CHECK_INT(state->player.equipment[SLOT_HELMET].uid, 60);
	events_free(&events);
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, ties_go_to_the_lowest_uid_and_worse_items_stay) {
	GameData data = items_data();
	GameEngine engine = auto_engine(&data, true);
	RunState *state = &engine.state;
	const ItemInstance items[] = {
	    make_item(72, "test_helmet", "common", 0),
	    make_item(71, "test_helmet", "common", 0),
	    make_item(73, "test_rod", "common", 0),
	};
	for (size_t i = 0; i < ARRAY_LEN(items); i++) {
		player_bag_push(&state->player, &items[i]);
	}
	EventList events = {0};
	auto_equip(state, &data, &events);
	CHECK_INT(state->player.equipment[SLOT_HELMET].uid, 71);
	REQUIRE_INT(state->player.bag_count, 2);
	CHECK_INT(state->player.bag[0].uid, 72);
	CHECK_INT(state->player.bag[1].uid, 73);
	events_clear(&events);
	auto_equip(state, &data, &events);
	CHECK_INT(events.count, 0);
	events_free(&events);
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, items_above_the_player_level_are_skipped) {
	GameData data = items_data();
	GameEngine engine = auto_engine(&data, true);
	RunState *state = &engine.state;
	ItemInstance axe = make_item(80, "test_axe", "mythic", 9);
	player_bag_push(&state->player, &axe);
	EventList events = {0};
	auto_equip(state, &data, &events);
	CHECK_INT(events.count, 0);
	state->player.level = 1 + 9 * data.balance.item_level_per_tier;
	auto_equip(state, &data, &events);
	REQUIRE(events.count > 0);
	CHECK_INT(event_get_int(&events.items[0], "uid"), 80);
	events_free(&events);
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, victory_triggers_auto_equip_only_when_enabled) {
	GameData data = items_data();
	calm(&data);
	data.balance.enemy_classes[ENEMY_NORMAL].drop_chance_pct = 100;
	// A starter sword without stats: any dropped weapon is an upgrade.
	for (int i = 0; i < data.item_count; i++) {
		if (str_eq(data.items[i].id, "sword")) {
			memset(data.items[i].has_stat, 0, sizeof(data.items[i].has_stat));
			memset(data.items[i].stats, 0, sizeof(data.items[i].stats));
		}
	}
	const bool modes[] = {true, false};
	for (size_t i = 0; i < ARRAY_LEN(modes); i++) {
		bool enabled = modes[i];
		GameEngine engine = auto_engine(&data, enabled);
		fight(&engine)->hp = 1;
		const EventList *events = step(&engine, cmd_attack());
		CHECK_INT(engine.state.phase, PHASE_MERCHANT);
		CHECK(has_event(events, "item_auto_equipped") == enabled);
		CHECK_INT(engine.state.stats.items_auto_equipped, enabled ? 1 : 0);
		engine_free(&engine);
	}
	game_data_free(&data);
}

TEST(SUITE, buying_a_stock_item_triggers_auto_equip) {
	GameData data = items_data();
	GameEngine engine = auto_engine(&data, true);
	RunState *state = &engine.state;
	ItemInstance axe = make_item(90, "test_axe", "common", 0);
	state->merchant_stock[0] = axe;
	state->stock_count = 1;
	state->player.gold = 10000;
	CHECK_STR(event_types(step(&engine, cmd_buy_stock_item(0))), "item_bought,item_auto_equipped,item_auto_sold");
	CHECK(state->player.equipped[SLOT_WEAPON]);
	CHECK(items_equal(&state->player.equipment[SLOT_WEAPON], &axe));

	GameEngine manual = auto_engine(&data, false);
	manual.state.merchant_stock[0] = axe;
	manual.state.stock_count = 1;
	manual.state.player.gold = 10000;
	CHECK_STR(event_types(step(&manual, cmd_buy_stock_item(0))), "item_bought");
	engine_free(&manual);
	engine_free(&engine);
	game_data_free(&data);
}
