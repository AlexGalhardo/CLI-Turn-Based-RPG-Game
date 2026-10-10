#include "application/simulator.h"

#include "application/bot.h"
#include "application/engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t summary_win_rate_pct(const SimulationSummary *summary) { return summary->wins * 100 / summary->runs; }

bool play_one(
    const GameData *data, const RunConfig *config, int64_t seed, RunResult *out, char *error, size_t error_size) {
	GameEngine engine;
	EventList events = {0};
	if (!engine_new_run(&engine, data, config, seed, &events, error, error_size)) {
		events_free(&events);
		return false;
	}
	bool finished = false;
	for (int step = 0; step < MAX_STEPS_PER_RUN; step++) {
		if (engine.state.phase == PHASE_GAME_OVER) {
			finished = true;
			break;
		}
		Command command = bot_choose(data, &engine.state);
		events_clear(&events);
		engine_step(&engine, &command, &events);
	}
	if (finished) {
		memset(out, 0, sizeof(*out));
		out->round = engine.state.round;
		out->level = engine.state.player.level;
		id_set(out->death_cause, engine.state.has_death_cause ? engine.state.death_cause : "");
		out->won = engine.state.won;
	} else {
		snprintf(error, error_size, "run did not finish (seed %lld)", (long long)seed);
	}
	events_free(&events);
	engine_free(&engine);
	return finished;
}

static int compare_int64(const void *a, const void *b) {
	int64_t left = *(const int64_t *)a;
	int64_t right = *(const int64_t *)b;
	return (left > right) - (left < right);
}

static int64_t percentile(const int64_t *sorted_values, int64_t count, int64_t percent) {
	int64_t index = count * percent / 100;
	return sorted_values[index < count - 1 ? index : count - 1];
}

// Most frequent first; ties by id.
static int compare_killers(const void *a, const void *b) {
	const CounterEntry *left = a;
	const CounterEntry *right = b;
	if (left->value != right->value) {
		return left->value > right->value ? -1 : 1;
	}
	return strcmp(left->key, right->key);
}

bool simulate(const GameData *data, const char *vocation, const char *difficulty, int64_t runs, int64_t base_seed,
    SimulationSummary *out, char *error, size_t error_size) {
	if (runs <= 0) {
		snprintf(error, error_size, "runs must be positive");
		return false;
	}
	memset(out, 0, sizeof(*out));
	id_set(out->vocation, vocation);
	id_set(out->difficulty, difficulty);
	out->runs = runs;

	RunConfig config = run_config("Bot", vocation, difficulty, false);
	int64_t *rounds = xmalloc((size_t)runs * sizeof(int64_t));
	Counter killers = {0};
	int64_t level_total = 0;
	bool ok = true;
	for (int64_t i = 0; i < runs && ok; i++) {
		RunResult result;
		ok = play_one(data, &config, base_seed + i, &result, error, error_size);
		if (!ok) {
			break;
		}
		rounds[i] = result.round;
		level_total += result.level;
		out->wins += result.won ? 1 : 0;
		if (result.death_cause[0] != '\0') {
			counter_add(&killers, result.death_cause, 1);
		}
	}
	if (ok) {
		qsort(rounds, (size_t)runs, sizeof(int64_t), compare_int64);
		out->min_round = rounds[0];
		out->p10_round = percentile(rounds, runs, 10);
		out->median_round = percentile(rounds, runs, 50);
		out->p90_round = percentile(rounds, runs, 90);
		out->max_round = rounds[runs - 1];
		out->mean_level = level_total / runs;
		qsort(killers.entries, (size_t)killers.count, sizeof(CounterEntry), compare_killers);
		for (int i = 0; i < killers.count && i < TOP_KILLERS; i++) {
			id_set(out->top_killers[i].monster_id, killers.entries[i].key);
			out->top_killers[i].count = killers.entries[i].value;
			out->top_killer_count++;
		}
	}
	counter_free(&killers);
	free(rounds);
	return ok;
}
