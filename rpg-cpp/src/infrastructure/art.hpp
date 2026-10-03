#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "assets/shared_files.hpp"
#include "domain/definitions.hpp"

namespace rpg::infrastructure {

// One ASCII art frame (its lines).
using Frame = std::vector<std::string>;
// Animation name (idle, attack, hurt) → frames.
using Animations = std::map<std::string, std::vector<Frame>, std::less<>>;

class ArtFormatError : public std::invalid_argument {
public:
	using std::invalid_argument::invalid_argument;
};

// Parses `@animation` headers with frames separated by `%%` (docs/data-format.md).
Animations parse_art(std::string_view text);

// The frame of an animation at a tick, falling back to idle; empty when there is no art.
Frame frame_for(const Animations& animations, std::string_view animation, std::int64_t tick);

// Loads and caches art from the shared tree.
class ArtLibrary {
public:
	explicit ArtLibrary(const assets::SharedFs& shared) : shared_(&shared) {}

	// The family art, or the boss's own art.
	const Animations& for_creature(const domain::MonsterDef& creature);

	// The animations of art/<folder>/<name>.txt (empty when missing or malformed).
	const Animations& load_file(std::string_view folder, std::string_view name);

private:
	const assets::SharedFs* shared_;
	std::map<std::string, Animations, std::less<>> cache_;
};

} // namespace rpg::infrastructure
