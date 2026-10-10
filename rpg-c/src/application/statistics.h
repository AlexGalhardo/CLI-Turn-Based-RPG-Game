// Deterministic run counters, derived only from engine events (docs/game-design.md §11).
#ifndef RPG_APPLICATION_STATISTICS_H
#define RPG_APPLICATION_STATISTICS_H

#include "application/events.h"
#include "domain/entities.h"

typedef struct {
	Id item_id;
	Id rarity;
	int64_t round;
} DroppedItem;

typedef struct {
	int64_t damage_dealt;
	int64_t damage_taken;
	int64_t healing_done;
	int64_t highest_hit;
	int64_t normal_attacks;
	int64_t crits;
	int64_t dodges;
	int64_t parries;
	int64_t defends;
	int64_t gold_looted;
	int64_t gold_spent;
	int64_t gold_earned;
	int64_t items_sold;
	int64_t items_auto_equipped;
	int64_t bosses_killed;
	int64_t elites_killed;
	Counter spells_cast;
	Counter potions_used;
	Counter potions_bought;
	Counter potions_dropped;
	Counter items_dropped;
	Counter kills;
	Counter statuses_applied;
	DroppedItem *dropped_items;
	size_t dropped_count;
	size_t dropped_capacity;
} RunStatistics;

void statistics_record(RunStatistics *stats, const EventList *events, int64_t current_round);
void statistics_free(RunStatistics *stats);
RunStatistics statistics_clone(const RunStatistics *stats);
JsonValue *statistics_to_json(const RunStatistics *stats);
void statistics_from_json(const JsonValue *raw, RunStatistics *out, JsonError *error);

#endif
