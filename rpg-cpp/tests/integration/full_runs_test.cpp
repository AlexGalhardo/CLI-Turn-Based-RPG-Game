// Whole runs driven by the bot: the engine must always terminate, be deterministic and survive save/restore.
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include "application/bot.hpp"
#include "application/engine.hpp"
#include "support/helpers.hpp"

using namespace rpg;
using application::Event;
using nlohmann::json;

namespace {

constexpr int max_steps = 50'000;

std::vector<std::vector<Event>> play_to_death(application::GameEngine& engine, const application::GreedyBot& bot) {
	std::vector<std::vector<Event>> log;
	for (int step = 0; step < max_steps; ++step) {
		if (engine.state.phase == domain::Phase::game_over) {
			return log;
		}
		auto events = engine.step(bot.choose(engine.state));
		for (const Event& event : events) {
			REQUIRE(event.type != "error");
		}
		log.push_back(std::move(events));
	}
	FAIL("run did not finish");
	return log;
}

application::GameEngine engine_for(std::string vocation, std::string difficulty, std::uint64_t seed) {
	auto created = application::GameEngine::new_run(rpg::testing::test_data(),
	    application::RunConfig{"Bot", std::move(vocation), std::move(difficulty), false}, seed);
	REQUIRE(created.has_value());
	return std::move(created->first);
}

} // namespace

TEST_CASE("the bot plays until the run ends", "[integration][full_runs]") {
	const std::string vocation = GENERATE("warrior", "archer", "mage");
	const std::string difficulty = GENERATE("easy", "normal", "hard");
	auto engine = engine_for(vocation, difficulty, 1234);
	const auto log = play_to_death(engine, application::GreedyBot(rpg::testing::test_data()));
	const auto& state = engine.state;
	REQUIRE(state.phase == domain::Phase::game_over);
	REQUIRE(state.round >= 1);
	REQUIRE(state.stats.damage_dealt > 0);
	if (state.won) {
		REQUIRE(state.round == rpg::testing::test_data().balance.final_round);
		REQUIRE_FALSE(state.death_cause.has_value());
		REQUIRE(log[log.size() - 2].back() == Event{"run_won", {{"round", state.round}}});
		REQUIRE(log.back() == std::vector<Event>{Event{"run_ended", {{"won", true}}}});
		REQUIRE(state.stats.total_kills() == state.round);
	} else {
		REQUIRE_FALSE(state.death_cause.value_or("").empty());
		REQUIRE(log.back().back().type == "player_died");
		REQUIRE(state.stats.total_kills() == state.round - 1);
	}
}

TEST_CASE("some bot runs are won", "[integration][full_runs]") {
	const application::GreedyBot bot(rpg::testing::test_data());
	std::int64_t won = 0;
	for (std::uint64_t seed = 2002; seed < 2006; ++seed) {
		auto engine = engine_for("archer", "easy", seed);
		play_to_death(engine, bot);
		won += engine.state.won ? 1 : 0;
	}
	REQUIRE(won > 0);
}

TEST_CASE("the same seed gives the same events", "[integration][full_runs]") {
	const application::GreedyBot bot(rpg::testing::test_data());
	std::vector<std::vector<std::vector<Event>>> logs;
	for (int i = 0; i < 2; ++i) {
		auto created = application::GameEngine::new_run(
		    rpg::testing::test_data(), application::RunConfig{"Bot", "archer", "normal", false}, 777);
		REQUIRE(created.has_value());
		auto log = play_to_death(created->first, bot);
		log.insert(log.begin(), created->second);
		logs.push_back(std::move(log));
	}
	REQUIRE(logs[0] == logs[1]);
}

TEST_CASE("different seeds diverge", "[integration][full_runs]") {
	const application::GreedyBot bot(rpg::testing::test_data());
	std::set<std::string> finals;
	for (const std::uint64_t seed : {1U, 2U, 3U}) {
		auto engine = engine_for("warrior", "normal", seed);
		play_to_death(engine, bot);
		finals.insert(application::to_json(engine.state.stats).dump());
	}
	REQUIRE(finals.size() > 1);
}

TEST_CASE("restoring mid-run continues identically", "[integration][full_runs]") {
	const auto& data = rpg::testing::test_data();
	const application::GreedyBot bot(data);
	auto reference = engine_for("mage", "hard", 99);
	const auto reference_log = play_to_death(reference, bot);

	auto engine = engine_for("mage", "hard", 99);
	std::vector<std::vector<Event>> log;
	while (engine.state.phase != domain::Phase::game_over) {
		if (engine.state.phase == domain::Phase::merchant && engine.state.round % 3 == 0) {
			const json snapshot = json::parse(application::to_json(engine.state).dump());
			engine =
			    application::GameEngine::restore(data, application::run_state_from_json(snapshot), engine.rng_state());
		}
		log.push_back(engine.step(bot.choose(engine.state)));
	}
	REQUIRE(log == reference_log);
}

TEST_CASE("a new run rejects an invalid config", "[integration][full_runs]") {
	const auto& data = rpg::testing::test_data();
	const auto knight =
	    application::GameEngine::new_run(data, application::RunConfig{"X", "knight", "normal", false}, 1);
	REQUIRE_FALSE(knight.has_value());
	REQUIRE(knight.error().starts_with("invalid run config"));
	const auto nightmare =
	    application::GameEngine::new_run(data, application::RunConfig{"X", "mage", "nightmare", false}, 1);
	REQUIRE_FALSE(nightmare.has_value());
	REQUIRE(nightmare.error().starts_with("invalid run config"));
}
