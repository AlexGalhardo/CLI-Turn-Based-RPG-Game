#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "application/events.hpp"
#include "application/run_state.hpp"
#include "domain/definitions.hpp"

namespace rpg::application {

// Profile constants (docs/game-design.md §11).
inline constexpr std::size_t hall_of_fame_size = 10;
inline constexpr std::int64_t bestiary_reveal_kills = 5;

// Kills of one creature across runs.
struct BestiaryEntry {
	std::int64_t kills = 0;
	std::string first_killed_at;

	bool operator==(const BestiaryEntry&) const = default;
};

// When an achievement was unlocked.
struct Unlock {
	std::string unlocked_at;
	std::string run_id;

	bool operator==(const Unlock&) const = default;
};

// One ranked finished run.
struct HallOfFameEntry {
	std::string run_id;
	std::string name;
	std::string vocation;
	std::string difficulty;
	std::int64_t round = 0;
	std::int64_t level = 0;
	std::string ended_at;
	bool won = false;

	bool operator==(const HallOfFameEntry&) const = default;
};

// The cross-run profile (profile.json).
struct Profile {
	std::map<std::string, BestiaryEntry, std::less<>> bestiary;
	std::map<std::string, Unlock, std::less<>> achievements;
	std::vector<HallOfFameEntry> hall_of_fame;

	bool operator==(const Profile&) const = default;
};

nlohmann::ordered_json to_json(const Profile& profile);
Profile profile_from_json(const nlohmann::json& document);

// Feeds the profile from engine events and reports newly unlocked achievements.
class ProfileService {
public:
	Profile profile;

	ProfileService(const domain::GameData& data, Profile initial) : profile(std::move(initial)), data_(&data) {}

	// Updates the bestiary and returns the achievements unlocked by this step.
	std::vector<domain::AchievementDef> observe(
	    std::span<const Event> events, const RunState& state, const std::string& now, const std::string& run_id);

	// Inserts a run into the Hall of Fame (won runs first, then round desc, level desc, earliest end first).
	void record_finished_run(const HallOfFameEntry& entry);

	// Whether a creature's weaknesses are shown in the bestiary.
	[[nodiscard]] bool revealed(std::string_view monster_id) const;

private:
	const domain::GameData* data_;

	[[nodiscard]] std::int64_t progress(const domain::AchievementDef& achievement, const RunState& state) const;
};

} // namespace rpg::application
