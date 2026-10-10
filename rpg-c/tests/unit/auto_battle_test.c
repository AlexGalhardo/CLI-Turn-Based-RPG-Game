// Auto-battle policy decisions (docs/game-design.md §13).
#include "application/auto_battle.h"
#include "domain/character.h"
#include "support/helpers.h"

#define SUITE "unit/auto_battle"

#define OFFENSE_TURN 1

static const char *const MODES[] = {AUTO_BATTLE_MELEE, AUTO_BATTLE_SPELLS, AUTO_BATTLE_BALANCED};

static GameEngine battle(const char *vocation, int64_t round_number) {
	GameEngine engine;
	EventList events = {0};
	char error[128];
	RunConfig config = run_config("Auto", vocation, "normal", false);
	if (!engine_new_run(&engine, test_data(), &config, 11, &events, error, sizeof(error))) {
		fatal("%s", error);
	}
	events_free(&events);
	engine.state.round = round_number;
	step(&engine, cmd_next_fight());
	return engine;
}

static Command choose(GameEngine *engine, const char *mode, int64_t turn) {
	engine->state.turn = turn;
	return auto_battle_choose(test_data(), mode, &engine->state);
}

static int64_t support_turn(const char *mode) {
	return find_auto_battle_mode(&test_data()->balance.auto_battle, mode)->support_every - 1;
}

TEST(SUITE, offensive_actions_per_mode) {
	GameEngine engine = battle("warrior", 0);
	engine.state.player.mp = 10000;
	CHECK_COMMAND(choose(&engine, AUTO_BATTLE_MELEE, OFFENSE_TURN + 1), cmd_attack());
	CHECK_COMMAND(choose(&engine, AUTO_BATTLE_SPELLS, OFFENSE_TURN), cmd_cast("annihilation"));
	CHECK_COMMAND(choose(&engine, AUTO_BATTLE_BALANCED, 2), cmd_cast("annihilation"));
	engine.state.player.mp = data_spell(test_data(), "brutal_strike")->mana;
	CHECK_COMMAND(choose(&engine, AUTO_BATTLE_SPELLS, OFFENSE_TURN), cmd_cast("brutal_strike"));
	engine.state.player.mp = 0;
	CHECK_COMMAND(choose(&engine, AUTO_BATTLE_SPELLS, OFFENSE_TURN), cmd_attack());
	engine_free(&engine);
}

TEST(SUITE, support_turn_cadence_per_mode) {
	CHECK_INT(support_turn(AUTO_BATTLE_MELEE), 4);
	CHECK_INT(support_turn(AUTO_BATTLE_SPELLS), 4);
	CHECK_INT(support_turn(AUTO_BATTLE_BALANCED), 1);
	// The menu lists the modes in this order.
	const AutoBattleDef *config = &test_data()->balance.auto_battle;
	REQUIRE_INT(config->mode_count, ARRAY_LEN(MODES));
	for (size_t i = 0; i < ARRAY_LEN(MODES); i++) {
		CHECK_STR(config->modes[i].id, MODES[i]);
	}
}

TEST(SUITE, support_turn_heals_below_half_hp) {
	for (size_t i = 0; i < ARRAY_LEN(MODES); i++) {
		const char *mode = MODES[i];
		GameEngine engine = battle("warrior", 0);
		Player *player = &engine.state.player;
		int64_t max_hp = build_sheet(player, test_data()).max_hp;
		player->hp = max_hp * 40 / 100;
		player->mp = 10000;
		CHECK_COMMAND(choose(&engine, mode, support_turn(mode)), cmd_cast("wound_cleansing"));
		player->mp = 0;
		CHECK_COMMAND(choose(&engine, mode, support_turn(mode)), cmd_use_potion("health_potion"));
		counter_free(&player->potions);
		CHECK_COMMAND(choose(&engine, mode, support_turn(mode)), cmd_attack());
		engine_free(&engine);
	}
}

TEST(SUITE, heal_waits_for_the_support_turn_above_the_emergency_line) {
	GameEngine engine = battle("warrior", 0);
	Player *player = &engine.state.player;
	player->hp = build_sheet(player, test_data()).max_hp * 40 / 100;
	CHECK_COMMAND(choose(&engine, AUTO_BATTLE_MELEE, OFFENSE_TURN), cmd_attack());
	engine_free(&engine);
}

TEST(SUITE, emergency_heal_on_any_turn) {
	GameEngine engine = battle("warrior", 0);
	Player *player = &engine.state.player;
	player->hp = build_sheet(player, test_data()).max_hp * 20 / 100;
	player->mp = 10000;
	CHECK_COMMAND(choose(&engine, AUTO_BATTLE_MELEE, OFFENSE_TURN), cmd_cast("wound_cleansing"));
	engine_free(&engine);
}

TEST(SUITE, best_potion_is_the_strongest_owned) {
	GameEngine engine = battle("warrior", 0);
	Player *player = &engine.state.player;
	player->hp = 1;
	player->mp = 0;
	counter_set(&player->potions, "strong_health_potion", 1);
	CHECK_COMMAND(choose(&engine, AUTO_BATTLE_MELEE, OFFENSE_TURN), cmd_use_potion("strong_health_potion"));
	engine_free(&engine);
}

TEST(SUITE, support_turn_drinks_mana_when_low) {
	GameEngine engine = battle("mage", 0);
	engine.state.player.mp = 0;
	int64_t turn = support_turn(AUTO_BATTLE_SPELLS);
	CHECK_COMMAND(choose(&engine, AUTO_BATTLE_SPELLS, turn), cmd_use_potion("mana_potion"));
	counter_free(&engine.state.player.potions);
	CHECK_COMMAND(choose(&engine, AUTO_BATTLE_SPELLS, turn), cmd_attack());
	engine_free(&engine);
}

TEST(SUITE, support_turn_defends_against_a_telegraphed_charge) {
	GameEngine engine = battle("warrior", 9);
	REQUIRE(engine.state.has_monster);
	MonsterInstance *boss = &engine.state.monster;
	CHECK(boss->is_boss);
	boss->boss_actions = test_data()->balance.boss_telegraph_every;
	int64_t turn = support_turn(AUTO_BATTLE_MELEE);
	CHECK_COMMAND(choose(&engine, AUTO_BATTLE_MELEE, turn), cmd_defend());
	CHECK_COMMAND(choose(&engine, AUTO_BATTLE_MELEE, turn + 1), cmd_attack());
	boss->boss_actions = 0;
	CHECK_COMMAND(choose(&engine, AUTO_BATTLE_MELEE, turn), cmd_attack());
	engine_free(&engine);
}

TEST(SUITE, policy_finishes_fights_with_valid_commands) {
	for (size_t i = 0; i < ARRAY_LEN(MODES); i++) {
		GameEngine engine = battle("archer", 0);
		for (int turn = 0; turn < 500 && engine.state.phase == PHASE_BATTLE; turn++) {
			const EventList *events = step(&engine, auto_battle_choose(test_data(), MODES[i], &engine.state));
			CHECK(!has_event(events, "error"));
		}
		CHECK(engine.state.phase != PHASE_BATTLE);
		engine_free(&engine);
	}
}

TEST(SUITE, policy_is_deterministic) {
	GameEngine engine = battle("mage", 0);
	uint32_t rng_state = engine.rng.state;
	Command first[ARRAY_LEN(MODES) * 6];
	for (int pass = 0; pass < 2; pass++) {
		size_t index = 0;
		for (size_t mode = 0; mode < ARRAY_LEN(MODES); mode++) {
			for (int64_t turn = 0; turn < 6; turn++) {
				Command chosen = choose(&engine, MODES[mode], turn);
				if (pass == 0) {
					first[index] = chosen;
				} else {
					CHECK_COMMAND(chosen, first[index]);
				}
				index++;
			}
		}
	}
	CHECK_INT(engine.rng.state, rng_state);
	engine_free(&engine);
}
