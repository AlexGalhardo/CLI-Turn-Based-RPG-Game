#include "infrastructure/art.hpp"

#include <algorithm>

namespace rpg::infrastructure {

namespace {

std::string_view trim(std::string_view text) {
	const auto first = text.find_first_not_of(" \t");
	if (first == std::string_view::npos) {
		return {};
	}
	const auto last = text.find_last_not_of(" \t");
	return text.substr(first, last - first + 1);
}

} // namespace

Animations parse_art(std::string_view text) {
	while (!text.empty() && text.back() == '\n') {
		text.remove_suffix(1);
	}
	Animations animations;
	std::vector<Frame>* current = nullptr;
	std::size_t start = 0;
	while (start <= text.size()) {
		const std::size_t end = std::min(text.find('\n', start), text.size());
		const std::string_view line = text.substr(start, end - start);
		start = end + 1;

		if (line.starts_with('@')) {
			current = &animations[std::string(trim(line.substr(1)))];
			*current = {Frame{}};
		} else if (line == "%%") {
			if (current == nullptr) {
				throw ArtFormatError("frame separator before any @animation");
			}
			current->emplace_back();
		} else if (current == nullptr) {
			throw ArtFormatError("art content before the first @animation");
		} else {
			current->back().emplace_back(line);
		}
	}
	return animations;
}

Frame frame_for(const Animations& animations, std::string_view animation, std::int64_t tick) {
	auto found = animations.find(animation);
	if (found == animations.end() || found->second.empty()) {
		found = animations.find("idle");
	}
	if (found == animations.end() || found->second.empty()) {
		return {};
	}
	const auto& frames = found->second;
	return frames[static_cast<std::size_t>(tick) % frames.size()];
}

const Animations& ArtLibrary::for_creature(const domain::MonsterDef& creature) {
	if (creature.is_boss) {
		return load_file("bosses", creature.id);
	}
	return load_file("families", creature.family);
}

const Animations& ArtLibrary::load_file(std::string_view folder, std::string_view name) {
	std::string key = std::string(folder) + "/" + std::string(name);
	if (const auto cached = cache_.find(key); cached != cache_.end()) {
		return cached->second;
	}
	Animations animations;
	if (const auto file = shared_->find("art/" + key + ".txt"); file != shared_->end()) {
		try {
			animations = parse_art(file->second);
		} catch (const ArtFormatError&) {
			animations.clear(); // malformed art degrades to "no art", like a missing file
		}
	}
	return cache_.emplace(std::move(key), std::move(animations)).first->second;
}

} // namespace rpg::infrastructure
