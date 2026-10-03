#include "domain/enums.hpp"

#include <cctype>
#include <stdexcept>

namespace rpg::domain {

std::string protection_stat(std::string_view element) {
	std::string result = "prot";
	result += element;
	if (result.size() > 4) {
		result[4] = static_cast<char>(std::toupper(static_cast<unsigned char>(result[4])));
	}
	return result;
}

std::string_view to_string(Phase phase) {
	switch (phase) {
	case Phase::merchant:
		return "merchant";
	case Phase::battle:
		return "battle";
	case Phase::victory:
		return "victory";
	case Phase::game_over:
		return "game_over";
	}
	throw std::logic_error("unknown phase");
}

Phase phase_from_string(std::string_view text) {
	if (text == "merchant") {
		return Phase::merchant;
	}
	if (text == "battle") {
		return Phase::battle;
	}
	if (text == "victory") {
		return Phase::victory;
	}
	if (text == "game_over") {
		return Phase::game_over;
	}
	throw std::invalid_argument("unknown phase: " + std::string(text));
}

} // namespace rpg::domain
