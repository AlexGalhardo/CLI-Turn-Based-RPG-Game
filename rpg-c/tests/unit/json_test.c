// The in-tree JSON module: saves must be written exactly like the reference's
// json.dumps(document, indent="\t", ensure_ascii=False).
#include "domain/json_types.h"
#include "support/helpers.h"

#include <stdlib.h>

#define SUITE "unit/json"

static JsonValue *parse(const char *text) {
	JsonError error = {0};
	JsonValue *value = json_parse(text, &error);
	if (value == NULL) {
		test_fail(__FILE__, __LINE__, "%s: %s", error.message, text);
	}
	return value;
}

static void check_dump(const JsonValue *value, bool pretty, const char *expected) {
	char *text = json_dump(value, pretty);
	test_check_str(__FILE__, __LINE__, "json_dump", text, expected);
	free(text);
}

TEST(SUITE, compact_round_trip_keeps_the_key_order) {
	const char *text = "{\"b\": 1, \"a\": [true, false, null, -5, \"x\"], \"c\": {}, \"d\": []}";
	JsonValue *document = parse(text);
	REQUIRE(document != NULL);
	REQUIRE_INT(document->type, JSON_OBJECT);
	REQUIRE_INT(document->count, 4);
	CHECK_STR(document->keys[0], "b");
	CHECK_STR(document->keys[1], "a");
	check_dump(document, false, text);
	json_free(document);
}

TEST(SUITE, pretty_output_uses_tabs_like_the_reference) {
	JsonValue *document = parse("{\"b\":1,\"a\":[true,false,null,-5,\"x\"],\"c\":{},\"d\":[],\"e\":{\"f\":[[]]}}");
	REQUIRE(document != NULL);
	check_dump(document, true,
	    "{\n"
	    "\t\"b\": 1,\n"
	    "\t\"a\": [\n"
	    "\t\ttrue,\n"
	    "\t\tfalse,\n"
	    "\t\tnull,\n"
	    "\t\t-5,\n"
	    "\t\t\"x\"\n"
	    "\t],\n"
	    "\t\"c\": {},\n"
	    "\t\"d\": [],\n"
	    "\t\"e\": {\n"
	    "\t\t\"f\": [\n"
	    "\t\t\t[]\n"
	    "\t\t]\n"
	    "\t}\n"
	    "}");
	json_free(document);

	JsonValue *scalar = json_int(7);
	check_dump(scalar, true, "7");
	json_free(scalar);
	JsonValue *empty = json_object();
	check_dump(empty, true, "{}");
	json_free(empty);
}

// Files written by the reference parse and print back byte for byte.
TEST(SUITE, pretty_output_reproduces_python_files) {
	const char *const files[] = {"python_save.json", "python_profile.json", "python_save_v1_migrated.json"};
	for (size_t i = 0; i < ARRAY_LEN(files); i++) {
		char *text = read_file(path_in(RPG_FIXTURES_DIR, files[i]));
		REQUIRE(text != NULL);
		JsonValue *document = parse(text);
		if (document != NULL) {
			char *dumped = json_dump(document, true);
			StrBuf written = {0};
			sb_append(&written, dumped);
			sb_append_char(&written, '\n');
			if (strcmp(written.data, text) != 0) {
				test_fail(__FILE__, __LINE__, "%s is not reproduced byte for byte", files[i]);
			}
			sb_free(&written);
			free(dumped);
			json_free(document);
		}
		free(text);
	}
}

TEST(SUITE, string_escapes) {
	JsonValue *value = parse("\"\\\"\\\\\\/\\b\\f\\n\\r\\t\\u00e9\\u20AC\\ud83d\\ude00\\u0001\\u007f\"");
	REQUIRE(value != NULL);
	REQUIRE_INT(value->type, JSON_STRING);
	// " \ / BS FF LF CR TAB, é (2 bytes), € (3 bytes), U+1F600 from a surrogate pair (4 bytes), 0x01, 0x7F.
	CHECK_STR(value->string, "\"\\/\b\f\n\r\t\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\x01\x7F");
	// Like ensure_ascii=False: only quotes, backslashes and control characters are escaped.
	check_dump(value, false, "\"\\\"\\\\/\\b\\f\\n\\r\\t\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\\u0001\x7F\"");
	json_free(value);

	// Raw UTF-8 passes through untouched, in keys too.
	const char *text = "{\"n\xC3\xA3o\": \"\xF0\x9F\x98\x80\"}";
	JsonValue *document = parse(text);
	REQUIRE(document != NULL);
	check_dump(document, false, text);
	json_free(document);

	JsonValue *controls = json_string("\x1F\x02");
	check_dump(controls, false, "\"\\u001f\\u0002\"");
	json_free(controls);
}

TEST(SUITE, whitespace_and_byte_order_mark) {
	JsonValue *document = parse("\xEF\xBB\xBF \r\n\t{ \"a\" :\n[ 1 ,2 ]\t} \n");
	REQUIRE(document != NULL);
	check_dump(document, false, "{\"a\": [1, 2]}");
	json_free(document);
}

TEST(SUITE, integers_only) {
	JsonValue *document = parse("[0, -1, 9007199254740993, 9223372036854775807, -9223372036854775807]");
	REQUIRE(document != NULL);
	REQUIRE_INT(document->count, 5);
	JsonError error = {0};
	CHECK_INT(json_as_int(document->items[0], &error), 0);
	CHECK_INT(json_as_int(document->items[1], &error), -1);
	// Beyond the 53 bits a double can hold exactly.
	CHECK_INT(json_as_int(document->items[2], &error), INT64_C(9007199254740993));
	CHECK_INT(json_as_int(document->items[3], &error), INT64_MAX);
	CHECK(!error.failed);
	check_dump(document, false, "[0, -1, 9007199254740993, 9223372036854775807, -9223372036854775807]");
	json_free(document);

	const char *const fractions[] = {"1.5", "[1e3]", "{\"a\": 2E2}", "-0.1"};
	for (size_t i = 0; i < ARRAY_LEN(fractions); i++) {
		error = (JsonError){0};
		CHECK(json_parse(fractions[i], &error) == NULL);
		CHECK(error.failed);
		CHECK_CONTAINS(error.message, "only integers");
	}
}

TEST(SUITE, invalid_documents_are_rejected) {
	const char *const broken[] = {
	    "",
	    "   ",
	    "{",
	    "[",
	    "{\"a\": 1",
	    "{\"a\": ",
	    "{\"a\"",
	    "{\"a",
	    "[1, 2",
	    "[1,",
	    "\"abc",
	    "\"abc\\",
	    "\"\\u12",
	    "tru",
	    "nul",
	    "-",
	    "{ not json",
	    "{\"a\": 1,}",
	    "[1 2]",
	    "{\"a\" 1}",
	    "{1: 2}",
	    "{} x",
	    "[] []",
	    "\"\\x\"",
	    "\"\\u12g4\"",
	    "\"line\nbreak\"",
	    "'single'",
	};
	for (size_t i = 0; i < ARRAY_LEN(broken); i++) {
		JsonError error = {0};
		JsonValue *value = json_parse(broken[i], &error);
		if (value != NULL || !error.failed || !str_starts_with(error.message, "invalid JSON at byte ")) {
			test_fail(__FILE__, __LINE__, "accepted or misreported: %s (%s)", broken[i], error.message);
		}
		json_free(value);
	}

	// 65 nested arrays are one too many.
	StrBuf deep = {0};
	for (int i = 0; i < 66; i++) {
		sb_append_char(&deep, '[');
	}
	JsonError error = {0};
	CHECK(json_parse(deep.data, &error) == NULL);
	CHECK_CONTAINS(error.message, "nesting too deep");
	sb_free(&deep);
}

TEST(SUITE, building_and_editing) {
	JsonValue *object = json_object();
	json_set(object, "z", json_int(1));
	json_set(object, "a", json_string("x"));
	json_set(object, "m", json_bool(true));
	// Replacing keeps the position of the key.
	json_set(object, "z", json_null());
	check_dump(object, false, "{\"z\": null, \"a\": \"x\", \"m\": true}");
	CHECK(json_has(object, "a") && !json_has(object, "b"));
	CHECK(json_is_null(json_get(object, "z")));
	CHECK(json_is_null(json_get(object, "missing")));
	CHECK(!json_is_null(json_get(object, "a")));

	json_remove(object, "a");
	json_remove(object, "missing");
	check_dump(object, false, "{\"z\": null, \"m\": true}");
	json_sort_keys(object);
	check_dump(object, false, "{\"m\": true, \"z\": null}");

	JsonValue *array = json_array();
	for (int i = 0; i < 20; i++) {
		json_push(array, json_int(i));
	}
	CHECK_INT(array->count, 20);
	CHECK_INT(array->items[19]->integer, 19);
	json_set(object, "list", array);
	CHECK(json_get(array, "key") == NULL);
	CHECK(json_get(NULL, "key") == NULL);
	json_free(object);
	json_free(NULL);
}

TEST(SUITE, equality_ignores_the_key_order) {
	JsonValue *left = parse("{\"a\": 1, \"b\": [1, {\"c\": null, \"d\": \"x\"}]}");
	JsonValue *same = parse("{\"b\": [1, {\"d\": \"x\", \"c\": null}], \"a\": 1}");
	const char *const others[] = {
	    "{\"a\": 1, \"b\": [{\"c\": null, \"d\": \"x\"}, 1]}",
	    "{\"a\": 1, \"b\": [1, {\"c\": null, \"d\": \"y\"}]}",
	    "{\"a\": 1, \"b\": [1, {\"c\": null}]}",
	    "{\"a\": true, \"b\": [1, {\"c\": null, \"d\": \"x\"}]}",
	    "{\"a\": 1, \"x\": [1, {\"c\": null, \"d\": \"x\"}]}",
	    "[]",
	};
	REQUIRE(left != NULL && same != NULL);
	CHECK(json_equal(left, same));
	CHECK(json_equal(same, left));
	for (size_t i = 0; i < ARRAY_LEN(others); i++) {
		JsonValue *other = parse(others[i]);
		CHECK(other != NULL && !json_equal(left, other) && !json_equal(other, left));
		json_free(other);
	}
	json_free(left);
	json_free(same);
}

TEST(SUITE, typed_readers_and_the_sticky_error) {
	JsonValue *document = parse("{\"n\": 5, \"s\": \"text\", \"t\": true, \"l\": [1], \"o\": {\"k\": 2}, \"z\": null}");
	REQUIRE(document != NULL);
	JsonError error = {0};
	CHECK_INT(json_read_int(document, "n", &error), 5);
	CHECK_STR(json_read_str(document, "s", &error), "text");
	CHECK(json_read_bool(document, "t", &error));
	CHECK_INT(json_read_array(document, "l", &error)->count, 1);
	CHECK_INT(json_read_int(json_read_object(document, "o", &error), "k", &error), 2);
	char small[5];
	json_read_text(document, "s", small, sizeof(small), &error);
	CHECK_STR(small, "text");
	CHECK(!error.failed);

	// The first problem is kept; later reads return harmless defaults.
	CHECK_INT(json_read_int(document, "missing", &error), 0);
	CHECK(error.failed);
	CHECK_STR(error.message, "missing key 'missing'");
	CHECK_STR(json_read_str(document, "n", &error), "");
	CHECK(!json_read_bool(document, "s", &error));
	CHECK_INT(json_read_array(document, "o", &error)->count, 0);
	CHECK_INT(json_read_object(document, "l", &error)->count, 0);
	CHECK_STR(error.message, "missing key 'missing'");

	error = (JsonError){0};
	json_read_int(document, "s", &error);
	CHECK_STR(error.message, "'s': expected int, got str");

	error = (JsonError){0};
	json_read_int(document, "z", &error);
	CHECK_STR(error.message, "'z': expected int, got null");

	error = (JsonError){0};
	json_read_int(json_get(document, "l"), "n", &error);
	CHECK_STR(error.message, "expected object, got list");

	error = (JsonError){0};
	char tiny[4];
	json_read_text(document, "s", tiny, sizeof(tiny), &error);
	CHECK(error.failed);
	CHECK_STR(tiny, "");

	error = (JsonError){0};
	json_copy_text(json_get(document, "s"), tiny, sizeof(tiny), &error);
	CHECK(error.failed);
	CHECK_STR(tiny, "");
	json_free(document);
}
