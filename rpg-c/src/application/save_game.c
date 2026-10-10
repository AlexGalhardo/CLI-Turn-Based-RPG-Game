#include "application/save_game.h"

#include <stdio.h>
#include <string.h>

typedef struct {
	int year, month, day, hour, minute, second;
} CivilTime;

// Days since 1970-01-01 → calendar date (Howard Hinnant's algorithm). Pure arithmetic: no time zone, no libc state.
static CivilTime civil_time(int64_t unix_seconds) {
	int64_t days = unix_seconds / 86400;
	int64_t rest = unix_seconds % 86400;
	if (rest < 0) {
		rest += 86400;
		days--;
	}
	days += 719468;
	int64_t era = (days >= 0 ? days : days - 146096) / 146097;
	int64_t day_of_era = days - era * 146097;
	int64_t year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
	int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
	int64_t month_index = (5 * day_of_year + 2) / 153;
	CivilTime time = {
	    .day = (int)(day_of_year - (153 * month_index + 2) / 5 + 1),
	    .month = (int)(month_index < 10 ? month_index + 3 : month_index - 9),
	    .hour = (int)(rest / 3600),
	    .minute = (int)(rest % 3600 / 60),
	    .second = (int)(rest % 60),
	};
	time.year = (int)(year_of_era + era * 400 + (time.month <= 2 ? 1 : 0));
	return time;
}

void format_timestamp(int64_t unix_seconds, char out[TIMESTAMP_SIZE]) {
	CivilTime t = civil_time(unix_seconds);
	snprintf(out, TIMESTAMP_SIZE, "%04d-%02d-%02dT%02d:%02d:%02dZ", t.year, t.month, t.day, t.hour, t.minute, t.second);
}

void make_run_id(int64_t started_at, int64_t seed, char out[RUN_ID_SIZE]) {
	CivilTime t = civil_time(started_at);
	snprintf(out, RUN_ID_SIZE, "%04d%02d%02dT%02d%02d%02dZ-%lld", t.year, t.month, t.day, t.hour, t.minute, t.second,
	    (long long)seed);
}

bool check_schema(const JsonValue *document, const char *what, char *error, size_t error_size) {
	JsonError json_error = {0};
	int64_t version = json_read_int(document, "schemaVersion", &json_error);
	if (json_error.failed) {
		snprintf(error, error_size, "%s: %s", what, json_error.message);
		return false;
	}
	if (version > SCHEMA_VERSION) {
		snprintf(error, error_size, "%s uses schema %lld; update the game (supports %d)", what, (long long)version,
		    SCHEMA_VERSION);
		return false;
	}
	return true;
}

static JsonValue *session_to_json(const SessionInfo *session) {
	JsonValue *object = json_object();
	json_set(object, "runId", json_string(session->run_id));
	json_set(object, "startedAt", json_string(session->started_at));
	json_set(object, "playTimeSeconds", json_int(session->play_time_seconds));
	json_set(object, "sessions", json_int(session->sessions));
	return object;
}

JsonValue *save_game_to_json(const SaveGame *save) {
	JsonValue *object = json_object();
	json_set(object, "schemaVersion", json_int(SCHEMA_VERSION));
	json_set(object, "gameVersion", json_string(save->game_version));
	json_set(object, "implementation", json_string(save->implementation));
	json_set(object, "savedAt", json_string(save->saved_at));
	json_set(object, "rngState", json_int(save->rng_state));
	json_set(object, "session", session_to_json(&save->session));
	json_set(object, "run", run_state_to_json(&save->run));
	return object;
}

bool save_game_from_json(const JsonValue *raw, SaveGame *out, char *error, size_t error_size) {
	if (!check_schema(raw, "save.json", error, error_size)) {
		return false;
	}
	JsonError json_error = {0};
	memset(out, 0, sizeof(*out));
	json_read_text(raw, "gameVersion", out->game_version, VERSION_SIZE, &json_error);
	json_read_text(raw, "implementation", out->implementation, VERSION_SIZE, &json_error);
	json_read_text(raw, "savedAt", out->saved_at, TIMESTAMP_SIZE, &json_error);
	out->rng_state = (uint32_t)json_read_int(raw, "rngState", &json_error);
	const JsonValue *session = json_read_object(raw, "session", &json_error);
	json_read_text(session, "runId", out->session.run_id, RUN_ID_SIZE, &json_error);
	json_read_text(session, "startedAt", out->session.started_at, TIMESTAMP_SIZE, &json_error);
	out->session.play_time_seconds = json_read_int(session, "playTimeSeconds", &json_error);
	out->session.sessions = json_read_int(session, "sessions", &json_error);
	bool run_loaded = run_state_from_json(json_read_object(raw, "run", &json_error), &out->run, &json_error);
	if (json_error.failed) {
		if (run_loaded) {
			run_state_free(&out->run);
		}
		snprintf(error, error_size, "save.json: %s", json_error.message);
		return false;
	}
	return true;
}

void save_game_free(SaveGame *save) { run_state_free(&save->run); }

JsonValue *run_record_to_json(const RunRecord *record) {
	JsonValue *object = json_object();
	json_set(object, "schemaVersion", json_int(SCHEMA_VERSION));
	json_set(object, "runId", json_string(record->run_id));
	json_set(object, "name", json_string(record->name));
	json_set(object, "vocation", json_string(record->vocation));
	json_set(object, "difficulty", json_string(record->difficulty));
	json_set(object, "seed", json_int(record->seed));
	json_set(object, "implementation", json_string(record->implementation));
	json_set(object, "gameVersion", json_string(record->game_version));
	json_set(object, "startedAt", json_string(record->started_at));
	json_set(object, "endedAt", json_string(record->ended_at));
	json_set(object, "playTimeSeconds", json_int(record->play_time_seconds));
	json_set(object, "sessions", json_int(record->sessions));
	json_set(object, "round", json_int(record->round));
	json_set(object, "level", json_int(record->level));
	json_set(object, "magicLevel", json_int(record->magic_level));
	json_set(object, "deathCause", json_string(record->death_cause));
	json_set(object, "won", json_bool(record->won));
	json_set(object, "stats", statistics_to_json(&record->stats));
	return object;
}

bool run_record_from_json(const JsonValue *raw, RunRecord *out, char *error, size_t error_size) {
	if (!check_schema(raw, "history record", error, error_size)) {
		return false;
	}
	JsonError json_error = {0};
	memset(out, 0, sizeof(*out));
	json_read_text(raw, "runId", out->run_id, RUN_ID_SIZE, &json_error);
	json_read_text(raw, "name", out->name, NAME_SIZE, &json_error);
	json_read_text(raw, "vocation", out->vocation, ID_SIZE, &json_error);
	json_read_text(raw, "difficulty", out->difficulty, ID_SIZE, &json_error);
	out->seed = json_read_int(raw, "seed", &json_error);
	json_read_text(raw, "implementation", out->implementation, VERSION_SIZE, &json_error);
	json_read_text(raw, "gameVersion", out->game_version, VERSION_SIZE, &json_error);
	json_read_text(raw, "startedAt", out->started_at, TIMESTAMP_SIZE, &json_error);
	json_read_text(raw, "endedAt", out->ended_at, TIMESTAMP_SIZE, &json_error);
	out->play_time_seconds = json_read_int(raw, "playTimeSeconds", &json_error);
	out->sessions = json_read_int(raw, "sessions", &json_error);
	out->round = json_read_int(raw, "round", &json_error);
	out->level = json_read_int(raw, "level", &json_error);
	out->magic_level = json_read_int(raw, "magicLevel", &json_error);
	json_read_text(raw, "deathCause", out->death_cause, ID_SIZE, &json_error);
	out->won = json_read_bool(raw, "won", &json_error);
	statistics_from_json(json_read_object(raw, "stats", &json_error), &out->stats, &json_error);
	if (json_error.failed) {
		statistics_free(&out->stats);
		snprintf(error, error_size, "history record: %s", json_error.message);
		return false;
	}
	return true;
}

void run_record_free(RunRecord *record) { statistics_free(&record->stats); }
