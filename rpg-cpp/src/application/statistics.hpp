#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "application/events.hpp"
#include "domain/entities.hpp"

namespace rpg::application {

// A drop recorded for the run history.
struct DroppedItem {
	std::string item_id;
	std::string rarity;
	std::int64_t round = 0;

	bool operator==(const DroppedItem&) const = default;
};

// Deterministic run counters derived only from engine events (docs/game-design.md §11).
struct RunStatistics {
	std::int64_t damage_dealt = 0;
	std::int64_t damage_taken = 0;
	std::int64_t healing_done = 0;
	std::int64_t highest_hit = 0;
	std::int64_t normal_attacks = 0;
	std::int64_t crits = 0;
	std::int64_t dodges = 0;
	std::int64_t parries = 0;
	std::int64_t defends = 0;
	std::int64_t gold_looted = 0;
	std::int64_t gold_spent = 0;
	std::int64_t gold_earned = 0;
	std::int64_t items_sold = 0;
	std::int64_t bosses_killed = 0;
	domain::CountMap spells_cast;
	domain::CountMap potions_used;
	domain::CountMap potions_bought;
	domain::CountMap items_dropped;
	domain::CountMap kills;
	domain::CountMap statuses_applied;
	std::vector<DroppedItem> dropped_items;

	// The kills of every monster summed.
	[[nodiscard]] std::int64_t total_kills() const;

	// Updates the counters from the events of one step.
	void record(std::span<const Event> events, std::int64_t current_round);

	bool operator==(const RunStatistics&) const = default;

private:
	void record_one(const Event& event, std::int64_t current_round);
	void dealt(const Event& event);
};

} // namespace rpg::application
