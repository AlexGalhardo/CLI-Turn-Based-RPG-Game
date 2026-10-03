#include "main_run.hpp"

#include <exception>
#include <memory>
#include <vector>

#include "application/simulator.hpp"
#include "assets/shared_files.hpp"
#include "infrastructure/art.hpp"
#include "infrastructure/data_loader.hpp"
#include "infrastructure/paths.hpp"
#include "infrastructure/repositories.hpp"
#include "presentation/simulator_report.hpp"
#include "presentation/tui/app.hpp"
#include "version.hpp"

namespace rpg {

int run_simulator(
    const domain::GameData& data, const presentation::CliOptions& options, std::ostream& out, std::ostream& err) {
	std::vector<std::string> vocations;
	if (options.vocation.empty()) {
		for (const auto& vocation : data.vocations) {
			vocations.push_back(vocation.id);
		}
	} else {
		vocations.push_back(options.vocation);
	}
	std::vector<std::string> difficulties;
	if (options.difficulty.empty()) {
		for (const auto& difficulty : data.balance.difficulties) {
			difficulties.push_back(difficulty.id);
		}
	} else {
		difficulties.push_back(options.difficulty);
	}
	// `options.seed or 1` in the reference: a missing seed and seed 0 both start at 1.
	const std::uint64_t base_seed = options.seed.value_or(0) == 0 ? 1 : *options.seed;

	std::vector<application::SimulationSummary> summaries;
	for (const auto& vocation : vocations) {
		for (const auto& difficulty : difficulties) {
			auto summary = application::simulate(data, vocation, difficulty, options.simulate, base_seed);
			if (!summary.has_value()) {
				err << "error: " << summary.error() << '\n';
				return 2;
			}
			summaries.push_back(std::move(*summary));
		}
	}
	out << presentation::render_report(summaries, data) << '\n';
	return 0;
}

presentation::Services build_services(const domain::GameData& data, const std::filesystem::path& data_dir) {
	return presentation::Services{
	    .data = &data,
	    .shared = &assets::embedded_shared(),
	    .settings = std::make_shared<infrastructure::SettingsRepository>(data_dir),
	    .repositories = infrastructure::file_repositories(data_dir),
	    .clock = std::make_shared<infrastructure::SystemClock>(),
	    .version = std::string(version),
	};
}

int run(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
	const auto parsed = presentation::parse_cli(args);
	if (!parsed.has_value()) {
		const std::string_view usage = presentation::help_text.substr(0, presentation::help_text.find("\n\n"));
		err << usage << "\nrpg: error: " << parsed.error() << '\n';
		return 2;
	}
	if (parsed->exit) {
		out << parsed->output << '\n';
		return 0;
	}

	try {
		const domain::GameData data = infrastructure::load_game_data(assets::embedded_shared());
		const presentation::CliOptions& options = parsed->options;
		if (options.simulate > 0) {
			return run_simulator(data, options, out, err);
		}

		presentation::Controller controller(
		    build_services(data, infrastructure::resolve_data_dir(options.data_dir)), options.seed, options.lang);
		infrastructure::ArtLibrary art(assets::embedded_shared());
		const bool animate = !options.no_anim && infrastructure::get_env("RPG_NO_ANIM").empty();
		presentation::tui::run_tui(controller, art, animate);
		if (controller.session.has_value()) {
			controller.session->save_and_quit();
		}
		return 0;
	} catch (const std::exception& failure) {
		err << "rpg: " << failure.what() << '\n';
		return 1;
	}
}

} // namespace rpg
