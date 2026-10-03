#pragma once

#include <stdexcept>

#include "assets/shared_files.hpp"
#include "domain/definitions.hpp"

namespace rpg::infrastructure {

// Every data loading error (missing file, malformed JSON, missing or mistyped field).
class DataError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

// Parses shared/data (untrusted JSON, validated by schemas in CI) into domain definitions.
domain::GameData load_game_data(const assets::SharedFs& shared);

} // namespace rpg::infrastructure
