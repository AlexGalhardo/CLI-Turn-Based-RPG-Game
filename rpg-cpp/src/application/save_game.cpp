#include "application/save_game.hpp"

#include <charconv>
#include <format>

namespace rpg::application {

using nlohmann::json;
using nlohmann::ordered_json;

namespace {

struct DateTime {
	std::chrono::year_month_day date;
	std::chrono::hh_mm_ss<std::chrono::seconds> time;
};

DateTime split(TimePoint moment) {
	const auto seconds = std::chrono::floor<std::chrono::seconds>(moment);
	const auto day = std::chrono::floor<std::chrono::days>(seconds);
	return DateTime{std::chrono::year_month_day{day}, std::chrono::hh_mm_ss{seconds - day}};
}

int read_number(std::string_view text, std::size_t offset, std::size_t length) {
	int value = 0;
	const char* first = text.data() + offset;
	const auto [end, error] = std::from_chars(first, first + length, value);
	if (error != std::errc{} || end != first + length) {
		throw std::invalid_argument("invalid timestamp: " + std::string(text));
	}
	return value;
}

} // namespace

std::string format_timestamp(TimePoint moment) {
	const auto [date, time] = split(moment);
	return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", static_cast<int>(date.year()),
	    static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()), time.hours().count(),
	    time.minutes().count(), time.seconds().count());
}

TimePoint parse_timestamp(std::string_view text) {
	// yyyy-MM-ddTHH:mm:ssZ (std::chrono::parse is not available in every standard library yet).
	if (text.size() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
	    text[16] != ':' || text[19] != 'Z') {
		throw std::invalid_argument("invalid timestamp: " + std::string(text));
	}
	const std::chrono::year_month_day date{std::chrono::year{read_number(text, 0, 4)},
	    std::chrono::month{static_cast<unsigned>(read_number(text, 5, 2))},
	    std::chrono::day{static_cast<unsigned>(read_number(text, 8, 2))}};
	if (!date.ok()) {
		throw std::invalid_argument("invalid timestamp: " + std::string(text));
	}
	return std::chrono::sys_days{date} + std::chrono::hours{read_number(text, 11, 2)} +
	       std::chrono::minutes{read_number(text, 14, 2)} + std::chrono::seconds{read_number(text, 17, 2)};
}

std::string make_run_id(TimePoint started_at, std::uint64_t seed) {
	const auto [date, time] = split(started_at);
	return std::format("{:04}{:02}{:02}T{:02}{:02}{:02}Z-{}", static_cast<int>(date.year()),
	    static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()), time.hours().count(),
	    time.minutes().count(), time.seconds().count(), seed);
}

void check_schema(const json& document, std::string_view what) {
	const std::int64_t version = document.value("schemaVersion", std::int64_t{0});
	if (version > schema_version) {
		throw NewerSchemaError(std::format(
		    "{} uses schema {} (supported: {}); update the game to read it", what, version, schema_version));
	}
}

ordered_json to_json(const SaveGame& save) {
	return ordered_json{
	    {"schemaVersion", save.schema},
	    {"gameVersion", save.game_version},
	    {"implementation", save.implementation_name},
	    {"savedAt", save.saved_at},
	    {"rngState", save.rng_state},
	    {"session",
	        ordered_json{
	            {"runId", save.session.run_id},
	            {"startedAt", save.session.started_at},
	            {"playTimeSeconds", save.session.play_time_seconds},
	            {"sessions", save.session.sessions},
	        }},
	    {"run", to_json(save.run)},
	};
}

SaveGame save_game_from_json(const json& document) {
	check_schema(document, "save.json");
	const json& session = document.at("session");
	return SaveGame{
	    .schema = document.at("schemaVersion").get<std::int64_t>(),
	    .game_version = document.value("gameVersion", std::string{}),
	    .implementation_name = document.value("implementation", std::string{}),
	    .saved_at = document.value("savedAt", std::string{}),
	    .rng_state = document.at("rngState").get<std::uint32_t>(),
	    .session =
	        SessionInfo{
	            .run_id = session.at("runId").get<std::string>(),
	            .started_at = session.at("startedAt").get<std::string>(),
	            .play_time_seconds = session.value("playTimeSeconds", std::int64_t{0}),
	            .sessions = session.value("sessions", std::int64_t{1}),
	        },
	    .run = run_state_from_json(document.at("run")),
	};
}

ordered_json to_json(const RunRecord& record) {
	return ordered_json{
	    {"schemaVersion", record.schema},
	    {"runId", record.run_id},
	    {"name", record.name},
	    {"vocation", record.vocation},
	    {"difficulty", record.difficulty},
	    {"seed", record.seed},
	    {"implementation", record.implementation_name},
	    {"gameVersion", record.game_version},
	    {"startedAt", record.started_at},
	    {"endedAt", record.ended_at},
	    {"playTimeSeconds", record.play_time_seconds},
	    {"sessions", record.sessions},
	    {"round", record.round},
	    {"level", record.level},
	    {"magicLevel", record.magic_level},
	    {"deathCause", record.death_cause},
	    {"stats", to_json(record.stats)},
	};
}

RunRecord run_record_from_json(const json& document) {
	check_schema(document, "history record");
	return RunRecord{
	    .schema = document.at("schemaVersion").get<std::int64_t>(),
	    .run_id = document.at("runId").get<std::string>(),
	    .name = document.at("name").get<std::string>(),
	    .vocation = document.at("vocation").get<std::string>(),
	    .difficulty = document.at("difficulty").get<std::string>(),
	    .seed = document.at("seed").get<std::uint64_t>(),
	    .implementation_name = document.value("implementation", std::string{}),
	    .game_version = document.value("gameVersion", std::string{}),
	    .started_at = document.at("startedAt").get<std::string>(),
	    .ended_at = document.at("endedAt").get<std::string>(),
	    .play_time_seconds = document.value("playTimeSeconds", std::int64_t{0}),
	    .sessions = document.value("sessions", std::int64_t{1}),
	    .round = document.at("round").get<std::int64_t>(),
	    .level = document.at("level").get<std::int64_t>(),
	    .magic_level = document.value("magicLevel", std::int64_t{1}),
	    .death_cause = document.value("deathCause", std::string{}),
	    .stats = run_statistics_from_json(document.at("stats")),
	};
}

} // namespace rpg::application
