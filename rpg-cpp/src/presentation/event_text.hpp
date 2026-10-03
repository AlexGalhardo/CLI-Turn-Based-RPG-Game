#pragma once

#include <string>

#include "application/events.hpp"
#include "application/run_state.hpp"
#include "domain/definitions.hpp"
#include "infrastructure/i18n.hpp"

namespace rpg::presentation {

// Turns engine events into translated sentences.
class EventFormatter {
public:
	EventFormatter(const domain::GameData& data, const infrastructure::Translator& translator) :
	    data_(&data), translator_(&translator) {}

	[[nodiscard]] std::string format(const application::Event& event, const application::RunState& state) const;

private:
	const domain::GameData* data_;
	const infrastructure::Translator* translator_;

	[[nodiscard]] std::string display_name(std::string_view field, const std::string& identifier) const;
};

} // namespace rpg::presentation
