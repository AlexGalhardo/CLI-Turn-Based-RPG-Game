#include "application/simulator.hpp"

#include <algorithm>
#include <format>
#include <map>

#include "application/bot.hpp"
#include "application/engine.hpp"

namespace rpg::application {

namespace {

constexpr int max_steps_per_run = 200'000;

std::int64_t percentile(const std::vector<std::int64_t>& sorted, std::int64_t percent) {
	const auto size = static_cast<std::int64_t>(sorted.size());
	return sorted[static_cast<std::size_t>(std::min(size - 1, size * percent / 100))];
}

} // namespace

std::expected<RunResult, std::string> play_one(
    const domain::GameData& data, const RunConfig& config, std::uint64_t seed) {
	auto created = GameEngine::new_run(data, config, seed);
	if (!created.has_value()) {
		return std::unexpected(created.error());
	}
	GameEngine& engine = created->first;
	const GreedyBot bot(data);
	for (int step = 0; step < max_steps_per_run; ++step) {
		if (engine.state.phase == domain::Phase::game_over) {
			return RunResult{engine.state.round, engine.state.player.level, engine.state.death_cause.value_or("")};
		}
		engine.step(bot.choose(engine.state));
	}
	return std::unexpected(std::format("run did not finish (seed {})", seed));
}

std::expected<SimulationSummary, std::string> simulate(const domain::GameData& data, const std::string& vocation,
    const std::string& difficulty, std::int64_t runs, std::uint64_t base_seed) {
	if (runs <= 0) {
		return std::unexpected("runs must be positive");
	}
	std::vector<std::int64_t> rounds;
	std::map<std::string, std::int64_t> killers;
	std::int64_t levels = 0;
	for (std::int64_t i = 0; i < runs; ++i) {
		auto result = play_one(data, RunConfig{"Bot", vocation, difficulty}, base_seed + static_cast<std::uint64_t>(i));
		if (!result.has_value()) {
			return std::unexpected(result.error());
		}
		rounds.push_back(result->round);
		killers[result->death_cause] += 1;
		levels += result->level;
	}
	std::ranges::sort(rounds);

	std::vector<std::pair<std::string, std::int64_t>> top(killers.begin(), killers.end());
	// Most deaths first, ties by id: the map already yields ids in order, so a stable sort keeps that tie-break.
	std::ranges::stable_sort(top, std::ranges::greater{}, &std::pair<std::string, std::int64_t>::second);
	top.resize(std::min<std::size_t>(3, top.size()));

	return SimulationSummary{
	    .vocation = vocation,
	    .difficulty = difficulty,
	    .runs = runs,
	    .min_round = rounds.front(),
	    .p10_round = percentile(rounds, 10),
	    .median_round = percentile(rounds, 50),
	    .p90_round = percentile(rounds, 90),
	    .max_round = rounds.back(),
	    .mean_level = levels / runs,
	    .top_killers = std::move(top),
	};
}

} // namespace rpg::application
