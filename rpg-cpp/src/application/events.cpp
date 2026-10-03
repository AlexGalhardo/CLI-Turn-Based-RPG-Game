#include "application/events.hpp"

namespace rpg::application {

namespace {

template <typename T> const T* field_as(const Event& event, std::string_view field) {
	const auto found = event.fields.find(field);
	return found == event.fields.end() ? nullptr : std::get_if<T>(&found->second);
}

} // namespace

std::int64_t Event::integer(std::string_view field) const {
	const auto* value = field_as<std::int64_t>(*this, field);
	return value == nullptr ? 0 : *value;
}

std::string Event::text(std::string_view field) const {
	const auto* value = field_as<std::string>(*this, field);
	return value == nullptr ? std::string{} : *value;
}

bool Event::flag(std::string_view field) const {
	const auto* value = field_as<bool>(*this, field);
	return value != nullptr && *value;
}

Event error_event(std::string_view code) { return Event{"error", {{"code", std::string(code)}}}; }

} // namespace rpg::application
