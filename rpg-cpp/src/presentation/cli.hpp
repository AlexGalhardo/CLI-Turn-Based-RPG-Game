#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rpg::presentation {

// The flags shared by every implementation (docs/tui.md).
struct CliOptions {
	std::optional<std::uint64_t> seed;
	std::string lang;
	bool no_anim = false;
	std::string data_dir;
	std::int64_t simulate = 0;
	std::string vocation;
	std::string difficulty;
};

// Either options to run with, or text to print before exiting (--help, --version).
struct CliResult {
	CliOptions options;
	bool exit = false;
	std::string output;
};

// The argparse help of the reference implementation.
extern const std::string_view help_text;

// Parses command-line arguments (without the program name). Usage errors are returned as the message argparse would
// print after "rpg: error: "; the caller exits with code 2, like argparse.
std::expected<CliResult, std::string> parse_cli(std::span<const std::string_view> args);

} // namespace rpg::presentation
