#include "application/profile.hpp"

#include <algorithm>

#include "application/save_game.hpp"

namespace rpg::application {

using nlohmann::json;
using nlohmann::ordered_json;

ordered_json to_json(const Profile& profile) {
	ordered_json bestiary = ordered_json::object();
	for (const auto& [monster_id, entry] : profile.bestiary) {
		bestiary[monster_id] = ordered_json{{"kills", entry.kills}, {"firstKilledAt", entry.first_killed_at}};
	}
	ordered_json achievements = ordered_json::object();
	for (const auto& [achievement_id, unlock] : profile.achievements) {
		achievements[achievement_id] = ordered_json{{"unlockedAt", unlock.unlocked_at}, {"runId", unlock.run_id}};
	}
	ordered_json hall = ordered_json::array();
	for (const HallOfFameEntry& entry : profile.hall_of_fame) {
		hall.push_back(ordered_json{
		    {"runId", entry.run_id},
		    {"name", entry.name},
		    {"vocation", entry.vocation},
		    {"difficulty", entry.difficulty},
		    {"round", entry.round},
		    {"level", entry.level},
		    {"endedAt", entry.ended_at},
		});
	}
	return ordered_json{
	    {"schemaVersion", schema_version},
	    {"bestiary", bestiary},
	    {"achievements", achievements},
	    {"hallOfFame", hall},
	};
}

Profile profile_from_json(const json& document) {
	check_schema(document, "profile.json");
	Profile profile;
	// Named locals: a temporary in a range-for initializer chain is not lifetime-extended before C++23 (P2718).
	const json bestiary = document.value("bestiary", json::object());
	const json achievements = document.value("achievements", json::object());
	const json hall = document.value("hallOfFame", json::array());
	for (const auto& [monster_id, entry] : bestiary.items()) {
		profile.bestiary.emplace(monster_id,
		    BestiaryEntry{entry.at("kills").get<std::int64_t>(), entry.value("firstKilledAt", std::string{})});
	}
	for (const auto& [achievement_id, unlock] : achievements.items()) {
		profile.achievements.emplace(
		    achievement_id, Unlock{unlock.value("unlockedAt", std::string{}), unlock.value("runId", std::string{})});
	}
	for (const auto& entry : hall) {
		profile.hall_of_fame.push_back(HallOfFameEntry{
		    entry.at("runId").get<std::string>(),
		    entry.at("name").get<std::string>(),
		    entry.at("vocation").get<std::string>(),
		    entry.at("difficulty").get<std::string>(),
		    entry.at("round").get<std::int64_t>(),
		    entry.at("level").get<std::int64_t>(),
		    entry.at("endedAt").get<std::string>(),
		});
	}
	return profile;
}

std::vector<domain::AchievementDef> ProfileService::observe(
    std::span<const Event> events, const RunState& state, const std::string& now, const std::string& run_id) {
	for (const Event& event : events) {
		if (event.type != "monster_killed") {
			continue;
		}
		const std::string monster_id = event.text("monsterId");
		if (const auto found = profile.bestiary.find(monster_id); found != profile.bestiary.end()) {
			found->second.kills += 1;
		} else {
			profile.bestiary.emplace(monster_id, BestiaryEntry{1, now});
		}
	}

	std::vector<domain::AchievementDef> unlocked;
	for (const domain::AchievementDef& achievement : data_->achievements) {
		if (profile.achievements.contains(achievement.id)) {
			continue;
		}
		if (progress(achievement, state) >= achievement.value) {
			profile.achievements.emplace(achievement.id, Unlock{now, run_id});
			unlocked.push_back(achievement);
		}
	}
	return unlocked;
}

std::int64_t ProfileService::progress(const domain::AchievementDef& achievement, const RunState& state) const {
	const domain::Player& player = state.player;
	const std::string& type = achievement.type;
	if (type == "kills_total") {
		std::int64_t total = 0;
		for (const auto& [monster_id, entry] : profile.bestiary) {
			total += entry.kills;
		}
		return total;
	}
	if (type == "bosses_total") {
		std::int64_t total = 0;
		for (const domain::MonsterDef& boss : data_->bosses) {
			if (const auto found = profile.bestiary.find(boss.id); found != profile.bestiary.end()) {
				total += found->second.kills;
			}
		}
		return total;
	}
	if (type == "round_reached") {
		return state.round;
	}
	if (type == "level_reached") {
		return player.level;
	}
	if (type == "legendary_found") {
		return domain::count_of(state.stats.items_dropped, "legendary");
	}
	if (type == "spell_level_3") {
		const std::int64_t threshold = data_->balance.spell_levels.back().uses;
		return std::ranges::count_if(player.spell_uses, [&](const auto& entry) { return entry.second >= threshold; });
	}
	if (type == "gold_held") {
		return player.gold;
	}
	if (type == "distinct_monsters") {
		return static_cast<std::int64_t>(profile.bestiary.size());
	}
	if (type == "hard_round_reached") {
		return state.config.difficulty_id == "hard" ? state.round : 0;
	}
	return 0;
}

void ProfileService::record_finished_run(const HallOfFameEntry& entry) {
	std::vector<HallOfFameEntry> ranking = profile.hall_of_fame;
	ranking.push_back(entry);
	std::ranges::stable_sort(ranking, [](const HallOfFameEntry& a, const HallOfFameEntry& b) {
		if (a.round != b.round) {
			return a.round > b.round;
		}
		if (a.level != b.level) {
			return a.level > b.level;
		}
		return a.ended_at < b.ended_at;
	});
	ranking.resize(std::min(hall_of_fame_size, ranking.size()));
	profile.hall_of_fame = std::move(ranking);
}

bool ProfileService::revealed(std::string_view monster_id) const {
	const auto found = profile.bestiary.find(monster_id);
	return found != profile.bestiary.end() && found->second.kills >= bestiary_reveal_kills;
}

} // namespace rpg::application
