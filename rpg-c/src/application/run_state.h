// Everything needed to continue a run, except the PRNG state (kept by the engine).
// A RunState owns heap memory (counters, the dropped-items list): copy it with run_state_clone() and release it
// with run_state_free(), never with plain assignment.
#ifndef RPG_APPLICATION_RUN_STATE_H
#define RPG_APPLICATION_RUN_STATE_H

#include "application/statistics.h"
#include "domain/entities.h"

typedef struct {
	char name[NAME_SIZE];
	Id vocation_id;
	Id difficulty_id;
	bool auto_equip;
} RunConfig;

RunConfig run_config(const char *name, const char *vocation_id, const char *difficulty_id, bool auto_equip);
JsonValue *run_config_to_json(const RunConfig *config);
void run_config_from_json(const JsonValue *raw, RunConfig *out, JsonError *error);

typedef struct {
	int64_t seed;
	RunConfig config;
	Player player;
	Phase phase;
	int64_t round;
	int64_t turn;
	bool has_monster;
	MonsterInstance monster;
	int stock_count;
	ItemInstance merchant_stock[MAX_MERCHANT_STOCK];
	int64_t next_item_uid;
	bool has_death_cause;
	Id death_cause;
	bool won;
	RunStatistics stats;
} RunState;

int64_t run_state_take_item_uid(RunState *state);
void run_state_free(RunState *state);
RunState run_state_clone(const RunState *state);
JsonValue *run_state_to_json(const RunState *state);
// On failure `out` is left released (nothing to free).
bool run_state_from_json(const JsonValue *raw, RunState *out, JsonError *error);

#endif
