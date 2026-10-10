#include "support/test.h"

#include "support/helpers.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	const char *suite;
	const char *name;
	TestFunction function;
} TestCase;

static TestCase *tests;
static int test_count;
static int test_capacity;
static int failures_in_current_test;

void test_register(const char *suite, const char *name, TestFunction function) {
	if (test_count == test_capacity) {
		test_capacity = test_capacity == 0 ? 128 : test_capacity * 2;
		tests = realloc(tests, (size_t)test_capacity * sizeof(TestCase));
		if (tests == NULL) {
			abort();
		}
	}
	tests[test_count++] = (TestCase){suite, name, function};
}

void test_fail(const char *file, int line, const char *format, ...) {
	va_list args;
	va_start(args, format);
	printf("    %s:%d: ", file, line);
	vprintf(format, args);
	printf("\n");
	va_end(args);
	failures_in_current_test++;
}

bool test_check_int(const char *file, int line, const char *expression, int64_t actual, int64_t expected) {
	if (actual == expected) {
		return true;
	}
	test_fail(file, line, "%s: expected %lld, got %lld", expression, (long long)expected, (long long)actual);
	return false;
}

bool test_check_str(const char *file, int line, const char *expression, const char *actual, const char *expected) {
	if (strcmp(actual, expected) == 0) {
		return true;
	}
	test_fail(file, line, "%s: expected \"%s\", got \"%s\"", expression, expected, actual);
	return false;
}

bool test_check_contains(const char *file, int line, const char *text, const char *part) {
	if (strstr(text, part) != NULL) {
		return true;
	}
	test_fail(file, line, "\"%s\" not found in:\n%s", part, text);
	return false;
}

static int compare_tests(const void *a, const void *b) {
	const TestCase *left = a;
	const TestCase *right = b;
	int order = strcmp(left->suite, right->suite);
	return order != 0 ? order : strcmp(left->name, right->name);
}

int main(int argc, char **argv) {
	const char *filter = argc > 1 ? argv[1] : "";
	setvbuf(stdout, NULL, _IONBF, 0);
	// Tests must never touch the real save directory, and never wait on animation timers.
	char data_dir[TEST_PATH_SIZE];
	temp_dir_create(data_dir);
	test_setenv("RPG_DATA_DIR", data_dir);
	test_setenv("RPG_NO_ANIM", "1");

	// Constructors run in link order; sorting makes the output stable.
	qsort(tests, (size_t)test_count, sizeof(TestCase), compare_tests);
	int ran = 0;
	int failed = 0;
	for (int i = 0; i < test_count; i++) {
		char full_name[256];
		snprintf(full_name, sizeof(full_name), "%s/%s", tests[i].suite, tests[i].name);
		if (strcmp(filter, "--list") == 0) {
			printf("%s\n", full_name);
			continue;
		}
		if (strstr(full_name, filter) == NULL) {
			continue;
		}
		failures_in_current_test = 0;
		tests[i].function();
		ran++;
		if (failures_in_current_test > 0) {
			failed++;
			printf("FAIL %s\n", full_name);
		}
	}
	temp_dir_remove(data_dir);
	if (strcmp(filter, "--list") == 0) {
		return 0;
	}
	printf("%d tests, %d failed\n", ran, failed);
	if (ran == 0) {
		printf("no test matches \"%s\"\n", filter);
		return 1;
	}
	return failed == 0 ? 0 : 1;
}
