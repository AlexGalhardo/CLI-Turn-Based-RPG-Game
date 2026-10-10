// A small JSON document model with a parser and a writer (the port has no third-party libraries).
//
// It covers what the game needs: objects keep their key order (saves are written in the reference's order), numbers
// are 64-bit integers (the data files and saves hold no fractions) and strings are UTF-8.
// Reading untrusted documents (data files, saves) goes through the json_read_* helpers and a sticky JsonError: the
// first problem is recorded, later reads return a harmless default, and the caller checks `failed` once at the end.
#ifndef RPG_DOMAIN_JSON_TYPES_H
#define RPG_DOMAIN_JSON_TYPES_H

#include "domain/base.h"

typedef enum { JSON_NULL, JSON_BOOL, JSON_INT, JSON_STRING, JSON_ARRAY, JSON_OBJECT } JsonType;

typedef struct JsonValue JsonValue;
struct JsonValue {
	JsonType type;
	bool boolean;
	int64_t integer;
	char *string;
	// Arrays use `items`; objects use `keys[i]` → `items[i]`.
	JsonValue **items;
	char **keys;
	size_t count;
	size_t capacity;
};

typedef struct {
	bool failed;
	char message[200];
} JsonError;

void json_error_set(JsonError *error, const char *format, ...) RPG_PRINTF(2, 3);

// ── building ────────────────────────────────────────────────────────────────
JsonValue *json_null(void);
JsonValue *json_bool(bool value);
JsonValue *json_int(int64_t value);
JsonValue *json_string(const char *value);
JsonValue *json_array(void);
JsonValue *json_object(void);
// Both take ownership of `value`. json_set replaces an existing key in place.
void json_push(JsonValue *array, JsonValue *value);
void json_set(JsonValue *object, const char *key, JsonValue *value);
void json_remove(JsonValue *object, const char *key);
void json_sort_keys(JsonValue *object);
void json_free(JsonValue *value);

// ── text ────────────────────────────────────────────────────────────────────
// Returns NULL and fills `error` when the text is not valid JSON.
JsonValue *json_parse(const char *text, JsonError *error);
// `pretty` writes one member per line with tab indentation, like Python's json.dumps(indent="\t"). Caller frees.
char *json_dump(const JsonValue *value, bool pretty);

// ── reading ─────────────────────────────────────────────────────────────────
// NULL when `value` is not an object or has no such key.
JsonValue *json_get(const JsonValue *object, const char *key);
bool json_has(const JsonValue *object, const char *key);
bool json_is_null(const JsonValue *value);
// Structural equality; object key order is ignored.
bool json_equal(const JsonValue *a, const JsonValue *b);

int64_t json_as_int(const JsonValue *value, JsonError *error);
const char *json_as_str(const JsonValue *value, JsonError *error);
bool json_as_bool(const JsonValue *value, JsonError *error);

int64_t json_read_int(const JsonValue *object, const char *key, JsonError *error);
const char *json_read_str(const JsonValue *object, const char *key, JsonError *error);
bool json_read_bool(const JsonValue *object, const char *key, JsonError *error);
// Always return a usable value: a shared empty array/object after a failure.
const JsonValue *json_read_array(const JsonValue *object, const char *key, JsonError *error);
const JsonValue *json_read_object(const JsonValue *object, const char *key, JsonError *error);
// Copies a string member into a fixed buffer, failing when it does not fit.
void json_read_text(const JsonValue *object, const char *key, char *destination, size_t size, JsonError *error);
void json_copy_text(const JsonValue *value, char *destination, size_t size, JsonError *error);

#endif
