#include "presentation/cli.hpp"

#include <charconv>
#include <format>

#include "infrastructure/i18n.hpp"
#include "version.hpp"

namespace rpg::presentation {

const std::string_view help_text = R"(usage: rpg [-h] [--version] [--seed SEED] [--lang {en,pt-BR}] [--no-anim]
           [--data-dir DATA_DIR] [--simulate N] [--vocation VOCATION]
           [--difficulty DIFFICULTY]

Endless turn-based RPG for the terminal.

options:
  -h, --help            show this help message and exit
  --version             show program's version number and exit
  --seed SEED           deterministic run
  --lang {en,pt-BR}     override the saved language
  --no-anim             disable animations
  --data-dir DATA_DIR   saves/profile location
  --simulate N          run N headless bot games and print a report
  --vocation VOCATION   (simulator) restrict to one vocation
  --difficulty DIFFICULTY
                        (simulator) restrict to one difficulty)";

namespace {

// The whole text must be an integer: "12abc" and "" are rejected, unlike std::stoll.
std::optional<std::int64_t> parse_integer(std::string_view text) {
	std::int64_t value = 0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (error != std::errc{} || end != text.data() + text.size()) {
		return std::nullopt;
	}
	return value;
}

} // namespace

std::expected<CliResult, std::string> parse_cli(std::span<const std::string_view> args) {
	CliResult result;
	CliOptions& options = result.options;
	bool show_help = false;
	bool show_version = false;

	for (std::size_t index = 0; index < args.size(); ++index) {
		std::string_view flag = args[index];
		std::optional<std::string_view> inline_value;
		if (const auto equals = flag.find('='); flag.starts_with("--") && equals != std::string_view::npos) {
			inline_value = flag.substr(equals + 1);
			flag = flag.substr(0, equals);
		}
		// Takes the flag's value from `--flag=value` or from the next argument.
		const auto value = [&]() -> std::expected<std::string_view, std::string> {
			if (inline_value.has_value()) {
				return *inline_value;
			}
			if (index + 1 >= args.size()) {
				return std::unexpected(std::format("argument {}: expected one argument", flag));
			}
			return args[++index];
		};

		if (flag == "-h" || flag == "--help") {
			show_help = true;
		} else if (flag == "--version") {
			show_version = true;
		} else if (flag == "--no-anim") {
			options.no_anim = true;
		} else if (flag == "--seed") {
			const auto text = value();
			if (!text) {
				return std::unexpected(text.error());
			}
			const auto number = parse_integer(*text);
			if (!number.has_value()) {
				return std::unexpected(std::format("argument --seed: invalid _non_negative value: '{}'", *text));
			}
			if (*number < 0) {
				return std::unexpected("argument --seed: must be >= 0");
			}
			options.seed = static_cast<std::uint64_t>(*number);
		} else if (flag == "--simulate") {
			const auto text = value();
			if (!text) {
				return std::unexpected(text.error());
			}
			const auto number = parse_integer(*text);
			if (!number.has_value()) {
				return std::unexpected(std::format("argument --simulate: invalid _positive value: '{}'", *text));
			}
			if (*number <= 0) {
				return std::unexpected("argument --simulate: must be > 0");
			}
			options.simulate = *number;
		} else if (flag == "--lang") {
			const auto text = value();
			if (!text) {
				return std::unexpected(text.error());
			}
			if (!infrastructure::is_supported_locale(*text)) {
				return std::unexpected(
				    std::format("argument --lang: invalid choice: '{}' (choose from 'en', 'pt-BR')", *text));
			}
			options.lang = *text;
		} else if (flag == "--data-dir" || flag == "--vocation" || flag == "--difficulty") {
			const auto text = value();
			if (!text) {
				return std::unexpected(text.error());
			}
			std::string& target = flag == "--data-dir"   ? options.data_dir
			                      : flag == "--vocation" ? options.vocation
			                                             : options.difficulty;
			target = *text;
		} else {
			return std::unexpected(std::format("unrecognized arguments: {}", args[index]));
		}
	}

	// argparse acts on --help/--version as soon as it meets them, so they win over any other flag.
	if (show_help) {
		result.exit = true;
		result.output = help_text;
	} else if (show_version) {
		result.exit = true;
		result.output = std::format("rpg {} (cpp)", version);
	}
	return result;
}

} // namespace rpg::presentation
