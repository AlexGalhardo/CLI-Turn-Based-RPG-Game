#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "domain/rng.hpp"

using Catch::Matchers::ContainsSubstring;
using rpg::domain::Rng;

TEST_CASE("the seed is reduced modulo 2^32", "[unit][rng]") {
	REQUIRE(Rng((1ULL << 32U) + 42U).next_u32() == Rng(42).next_u32());
}

TEST_CASE("a restored state continues the sequence", "[unit][rng]") {
	Rng rng(7);
	rng.next_u32();
	Rng clone(rng.state());
	for (int i = 0; i < 5; ++i) {
		REQUIRE(clone.next_u32() == rng.next_u32());
	}
}

TEST_CASE("roll is inclusive and bounded", "[unit][rng]") {
	Rng rng(1);
	std::set<std::int64_t> values;
	for (int i = 0; i < 500; ++i) {
		values.insert(rng.roll(3, 5));
	}
	REQUIRE(values == std::set<std::int64_t>{3, 4, 5});
}

TEST_CASE("roll rejects an inverted range", "[unit][rng]") {
	REQUIRE_THROWS_WITH(Rng(1).roll(5, 4), ContainsSubstring("invalid range"));
}

TEST_CASE("certain chances do not consume a number", "[unit][rng]") {
	const auto [percent, expected] =
	    GENERATE(table<std::int64_t, bool>({{0, false}, {-5, false}, {100, true}, {150, true}}));
	Rng rng(9);
	const auto state = rng.state();
	REQUIRE(rng.chance(percent) == expected);
	REQUIRE(rng.state() == state);
}

TEST_CASE("chance consumes exactly one number", "[unit][rng]") {
	Rng rng(9);
	Rng reference(9);
	const bool result = rng.chance(50);
	REQUIRE(result == (reference.roll(1, 100) <= 50));
	REQUIRE(rng.state() == reference.state());
}

TEST_CASE("weighted respects zero weights", "[unit][rng]") {
	Rng rng(3);
	const std::vector<std::int64_t> weights{0, 5, 0};
	for (int i = 0; i < 50; ++i) {
		REQUIRE(rng.weighted(weights) == 1);
	}
}

TEST_CASE("weighted rejects an empty total", "[unit][rng]") {
	const std::vector<std::int64_t> weights{0, 0};
	REQUIRE_THROWS_WITH(Rng(3).weighted(weights), ContainsSubstring("positive"));
}

TEST_CASE("pick returns an element and rejects an empty list", "[unit][rng]") {
	Rng rng(5);
	REQUIRE(rng.pick(std::vector<std::string>{"a"}) == "a");
	REQUIRE_THROWS_WITH(rng.pick(std::vector<std::string>{}), ContainsSubstring("empty"));
}
