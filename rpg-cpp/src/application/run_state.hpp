#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "application/statistics.hpp"
#include "domain/entities.hpp"
#include "domain/enums.hpp"

namespace rpg::application {

// What the player chooses for a new run.
struct RunConfig {
	std::string name;
	std::string vocation_id;
	std::string difficulty_id;

	bool operator==(const RunConfig&) const = default;
};

// Everything needed to continue a run, except the PRNG state (kept by the engine). A value type: copying it is the
// deep "snapshot" that Go and TypeScript build by serialising and parsing the state.
struct RunState {
	std::uint64_t seed = 0;
	RunConfig config;
	domain::Player player;
	domain::Phase phase = domain::Phase::merchant;
	std::int64_t round = 0;
	std::int64_t turn = 0;
	std::optional<domain::MonsterInstance> monster;
	std::vector<domain::ItemInstance> merchant_stock;
	std::int64_t next_item_uid = 1;
	std::optional<std::string> death_cause;
	RunStatistics stats;

	// The next item uid; advances the counter.
	std::int64_t take_item_uid() { return next_item_uid++; }

	bool operator==(const RunState&) const = default;
};

// JSON in the shared save format (exactly the reference `RunState.to_dict()`), with the reference key order.
nlohmann::ordered_json to_json(const RunState& state);
nlohmann::ordered_json to_json(const RunStatistics& stats);
nlohmann::ordered_json to_json(const RunConfig& config);

// Parses the shared save format; missing collections read as empty. Throws nlohmann::json::exception on bad input.
RunState run_state_from_json(const nlohmann::json& document);
RunStatistics run_statistics_from_json(const nlohmann::json& document);

} // namespace rpg::application
