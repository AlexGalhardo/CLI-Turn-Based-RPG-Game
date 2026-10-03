#pragma once

#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace rpg::application {

// An event field: events only carry integers, strings and booleans (docs/cross-language-parity.md §3).
using EventValue = std::variant<std::int64_t, std::string, bool>;

// A flat engine event: a type plus named fields, compared structurally (like the golden JSON objects).
struct Event {
	std::string type;
	std::map<std::string, EventValue, std::less<>> fields;

	Event() = default;
	explicit Event(std::string event_type) : type(std::move(event_type)) {}
	Event(std::string event_type, std::initializer_list<std::pair<const std::string, EventValue>> values) :
	    type(std::move(event_type)), fields(values) {}

	// Typed accessors: a missing field (or one of another type) reads as 0 / "" / false.
	[[nodiscard]] std::int64_t integer(std::string_view field) const;
	[[nodiscard]] std::string text(std::string_view field) const;
	[[nodiscard]] bool flag(std::string_view field) const;
	[[nodiscard]] bool has(std::string_view field) const { return fields.contains(field); }

	bool operator==(const Event&) const = default;
};

// Builds an `error` event.
Event error_event(std::string_view code);

// Error codes of `error` events.
namespace error_code {
inline constexpr std::string_view not_enough_mana = "not_enough_mana";
inline constexpr std::string_view not_enough_gold = "not_enough_gold";
inline constexpr std::string_view no_potion = "no_potion";
inline constexpr std::string_view unknown_spell = "unknown_spell";
inline constexpr std::string_view unknown_potion = "unknown_potion";
inline constexpr std::string_view potion_locked = "potion_locked";
inline constexpr std::string_view invalid_phase = "invalid_phase";
inline constexpr std::string_view invalid_quantity = "invalid_quantity";
inline constexpr std::string_view bag_full = "bag_full";
inline constexpr std::string_view cannot_equip = "cannot_equip";
inline constexpr std::string_view invalid_item = "invalid_item";
} // namespace error_code

} // namespace rpg::application
