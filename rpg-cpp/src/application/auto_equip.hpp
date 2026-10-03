#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "application/events.hpp"
#include "application/run_state.hpp"
#include "domain/definitions.hpp"
#include "domain/entities.hpp"

namespace rpg::application {

// The highest-score bag item the player can wear in `slot` now; ties go to the lowest uid.
std::optional<domain::ItemInstance> best_bag_item(
    const RunState& state, const domain::GameData& data, std::string_view slot);

// Auto-equip with auto-sell (docs/game-design.md §8.1). Consumes no randomness.
std::vector<Event> auto_equip(RunState& state, const domain::GameData& data);

} // namespace rpg::application
