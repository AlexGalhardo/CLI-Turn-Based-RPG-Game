// Headless balance simulator: the bot plays many runs and we aggregate how often it wins and how far it gets.
#ifndef RPG_APPLICATION_SIMULATOR_H
#define RPG_APPLICATION_SIMULATOR_H

#include "application/run_state.h"

#define MAX_STEPS_PER_RUN 200000
#define TOP_KILLERS 3

typedef struct {
	int64_t round;
	int64_t level;
	Id death_cause; // empty for a won run
	bool won;
} RunResult;

typedef struct {
	Id monster_id;
	int64_t count;
} Killer;

typedef struct {
	Id vocation;
	Id difficulty;
	int64_t runs;
	int64_t wins;
	int64_t min_round;
	int64_t p10_round;
	int64_t median_round;
	int64_t p90_round;
	int64_t max_round;
	int64_t mean_level;
	int top_killer_count;
	Killer top_killers[TOP_KILLERS];
} SimulationSummary;

int64_t summary_win_rate_pct(const SimulationSummary *summary);
// Both return false and fill `error` on failure (invalid config, runs <= 0, a run that never ends).
bool play_one(
    const GameData *data, const RunConfig *config, int64_t seed, RunResult *out, char *error, size_t error_size);
bool simulate(const GameData *data, const char *vocation, const char *difficulty, int64_t runs, int64_t base_seed,
    SimulationSummary *out, char *error, size_t error_size);

#endif
