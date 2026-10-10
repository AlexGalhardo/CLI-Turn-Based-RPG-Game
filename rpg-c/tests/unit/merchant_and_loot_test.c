#include "application/loot.h"
#include "application/merchant.h"
#include "domain/character.h"
#include "support/helpers.h"

#include <stdlib.h>

#define SUITE "unit/merchant_and_loot"

static GameData items_data(void) {
	GameData data = data_copy();
	add_test_items(&data);
	return data;
}

static AffixDef affix(const char *id, Stat stat, int64_t min, int64_t max, int64_t per_tier, Slot a, Slot b) {
	AffixDef definition;
	memset(&definition, 0, sizeof(definition));
	id_set(definition.id, id);
	definition.stat = stat;
	definition.min = min;
	definition.max = max;
	definition.per_tier = per_tier;
	definition.slots[a] = true;
	definition.slots[b] = true;
	return definition;
}

// The test items plus a small affix table where two affixes share a stat.
static GameData loot_data(void) {
	GameData data = items_data();
	free(data.affixes);
	data.affix_count = 4;
	data.affixes = xcalloc(4, sizeof(AffixDef));
	data.affixes[0] = affix("of_power", STAT_ATTACK, 1, 3, 2, SLOT_WEAPON, SLOT_WEAPON);
	data.affixes[1] = affix("of_the_bear", STAT_MAX_HP, 5, 10, 5, SLOT_WEAPON, SLOT_HELMET);
	data.affixes[2] = affix("of_speed", STAT_DODGE, 1, 2, 0, SLOT_WEAPON, SLOT_RING);
	data.affixes[3] = affix("of_speed_2", STAT_DODGE, 1, 2, 0, SLOT_WEAPON, SLOT_WEAPON);
	return data;
}

static void push_items(Player *player, int64_t first_uid, int64_t count, const char *item_id) {
	for (int64_t i = 0; i < count; i++) {
		ItemInstance item = make_item(first_uid + i, item_id, "common", 0);
		player_bag_push(player, &item);
	}
}

TEST(SUITE, buy_potion_rules) {
	GameEngine engine = new_engine(test_data(), "warrior", "normal", 42);
	Player *player = &engine.state.player;
	int64_t gold = player->gold;
	CHECK_EVENTS(step(&engine, cmd_buy_potion("health_potion", 2)),
	    "[{\"type\": \"potion_bought\", \"potionId\": \"health_potion\", \"quantity\": 2, \"gold\": 100}]");
	CHECK_INT(player->gold, gold - 100);
	CHECK(is_error(step(&engine, cmd_buy_potion("health_potion", 1)), "not_enough_gold"));
	CHECK(is_error(step(&engine, cmd_buy_potion("health_potion", 0)), "invalid_quantity"));
	CHECK(is_error(step(&engine, cmd_buy_potion("great_health_potion", 1)), "potion_locked"));
	CHECK(is_error(step(&engine, cmd_buy_potion("elixir", 1)), "unknown_potion"));
	CHECK_INT(counter_get(&engine.state.stats.potions_bought, "health_potion"), 2);
	CHECK_INT(engine.state.stats.gold_spent, 100);
	engine_free(&engine);
}

// The ids of the potions the merchant sells now, in the data order.
static const char *available_potions(const RunState *state, const GameData *data, int *count) {
	static StrBuf ids;
	sb_clear(&ids);
	sb_append(&ids, "");
	*count = 0;
	for (int i = 0; i < data->potion_count; i++) {
		if (potion_available(state, &data->potions[i])) {
			if (*count > 0) {
				sb_append_char(&ids, ',');
			}
			sb_append(&ids, data->potions[i].id);
			(*count)++;
		}
	}
	return ids.data;
}

TEST(SUITE, available_potions_unlock_by_round) {
	const GameData *data = test_data();
	GameEngine engine = new_engine(data, "warrior", "normal", 42);
	int count = 0;
	CHECK_STR(available_potions(&engine.state, data, &count), "health_potion,mana_potion");
	engine.state.round = 80;
	available_potions(&engine.state, data, &count);
	CHECK_INT(count, data->potion_count);
	engine_free(&engine);
}

TEST(SUITE, equip_swap_sell_and_unequip) {
	GameData data = items_data();
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	Player *player = &engine.state.player;
	ItemInstance axe = make_item(50, "test_axe", "common", 0);
	ItemInstance rod = make_item(51, "test_rod", "rare", 0);
	player_bag_push(player, &axe);
	player_bag_push(player, &rod);
	long long starter_uid = (long long)player->equipment[SLOT_WEAPON].uid;

	CHECK(is_error(step(&engine, cmd_equip(51)), "cannot_equip"));
	CHECK_EVENTS(step(&engine, cmd_equip(50)),
	    fmt("[{\"type\": \"item_unequipped\", \"uid\": %lld, \"itemId\": \"sword\", \"slot\": \"weapon\"},"
	        " {\"type\": \"item_equipped\", \"uid\": 50, \"itemId\": \"test_axe\", \"slot\": \"weapon\"}]",
	        starter_uid));
	CHECK_INT(build_sheet(player, &data).melee_min, 8 + 20);

	int64_t gold = player->gold;
	CHECK_EVENTS(step(&engine, cmd_sell_item(51)),
	    fmt("[{\"type\": \"item_sold\", \"uid\": 51, \"itemId\": \"test_rod\", \"gold\": %lld}]",
	        (long long)item_value(&rod, &data)));
	CHECK_INT(player->gold, gold + 250);
	CHECK(is_error(step(&engine, cmd_sell_item(51)), "invalid_item"));
	CHECK_EVENTS(step(&engine, cmd_unequip(SLOT_WEAPON)),
	    "[{\"type\": \"item_unequipped\", \"uid\": 50, \"itemId\": \"test_axe\", \"slot\": \"weapon\"}]");
	CHECK(is_error(step(&engine, cmd_unequip(SLOT_WEAPON)), "invalid_item"));
	CHECK(is_error(step(&engine, cmd_equip(999)), "invalid_item"));
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, unequip_with_full_bag_and_hp_clamp) {
	GameData data = items_data();
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	Player *player = &engine.state.player;
	wear(player, SLOT_HELMET, make_item(70, "test_helmet", "common", 0));
	player->hp = build_sheet(player, &data).max_hp;
	push_items(player, 100, data.balance.bag_capacity, "test_ring");
	CHECK(is_error(step(&engine, cmd_unequip(SLOT_HELMET)), "bag_full"));
	player_bag_remove(player, player->bag_count - 1);
	step(&engine, cmd_unequip(SLOT_HELMET));
	CHECK(!player->equipped[SLOT_HELMET]);
	CHECK_INT(player->hp, build_sheet(player, &data).max_hp);
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, merchant_stock_purchase) {
	GameData data = loot_data();
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	RunState *state = &engine.state;
	REQUIRE_INT(state->stock_count, data.balance.merchant_stock_size);
	ItemInstance item = state->merchant_stock[0];
	int64_t price = stock_price(&item, &data);
	state->player.gold = price;
	CHECK_EVENTS(step(&engine, cmd_buy_stock_item(0)),
	    fmt("[{\"type\": \"item_bought\", \"uid\": %lld, \"itemId\": \"%s\", \"gold\": %lld}]", (long long)item.uid,
	        item.item_id, (long long)price));
	int index = player_bag_index(&state->player, item.uid);
	CHECK(index >= 0 && items_equal(&state->player.bag[index], &item));
	CHECK(is_error(step(&engine, cmd_buy_stock_item(0)), "not_enough_gold"));
	CHECK(is_error(step(&engine, cmd_buy_stock_item(9)), "invalid_item"));
	push_items(&state->player, 200, 30, "test_ring");
	CHECK(is_error(step(&engine, cmd_buy_stock_item(0)), "bag_full"));
	step(&engine, cmd_next_fight());
	CHECK_INT(state->stock_count, 0);
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, generate_item_is_deterministic_and_unique_affixes) {
	GameData data = loot_data();
	const VocationDef *vocation = data_vocation(&data, "warrior");
	const RarityWeights *weights = &balance_enemy_class(&data.balance, ENEMY_BOSS)->rarity_weights;
	for (int64_t uid = 0; uid < 20; uid++) {
		ItemInstance first;
		ItemInstance second;
		Rng rng = rng_new(5);
		REQUIRE(generate_item(&data, &rng, vocation, 0, weights, uid, &first));
		rng = rng_new(5);
		REQUIRE(generate_item(&data, &rng, vocation, 0, weights, uid, &second));
		CHECK(items_equal(&first, &second));
		CHECK_INT(first.uid, uid);
	}
	Rng rng = rng_new(11);
	for (int64_t uid = 0; uid < 200; uid++) {
		ItemInstance item;
		REQUIRE(generate_item(&data, &rng, vocation, 1, weights, uid, &item));
		CHECK(str_eq(item.rarity, "legendary") || str_eq(item.rarity, "mythic"));
		bool seen[STAT_COUNT] = {false};
		for (int i = 0; i < item.affix_count; i++) {
			CHECK(!seen[item.affixes[i].stat]);
			seen[item.affixes[i].stat] = true;
		}
		CHECK(can_use(data_item(&data, item.item_id), vocation));
	}
	game_data_free(&data);
}

TEST(SUITE, generate_item_without_candidates_consumes_nothing) {
	GameData data = data_copy();
	data.item_count = 0;
	Rng rng = rng_new(3);
	const RarityWeights *weights = &balance_enemy_class(&data.balance, ENEMY_NORMAL)->rarity_weights;
	ItemInstance item;
	CHECK(!generate_item(&data, &rng, data_vocation(&data, "mage"), 9, weights, 1, &item));
	CHECK_INT(rng.state, 3);
	game_data_free(&data);
}

TEST(SUITE, roll_rarity_skips_zero_weights_and_single_options) {
	const GameData *data = test_data();
	Rng rng = rng_new(1);
	RarityWeights weights = {{0}};
	weights.weights[rarity_index(data, "rare")] = 5;
	CHECK_STR(roll_rarity(data, &rng, &weights)->id, "rare");

	memset(&weights, 0, sizeof(weights));
	weights.weights[rarity_index(data, "mythic")] = 3;
	CHECK_STR(roll_rarity(data, &rng, &weights)->id, "mythic");
	CHECK_INT(rng.state, 1);

	memset(&weights, 0, sizeof(weights));
	weights.weights[rarity_index(data, "common")] = 1;
	weights.weights[rarity_index(data, "legendary")] = 1;
	bool common = false;
	bool legendary = false;
	for (int i = 0; i < 40; i++) {
		const char *rolled = roll_rarity(data, &rng, &weights)->id;
		common = common || str_eq(rolled, "common");
		legendary = legendary || str_eq(rolled, "legendary");
		CHECK(str_eq(rolled, "common") || str_eq(rolled, "legendary"));
	}
	CHECK(common && legendary);
	CHECK(rng.state != 1);
	// The reference also asserts that a table without a positive weight raises: here that is a bug that stops the
	// program (fatal), so it cannot run in-process.
}

// Checks that the item has exactly the stats listed in `expected` (pairs of stat, value).
static void check_item_stats(
    const ItemInstance *item, const GameData *data, const int64_t expected[][2], size_t expected_count) {
	int64_t values[STAT_COUNT];
	bool present[STAT_COUNT];
	item_stats(item, data, values, present);
	for (int stat = 0; stat < STAT_COUNT; stat++) {
		bool listed = false;
		for (size_t i = 0; i < expected_count; i++) {
			if (expected[i][0] == stat) {
				listed = true;
				test_check_int(__FILE__, __LINE__, stat_name((Stat)stat), values[stat], expected[i][1]);
			}
		}
		if (present[stat] != listed) {
			test_fail(__FILE__, __LINE__, "%s: stat %s %s", item->item_id, stat_name((Stat)stat),
			    listed ? "is missing" : "is unexpected");
		}
	}
}

TEST(SUITE, rarities_scale_base_stats_and_affix_counts) {
	GameData data = items_data();
	const struct {
		const char *rarity;
		int64_t attack;
		int64_t affixes;
	} cases[] = {{"common", 20, 0}, {"rare", 30, 1}, {"legendary", 40, 2}, {"mythic", 60, 2}};
	for (size_t i = 0; i < ARRAY_LEN(cases); i++) {
		ItemInstance axe = make_item(1, "test_axe", cases[i].rarity, 0);
		const int64_t expected[][2] = {{STAT_ATTACK, cases[i].attack}};
		check_item_stats(&axe, &data, expected, 1);
		const RarityDef *definition = balance_rarity(&data.balance, cases[i].rarity);
		CHECK_INT(definition->affix_min, cases[i].affixes);
		CHECK_INT(definition->affix_max, cases[i].affixes);
	}
	game_data_free(&data);
}

TEST(SUITE, item_stats_apply_rarity_and_affixes) {
	GameData data = items_data();
	ItemInstance item = make_item(1, "test_helmet", "legendary", 0);
	item.affix_count = 1;
	item.affixes[0] = (AffixRoll){STAT_MAX_HP, 7};
	const int64_t expected[][2] = {{STAT_ARMOR, 20}, {STAT_MAX_HP, 107}};
	check_item_stats(&item, &data, expected, 2);
	CHECK_INT(item_value(&item, &data), 600);
	game_data_free(&data);
}

TEST(SUITE, item_score_weights_final_stats) {
	GameData data = items_data();
	const int64_t *weights = data.balance.item_score_weights;
	ItemInstance common = make_item(1, "test_helmet", "common", 0);
	CHECK_INT(item_score(&common, &data), 10 * weights[STAT_ARMOR] + 50 * weights[STAT_MAX_HP]);
	ItemInstance rare = make_item(1, "test_helmet", "rare", 0);
	ItemInstance legendary = make_item(1, "test_helmet", "legendary", 0);
	CHECK(item_score(&common, &data) < item_score(&rare, &data));
	CHECK(item_score(&rare, &data) < item_score(&legendary, &data));
	ItemInstance with_affix = common;
	with_affix.affix_count = 1;
	with_affix.affixes[0] = (AffixRoll){STAT_DODGE, 2};
	CHECK_INT(item_score(&with_affix, &data), item_score(&common, &data) + 2 * weights[STAT_DODGE]);
	game_data_free(&data);
}

TEST(SUITE, required_level_grows_with_the_item_tier) {
	const GameData *data = test_data();
	int64_t per_tier = data->balance.item_level_per_tier;
	ItemInstance low = make_item(1, "sword", "common", 0);
	ItemInstance high = make_item(1, "sword", "common", 3);
	CHECK_INT(required_level(&low, data), 1);
	CHECK_INT(required_level(&high, data), 1 + 3 * per_tier);
}

TEST(SUITE, equip_rejects_items_above_the_player_level) {
	GameData data = items_data();
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	Player *player = &engine.state.player;
	ItemInstance axe = make_item(60, "test_axe", "common", 5);
	player_bag_push(player, &axe);
	uint32_t rng_state = engine.rng.state;
	CHECK(is_error(step(&engine, cmd_equip(60)), "level_too_low"));
	CHECK(player_bag_index(player, 60) >= 0);
	CHECK_INT(engine.rng.state, rng_state);
	player->level = required_level(&axe, &data);
	const EventList *events = step(&engine, cmd_equip(60));
	REQUIRE(events->count > 0);
	CHECK_EVENT(&events->items[events->count - 1],
	    "{\"type\": \"item_equipped\", \"uid\": 60, \"itemId\": \"test_axe\", \"slot\": \"weapon\"}");
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, selling_an_equipped_uid_is_rejected) {
	GameEngine engine = new_engine(test_data(), "warrior", "normal", 42);
	Player *player = &engine.state.player;
	ItemInstance weapon = player->equipment[SLOT_WEAPON];
	int64_t gold = player->gold;
	CHECK(is_error(step(&engine, cmd_sell_item(weapon.uid)), "invalid_item"));
	CHECK(player->equipped[SLOT_WEAPON]);
	CHECK(items_equal(&player->equipment[SLOT_WEAPON], &weapon));
	CHECK_INT(player->gold, gold);
	engine_free(&engine);
}
