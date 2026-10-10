#include "application/run_state.h"

#include <string.h>

RunConfig run_config(const char *name, const char *vocation_id, const char *difficulty_id, bool auto_equip) {
	RunConfig config;
	memset(&config, 0, sizeof(config));
	str_copy(config.name, NAME_SIZE, name);
	id_set(config.vocation_id, vocation_id);
	id_set(config.difficulty_id, difficulty_id);
	config.auto_equip = auto_equip;
	return config;
}

JsonValue *run_config_to_json(const RunConfig *config) {
	JsonValue *object = json_object();
	json_set(object, "name", json_string(config->name));
	json_set(object, "vocation", json_string(config->vocation_id));
	json_set(object, "difficulty", json_string(config->difficulty_id));
	json_set(object, "autoEquip", json_bool(config->auto_equip));
	return object;
}

void run_config_from_json(const JsonValue *raw, RunConfig *out, JsonError *error) {
	memset(out, 0, sizeof(*out));
	json_read_text(raw, "name", out->name, NAME_SIZE, error);
	json_read_text(raw, "vocation", out->vocation_id, ID_SIZE, error);
	json_read_text(raw, "difficulty", out->difficulty_id, ID_SIZE, error);
	out->auto_equip = json_read_bool(raw, "autoEquip", error);
}

int64_t run_state_take_item_uid(RunState *state) { return state->next_item_uid++; }

void run_state_free(RunState *state) {
	player_free(&state->player);
	statistics_free(&state->stats);
}

RunState run_state_clone(const RunState *state) {
	RunState copy = *state;
	copy.player = player_clone(&state->player);
	copy.stats = statistics_clone(&state->stats);
	return copy;
}

JsonValue *run_state_to_json(const RunState *state) {
	JsonValue *object = json_object();
	json_set(object, "seed", json_int(state->seed));
	json_set(object, "config", run_config_to_json(&state->config));
	json_set(object, "player", player_to_json(&state->player));
	json_set(object, "phase", json_string(phase_name(state->phase)));
	json_set(object, "round", json_int(state->round));
	json_set(object, "turn", json_int(state->turn));
	json_set(object, "monster", state->has_monster ? monster_instance_to_json(&state->monster) : json_null());
	JsonValue *stock = json_array();
	for (int i = 0; i < state->stock_count; i++) {
		json_push(stock, item_instance_to_json(&state->merchant_stock[i]));
	}
	json_set(object, "merchantStock", stock);
	json_set(object, "nextItemUid", json_int(state->next_item_uid));
	json_set(object, "deathCause", state->has_death_cause ? json_string(state->death_cause) : json_null());
	json_set(object, "won", json_bool(state->won));
	json_set(object, "stats", statistics_to_json(&state->stats));
	return object;
}

bool run_state_from_json(const JsonValue *raw, RunState *out, JsonError *error) {
	memset(out, 0, sizeof(*out));
	out->seed = json_read_int(raw, "seed", error);
	run_config_from_json(json_read_object(raw, "config", error), &out->config, error);
	player_from_json(json_read_object(raw, "player", error), &out->player, error);
	const char *phase = json_read_str(raw, "phase", error);
	if (!error->failed && !phase_parse(phase, &out->phase)) {
		json_error_set(error, "unknown phase: %.40s", phase);
	}
	out->round = json_read_int(raw, "round", error);
	out->turn = json_read_int(raw, "turn", error);
	const JsonValue *monster = json_get(raw, "monster");
	if (monster == NULL) {
		json_error_set(error, "missing key 'monster'");
	} else if (monster->type != JSON_NULL) {
		out->has_monster = true;
		monster_instance_from_json(monster, &out->monster, error);
	}
	const JsonValue *stock = json_read_array(raw, "merchantStock", error);
	if (stock->count > MAX_MERCHANT_STOCK) {
		json_error_set(error, "merchant stock too large");
	} else {
		for (size_t i = 0; i < stock->count; i++) {
			item_instance_from_json(stock->items[i], &out->merchant_stock[out->stock_count++], error);
		}
	}
	out->next_item_uid = json_read_int(raw, "nextItemUid", error);
	const JsonValue *death_cause = json_get(raw, "deathCause");
	if (death_cause == NULL) {
		json_error_set(error, "missing key 'deathCause'");
	} else if (death_cause->type != JSON_NULL) {
		out->has_death_cause = true;
		json_copy_text(death_cause, out->death_cause, ID_SIZE, error);
	}
	out->won = json_read_bool(raw, "won", error);
	statistics_from_json(json_read_object(raw, "stats", error), &out->stats, error);
	if (error->failed) {
		run_state_free(out);
		return false;
	}
	return true;
}
