#include "infrastructure/paths.hpp"

#include <cstdlib>
#include <string>

namespace rpg::infrastructure {

std::string get_env(std::string_view name) {
	const std::string key(name);
	// std::getenv is fine here: the program reads its environment once at start-up, from a single thread.
	const char* value = std::getenv(key.c_str()); // NOLINT(concurrency-mt-unsafe)
	return value == nullptr ? std::string{} : std::string(value);
}

std::filesystem::path resolve_data_dir(std::string_view cli_value) {
	if (!cli_value.empty()) {
		return std::filesystem::path(cli_value);
	}
	if (const std::string override_dir = get_env(data_dir_env); !override_dir.empty()) {
		return std::filesystem::path(override_dir);
	}
#ifdef _WIN32
	const std::string home = get_env("USERPROFILE");
#else
	const std::string home = get_env("HOME");
#endif
	if (home.empty()) {
		return std::filesystem::path(default_data_dir_name);
	}
	return std::filesystem::path(home) / default_data_dir_name;
}

} // namespace rpg::infrastructure
