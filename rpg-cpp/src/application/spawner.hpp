#pragma once

#include <cstdint>
#include <utility>

#include "domain/definitions.hpp"
#include "domain/entities.hpp"
#include "domain/formulas.hpp"
#include "domain/rng.hpp"

namespace rpg::application {

// The monster of a round: tier, cycle, position and difficulty scaling (docs/game-design.md §3).
std::pair<domain::MonsterInstance, domain::RoundInfo> spawn_monster(
    const domain::GameData& data, domain::Rng& rng, std::int64_t round, const domain::DifficultyDef& difficulty);

} // namespace rpg::application
