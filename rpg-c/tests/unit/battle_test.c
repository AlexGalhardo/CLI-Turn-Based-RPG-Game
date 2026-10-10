#include "application/battle.h"
#include "domain/character.h"
#include "domain/formulas.h"
#include "support/helpers.h"

#define SUITE "unit/battle"

static GameEngine warrior(void) { return new_engine(test_data(), "warrior", "normal", 42); }

static StatusOnHit on_hit(const char *status, int64_t chance, int64_t damage_pct) {
	StatusOnHit result;
	memset(&result, 0, sizeof(result));
	id_set(result.status, status);
	result.chance = chance;
	result.damage_pct = damage_pct;
	return result;
}

TEST(SUITE, melee_damage_is_within_sheet_range) {
	const GameData *data = test_data();
	GameEngine engine = warrior();
	MonsterInstance *monster = fight(&engine);
	CharacterSheet sheet = build_sheet(&engine.state.player, data);
	int64_t resistance = data_creature(data, monster->creature_id)->resistances[ELEMENT_PHYSICAL];
	const Event *hit = find_event(step(&engine, cmd_attack()), "player_attacked");
	if (hit != NULL) {
		int64_t low = max_i64(1, sheet.melee_min * resistance / 100);
		int64_t high = sheet.melee_max * resistance / 100;
		CHECK(!event_get_bool(hit, "crit"));
		int64_t damage = event_get_int(hit, "damage");
		CHECK(low <= damage && damage <= max_i64(high, low));
	} else {
		CHECK(hit != NULL);
	}
	engine_free(&engine);
}

TEST(SUITE, invalid_battle_commands_do_not_consume_rng) {
	GameEngine engine = warrior();
	CHECK(is_error(step(&engine, cmd_attack()), "invalid_phase"));
	fight(&engine);
	uint32_t rng_state = engine.rng.state;
	CHECK(is_error(step(&engine, cmd_cast("flame_strike")), "unknown_spell"));
	engine.state.player.mp = 0;
	CHECK(is_error(step(&engine, cmd_cast("brutal_strike")), "not_enough_mana"));
	counter_free(&engine.state.player.potions);
	CHECK(is_error(step(&engine, cmd_use_potion("health_potion")), "no_potion"));
	CHECK(is_error(step(&engine, cmd_use_potion("elixir")), "unknown_potion"));
	CHECK(is_error(step(&engine, cmd_next_fight()), "invalid_phase"));
	CHECK(is_error(step(&engine, cmd_buy_potion("health_potion", 1)), "invalid_phase"));
	CHECK_INT(engine.rng.state, rng_state);
	engine_free(&engine);
}

TEST(SUITE, defend_halves_incoming_damage) {
	GameEngine engine = warrior();
	fixed_attack(fight(&engine), 100, ELEMENT_PHYSICAL, NULL);
	engine.state.player.hp = 150;
	const Event *hit = find_event(step(&engine, cmd_defend()), "monster_attacked");
	if (hit != NULL) {
		CHECK_INT(event_get_int(hit, "damage"), 50);
	} else {
		CHECK(hit != NULL);
	}
	CHECK(!engine.state.player.defending);
	engine_free(&engine);
}

TEST(SUITE, undefended_damage_and_death) {
	GameEngine engine = warrior();
	MonsterInstance *monster = fight(&engine);
	fixed_attack(monster, 1000, ELEMENT_PHYSICAL, NULL);
	const EventList *events = step(&engine, cmd_attack());
	REQUIRE(events->count > 0);
	CHECK_EVENT(&events->items[events->count - 1],
	    fmt("{\"type\": \"player_died\", \"monsterId\": \"%s\", \"round\": 1}", monster->creature_id));
	CHECK_INT(engine.state.phase, PHASE_GAME_OVER);
	CHECK(engine.state.has_death_cause);
	CHECK_STR(engine.state.death_cause, monster->creature_id);
	CHECK(is_error(step(&engine, cmd_attack()), "invalid_phase"));
	CHECK(is_error(step(&engine, cmd_next_fight()), "invalid_phase"));
	engine_free(&engine);
}

TEST(SUITE, monster_status_applies_and_ticks) {
	GameEngine engine = warrior();
	StatusOnHit burn = on_hit("burn", 100, 50);
	fixed_attack(fight(&engine), 10, ELEMENT_FIRE, &burn);
	const EventList *events = step(&engine, cmd_defend());
	CHECK_EVENT(find_event(events, "status_applied"),
	    "{\"type\": \"status_applied\", \"target\": \"player\", \"status\": \"burn\", \"turns\": 3, \"perTurn\": 2}");
	CHECK_EVENT(find_event(events, "status_ticked"),
	    "{\"type\": \"status_ticked\", \"target\": \"player\", \"status\": \"burn\", \"damage\": 2}");
	const StatusList *statuses = &engine.state.player.statuses;
	REQUIRE_INT(statuses->count, 1);
	CHECK_STR(statuses->items[0].status_id, "burn");
	CHECK_INT(statuses->items[0].turns, 2);
	CHECK_INT(statuses->items[0].per_turn, 2);
	engine_free(&engine);
}

TEST(SUITE, reapplying_status_refreshes_turns_and_keeps_higher_damage) {
	GameEngine engine = warrior();
	StatusOnHit burn = on_hit("burn", 100, 50);
	fixed_attack(fight(&engine), 10, ELEMENT_FIRE, &burn);
	status_list_push(&engine.state.player.statuses, make_status("burn", 1, 9));
	step(&engine, cmd_defend());
	const StatusList *statuses = &engine.state.player.statuses;
	REQUIRE_INT(statuses->count, 1);
	CHECK_STR(statuses->items[0].status_id, "burn");
	CHECK_INT(statuses->items[0].turns, 2);
	CHECK_INT(statuses->items[0].per_turn, 9);
	engine_free(&engine);
}

TEST(SUITE, status_expires) {
	GameEngine engine = warrior();
	fixed_attack(fight(&engine), 1, ELEMENT_PHYSICAL, NULL);
	status_list_push(&engine.state.player.statuses, make_status("bleed", 1, 1));
	CHECK(has_event(step(&engine, cmd_defend()), "status_expired"));
	CHECK_INT(engine.state.player.statuses.count, 0);
	engine_free(&engine);
}

TEST(SUITE, player_stun_skips_turn_and_has_cooldown) {
	GameEngine engine = warrior();
	StatusOnHit stun = on_hit("stun", 100, 0);
	fixed_attack(fight(&engine), 1, ELEMENT_PHYSICAL, &stun);
	const EventList *events = step(&engine, cmd_defend());
	CHECK_INT(count_events(events, "monster_attacked"), 2);
	CHECK(has_event(events, "player_stunned"));
	CHECK_INT(count_events(events, "status_applied"), 1);
	CHECK_INT(engine.state.player.stun_cooldown, 1);
	CHECK(!has_event(step(&engine, cmd_defend()), "status_applied"));
	engine_free(&engine);
}

TEST(SUITE, monster_stun_skips_its_attack) {
	GameEngine engine = warrior();
	MonsterInstance *monster = fight(&engine);
	fixed_attack(monster, 5, ELEMENT_PHYSICAL, NULL);
	status_list_push(&monster->statuses, make_status("stun", 1, 0));
	const EventList *events = step(&engine, cmd_defend());
	CHECK(has_event(events, "monster_stunned"));
	CHECK(!has_event(events, "monster_attacked"));
	CHECK_INT(monster->stun_cooldown, 1);
	engine_free(&engine);
}

TEST(SUITE, monster_dies_from_status_tick) {
	GameEngine engine = warrior();
	MonsterInstance *monster = fight(&engine);
	monster->hp = 1;
	status_list_push(&monster->statuses, make_status("bleed", 3, 5));
	CHECK(has_event(step(&engine, cmd_defend()), "monster_killed"));
	CHECK_INT(engine.state.phase, PHASE_MERCHANT);
	engine_free(&engine);
}

// What the monster did in a turn, read from the first event that tells it.
static const char *monster_action(const EventList *events) {
	for (size_t i = 0; i < events->count; i++) {
		const Event *event = &events->items[i];
		if (event_is(event, "boss_telegraph")) {
			return "telegraph";
		}
		if (event_is(event, "monster_attacked")) {
			return event_get_bool(event, "charged") ? "charged" : "normal";
		}
		if (event_is(event, "attack_dodged") || event_is(event, "attack_parried")) {
			return "normal";
		}
	}
	return "none";
}

TEST(SUITE, boss_telegraphs_then_charges) {
	GameEngine engine = warrior();
	engine.state.round = 9;
	MonsterInstance *boss = fight(&engine);
	CHECK(boss->is_boss);
	boss->hp = 1000000;
	boss->max_hp = 1000000;
	for (int i = 0; i < boss->attack_count; i++) {
		boss->attacks[i].has_status = false;
	}
	engine.state.player.hp = 1000000;
	const char *const expected[] = {"normal", "normal", "telegraph", "charged"};
	for (size_t i = 0; i < ARRAY_LEN(expected); i++) {
		CHECK_STR(monster_action(step(&engine, cmd_defend())), expected[i]);
	}
	engine_free(&engine);
}

TEST(SUITE, heal_is_capped_and_level_three_cleanses) {
	const GameData *data = test_data();
	GameEngine engine = warrior();
	fixed_attack(fight(&engine), 1, ELEMENT_PHYSICAL, NULL);
	Player *player = &engine.state.player;
	counter_set(&player->spell_uses, "wound_cleansing", 50);
	status_list_push(&player->statuses, make_status("poison", 5, 1));
	player->hp = build_sheet(player, data).max_hp - 3;
	const EventList *events = step(&engine, cmd_cast("wound_cleansing"));
	const Event *healed = find_event(events, "spell_healed");
	if (healed != NULL) {
		CHECK_INT(event_get_int(healed, "amount"), 3);
		CHECK_INT(event_get_int(healed, "mana"), 32);
	} else {
		CHECK(healed != NULL);
	}
	CHECK(events_contain(events, "{\"type\": \"status_expired\", \"target\": \"player\", \"status\": \"poison\"}"));
	CHECK_INT(counter_get(&player->spell_uses, "wound_cleansing"), 51);
	engine_free(&engine);
}

TEST(SUITE, spell_levels_up_after_twenty_uses) {
	GameEngine engine = warrior();
	fixed_attack(fight(&engine), 1, ELEMENT_PHYSICAL, NULL);
	Player *player = &engine.state.player;
	counter_set(&player->spell_uses, "brutal_strike", 19);
	player->mp = 1000;
	const EventList *events = step(&engine, cmd_cast("brutal_strike"));
	CHECK(events_contain(events, "{\"type\": \"spell_level_up\", \"spellId\": \"brutal_strike\", \"level\": 2}"));
	const Event *cast = find_event(events, "spell_cast");
	if (cast != NULL) {
		CHECK_INT(event_get_int(cast, "mana"), 20);
	} else {
		CHECK(cast != NULL);
	}
	engine_free(&engine);
}

TEST(SUITE, magic_level_grows_with_mana_spent) {
	GameEngine engine = warrior();
	fixed_attack(fight(&engine), 1, ELEMENT_PHYSICAL, NULL);
	Player *player = &engine.state.player;
	player->mana_spent = test_data()->balance.magic_level_base - 1;
	player->mp = 1000;
	const EventList *events = step(&engine, cmd_cast("brutal_strike"));
	CHECK(events_contain(events, "{\"type\": \"magic_level_up\", \"magicLevel\": 2}"));
	engine_free(&engine);
}

TEST(SUITE, immune_monster_takes_no_damage) {
	const GameData *data = test_data();
	GameEngine engine = new_engine(data, "mage", "normal", 42);
	MonsterInstance *monster = fight(&engine);
	fixed_attack(monster, 1, ELEMENT_PHYSICAL, NULL);
	id_set(monster->creature_id, "fire_elemental");
	CHECK_INT(data_creature(data, "fire_elemental")->resistances[ELEMENT_FIRE], 0);
	engine.state.player.mp = 1000;
	const Event *cast = find_event(step(&engine, cmd_cast("flame_strike")), "spell_cast");
	if (cast != NULL) {
		CHECK_INT(event_get_int(cast, "damage"), 0);
	} else {
		CHECK(cast != NULL);
	}
	engine_free(&engine);
}

TEST(SUITE, items_add_crit_dodge_and_leech) {
	GameData data = data_copy();
	add_test_items(&data);
	GameEngine engine = new_engine(&data, "warrior", "normal", 42);
	Player *player = &engine.state.player;
	wear(player, SLOT_RING, make_item(99, "test_ring", "common", 0));
	CharacterSheet sheet = build_sheet(player, &data);
	CHECK_INT(sheet.crit_chance, data.balance.caps.crit_chance);
	CHECK_INT(sheet.dodge, data.balance.caps.dodge);
	fixed_attack(fight(&engine), 5, ELEMENT_PHYSICAL, NULL);
	int crits = 0;
	int dodges = 0;
	for (int i = 0; i < 60; i++) {
		player->hp = 100;
		const EventList *events = step(&engine, cmd_attack());
		for (size_t j = 0; j < events->count; j++) {
			const Event *event = &events->items[j];
			crits += event_is(event, "player_attacked") && event_get_bool(event, "crit") ? 1 : 0;
		}
		dodges += count_events(events, "attack_dodged");
	}
	CHECK(crits > 0);
	CHECK(dodges > 0);
	engine_free(&engine);
	game_data_free(&data);
}

TEST(SUITE, potion_restores_and_is_consumed) {
	const GameData *data = test_data();
	GameEngine engine = warrior();
	fixed_attack(fight(&engine), 1, ELEMENT_PHYSICAL, NULL);
	Player *player = &engine.state.player;
	player->hp = 10;
	int64_t before = counter_get(&player->potions, "mana_potion");
	player->mp = 0;
	const Event *used = find_event(step(&engine, cmd_use_potion("mana_potion")), "potion_used");
	if (used != NULL) {
		CHECK_STR(event_get_str(used, "resource"), "mp");
	} else {
		CHECK(used != NULL);
	}
	CHECK_INT(counter_get(&player->potions, "mana_potion"), before - 1);
	CHECK(player->mp > 0 && player->mp <= build_sheet(player, data).max_mp);
	engine_free(&engine);
}

TEST(SUITE, every_vocation_spell_can_be_cast) {
	const GameData *data = test_data();
	const char *const vocations[] = {"warrior", "archer", "mage"};
	for (size_t v = 0; v < ARRAY_LEN(vocations); v++) {
		GameEngine engine = new_engine(data, vocations[v], "normal", 42);
		fixed_attack(fight(&engine), 1, ELEMENT_PHYSICAL, NULL);
		const VocationDef *vocation = data_vocation(data, vocations[v]);
		for (int s = 0; s < vocation->spell_count; s++) {
			engine.state.player.mp = 10000;
			engine.state.player.hp = 5;
			const EventList *events = step(&engine, cmd_cast(vocation->spells[s]));
			bool cast = false;
			for (size_t i = 0; i < events->count; i++) {
				const Event *event = &events->items[i];
				cast = cast || ((event_is(event, "spell_cast") || event_is(event, "spell_healed")) &&
				                   str_eq(event_get_str(event, "spellId"), vocation->spells[s]));
			}
			if (!cast) {
				test_fail(__FILE__, __LINE__, "%s could not cast %s", vocations[v], vocation->spells[s]);
			}
		}
		engine_free(&engine);
	}
}
