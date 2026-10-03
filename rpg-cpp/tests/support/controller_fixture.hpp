#pragma once

#include <filesystem>
#include <memory>
#include <string_view>

#include "assets/shared_files.hpp"
#include "infrastructure/repositories.hpp"
#include "presentation/controller.hpp"
#include "support/helpers.hpp"
#include "version.hpp"

namespace rpg::testing {

// Real file adapters over a test directory, like the reference make_controller().
inline presentation::Services services_for(const std::filesystem::path& directory) {
	return presentation::Services{
	    .data = &test_data(),
	    .shared = &assets::embedded_shared(),
	    .settings = std::make_shared<infrastructure::SettingsRepository>(directory),
	    .repositories = infrastructure::file_repositories(directory),
	    .clock = std::make_shared<infrastructure::SystemClock>(),
	    .version = std::string(version),
	};
}

inline presentation::Controller make_controller(const std::filesystem::path& directory, std::string_view lang = "en") {
	return presentation::Controller(services_for(directory), 7, lang);
}

// Title → new run → normal difficulty → name → vocation.
inline void start_run(
    presentation::Controller& controller, std::string_view name = "Zed", std::string_view vocation_key = "1") {
	controller.press("2");
	controller.press("2");
	for (const char character : name) {
		controller.press(std::string(1, character));
	}
	controller.press("enter");
	controller.press(vocation_key);
}

} // namespace rpg::testing
