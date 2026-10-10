#include "domain/rng.h"
#include "support/helpers.h"

#include <stdio.h>

#define SUITE "unit/rng"

TEST(SUITE, matches_reference_vectors) {
	JsonValue *document = load_json_file(RPG_GOLDEN_DIR "/prng.json");
	REQUIRE(document != NULL);
	JsonError error = {0};
	const JsonValue *vectors = json_read_array(document, "vectors", &error);
	CHECK(vectors->count >= 5);
	for (size_t i = 0; i < vectors->count; i++) {
		Rng rng = rng_new(json_read_int(vectors->items[i], "seed", &error));
		const JsonValue *outputs = json_read_array(vectors->items[i], "outputs", &error);
		for (size_t j = 0; j < outputs->count; j++) {
			CHECK_INT(rng_next_u32(&rng), json_as_int(outputs->items[j], &error));
		}
	}
	CHECK(!error.failed);
	json_free(document);
}

TEST(SUITE, seed_is_reduced_modulo_2_32) {
	Rng big = rng_new(4294967296 + 42);
	Rng small = rng_new(42);
	CHECK_INT(rng_next_u32(&big), rng_next_u32(&small));
}

TEST(SUITE, state_round_trip_continues_sequence) {
	Rng rng = rng_new(7);
	rng_next_u32(&rng);
	Rng clone = {.state = rng.state};
	for (int i = 0; i < 5; i++) {
		CHECK_INT(rng_next_u32(&clone), rng_next_u32(&rng));
	}
}

TEST(SUITE, roll_is_inclusive_and_bounded) {
	Rng rng = rng_new(1);
	bool seen[6] = {false};
	for (int i = 0; i < 500; i++) {
		int64_t value = rng_roll(&rng, 3, 5);
		REQUIRE(value >= 3 && value <= 5);
		seen[value] = true;
	}
	CHECK(seen[3] && seen[4] && seen[5]);
}

TEST(SUITE, certain_chances_do_not_consume) {
	const int64_t percents[] = {0, -5, 100, 150};
	const bool expected[] = {false, false, true, true};
	for (size_t i = 0; i < 4; i++) {
		Rng rng = rng_new(9);
		uint32_t state = rng.state;
		CHECK(rng_chance(&rng, percents[i]) == expected[i]);
		CHECK_INT(rng.state, state);
	}
}

TEST(SUITE, chance_consumes_one_number) {
	Rng rng = rng_new(9);
	Rng reference = rng_new(9);
	bool result = rng_chance(&rng, 50);
	CHECK(result == (rng_roll(&reference, 1, 100) <= 50));
	CHECK_INT(rng.state, reference.state);
}

TEST(SUITE, weighted_respects_zero_weights) {
	Rng rng = rng_new(3);
	const int64_t weights[] = {0, 5, 0};
	for (int i = 0; i < 50; i++) {
		CHECK_INT(rng_weighted(&rng, weights, 3), 1);
	}
}

TEST(SUITE, pick_single_element) {
	Rng rng = rng_new(5);
	CHECK_INT(rng_pick(&rng, 1), 0);
}
