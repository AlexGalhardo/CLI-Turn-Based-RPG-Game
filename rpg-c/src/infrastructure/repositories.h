// JSON file repositories under the player's data directory (docs/persistence.md), and the system clock.
#ifndef RPG_INFRASTRUCTURE_REPOSITORIES_H
#define RPG_INFRASTRUCTURE_REPOSITORIES_H

#include "application/ports.h"
#include "infrastructure/filesystem.h"
#include "infrastructure/i18n.h"

// Pretty-prints `document` (tabs, trailing newline) to `path` atomically; stops the program when it cannot write.
void write_json_atomic(const char *path, const JsonValue *document);
// Parses a JSON file. LOAD_MISSING when it does not exist; LOAD_FAILED (with `error`) when it is not valid JSON
// or not an object. On LOAD_OK the caller frees `*out` with json_free().
LoadResult read_json_object(const char *path, JsonValue **out, char *error, size_t error_size);

Clock system_clock(void);

typedef struct {
	bool has_locale;
	char locale[LOCALE_SIZE];
	bool auto_equip;
	int64_t battle_speed; // 1 or 2
} Settings;

Settings default_settings(void);
// A missing file loads as the defaults (LOAD_OK).
LoadResult settings_load(const char *data_dir, Settings *out, char *error, size_t error_size);
void settings_save(const char *data_dir, const Settings *settings);

// The save, history and profile ports backed by files in `data_dir` (which must outlive the result).
Repositories file_repositories(const char *data_dir);
// Every finished run, sorted by file name. Free each record with run_record_free(), then the array with free().
LoadResult history_list(const char *data_dir, RunRecord **out, size_t *count, char *error, size_t error_size);

#endif
