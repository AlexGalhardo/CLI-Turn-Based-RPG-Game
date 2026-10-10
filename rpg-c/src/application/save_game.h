// Save file and finished-run record formats, shared by every implementation (docs/persistence.md).
#ifndef RPG_APPLICATION_SAVE_GAME_H
#define RPG_APPLICATION_SAVE_GAME_H

#include "application/run_state.h"

#define SCHEMA_VERSION 2
#define IMPLEMENTATION "c"

#define TIMESTAMP_SIZE 32
#define RUN_ID_SIZE 64
#define VERSION_SIZE 32

// Time is a count of seconds since 1970-01-01T00:00:00Z (the Clock port returns it).
// Writes "2026-09-27T21:04:11Z".
void format_timestamp(int64_t unix_seconds, char out[TIMESTAMP_SIZE]);
// `<startedAt as yyyyMMddTHHmmssZ>-<seed>`
void make_run_id(int64_t started_at, int64_t seed, char out[RUN_ID_SIZE]);

// Fails when the document was written by a newer game version: such a file is never overwritten.
bool check_schema(const JsonValue *document, const char *what, char *error, size_t error_size);

typedef struct {
	char run_id[RUN_ID_SIZE];
	char started_at[TIMESTAMP_SIZE];
	int64_t play_time_seconds;
	int64_t sessions;
} SessionInfo;

typedef struct {
	char game_version[VERSION_SIZE];
	char implementation[VERSION_SIZE];
	char saved_at[TIMESTAMP_SIZE];
	uint32_t rng_state;
	SessionInfo session;
	RunState run; // owned: release with save_game_free()
} SaveGame;

JsonValue *save_game_to_json(const SaveGame *save);
bool save_game_from_json(const JsonValue *raw, SaveGame *out, char *error, size_t error_size);
void save_game_free(SaveGame *save);

// A finished run, written to history/<runId>.json.
typedef struct {
	char run_id[RUN_ID_SIZE];
	char name[NAME_SIZE];
	Id vocation;
	Id difficulty;
	int64_t seed;
	char implementation[VERSION_SIZE];
	char game_version[VERSION_SIZE];
	char started_at[TIMESTAMP_SIZE];
	char ended_at[TIMESTAMP_SIZE];
	int64_t play_time_seconds;
	int64_t sessions;
	int64_t round;
	int64_t level;
	int64_t magic_level;
	Id death_cause; // empty for a won run
	bool won;
	RunStatistics stats; // owned: release with run_record_free()
} RunRecord;

JsonValue *run_record_to_json(const RunRecord *record);
bool run_record_from_json(const JsonValue *raw, RunRecord *out, char *error, size_t error_size);
void run_record_free(RunRecord *record);

#endif
