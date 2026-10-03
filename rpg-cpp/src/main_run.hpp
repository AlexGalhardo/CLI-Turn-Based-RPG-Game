#pragma once

#include <filesystem>
#include <ostream>
#include <span>
#include <string_view>

#include "domain/definitions.hpp"
#include "presentation/cli.hpp"
#include "presentation/controller.hpp"

namespace rpg {

// main() without the process exit, so the whole command line can be tested: flags → simulator or TUI.
// Returns the exit code (0 ok, 1 runtime failure, 2 usage error like argparse).
int run(std::span<const std::string_view> args, std::ostream& out, std::ostream& err);

// The simulator mode: one summary per vocation × difficulty, printed as the reference report.
int run_simulator(
    const domain::GameData& data, const presentation::CliOptions& options, std::ostream& out, std::ostream& err);

// Wires the real adapters for a data directory.
presentation::Services build_services(const domain::GameData& data, const std::filesystem::path& data_dir);

} // namespace rpg
