#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <vector>

#include "application/run_state.hpp"
#include "domain/definitions.hpp"

namespace rpg::application {

// The outcome of one simulated run.
struct RunResult {
	std::int64_t round = 0;
	std::int64_t level = 0;
	std::string death_cause;
};

// Aggregated simulated runs. top_killers holds (creature id, deaths), most deadly first.
struct SimulationSummary {
	std::string vocation;
	std::string difficulty;
	std::int64_t runs = 0;
	std::int64_t min_round = 0;
	std::int64_t p10_round = 0;
	std::int64_t median_round = 0;
	std::int64_t p90_round = 0;
	std::int64_t max_round = 0;
	std::int64_t mean_level = 0;
	std::vector<std::pair<std::string, std::int64_t>> top_killers;
};

// The bot plays a run until death. Errors are "invalid run config: ..." or a run that never ends.
std::expected<RunResult, std::string> play_one(
    const domain::GameData& data, const RunConfig& config, std::uint64_t seed);

// Runs `runs` games with consecutive seeds and aggregates how far the bot gets.
std::expected<SimulationSummary, std::string> simulate(const domain::GameData& data, const std::string& vocation,
    const std::string& difficulty, std::int64_t runs, std::uint64_t base_seed);

} // namespace rpg::application
