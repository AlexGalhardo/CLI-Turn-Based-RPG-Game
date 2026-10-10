// Cross-run profile: bestiary, achievements and Hall of Fame (docs/game-design.md §11).
#ifndef RPG_APPLICATION_PROFILE_H
#define RPG_APPLICATION_PROFILE_H

#include "application/events.h"
#include "application/run_state.h"
#include "application/save_game.h"

#define PROFILE_SCHEMA_VERSION 2
#define HALL_OF_FAME_SIZE 10
#define BESTIARY_REVEAL_KILLS 5
// A profile written by hand could hold more entries than the game ever keeps; loading stops at this limit.
#define MAX_HALL_OF_FAME 32

typedef struct {
	Id monster_id;
	int64_t kills;
	char first_killed_at[TIMESTAMP_SIZE];
} BestiaryEntry;

typedef struct {
	Id achievement_id;
	char unlocked_at[TIMESTAMP_SIZE];
	char run_id[RUN_ID_SIZE];
} Unlock;

typedef struct {
	char run_id[RUN_ID_SIZE];
	char name[NAME_SIZE];
	Id vocation;
	Id difficulty;
	int64_t round;
	int64_t level;
	char ended_at[TIMESTAMP_SIZE];
	bool won;
} HallOfFameEntry;

// Bestiary and achievements are kept sorted by id (the order they are written in).
typedef struct {
	BestiaryEntry *bestiary;
	size_t bestiary_count;
	size_t bestiary_capacity;
	Unlock *achievements;
	size_t achievement_count;
	size_t achievement_capacity;
	int hall_count;
	HallOfFameEntry hall_of_fame[MAX_HALL_OF_FAME];
} Profile;

void profile_free(Profile *profile);
JsonValue *profile_to_json(const Profile *profile);
bool profile_from_json(const JsonValue *raw, Profile *out, char *error, size_t error_size);
// NULL when the monster was never killed / the achievement is still locked.
const BestiaryEntry *profile_bestiary_entry(const Profile *profile, const char *monster_id);
const Unlock *profile_unlock(const Profile *profile, const char *achievement_id);

typedef struct {
	const AchievementDef **items;
	size_t count;
	size_t capacity;
} AchievementList;

void achievement_list_free(AchievementList *list);

// Feeds the profile from engine events and appends the achievements unlocked by this step to `unlocked`.
void profile_observe(Profile *profile, const GameData *data, const EventList *events, const RunState *state,
    const char *now, const char *run_id, AchievementList *unlocked);
void profile_record_finished_run(Profile *profile, const HallOfFameEntry *entry);
// A monster's bestiary entry shows its details after enough kills.
bool profile_revealed(const Profile *profile, const char *monster_id);

#endif
