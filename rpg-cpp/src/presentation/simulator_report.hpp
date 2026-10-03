#pragma once

#include <span>
#include <string>

#include "application/simulator.hpp"
#include "domain/definitions.hpp"

namespace rpg::presentation {

// Simulator summaries as an aligned table (byte-identical to the reference output).
std::string render_report(std::span<const application::SimulationSummary> summaries, const domain::GameData& data);

} // namespace rpg::presentation
