#include "infrastructure/repositories.h"

#include "infrastructure/migrations.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void write_json_atomic(const char *path, const JsonValue *document) {
	char *text = json_dump(document, true);
	StrBuf content = {0};
	sb_append(&content, text);
	sb_append_char(&content, '\n');
	bool ok = fs_write_text_atomic(path, content.data);
	free(text);
	sb_free(&content);
	if (!ok) {
		fatal("cannot write %s", path);
	}
}

LoadResult read_json_object(const char *path, JsonValue **out, char *error, size_t error_size) {
	*out = NULL;
	if (!fs_exists(path)) {
		return LOAD_MISSING;
	}
	char *text = fs_read_text(path);
	if (text == NULL) {
		snprintf(error, error_size, "cannot read %s", path);
		return LOAD_FAILED;
	}
	JsonError json_error = {0};
	JsonValue *document = json_parse(text, &json_error);
	free(text);
	if (document == NULL) {
		snprintf(error, error_size, "%s: %s", path, json_error.message);
		return LOAD_FAILED;
	}
	if (document->type != JSON_OBJECT) {
		json_free(document);
		snprintf(error, error_size, "%s: expected object", path);
		return LOAD_FAILED;
	}
	*out = document;
	return LOAD_OK;
}

static int64_t system_now(void *context) {
	(void)context;
	return (int64_t)time(NULL);
}

Clock system_clock(void) {
	Clock clock = {.now = system_now, .context = NULL};
	return clock;
}

// ── settings ────────────────────────────────────────────────────────────────

Settings default_settings(void) {
	Settings settings = {.battle_speed = 1};
	return settings;
}

LoadResult settings_load(const char *data_dir, Settings *out, char *error, size_t error_size) {
	char path[PATH_SIZE];
	path_join(path, data_dir, "settings.json");
	*out = default_settings();
	JsonValue *document;
	LoadResult result = read_json_object(path, &document, error, error_size);
	if (result != LOAD_OK) {
		return result == LOAD_MISSING ? LOAD_OK : result;
	}
	if (!check_schema(document, "settings.json", error, error_size)) {
		json_free(document);
		return LOAD_FAILED;
	}
	migrate_settings(document);
	JsonError json_error = {0};
	if (json_has(document, "locale")) {
		const char *locale = json_read_str(document, "locale", &json_error);
		if (locale_supported(locale)) {
			out->has_locale = true;
			str_copy(out->locale, LOCALE_SIZE, locale);
		}
	}
	int64_t speed = json_read_int(document, "battleSpeed", &json_error);
	out->battle_speed = speed == 1 || speed == 2 ? speed : 1;
	out->auto_equip = json_read_bool(document, "autoEquip", &json_error);
	json_free(document);
	if (json_error.failed) {
		snprintf(error, error_size, "settings.json: %s", json_error.message);
		return LOAD_FAILED;
	}
	return LOAD_OK;
}

void settings_save(const char *data_dir, const Settings *settings) {
	char path[PATH_SIZE];
	path_join(path, data_dir, "settings.json");
	JsonValue *document = json_object();
	json_set(document, "schemaVersion", json_int(SCHEMA_VERSION));
	if (settings->has_locale) {
		json_set(document, "locale", json_string(settings->locale));
	}
	json_set(document, "autoEquip", json_bool(settings->auto_equip));
	json_set(document, "battleSpeed", json_int(settings->battle_speed));
	write_json_atomic(path, document);
	json_free(document);
}

// ── save, history, profile ──────────────────────────────────────────────────

static void data_path(void *context, const char *name, char out[PATH_SIZE]) {
	path_join(out, (const char *)context, name);
}

static LoadResult load_save(void *context, SaveGame *out, char *error, size_t error_size) {
	char path[PATH_SIZE];
	data_path(context, "save.json", path);
	JsonValue *document;
	LoadResult result = read_json_object(path, &document, error, error_size);
	if (result != LOAD_OK) {
		return result;
	}
	// The version check comes first: a newer save must be refused, not "migrated".
	bool ok = check_schema(document, "save.json", error, error_size);
	if (ok) {
		migrate_save(document);
		ok = save_game_from_json(document, out, error, error_size);
	}
	json_free(document);
	return ok ? LOAD_OK : LOAD_FAILED;
}

static void write_save(void *context, const SaveGame *save) {
	char path[PATH_SIZE];
	data_path(context, "save.json", path);
	JsonValue *document = save_game_to_json(save);
	write_json_atomic(path, document);
	json_free(document);
}

static void delete_save(void *context) {
	char path[PATH_SIZE];
	data_path(context, "save.json", path);
	fs_remove_file(path);
}

static void add_history(void *context, const RunRecord *record) {
	char directory[PATH_SIZE];
	char name[RUN_ID_SIZE + 8];
	char path[PATH_SIZE];
	data_path(context, "history", directory);
	snprintf(name, sizeof(name), "%s.json", record->run_id);
	path_join(path, directory, name);
	JsonValue *document = run_record_to_json(record);
	write_json_atomic(path, document);
	json_free(document);
}

static LoadResult load_profile(void *context, Profile *out, char *error, size_t error_size) {
	char path[PATH_SIZE];
	data_path(context, "profile.json", path);
	memset(out, 0, sizeof(*out));
	JsonValue *document;
	LoadResult result = read_json_object(path, &document, error, error_size);
	if (result != LOAD_OK) {
		return result == LOAD_MISSING ? LOAD_OK : result;
	}
	bool ok = check_schema(document, "profile.json", error, error_size);
	if (ok) {
		migrate_profile(document);
		ok = profile_from_json(document, out, error, error_size);
	}
	json_free(document);
	return ok ? LOAD_OK : LOAD_FAILED;
}

static void save_profile(void *context, const Profile *profile) {
	char path[PATH_SIZE];
	data_path(context, "profile.json", path);
	JsonValue *document = profile_to_json(profile);
	write_json_atomic(path, document);
	json_free(document);
}

Repositories file_repositories(const char *data_dir) {
	Repositories repositories = {
	    // The functions only read through the pointer; the cast drops `const` to fit the generic context.
	    .context = (void *)(uintptr_t)data_dir,
	    .load_save = load_save,
	    .write_save = write_save,
	    .delete_save = delete_save,
	    .add_history = add_history,
	    .load_profile = load_profile,
	    .save_profile = save_profile,
	};
	return repositories;
}

LoadResult history_list(const char *data_dir, RunRecord **out, size_t *count, char *error, size_t error_size) {
	char directory[PATH_SIZE];
	path_join(directory, data_dir, "history");
	*out = NULL;
	*count = 0;
	size_t name_count = 0;
	char **names = fs_list(directory, ".json", &name_count);
	RunRecord *records = xcalloc(name_count, sizeof(RunRecord));
	LoadResult result = LOAD_OK;
	for (size_t i = 0; i < name_count && result == LOAD_OK; i++) {
		char path[PATH_SIZE];
		path_join(path, directory, names[i]);
		JsonValue *document;
		result = read_json_object(path, &document, error, error_size);
		if (result != LOAD_OK) {
			result = LOAD_FAILED;
			break;
		}
		bool ok = check_schema(document, "history record", error, error_size);
		if (ok) {
			migrate_history(document);
			ok = run_record_from_json(document, &records[*count], error, error_size);
		}
		json_free(document);
		if (ok) {
			(*count)++;
		} else {
			result = LOAD_FAILED;
		}
	}
	fs_free_names(names, name_count);
	if (result != LOAD_OK) {
		for (size_t i = 0; i < *count; i++) {
			run_record_free(&records[i]);
		}
		free(records);
		*count = 0;
		return result;
	}
	*out = records;
	return LOAD_OK;
}
