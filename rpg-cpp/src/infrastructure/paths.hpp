#pragma once

#include <filesystem>
#include <string_view>

namespace rpg::infrastructure {

inline constexpr std::string_view data_dir_env = "RPG_DATA_DIR";
inline constexpr std::string_view default_data_dir_name = ".cli-turn-based-rpg";

// The --data-dir flag, then RPG_DATA_DIR, then ~/.cli-turn-based-rpg (docs/persistence.md).
std::filesystem::path resolve_data_dir(std::string_view cli_value = {});

// The value of an environment variable, empty when unset.
std::string get_env(std::string_view name);

} // namespace rpg::infrastructure
