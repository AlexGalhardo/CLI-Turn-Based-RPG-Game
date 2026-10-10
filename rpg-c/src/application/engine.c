#include "application/engine.h"

#include "application/auto_equip.h"
#include "application/battle.h"
#include "application/loot.h"
#include "application/merchant.h"
#include "application/progression.h"
#include "application/spawner.h"
#include "domain/character.h"
#include "domain/formulas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool engine_new_run(GameEngine *engine, const GameData *data, const RunConfig *config, int64_t seed, EventList *events,
    char *error, size_t error_size) {
	const VocationDef *vocation = find_vocation(data, config->vocation_id);
	if (vocation == NULL || find_difficulty(&data->balance, config->difficulty_id) == NULL) {
		snprintf(error, error_size, "invalid run config: '%s'",
		    vocation == NULL ? config->vocation_id : config->difficulty_id);
		return false;
	}

	memset(engine, 0, sizeof(*engine));
	engine->data = data;
	engine->rng = rng_new(seed);
	RunState *state = &engine->state;
	state->seed = seed;
	state->config = *config;
	state->phase = PHASE_MERCHANT;
	state->next_item_uid = 1;

	Player *player = &state->player;
	str_copy(player->name, NAME_SIZE, config->name);
	id_set(player->vocation_id, vocation->id);
	player->hp = vocation->start_hp;
	player->mp = vocation->start_mp;
	player->gold = data->balance.starting_gold;
	player->level = 1;
	player->magic_level = 1;
	for (int i = 0; i < data->balance.starting_potion_count; i++) {
		counter_set(
		    &player->potions, data->balance.starting_potions[i].potion_id, data->balance.starting_potions[i].quantity);
	}

	const ItemDef *starter = data_item(data, vocation->starter_weapon);
	ItemInstance *weapon = &player->equipment[starter->slot];
	weapon->uid = run_state_take_item_uid(state);
	id_set(weapon->item_id, starter->id);
	id_set(weapon->rarity, "common");
	weapon->tier = starter->tier;
	player->equipped[starter->slot] = true;

	Event started = event_new("run_started");
	event_int(&started, "seed", seed);
	event_str(&started, "vocation", vocation->id);
	event_str(&started, "difficulty", config->difficulty_id);
	events_push(events, started);
	merchant_enter(data, &engine->rng, state, events);
	return true;
}

void engine_restore(GameEngine *engine, const GameData *data, RunState state, uint32_t rng_state) {
	engine->data = data;
	engine->state = state;
	engine->rng.state = rng_state;
}

void engine_free(GameEngine *engine) { run_state_free(&engine->state); }

static void next_fight(GameEngine *engine, EventList *events) {
	RunState *state = &engine->state;
	state->round++;
	const DifficultyDef *difficulty = balance_difficulty(&engine->data->balance, state->config.difficulty_id);
	RoundInfo info = spawn_monster(engine->data, &engine->rng, state->round, difficulty, &state->monster);
	state->has_monster = true;
	state->phase = PHASE_BATTLE;
	state->turn = 1;
	state->stock_count = 0;
	Event started = event_new("round_started");
	event_int(&started, "round", state->round);
	event_int(&started, "tier", info.tier);
	event_int(&started, "cycle", info.cycle);
	event_str(&started, "monsterId", state->monster.creature_id);
	event_bool(&started, "isBoss", state->monster.is_boss);
	event_str(&started, "enemyClass", enemy_class_name(state->monster.enemy_class));
	event_int(&started, "hp", state->monster.hp);
	events_push(events, started);
}

static void drop_item(GameEngine *engine, const EnemyClassDef *row, EventList *events) {
	RunState *state = &engine->state;
	const GameData *data = engine->data;
	ItemInstance item;
	int64_t tier = round_info(state->round, &data->balance, data_tier_count(data)).tier;
	if (!generate_item(data, &engine->rng, data_vocation(data, state->player.vocation_id), tier, &row->rarity_weights,
	        state->next_item_uid, &item)) {
		return;
	}
	run_state_take_item_uid(state);
	Event dropped = event_new("item_dropped");
	event_int(&dropped, "uid", item.uid);
	event_str(&dropped, "itemId", item.item_id);
	event_str(&dropped, "rarity", item.rarity);
	events_push(events, dropped);
	if (state->player.bag_count >= data->balance.bag_capacity) {
		int64_t value = item_value(&item, data);
		state->player.gold += value;
		Event sold = event_new("item_auto_sold");
		event_int(&sold, "uid", item.uid);
		event_str(&sold, "itemId", item.item_id);
		event_int(&sold, "gold", value);
		events_push(events, sold);
	} else {
		player_bag_push(&state->player, &item);
	}
}

static void drop_potion(GameEngine *engine, EventList *events) {
	RunState *state = &engine->state;
	const GameData *data = engine->data;
	const PotionDef **unlocked = xmalloc((size_t)data->potion_count * sizeof(*unlocked));
	size_t count = 0;
	for (int i = 0; i < data->potion_count; i++) {
		if (data->potions[i].unlock_round <= state->round) {
			unlocked[count++] = &data->potions[i];
		}
	}
	if (count > 0) {
		const PotionDef *potion = unlocked[rng_pick(&engine->rng, count)];
		counter_add(&state->player.potions, potion->id, 1);
		Event dropped = event_new("potion_dropped");
		event_str(&dropped, "potionId", potion->id);
		events_push(events, dropped);
	}
	free(unlocked);
}

// One rule for the three classes: chance(100) and chance(0) consume nothing (docs/game-design.md §8).
static void drops(GameEngine *engine, EnemyClass enemy_class, EventList *events) {
	const EnemyClassDef *row = balance_enemy_class(&engine->data->balance, enemy_class);
	if (rng_chance(&engine->rng, row->drop_chance_pct)) {
		for (int64_t i = 0; i < row->drops; i++) {
			drop_item(engine, row, events);
		}
	}
	if (rng_chance(&engine->rng, row->potion_drop_pct)) {
		drop_potion(engine, events);
	}
}

static void victory(GameEngine *engine, EventList *events) {
	RunState *state = &engine->state;
	const GameData *data = engine->data;
	Player *player = &state->player;
	if (!state->has_monster) {
		fatal("victory without a monster");
	}
	MonsterInstance monster = state->monster;
	Event killed = event_new("monster_killed");
	event_str(&killed, "monsterId", monster.creature_id);
	event_bool(&killed, "isBoss", monster.is_boss);
	event_str(&killed, "enemyClass", enemy_class_name(monster.enemy_class));
	events_push(events, killed);
	gain_experience(data, player, monster.xp, events);

	int64_t gold = rng_roll(&engine->rng, monster.gold_min, monster.gold_max);
	player->gold += gold;
	Event looted = event_new("gold_looted");
	event_int(&looted, "amount", gold);
	events_push(events, looted);
	drops(engine, monster.enemy_class, events);
	if (state->config.auto_equip) {
		auto_equip(state, data, events);
	}

	player->statuses.count = 0;
	player->stun_cooldown = 0;
	player->defending = false;
	CharacterSheet sheet = build_sheet(player, data);
	player->hp = min_i64(player->hp, sheet.max_hp);
	player->mp = min_i64(player->mp, sheet.max_mp);
	state->has_monster = false;
	state->turn = 0;
	if (state->round == data->balance.final_round) {
		state->won = true;
		state->phase = PHASE_VICTORY;
		Event won = event_new("run_won");
		event_int(&won, "round", state->round);
		events_push(events, won);
		return;
	}
	state->phase = PHASE_MERCHANT;
	merchant_enter(data, &engine->rng, state, events);
}

static void defeat(GameEngine *engine, EventList *events) {
	RunState *state = &engine->state;
	const char *monster_id = state->has_monster ? state->monster.creature_id : "";
	state->phase = PHASE_GAME_OVER;
	state->has_death_cause = true;
	id_set(state->death_cause, monster_id);
	Event died = event_new("player_died");
	event_str(&died, "monsterId", monster_id);
	event_int(&died, "round", state->round);
	events_push(events, died);
}

static void battle_turn(GameEngine *engine, const Command *command, EventList *events) {
	Battle battle = {.data = engine->data, .rng = &engine->rng, .state = &engine->state};
	Event invalid;
	if (!battle_validate(&battle, command, &invalid)) {
		events_push(events, invalid);
		return;
	}
	BattleOutcome outcome = battle_play_turn(&battle, command, events);
	if (outcome == BATTLE_VICTORY) {
		victory(engine, events);
	} else if (outcome == BATTLE_DEFEAT) {
		defeat(engine, events);
	}
}

static void dispatch(GameEngine *engine, const Command *command, EventList *events) {
	RunState *state = &engine->state;
	Phase required = PHASE_MERCHANT;
	switch (command->type) {
	case CMD_ATTACK:
	case CMD_CAST:
	case CMD_USE_POTION:
	case CMD_DEFEND:
		required = PHASE_BATTLE;
		break;
	case CMD_END_RUN:
	case CMD_CONTINUE_RUN:
		required = PHASE_VICTORY;
		break;
	case CMD_NEXT_FIGHT:
	case CMD_BUY_POTION:
	case CMD_SELL_ITEM:
	case CMD_EQUIP:
	case CMD_UNEQUIP:
	case CMD_BUY_STOCK_ITEM:
		required = PHASE_MERCHANT;
		break;
	}
	if (state->phase != required) {
		events_push(events, event_error(ERROR_INVALID_PHASE));
		return;
	}
	switch (command->type) {
	case CMD_ATTACK:
	case CMD_CAST:
	case CMD_USE_POTION:
	case CMD_DEFEND:
		battle_turn(engine, command, events);
		break;
	case CMD_NEXT_FIGHT:
		next_fight(engine, events);
		break;
	case CMD_END_RUN: {
		state->phase = PHASE_GAME_OVER;
		state->has_death_cause = false;
		state->death_cause[0] = '\0';
		Event ended = event_new("run_ended");
		event_bool(&ended, "won", state->won);
		events_push(events, ended);
		break;
	}
	case CMD_CONTINUE_RUN:
		state->phase = PHASE_MERCHANT;
		merchant_enter(engine->data, &engine->rng, state, events);
		break;
	case CMD_BUY_POTION:
	case CMD_SELL_ITEM:
	case CMD_EQUIP:
	case CMD_UNEQUIP:
	case CMD_BUY_STOCK_ITEM:
		merchant_handle(engine->data, state, command, events);
		break;
	}
}

void engine_step(GameEngine *engine, const Command *command, EventList *events) {
	// Only the events of this command feed the statistics, even when `events` already holds earlier ones.
	size_t first = events->count;
	dispatch(engine, command, events);
	EventList produced = {.items = events->items + first, .count = events->count - first};
	statistics_record(&engine->state.stats, &produced, engine->state.round);
}
