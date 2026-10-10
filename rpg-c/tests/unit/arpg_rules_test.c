// M8 rules (docs/game-design.md §3, §6, §8, §9): enemy classes, monster dodge/parry/heal/crit, parry reflects,
// class drop tables, potion drops, spell level effects and the victory phase.
#include "application/spawner.h"
#include "domain/character.h"
#include "domain/formulas.h"
#include "support/helpers.h"

#define SUITE "unit/arpg_rules"

#define MAX_TURNS 300

// Starts the next fight against a monster with one fixed attack and 10 000 HP.
static MonsterInstance *arpg_fight(GameEngine *engine, int64_t damage, Element element) {
	MonsterInstance *monster = fight(engine);
	fixed_attack(monster, damage, element, NULL);
	monster->hp = 10000;
	monster->max_hp = 10000;
	return monster;
}

// Finishes the current fight with melee hits (the monster is left with 1 HP before each hit).
static const EventList *kill(GameEngine *engine) {
	for (int i = 0; i < MAX_TURNS && engine->state.has_monster; i++) {
		engine->state.monster.hp = 1;
		engine->state.player.hp = 1000000;
		const EventList *events = step(engine, cmd_attack());
		if (engine->state.phase != PHASE_BATTLE) {
			return events;
		}
	}
	fatal("the fight did not end");
}

static const char *physical_resistant_100(const GameData *data) {
	for (int i = 0; i < data->monster_count; i++) {
		if (data->monsters[i].resistances[ELEMENT_PHYSICAL] == 100) {
			return data->monsters[i].id;
		}
	}
	fatal("no monster takes full physical damage");
}

static GameData calm_data(void) {
	GameData data = data_copy();
	calm(&data);
	return data;
}

// ── spawn ────────────────────────────────────────────────────────────────────

TEST(SUITE, elite_spawn_scales_stats_and_rewards) {
	GameData data = calm_data();
	const DifficultyDef *difficulty = balance_difficulty(&data.balance, "normal");
	MonsterInstance normal;
	MonsterInstance elite;
	Rng rng = rng_new(5);
	spawn_monster(&data, &rng, 3, difficulty, &normal);
	all_elites(&data);
	rng = rng_new(5);
	spawn_monster(&data, &rng, 3, difficulty, &elite);
	const EnemyClassDef *row = balance_enemy_class(&test_data()->balance, ENEMY_ELITE);
	CHECK_INT(normal.enemy_class, ENEMY_NORMAL);
	CHECK_INT(elite.enemy_class, ENEMY_ELITE);
	CHECK_STR(elite.creature_id, normal.creature_id);
	CHECK_INT(elite.max_hp, pct(normal.max_hp, row->stat_pct));
	CHECK_INT(elite.attacks[0].max, pct(normal.attacks[0].max, row->stat_pct));
	CHECK_INT(elite.xp, pct(normal.xp, row->reward_pct));
	CHECK_INT(elite.gold_max, pct(normal.gold_max, row->reward_pct));
	game_data_free(&data);
}

TEST(SUITE, elite_roll_follows_the_monster_pick) {
	const GameData *data = test_data();
	const DifficultyDef *difficulty = balance_difficulty(&data->balance, "normal");
	Rng rng = rng_new(77);
	int counts[ENEMY_CLASS_COUNT] = {0};
	for (int i = 0; i < 300; i++) {
		MonsterInstance monster;
		spawn_monster(data, &rng, 1 + i % 9, difficulty, &monster);
		counts[monster.enemy_class]++;
	}
	CHECK(counts[ENEMY_NORMAL] > 0);
	CHECK_INT(counts[ENEMY_BOSS], 0);
	CHECK(counts[ENEMY_ELITE] >= 30 && counts[ENEMY_ELITE] <= 90);
	MonsterInstance boss;
	rng = rng_new(1);
	spawn_monster(data, &rng, 10, difficulty, &boss);
	CHECK_INT(boss.enemy_class, ENEMY_BOSS);
}

TEST(SUITE, round_started_reports_the_enemy_class) {
	GameData data = data_copy();
	all_elites(&data);
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	const EventList *events = step(&engine, cmd_next_fight());
	REQUIRE(events->count > 0);
	CHECK_STR(event_get_str(&events->items[0], "enemyClass"), "elite");
	CHECK(!event_get_bool(&events->items[0], "isBoss"));
	engine_free(&engine);
	game_data_free(&data);
}

// ── monster dodge, parry, heal and crit ──────────────────────────────────────

TEST(SUITE, monster_dodge_stops_melee_and_spells) {
	GameData data = calm_data();
	data.balance.enemy_classes[ENEMY_NORMAL].dodge = 100;
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	MonsterInstance *monster = arpg_fight(&engine, 1, ELEMENT_PHYSICAL);
	const EventList *events = step(&engine, cmd_attack());
	REQUIRE(events->count > 0);
	CHECK_EVENT(&events->items[0], "{\"type\": \"monster_dodged\"}");
	CHECK(!has_event(events, "player_attacked"));
	CHECK_INT(monster->hp, monster->max_hp);
	Player *player = &engine.state.player;
	player->mp = 1000;
	events = step(&engine, cmd_cast("brutal_strike"));
	REQUIRE(events->count > 0);
	CHECK_EVENT(&events->items[0], "{\"type\": \"monster_dodged\"}");
	CHECK(player->mp < 1000);
	CHECK_INT(counter_get(&player->spell_uses, "brutal_strike"), 1);
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, monster_parry_reflects_physical_hits) {
	GameData data = calm_data();
	data.balance.enemy_classes[ENEMY_NORMAL].parry = 100;
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	MonsterInstance *monster = arpg_fight(&engine, 1, ELEMENT_PHYSICAL);
	Player *player = &engine.state.player;
	CHECK(!has_event(step(&engine, cmd_defend()), "monster_parried"));
	int64_t hp = player->hp;
	const EventList *events = step(&engine, cmd_attack());
	REQUIRE(events->count > 0);
	const Event *parried = &events->items[0];
	REQUIRE(event_is(parried, "monster_parried"));
	int64_t reflected = event_get_int(parried, "reflected");
	CHECK(reflected >= 1);
	CHECK(!has_event(events, "leeched"));
	CHECK_INT(monster->hp, monster->max_hp);
	int64_t hits = 0;
	int64_t regen = 0;
	for (size_t i = 0; i < events->count; i++) {
		const Event *event = &events->items[i];
		if (event_is(event, "monster_attacked")) {
			hits += event_get_int(event, "damage");
		} else if (event_is(event, "regenerated")) {
			regen += event_get_int(event, "hp");
		}
	}
	CHECK_INT(player->hp, hp - reflected - hits + regen);
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, monster_parry_reflect_can_kill_the_player) {
	GameData data = calm_data();
	data.balance.enemy_classes[ENEMY_NORMAL].parry = 100;
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	MonsterInstance *monster = arpg_fight(&engine, 1, ELEMENT_PHYSICAL);
	engine.state.player.hp = 1;
	CHECK_STR(event_types(step(&engine, cmd_attack())), "monster_parried,player_died");
	CHECK_INT(engine.state.phase, PHASE_GAME_OVER);
	CHECK_INT(monster->hp, monster->max_hp);
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, monster_parry_ignores_non_physical_spells) {
	GameData data = calm_data();
	data.balance.enemy_classes[ENEMY_NORMAL].parry = 100;
	GameEngine engine = new_engine(&data, "mage", "normal", 42);
	arpg_fight(&engine, 1, ELEMENT_PHYSICAL);
	engine.state.player.mp = 1000;
	const EventList *events = step(&engine, cmd_cast("flame_strike"));
	CHECK(!has_event(events, "monster_parried"));
	CHECK(has_event(events, "spell_cast"));
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, monster_heals_instead_of_attacking) {
	GameData data = calm_data();
	data.balance.enemy_classes[ENEMY_NORMAL].heal = 100;
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	MonsterInstance *monster = arpg_fight(&engine, 1, ELEMENT_PHYSICAL);
	const EventList *events = step(&engine, cmd_defend());
	CHECK(!has_event(events, "monster_healed"));
	CHECK(has_event(events, "monster_attacked"));
	monster->hp = monster->max_hp - 5;
	events = step(&engine, cmd_defend());
	CHECK(events_contain(events, "{\"type\": \"monster_healed\", \"amount\": 5}"));
	CHECK(!has_event(events, "monster_attacked"));
	monster->hp = 100;
	events = step(&engine, cmd_defend());
	int64_t amount = pct(monster->max_hp, test_data()->balance.monster_heal_pct);
	CHECK(events_contain(events, fmt("{\"type\": \"monster_healed\", \"amount\": %lld}", (long long)amount)));
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, healing_boss_does_not_advance_its_pattern) {
	GameData data = calm_data();
	data.balance.enemy_classes[ENEMY_BOSS].heal = 100;
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	engine.state.round = 9;
	MonsterInstance *boss = fight(&engine);
	boss->hp = boss->max_hp - 1;
	engine.state.player.hp = 1000000;
	step(&engine, cmd_defend());
	CHECK_INT(boss->boss_actions, 0);
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, monster_crit_multiplies_raw_damage) {
	const int64_t cases[][2] = {{0, 100}, {100, 150}};
	for (size_t i = 0; i < ARRAY_LEN(cases); i++) {
		GameData data = calm_data();
		data.balance.enemy_classes[ENEMY_NORMAL].crit = cases[i][0];
		GameEngine engine = new_engine(&data, "warrior", "normal", 42);
		arpg_fight(&engine, 100, ELEMENT_FIRE);
		engine.state.player.hp = 1000;
		const Event *hit = find_event(step(&engine, cmd_attack()), "monster_attacked");
		if (hit != NULL) {
			CHECK(event_get_bool(hit, "crit") == (cases[i][0] == 100));
			CHECK_INT(event_get_int(hit, "damage"), cases[i][1]);
		} else {
			CHECK(hit != NULL);
		}
		engine_free(&engine);
		game_data_free(&data);
	}
}

static GameData parry_data(void) {
	GameData data = calm_data();
	ItemDef *shield = add_item(&data, "test_parry_shield", "Test Shield", SLOT_SHIELD, "shield");
	shield->has_stat[STAT_PARRY] = true;
	shield->stats[STAT_PARRY] = 100;
	shield->value = 10;
	Caps *caps = &data.balance.caps;
	caps->dodge = 0;
	caps->parry = 100;
	caps->leech = 25;
	caps->protection = 75;
	return data;
}

TEST(SUITE, player_parry_reflects_damage_to_the_monster) {
	GameData data = parry_data();
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	wear(&engine.state.player, SLOT_SHIELD, make_item(90, "test_parry_shield", "common", 0));
	CHECK_INT(build_sheet(&engine.state.player, &data).parry, 100);
	MonsterInstance *monster = arpg_fight(&engine, 50, ELEMENT_PHYSICAL);
	const EventList *events = step(&engine, cmd_defend());
	CHECK(events_contain(events, "{\"type\": \"attack_parried\", \"attackId\": \"test_hit\", \"reflected\": 10}"));
	CHECK_INT(monster->hp, monster->max_hp - 10);
	CHECK_INT(engine.state.stats.parries, 1);
	monster->hp = 5;
	CHECK(has_event(step(&engine, cmd_defend()), "monster_killed"));
	CHECK_INT(engine.state.phase, PHASE_MERCHANT);
	engine_free(&engine);
	game_data_free(&data);
}

// ── victory, drops and potions ───────────────────────────────────────────────

static bool rarity_is(const Event *drop, const char *a, const char *b) {
	const char *rarity = event_get_str(drop, "rarity");
	return str_eq(rarity, a) || str_eq(rarity, b);
}

TEST(SUITE, normal_drop_table) {
	GameData data = calm_data();
	data.balance.enemy_classes[ENEMY_NORMAL].drop_chance_pct = 100;
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	arpg_fight(&engine, 1, ELEMENT_PHYSICAL);
	const EventList *events = kill(&engine);
	CHECK_INT(count_events(events, "item_dropped"), 1);
	const Event *drop = find_event(events, "item_dropped");
	CHECK(drop != NULL && rarity_is(drop, "common", "rare"));
	CHECK(!has_event(events, "potion_dropped"));
	engine_free(&engine);

	data.balance.enemy_classes[ENEMY_NORMAL].drop_chance_pct = 0;
	engine = new_engine(&data, "warrior", "normal", 42);
	arpg_fight(&engine, 1, ELEMENT_PHYSICAL);
	CHECK(!has_event(kill(&engine), "item_dropped"));
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, elite_drops_a_good_item_and_maybe_a_potion) {
	GameData data = calm_data();
	data.balance.enemy_classes[ENEMY_ELITE].potion_drop_pct = 100;
	all_elites(&data);
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	arpg_fight(&engine, 1, ELEMENT_PHYSICAL);
	Counter before = counter_clone(&engine.state.player.potions);
	const EventList *events = kill(&engine);
	CHECK_INT(count_events(events, "item_dropped"), 1);
	const Event *drop = find_event(events, "item_dropped");
	CHECK(drop != NULL && rarity_is(drop, "rare", "legendary"));
	const Event *potion = find_event(events, "potion_dropped");
	if (potion != NULL) {
		const char *potion_id = event_get_str(potion, "potionId");
		CHECK(data_potion(&data, potion_id)->unlock_round <= 1);
		CHECK_INT(counter_get(&engine.state.player.potions, potion_id), counter_get(&before, potion_id) + 1);
		CHECK_INT(counter_get(&engine.state.stats.potions_dropped, potion_id), 1);
	} else {
		CHECK(potion != NULL);
	}
	CHECK_INT(engine.state.stats.elites_killed, 1);
	const Event *killed = find_event(events, "monster_killed");
	CHECK(killed != NULL && str_eq(event_get_str(killed, "enemyClass"), "elite"));
	counter_free(&before);
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, potion_drop_without_unlocked_potions_consumes_nothing) {
	GameData data = calm_data();
	data.balance.enemy_classes[ENEMY_NORMAL].drop_chance_pct = 0;
	data.balance.enemy_classes[ENEMY_NORMAL].potion_drop_pct = 100;
	for (int i = 0; i < data.potion_count; i++) {
		data.potions[i].unlock_round = 99;
	}
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	arpg_fight(&engine, 1, ELEMENT_PHYSICAL);
	CHECK(!has_event(kill(&engine), "potion_dropped"));
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, boss_drops_several_top_items) {
	GameData data = calm_data();
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	engine.state.round = 9;
	arpg_fight(&engine, 1, ELEMENT_PHYSICAL);
	const EventList *events = kill(&engine);
	CHECK_INT(count_events(events, "item_dropped"), balance_enemy_class(&test_data()->balance, ENEMY_BOSS)->drops);
	for (size_t i = 0; i < events->count; i++) {
		if (event_is(&events->items[i], "item_dropped")) {
			CHECK(rarity_is(&events->items[i], "legendary", "mythic"));
		}
	}
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, full_bag_auto_sells_drops) {
	GameData data = calm_data();
	data.balance.enemy_classes[ENEMY_NORMAL].drop_chance_pct = 100;
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	Player *player = &engine.state.player;
	for (int64_t i = 0; i < data.balance.bag_capacity; i++) {
		ItemInstance sword = make_item(500 + i, "sword", "common", 0);
		player_bag_push(player, &sword);
	}
	arpg_fight(&engine, 1, ELEMENT_PHYSICAL);
	CHECK(has_event(kill(&engine), "item_auto_sold"));
	CHECK_INT(player->bag_count, data.balance.bag_capacity);
	engine_free(&engine);
	game_data_free(&data);
}

static const EventList *final_victory(GameEngine *engine) {
	engine->state.round = engine->data->balance.final_round - 1;
	arpg_fight(engine, 1, ELEMENT_PHYSICAL);
	return kill(engine);
}

TEST(SUITE, beating_the_final_boss_enters_the_victory_phase) {
	GameData data = calm_data();
	long long final_round = (long long)data.balance.final_round;
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	const EventList *events = final_victory(&engine);
	REQUIRE(events->count > 0);
	CHECK_EVENT(&events->items[events->count - 1], fmt("{\"type\": \"run_won\", \"round\": %lld}", final_round));
	CHECK(!has_event(events, "merchant_entered"));
	CHECK_INT(engine.state.phase, PHASE_VICTORY);
	CHECK(engine.state.won);
	uint32_t rng_state = engine.rng.state;
	int stock_count = engine.state.stock_count;
	ItemInstance stock[MAX_MERCHANT_STOCK];
	memcpy(stock, engine.state.merchant_stock, sizeof(stock));
	const Command commands[] = {cmd_attack(), cmd_defend(), cmd_next_fight(), cmd_buy_potion("health_potion", 1),
	    cmd_equip(1), cmd_sell_item(1)};
	for (size_t i = 0; i < ARRAY_LEN(commands); i++) {
		CHECK(is_error(step(&engine, commands[i]), "invalid_phase"));
	}
	CHECK_INT(engine.rng.state, rng_state);
	REQUIRE_INT(engine.state.stock_count, stock_count);
	for (int i = 0; i < stock_count; i++) {
		CHECK(items_equal(&engine.state.merchant_stock[i], &stock[i]));
	}
	CHECK_EVENTS(step(&engine, cmd_end_run()), "[{\"type\": \"run_ended\", \"won\": true}]");
	CHECK_INT(engine.state.phase, PHASE_GAME_OVER);
	CHECK(!engine.state.has_death_cause);
	CHECK(is_error(step(&engine, cmd_continue_run()), "invalid_phase"));
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, continue_run_enters_the_merchant) {
	GameData data = calm_data();
	int64_t final_round = data.balance.final_round;
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	final_victory(&engine);
	CHECK_EVENTS(step(&engine, cmd_continue_run()),
	    fmt("[{\"type\": \"merchant_entered\", \"round\": %lld}]", (long long)final_round));
	CHECK_INT(engine.state.phase, PHASE_MERCHANT);
	CHECK_INT(engine.state.stock_count, data.balance.merchant_stock_size);
	step(&engine, cmd_next_fight());
	CHECK_INT(engine.state.round, final_round + 1);
	CHECK(engine.state.won);
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, victory_commands_are_rejected_outside_the_victory_phase) {
	GameEngine engine = new_engine(test_data(), "warrior", "normal", 42);
	CHECK(is_error(step(&engine, cmd_end_run()), "invalid_phase"));
	CHECK(is_error(step(&engine, cmd_continue_run()), "invalid_phase"));
	engine_free(&engine);
}

// ── spell levels ─────────────────────────────────────────────────────────────

// The base roll the next cast will make, taken from a copy of the engine's PRNG.
static int64_t next_spell_roll(const GameEngine *engine, const SpellDef *spell) {
	const Player *player = &engine->state.player;
	Rng rng = {.state = engine->rng.state};
	int64_t bonus = player->level * spell->per_level + player->magic_level * spell->per_magic_level;
	return rng_roll(&rng, spell->min + bonus, spell->max + bonus);
}

TEST(SUITE, spell_level_effect_scales_damage) {
	const int64_t cases[][2] = {{0, 100}, {20, 150}, {50, 200}};
	for (size_t i = 0; i < ARRAY_LEN(cases); i++) {
		GameData data = calm_data();
		GameEngine engine = new_engine(&data, "warrior", "normal", 42);
		MonsterInstance *monster = arpg_fight(&engine, 1, ELEMENT_PHYSICAL);
		id_set(monster->creature_id, physical_resistant_100(&data));
		Player *player = &engine.state.player;
		counter_set(&player->spell_uses, "brutal_strike", cases[i][0]);
		player->mp = 1000;
		int64_t base = next_spell_roll(&engine, data_spell(&data, "brutal_strike"));
		const Event *cast = find_event(step(&engine, cmd_cast("brutal_strike")), "spell_cast");
		if (cast != NULL) {
			CHECK_INT(event_get_int(cast, "damage"), max_i64(1, pct(base, cases[i][1])));
		} else {
			CHECK(cast != NULL);
		}
		engine_free(&engine);
		game_data_free(&data);
	}
}

TEST(SUITE, spell_level_effect_scales_healing) {
	GameData data = calm_data();
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	arpg_fight(&engine, 1, ELEMENT_PHYSICAL);
	Player *player = &engine.state.player;
	counter_set(&player->spell_uses, "wound_cleansing", 20);
	player->mp = 1000;
	player->hp = 1;
	int64_t expected = pct(next_spell_roll(&engine, data_spell(&data, "wound_cleansing")), 150);
	int64_t room = build_sheet(player, &data).max_hp - player->hp;
	const Event *healed = find_event(step(&engine, cmd_cast("wound_cleansing")), "spell_healed");
	if (healed != NULL) {
		CHECK_INT(event_get_int(healed, "amount"), min_i64(expected, room));
	} else {
		CHECK(healed != NULL);
	}
	engine_free(&engine);
	game_data_free(&data);
}
