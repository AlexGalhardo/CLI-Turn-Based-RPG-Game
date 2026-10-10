#include "domain/json_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void json_error_set(JsonError *error, const char *format, ...) {
	if (error == NULL || error->failed) {
		return;
	}
	va_list args;
	va_start(args, format);
	vsnprintf(error->message, sizeof(error->message), format, args);
	va_end(args);
	error->failed = true;
}

// ── building ────────────────────────────────────────────────────────────────

static JsonValue *json_new(JsonType type) {
	JsonValue *value = xcalloc(1, sizeof(JsonValue));
	value->type = type;
	return value;
}

JsonValue *json_null(void) { return json_new(JSON_NULL); }

JsonValue *json_bool(bool value) {
	JsonValue *result = json_new(JSON_BOOL);
	result->boolean = value;
	return result;
}

JsonValue *json_int(int64_t value) {
	JsonValue *result = json_new(JSON_INT);
	result->integer = value;
	return result;
}

JsonValue *json_string(const char *value) {
	JsonValue *result = json_new(JSON_STRING);
	result->string = xstrdup(value);
	return result;
}

JsonValue *json_array(void) { return json_new(JSON_ARRAY); }

JsonValue *json_object(void) { return json_new(JSON_OBJECT); }

static void json_grow(JsonValue *container) {
	if (container->count < container->capacity) {
		return;
	}
	container->capacity = container->capacity == 0 ? 8 : container->capacity * 2;
	container->items = xrealloc(container->items, container->capacity * sizeof(JsonValue *));
	if (container->type == JSON_OBJECT) {
		container->keys = xrealloc(container->keys, container->capacity * sizeof(char *));
	}
}

void json_push(JsonValue *array, JsonValue *value) {
	json_grow(array);
	array->items[array->count++] = value;
}

static size_t json_find(const JsonValue *object, const char *key) {
	for (size_t i = 0; i < object->count; i++) {
		if (strcmp(object->keys[i], key) == 0) {
			return i;
		}
	}
	return object->count;
}

void json_set(JsonValue *object, const char *key, JsonValue *value) {
	size_t index = json_find(object, key);
	if (index < object->count) {
		json_free(object->items[index]);
		object->items[index] = value;
		return;
	}
	json_grow(object);
	object->keys[object->count] = xstrdup(key);
	object->items[object->count] = value;
	object->count++;
}

void json_remove(JsonValue *object, const char *key) {
	size_t index = json_find(object, key);
	if (index == object->count) {
		return;
	}
	free(object->keys[index]);
	json_free(object->items[index]);
	for (size_t i = index + 1; i < object->count; i++) {
		object->keys[i - 1] = object->keys[i];
		object->items[i - 1] = object->items[i];
	}
	object->count--;
}

void json_sort_keys(JsonValue *object) {
	// Insertion sort: the objects sorted here have a handful of keys.
	for (size_t i = 1; i < object->count; i++) {
		char *key = object->keys[i];
		JsonValue *item = object->items[i];
		size_t j = i;
		while (j > 0 && strcmp(object->keys[j - 1], key) > 0) {
			object->keys[j] = object->keys[j - 1];
			object->items[j] = object->items[j - 1];
			j--;
		}
		object->keys[j] = key;
		object->items[j] = item;
	}
}

void json_free(JsonValue *value) {
	if (value == NULL) {
		return;
	}
	for (size_t i = 0; i < value->count; i++) {
		json_free(value->items[i]);
		if (value->type == JSON_OBJECT) {
			free(value->keys[i]);
		}
	}
	free(value->items);
	free(value->keys);
	free(value->string);
	free(value);
}

// ── parser (recursive descent) ──────────────────────────────────────────────

typedef struct {
	const char *start;
	const char *at;
	JsonError *error;
} Parser;

static JsonValue *parse_value(Parser *parser, int depth);

static void parse_fail(Parser *parser, const char *what) {
	json_error_set(parser->error, "invalid JSON at byte %zu: %s", (size_t)(parser->at - parser->start), what);
}

static void skip_whitespace(Parser *parser) {
	while (*parser->at == ' ' || *parser->at == '\t' || *parser->at == '\n' || *parser->at == '\r') {
		parser->at++;
	}
}

static bool parse_hex4(Parser *parser, unsigned *out) {
	unsigned value = 0;
	for (int i = 0; i < 4; i++) {
		char c = *parser->at;
		unsigned digit;
		if (c >= '0' && c <= '9') {
			digit = (unsigned)(c - '0');
		} else if (c >= 'a' && c <= 'f') {
			digit = (unsigned)(c - 'a') + 10;
		} else if (c >= 'A' && c <= 'F') {
			digit = (unsigned)(c - 'A') + 10;
		} else {
			return false;
		}
		value = value * 16 + digit;
		parser->at++;
	}
	*out = value;
	return true;
}

static void append_utf8(StrBuf *out, unsigned code_point) {
	if (code_point < 0x80) {
		sb_append_char(out, (char)code_point);
	} else if (code_point < 0x800) {
		sb_append_char(out, (char)(0xC0 | (code_point >> 6)));
		sb_append_char(out, (char)(0x80 | (code_point & 0x3F)));
	} else if (code_point < 0x10000) {
		sb_append_char(out, (char)(0xE0 | (code_point >> 12)));
		sb_append_char(out, (char)(0x80 | ((code_point >> 6) & 0x3F)));
		sb_append_char(out, (char)(0x80 | (code_point & 0x3F)));
	} else {
		sb_append_char(out, (char)(0xF0 | (code_point >> 18)));
		sb_append_char(out, (char)(0x80 | ((code_point >> 12) & 0x3F)));
		sb_append_char(out, (char)(0x80 | ((code_point >> 6) & 0x3F)));
		sb_append_char(out, (char)(0x80 | (code_point & 0x3F)));
	}
}

// Parses a quoted string and returns it as a heap string, or NULL on error.
static char *parse_string(Parser *parser) {
	if (*parser->at != '"') {
		parse_fail(parser, "expected a string");
		return NULL;
	}
	parser->at++;
	StrBuf out = {0};
	for (;;) {
		unsigned char c = (unsigned char)*parser->at;
		if (c == '"') {
			parser->at++;
			return sb_take(&out);
		}
		if (c < 0x20) {
			parse_fail(parser, "unterminated string");
			break;
		}
		parser->at++;
		if (c != '\\') {
			sb_append_char(&out, (char)c);
			continue;
		}
		char escape = *parser->at++;
		if (escape == 'u') {
			unsigned code_point;
			if (!parse_hex4(parser, &code_point)) {
				parse_fail(parser, "invalid \\u escape");
				break;
			}
			// A high surrogate must be followed by a low one: together they encode one code point.
			if (code_point >= 0xD800 && code_point <= 0xDBFF && parser->at[0] == '\\' && parser->at[1] == 'u') {
				unsigned low;
				parser->at += 2;
				if (!parse_hex4(parser, &low) || low < 0xDC00 || low > 0xDFFF) {
					parse_fail(parser, "invalid surrogate pair");
					break;
				}
				code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
			}
			append_utf8(&out, code_point);
			continue;
		}
		const char *escapes = "\"\\/bfnrt";
		const char *values = "\"\\/\b\f\n\r\t";
		const char *found = escape == '\0' ? NULL : strchr(escapes, escape);
		if (found == NULL) {
			parse_fail(parser, "invalid escape");
			break;
		}
		sb_append_char(&out, values[found - escapes]);
	}
	sb_free(&out);
	return NULL;
}

static JsonValue *parse_number(Parser *parser) {
	char *end = NULL;
	long long value = strtoll(parser->at, &end, 10);
	if (end == parser->at) {
		parse_fail(parser, "expected a value");
		return NULL;
	}
	if (*end == '.' || *end == 'e' || *end == 'E') {
		parser->at = end;
		parse_fail(parser, "only integers are supported");
		return NULL;
	}
	parser->at = end;
	return json_int(value);
}

static bool parse_literal(Parser *parser, const char *literal) {
	size_t length = strlen(literal);
	if (strncmp(parser->at, literal, length) != 0) {
		return false;
	}
	parser->at += length;
	return true;
}

static JsonValue *parse_container(Parser *parser, int depth) {
	bool is_object = *parser->at == '{';
	char close = is_object ? '}' : ']';
	JsonValue *container = is_object ? json_object() : json_array();
	parser->at++;
	skip_whitespace(parser);
	if (*parser->at == close) {
		parser->at++;
		return container;
	}
	for (;;) {
		skip_whitespace(parser);
		char *key = NULL;
		if (is_object) {
			key = parse_string(parser);
			if (key == NULL) {
				break;
			}
			skip_whitespace(parser);
			if (*parser->at != ':') {
				free(key);
				parse_fail(parser, "expected ':'");
				break;
			}
			parser->at++;
		}
		JsonValue *item = parse_value(parser, depth + 1);
		if (item == NULL) {
			free(key);
			break;
		}
		if (is_object) {
			json_set(container, key, item);
			free(key);
		} else {
			json_push(container, item);
		}
		skip_whitespace(parser);
		if (*parser->at == ',') {
			parser->at++;
			continue;
		}
		if (*parser->at == close) {
			parser->at++;
			return container;
		}
		parse_fail(parser, "expected ',' or the end of the container");
		break;
	}
	json_free(container);
	return NULL;
}

static JsonValue *parse_value(Parser *parser, int depth) {
	if (depth > 64) {
		parse_fail(parser, "nesting too deep");
		return NULL;
	}
	skip_whitespace(parser);
	char c = *parser->at;
	if (c == '{' || c == '[') {
		return parse_container(parser, depth);
	}
	if (c == '"') {
		char *text = parse_string(parser);
		if (text == NULL) {
			return NULL;
		}
		JsonValue *value = json_new(JSON_STRING);
		value->string = text;
		return value;
	}
	if (parse_literal(parser, "true")) {
		return json_bool(true);
	}
	if (parse_literal(parser, "false")) {
		return json_bool(false);
	}
	if (parse_literal(parser, "null")) {
		return json_null();
	}
	return parse_number(parser);
}

JsonValue *json_parse(const char *text, JsonError *error) {
	Parser parser = {.start = text, .at = text, .error = error};
	// A UTF-8 byte order mark is not JSON, but editors on Windows add it.
	if (strncmp(parser.at, "\xEF\xBB\xBF", 3) == 0) {
		parser.at += 3;
	}
	JsonValue *value = parse_value(&parser, 0);
	if (value == NULL) {
		return NULL;
	}
	skip_whitespace(&parser);
	if (*parser.at != '\0') {
		parse_fail(&parser, "unexpected text after the document");
		json_free(value);
		return NULL;
	}
	return value;
}

// ── writer ──────────────────────────────────────────────────────────────────

static void dump_string(StrBuf *out, const char *text) {
	sb_append_char(out, '"');
	for (const unsigned char *c = (const unsigned char *)text; *c != '\0'; c++) {
		switch (*c) {
		case '"':
			sb_append(out, "\\\"");
			break;
		case '\\':
			sb_append(out, "\\\\");
			break;
		case '\n':
			sb_append(out, "\\n");
			break;
		case '\r':
			sb_append(out, "\\r");
			break;
		case '\t':
			sb_append(out, "\\t");
			break;
		case '\b':
			sb_append(out, "\\b");
			break;
		case '\f':
			sb_append(out, "\\f");
			break;
		default:
			if (*c < 0x20) {
				sb_appendf(out, "\\u%04x", (unsigned)*c);
			} else {
				sb_append_char(out, (char)*c);
			}
		}
	}
	sb_append_char(out, '"');
}

static void dump_indent(StrBuf *out, bool pretty, int depth) {
	if (!pretty) {
		return;
	}
	sb_append_char(out, '\n');
	for (int i = 0; i < depth; i++) {
		sb_append_char(out, '\t');
	}
}

static void dump_value(StrBuf *out, const JsonValue *value, bool pretty, int depth) {
	switch (value->type) {
	case JSON_NULL:
		sb_append(out, "null");
		return;
	case JSON_BOOL:
		sb_append(out, value->boolean ? "true" : "false");
		return;
	case JSON_INT:
		sb_appendf(out, "%lld", (long long)value->integer);
		return;
	case JSON_STRING:
		dump_string(out, value->string);
		return;
	case JSON_ARRAY:
	case JSON_OBJECT:
		break;
	}
	bool is_object = value->type == JSON_OBJECT;
	sb_append_char(out, is_object ? '{' : '[');
	for (size_t i = 0; i < value->count; i++) {
		if (i > 0) {
			sb_append(out, pretty ? "," : ", ");
		}
		dump_indent(out, pretty, depth + 1);
		if (is_object) {
			dump_string(out, value->keys[i]);
			sb_append(out, ": ");
		}
		dump_value(out, value->items[i], pretty, depth + 1);
	}
	if (value->count > 0) {
		dump_indent(out, pretty, depth);
	}
	sb_append_char(out, is_object ? '}' : ']');
}

char *json_dump(const JsonValue *value, bool pretty) {
	StrBuf out = {0};
	dump_value(&out, value, pretty, 0);
	return sb_take(&out);
}

// ── reading ─────────────────────────────────────────────────────────────────

static const JsonValue EMPTY_ARRAY = {.type = JSON_ARRAY};
static const JsonValue EMPTY_OBJECT = {.type = JSON_OBJECT};

JsonValue *json_get(const JsonValue *object, const char *key) {
	if (object == NULL || object->type != JSON_OBJECT) {
		return NULL;
	}
	size_t index = json_find(object, key);
	return index < object->count ? object->items[index] : NULL;
}

bool json_has(const JsonValue *object, const char *key) { return json_get(object, key) != NULL; }

bool json_is_null(const JsonValue *value) { return value == NULL || value->type == JSON_NULL; }

bool json_equal(const JsonValue *a, const JsonValue *b) {
	if (a->type != b->type) {
		return false;
	}
	switch (a->type) {
	case JSON_NULL:
		return true;
	case JSON_BOOL:
		return a->boolean == b->boolean;
	case JSON_INT:
		return a->integer == b->integer;
	case JSON_STRING:
		return strcmp(a->string, b->string) == 0;
	case JSON_ARRAY:
		if (a->count != b->count) {
			return false;
		}
		for (size_t i = 0; i < a->count; i++) {
			if (!json_equal(a->items[i], b->items[i])) {
				return false;
			}
		}
		return true;
	case JSON_OBJECT:
		if (a->count != b->count) {
			return false;
		}
		for (size_t i = 0; i < a->count; i++) {
			const JsonValue *other = json_get(b, a->keys[i]);
			if (other == NULL || !json_equal(a->items[i], other)) {
				return false;
			}
		}
		return true;
	}
	return false;
}

static const char *type_name(const JsonValue *value) {
	static const char *const names[] = {"null", "bool", "int", "str", "list", "object"};
	return value == NULL ? "nothing" : names[value->type];
}

int64_t json_as_int(const JsonValue *value, JsonError *error) {
	if (value == NULL || value->type != JSON_INT) {
		json_error_set(error, "expected int, got %s", type_name(value));
		return 0;
	}
	return value->integer;
}

const char *json_as_str(const JsonValue *value, JsonError *error) {
	if (value == NULL || value->type != JSON_STRING) {
		json_error_set(error, "expected str, got %s", type_name(value));
		return "";
	}
	return value->string;
}

bool json_as_bool(const JsonValue *value, JsonError *error) {
	if (value == NULL || value->type != JSON_BOOL) {
		json_error_set(error, "expected bool, got %s", type_name(value));
		return false;
	}
	return value->boolean;
}

static const JsonValue *member(const JsonValue *object, const char *key, JsonType type, JsonError *error) {
	if (object == NULL || object->type != JSON_OBJECT) {
		json_error_set(error, "expected object, got %s", type_name(object));
		return NULL;
	}
	const JsonValue *value = json_get(object, key);
	if (value == NULL) {
		json_error_set(error, "missing key '%s'", key);
		return NULL;
	}
	if (value->type != type) {
		static const char *const names[] = {"null", "bool", "int", "str", "list", "object"};
		json_error_set(error, "'%s': expected %s, got %s", key, names[type], type_name(value));
		return NULL;
	}
	return value;
}

int64_t json_read_int(const JsonValue *object, const char *key, JsonError *error) {
	const JsonValue *value = member(object, key, JSON_INT, error);
	return value == NULL ? 0 : value->integer;
}

const char *json_read_str(const JsonValue *object, const char *key, JsonError *error) {
	const JsonValue *value = member(object, key, JSON_STRING, error);
	return value == NULL ? "" : value->string;
}

bool json_read_bool(const JsonValue *object, const char *key, JsonError *error) {
	const JsonValue *value = member(object, key, JSON_BOOL, error);
	return value != NULL && value->boolean;
}

const JsonValue *json_read_array(const JsonValue *object, const char *key, JsonError *error) {
	const JsonValue *value = member(object, key, JSON_ARRAY, error);
	return value == NULL ? &EMPTY_ARRAY : value;
}

const JsonValue *json_read_object(const JsonValue *object, const char *key, JsonError *error) {
	const JsonValue *value = member(object, key, JSON_OBJECT, error);
	return value == NULL ? &EMPTY_OBJECT : value;
}

void json_copy_text(const JsonValue *value, char *destination, size_t size, JsonError *error) {
	const char *text = json_as_str(value, error);
	if (strlen(text) >= size) {
		json_error_set(error, "text too long: %.40s", text);
		text = "";
	}
	memcpy(destination, text, strlen(text) + 1);
}

void json_read_text(const JsonValue *object, const char *key, char *destination, size_t size, JsonError *error) {
	const char *text = json_read_str(object, key, error);
	if (strlen(text) >= size) {
		json_error_set(error, "'%s': text too long", key);
		text = "";
	}
	memcpy(destination, text, strlen(text) + 1);
}
