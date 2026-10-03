#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "application/commands.hpp"
#include "application/engine.hpp"
#include "application/events.hpp"
#include "application/ports.hpp"
#include "application/profile.hpp"
#include "application/save_game.hpp"

namespace rpg::application {

// The collaborators of a game session.
struct SessionContext {
	Repositories repositories;
	std::shared_ptr<Clock> clock;
	std::string game_version;
};

// What a session step returns.
struct StepResult {
	std::vector<Event> events;
	std::vector<domain::AchievementDef> achievements;
};

// Wraps the pure engine with time, persistence and the profile.
class GameSession {
public:
	GameEngine engine;
	SessionInfo info;
	ProfileService profile;
	std::optional<RunRecord> finished_record;

	// Creates a new run and saves it at the merchant. Fails with "invalid run config: ..." for unknown ids.
	static std::expected<std::pair<GameSession, std::vector<Event>>, std::string> start(
	    const domain::GameData& data, const RunConfig& config, std::uint64_t seed, const SessionContext& context);

	// Continues the saved run, or std::nullopt when there is none.
	static std::optional<GameSession> resume(const domain::GameData& data, const SessionContext& context);

	[[nodiscard]] RunState& state() { return engine.state; }
	[[nodiscard]] const RunState& state() const { return engine.state; }

	// Applies a command and persists what the new state requires.
	StepResult step(const Command& command);

	// Persists play time. Mid-battle quits resume from the last merchant visit.
	void save_and_quit();

private:
	GameSession(GameEngine run, SessionInfo session_info, Profile loaded, SessionContext context);

	SessionContext context_;
	TimePoint segment_started_;
	// The last merchant snapshot: a plain copy of the value-type state (no serialisation round trip needed).
	std::optional<RunState> snapshot_;
	std::uint32_t snapshot_rng_ = 0;

	std::vector<domain::AchievementDef> after_step(const std::vector<Event>& events);
	TimePoint accumulate_play_time();
	void write_save();
	void finish();
};

} // namespace rpg::application
