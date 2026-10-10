// A tiny test harness (the port has no third-party libraries).
//
//   TEST("unit/rng", first_outputs_match) { CHECK_INT(rng_next_u32(&rng), 123); }
//
// TEST registers the function before main() runs; `rpg_tests <filter>` runs every test whose "suite/name" contains
// the filter, and ctest runs one entry per level ("unit/", "integration/", "golden/", "e2e/").
// CHECK* record a failure and keep going; REQUIRE* also leave the test function (use them before dereferencing).
#ifndef RPG_TESTS_SUPPORT_TEST_H
#define RPG_TESTS_SUPPORT_TEST_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*TestFunction)(void);

void test_register(const char *suite, const char *name, TestFunction function);
void test_fail(const char *file, int line, const char *format, ...) __attribute__((format(printf, 3, 4)));
bool test_check_int(const char *file, int line, const char *expression, int64_t actual, int64_t expected);
bool test_check_str(const char *file, int line, const char *expression, const char *actual, const char *expected);
// True when `text` contains `part`; records a failure otherwise.
bool test_check_contains(const char *file, int line, const char *text, const char *part);

#define TEST(suite, name)                                                                                              \
	static void test_##name(void);                                                                                     \
	__attribute__((constructor)) static void register_##name(void) { test_register(suite, #name, test_##name); }       \
	static void test_##name(void)

#define CHECK(condition)                                                                                               \
	do {                                                                                                               \
		if (!(condition)) {                                                                                            \
			test_fail(__FILE__, __LINE__, "CHECK(%s)", #condition);                                                    \
		}                                                                                                              \
	} while (0)

#define REQUIRE(condition)                                                                                             \
	do {                                                                                                               \
		if (!(condition)) {                                                                                            \
			test_fail(__FILE__, __LINE__, "REQUIRE(%s)", #condition);                                                  \
			return;                                                                                                    \
		}                                                                                                              \
	} while (0)

#define CHECK_INT(actual, expected) test_check_int(__FILE__, __LINE__, #actual, (int64_t)(actual), (int64_t)(expected))
#define CHECK_STR(actual, expected) test_check_str(__FILE__, __LINE__, #actual, (actual), (expected))
#define CHECK_CONTAINS(text, part) test_check_contains(__FILE__, __LINE__, (text), (part))
#define CHECK_NOT_CONTAINS(text, part)                                                                                 \
	do {                                                                                                               \
		if (strstr((text), (part)) != NULL) {                                                                          \
			test_fail(__FILE__, __LINE__, "unexpected \"%s\" in:\n%s", (part), (text));                                \
		}                                                                                                              \
	} while (0)

#define REQUIRE_INT(actual, expected)                                                                                  \
	do {                                                                                                               \
		if (!CHECK_INT(actual, expected)) {                                                                            \
			return;                                                                                                    \
		}                                                                                                              \
	} while (0)

#endif
