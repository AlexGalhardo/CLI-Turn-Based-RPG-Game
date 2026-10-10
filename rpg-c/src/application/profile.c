#include "application/profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void profile_free(Profile *profile) {
	free(profile->bestiary);
	free(profile->achievements);
	memset(profile, 0, sizeof(*profile));
}

void achievement_list_free(AchievementList *list) {
	free(list->items);
	memset(list, 0, sizeof(*list));
}

const BestiaryEntry *profile_bestiary_entry(const Profile *profile, const char *monster_id) {
	for (size_t i = 0; i < profile->bestiary_count; i++) {
		if (strcmp(profile->bestiary[i].monster_id, monster_id) == 0) {
			return &profile->bestiary[i];
		}
	}
	return NULL;
}

const Unlock *profile_unlock(const Profile *profile, const char *achievement_id) {
	for (size_t i = 0; i < profile->achievement_count; i++) {
		if (strcmp(profile->achievements[i].achievement_id, achievement_id) == 0) {
			return &profile->achievements[i];
		}
	}
	return NULL;
}

// Both lists stay sorted by id: a new entry is appended and then moved back to its place.
static void add_bestiary(Profile *profile, BestiaryEntry entry) {
	VEC_PUSH(profile->bestiary, profile->bestiary_count, profile->bestiary_capacity, entry);
	for (size_t i = profile->bestiary_count - 1; i > 0; i--) {
		if (strcmp(profile->bestiary[i - 1].monster_id, profile->bestiary[i].monster_id) <= 0) {
			break;
		}
		BestiaryEntry swap = profile->bestiary[i - 1];
		profile->bestiary[i - 1] = profile->bestiary[i];
		profile->bestiary[i] = swap;
	}
}

static void add_unlock(Profile *profile, Unlock unlock) {
	VEC_PUSH(profile->achievements, profile->achievement_count, profile->achievement_capacity, unlock);
	for (size_t i = profile->achievement_count - 1; i > 0; i--) {
		if (strcmp(profile->achievements[i - 1].achievement_id, profile->achievements[i].achievement_id) <= 0) {
			break;
		}
		Unlock swap = profile->achievements[i - 1];
		profile->achievements[i - 1] = profile->achievements[i];
		profile->achievements[i] = swap;
	}
}

static JsonValue *hall_entry_to_json(const HallOfFameEntry *entry) {
	JsonValue *object = json_object();
	json_set(object, "runId", json_string(entry->run_id));
	json_set(object, "name", json_string(entry->name));
	json_set(object, "vocation", json_string(entry->vocation));
	json_set(object, "difficulty", json_string(entry->difficulty));
	json_set(object, "round", json_int(entry->round));
	json_set(object, "level", json_int(entry->level));
	json_set(object, "endedAt", json_string(entry->ended_at));
	json_set(object, "won", json_bool(entry->won));
	return object;
}

JsonValue *profile_to_json(const Profile *profile) {
	JsonValue *object = json_object();
	json_set(object, "schemaVersion", json_int(PROFILE_SCHEMA_VERSION));
	JsonValue *bestiary = json_object();
	for (size_t i = 0; i < profile->bestiary_count; i++) {
		JsonValue *entry = json_object();
		json_set(entry, "kills", json_int(profile->bestiary[i].kills));
		json_set(entry, "firstKilledAt", json_string(profile->bestiary[i].first_killed_at));
		json_set(bestiary, profile->bestiary[i].monster_id, entry);
	}
	json_set(object, "bestiary", bestiary);
	JsonValue *achievements = json_object();
	for (size_t i = 0; i < profile->achievement_count; i++) {
		JsonValue *unlock = json_object();
		json_set(unlock, "unlockedAt", json_string(profile->achievements[i].unlocked_at));
		json_set(unlock, "runId", json_string(profile->achievements[i].run_id));
		json_set(achievements, profile->achievements[i].achievement_id, unlock);
	}
	json_set(object, "achievements", achievements);
	JsonValue *hall = json_array();
	for (int i = 0; i < profile->hall_count; i++) {
		json_push(hall, hall_entry_to_json(&profile->hall_of_fame[i]));
	}
	json_set(object, "hallOfFame", hall);
	return object;
}

bool profile_from_json(const JsonValue *raw, Profile *out, char *error, size_t error_size) {
	JsonError json_error = {0};
	memset(out, 0, sizeof(*out));
	const JsonValue *bestiary = json_read_object(raw, "bestiary", &json_error);
	for (size_t i = 0; i < bestiary->count && strlen(bestiary->keys[i]) < ID_SIZE; i++) {
		BestiaryEntry entry = {0};
		id_set(entry.monster_id, bestiary->keys[i]);
		entry.kills = json_read_int(bestiary->items[i], "kills", &json_error);
		json_read_text(bestiary->items[i], "firstKilledAt", entry.first_killed_at, TIMESTAMP_SIZE, &json_error);
		add_bestiary(out, entry);
	}
	const JsonValue *achievements = json_read_object(raw, "achievements", &json_error);
	for (size_t i = 0; i < achievements->count && strlen(achievements->keys[i]) < ID_SIZE; i++) {
		Unlock unlock = {0};
		id_set(unlock.achievement_id, achievements->keys[i]);
		json_read_text(achievements->items[i], "unlockedAt", unlock.unlocked_at, TIMESTAMP_SIZE, &json_error);
		json_read_text(achievements->items[i], "runId", unlock.run_id, RUN_ID_SIZE, &json_error);
		add_unlock(out, unlock);
	}
	const JsonValue *hall = json_read_array(raw, "hallOfFame", &json_error);
	for (size_t i = 0; i < hall->count && out->hall_count < MAX_HALL_OF_FAME; i++) {
		HallOfFameEntry *entry = &out->hall_of_fame[out->hall_count++];
		json_read_text(hall->items[i], "runId", entry->run_id, RUN_ID_SIZE, &json_error);
		json_read_text(hall->items[i], "name", entry->name, NAME_SIZE, &json_error);
		json_read_text(hall->items[i], "vocation", entry->vocation, ID_SIZE, &json_error);
		json_read_text(hall->items[i], "difficulty", entry->difficulty, ID_SIZE, &json_error);
		entry->round = json_read_int(hall->items[i], "round", &json_error);
		entry->level = json_read_int(hall->items[i], "level", &json_error);
		json_read_text(hall->items[i], "endedAt", entry->ended_at, TIMESTAMP_SIZE, &json_error);
		entry->won = json_read_bool(hall->items[i], "won", &json_error);
	}
	if (json_error.failed) {
		profile_free(out);
		snprintf(error, error_size, "profile.json: %s", json_error.message);
		return false;
	}
	return true;
}

static int64_t progress(
    const Profile *profile, const GameData *data, const AchievementDef *achievement, const RunState *state) {
	const Player *player = &state->player;
	const char *type = achievement->type;
	if (str_eq(type, "kills_total")) {
		int64_t total = 0;
		for (size_t i = 0; i < profile->bestiary_count; i++) {
			total += profile->bestiary[i].kills;
		}
		return total;
	}
	if (str_eq(type, "bosses_total")) {
		int64_t total = 0;
		for (int i = 0; i < data->boss_count; i++) {
			const BestiaryEntry *entry = profile_bestiary_entry(profile, data->bosses[i].id);
			total += entry == NULL ? 0 : entry->kills;
		}
		return total;
	}
	if (str_eq(type, "round_reached")) {
		return state->round;
	}
	if (str_eq(type, "level_reached")) {
		return player->level;
	}
	if (str_eq(type, "legendary_found")) {
		return counter_get(&state->stats.items_dropped, "legendary");
	}
	if (str_eq(type, "spell_level_3")) {
		int64_t threshold = data->balance.spell_levels[data->balance.spell_level_count - 1].uses;
		int64_t mastered = 0;
		for (int i = 0; i < player->spell_uses.count; i++) {
			mastered += player->spell_uses.entries[i].value >= threshold ? 1 : 0;
		}
		return mastered;
	}
	if (str_eq(type, "gold_held")) {
		return player->gold;
	}
	if (str_eq(type, "distinct_monsters")) {
		return (int64_t)profile->bestiary_count;
	}
	if (str_eq(type, "hard_round_reached")) {
		return str_eq(state->config.difficulty_id, "hard") ? state->round : 0;
	}
	if (str_eq(type, "run_won")) {
		return state->won ? 1 : 0;
	}
	return 0;
}

void profile_observe(Profile *profile, const GameData *data, const EventList *events, const RunState *state,
    const char *now, const char *run_id, AchievementList *unlocked) {
	for (size_t i = 0; i < events->count; i++) {
		if (!event_is(&events->items[i], "monster_killed")) {
			continue;
		}
		const char *monster_id = event_get_str(&events->items[i], "monsterId");
		BestiaryEntry *known = (BestiaryEntry *)profile_bestiary_entry(profile, monster_id);
		if (known != NULL) {
			known->kills++;
		} else {
			BestiaryEntry entry = {.kills = 1};
			id_set(entry.monster_id, monster_id);
			str_copy(entry.first_killed_at, TIMESTAMP_SIZE, now);
			add_bestiary(profile, entry);
		}
	}
	for (int i = 0; i < data->achievement_count; i++) {
		const AchievementDef *achievement = &data->achievements[i];
		if (profile_unlock(profile, achievement->id) != NULL) {
			continue;
		}
		if (progress(profile, data, achievement, state) >= achievement->value) {
			Unlock unlock = {0};
			id_set(unlock.achievement_id, achievement->id);
			str_copy(unlock.unlocked_at, TIMESTAMP_SIZE, now);
			str_copy(unlock.run_id, RUN_ID_SIZE, run_id);
			add_unlock(profile, unlock);
			VEC_PUSH(unlocked->items, unlocked->count, unlocked->capacity, achievement);
		}
	}
}

// Ranking: won runs first, then the furthest round, the highest level and the earliest date.
static bool ranks_before(const HallOfFameEntry *a, const HallOfFameEntry *b) {
	if (a->won != b->won) {
		return a->won;
	}
	if (a->round != b->round) {
		return a->round > b->round;
	}
	if (a->level != b->level) {
		return a->level > b->level;
	}
	return strcmp(a->ended_at, b->ended_at) < 0;
}

void profile_record_finished_run(Profile *profile, const HallOfFameEntry *entry) {
	if (profile->hall_count > HALL_OF_FAME_SIZE) {
		profile->hall_count = HALL_OF_FAME_SIZE;
	}
	// Stable insertion: the new entry goes after the existing ones it ties with.
	int position = profile->hall_count;
	for (int i = 0; i < profile->hall_count; i++) {
		if (ranks_before(entry, &profile->hall_of_fame[i])) {
			position = i;
			break;
		}
	}
	if (position >= HALL_OF_FAME_SIZE) {
		return;
	}
	int last = profile->hall_count < HALL_OF_FAME_SIZE ? profile->hall_count : HALL_OF_FAME_SIZE - 1;
	for (int i = last; i > position; i--) {
		profile->hall_of_fame[i] = profile->hall_of_fame[i - 1];
	}
	profile->hall_of_fame[position] = *entry;
	profile->hall_count = last + 1;
}

bool profile_revealed(const Profile *profile, const char *monster_id) {
	const BestiaryEntry *entry = profile_bestiary_entry(profile, monster_id);
	return entry != NULL && entry->kills >= BESTIARY_REVEAL_KILLS;
}
