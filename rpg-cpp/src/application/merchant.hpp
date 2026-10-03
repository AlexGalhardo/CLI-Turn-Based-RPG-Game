#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "application/commands.hpp"
#include "application/events.hpp"
#include "application/run_state.hpp"
#include "domain/definitions.hpp"
#include "domain/rng.hpp"

namespace rpg::application {

// The merchant's selling price for a stock item.
std::int64_t stock_price(const domain::ItemInstance& item, const domain::GameData& data);

// The potions unlocked for the next round, in data file order.
std::vector<std::string> available_potions(const RunState& state, const domain::GameData& data);

// The merchant phase: potions, bag, equipment and rotating stock (docs/game-design.md §10).
class Merchant {
public:
	Merchant(const domain::GameData& data, domain::Rng& rng, RunState& state) :
	    data_(&data), rng_(&rng), state_(&state) {}

	// Generates the rotating stock for the tier of the next round.
	std::vector<Event> enter();

	// Executes a merchant command.
	std::vector<Event> handle(const Command& command);

private:
	const domain::GameData* data_;
	domain::Rng* rng_;
	RunState* state_;

	std::vector<Event> buy_potion(const std::string& potion_id, std::int64_t quantity);
	std::vector<Event> sell(std::int64_t uid);
	std::vector<Event> equip(std::int64_t uid);
	std::vector<Event> unequip(const domain::Slot& slot);
	std::vector<Event> buy_stock(std::int64_t index);
	void clamp_resources();
};

} // namespace rpg::application
