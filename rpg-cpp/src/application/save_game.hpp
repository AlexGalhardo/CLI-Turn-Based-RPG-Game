#pragma once

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "application/run_state.hpp"
#include "application/statistics.hpp"

namespace rpg::application {

// Shared file format constants (docs/persistence.md).
inline constexpr std::int64_t schema_version = 1;
inline constexpr std::string_view implementation = "cpp";

using TimePoint = std::chrono::system_clock::time_point;

// A file written by a newer game version; it is never overwritten.
class NewerSchemaError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

// UTC ISO 8601 with a Z suffix: 2026-09-27T21:04:11Z.
std::string format_timestamp(TimePoint moment);

// Parses format_timestamp output; throws std::invalid_argument otherwise.
TimePoint parse_timestamp(std::string_view text);

// `<startedAt as yyyyMMddTHHmmssZ>-<seed>`.
std::string make_run_id(TimePoint started_at, std::uint64_t seed);

// Refuses documents written with a newer schema version.
void check_schema(const nlohmann::json& document, std::string_view what);

// Tracks a run across play sessions.
struct SessionInfo {
	std::string run_id;
	std::string started_at;
	std::int64_t play_time_seconds = 0;
	std::int64_t sessions = 1;

	bool operator==(const SessionInfo&) const = default;
};

// The shared save.json format.
struct SaveGame {
	std::int64_t schema = schema_version;
	std::string game_version;
	std::string implementation_name{implementation};
	std::string saved_at;
	std::uint32_t rng_state = 0;
	SessionInfo session;
	RunState run;
};

nlohmann::ordered_json to_json(const SaveGame& save);
// Parses save.json, refusing newer schemas.
SaveGame save_game_from_json(const nlohmann::json& document);

// A finished run, written to history/<runId>.json.
struct RunRecord {
	std::int64_t schema = schema_version;
	std::string run_id;
	std::string name;
	std::string vocation;
	std::string difficulty;
	std::uint64_t seed = 0;
	std::string implementation_name{implementation};
	std::string game_version;
	std::string started_at;
	std::string ended_at;
	std::int64_t play_time_seconds = 0;
	std::int64_t sessions = 0;
	std::int64_t round = 0;
	std::int64_t level = 0;
	std::int64_t magic_level = 0;
	std::string death_cause;
	RunStatistics stats;

	bool operator==(const RunRecord&) const = default;
};

nlohmann::ordered_json to_json(const RunRecord& record);
RunRecord run_record_from_json(const nlohmann::json& document);

} // namespace rpg::application
