#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include "assets/shared_files.hpp"

namespace rpg::infrastructure {

// English is the default and the fallback for missing keys.
inline constexpr std::string_view default_locale = "en";
inline constexpr std::array<std::string_view, 2> supported_locales{"en", "pt-BR"};

bool is_supported_locale(std::string_view locale);

// A template parameter already rendered as text: integers, strings and booleans convert implicitly, so call sites read
// like keyword arguments: t("hud.gold", {{"gold", player.gold}}).
class TemplateValue {
public:
	TemplateValue(std::string value) : text_(std::move(value)) {}  // NOLINT(google-explicit-constructor)
	TemplateValue(std::string_view value) : text_(value) {}        // NOLINT(google-explicit-constructor)
	TemplateValue(const char* value) : text_(value) {}             // NOLINT(google-explicit-constructor)
	TemplateValue(bool value) : text_(value ? "true" : "false") {} // NOLINT(google-explicit-constructor)
	template <std::integral Integer>
	    requires(!std::same_as<Integer, bool>)
	TemplateValue(Integer value) : text_(std::to_string(value)) {} // NOLINT(google-explicit-constructor)

	[[nodiscard]] const std::string& text() const { return text_; }

private:
	std::string text_;
};

using Params = std::map<std::string, TemplateValue, std::less<>>;

// Renders flat key → template translations with {placeholder} parameters.
class Translator {
public:
	// Throws std::invalid_argument for an unsupported locale.
	Translator(const assets::SharedFs& shared, std::string_view locale = default_locale);

	[[nodiscard]] const std::string& locale() const { return locale_; }

	// Whether a key exists.
	[[nodiscard]] bool has(std::string_view key) const;

	// Renders a key. Missing keys render as the key itself; missing params keep their {placeholder}.
	[[nodiscard]] std::string t(std::string_view key, const Params& params = {}) const;

private:
	using Messages = std::map<std::string, std::string, std::less<>>;

	std::string locale_;
	Messages fallback_;
	Messages messages_;
};

} // namespace rpg::infrastructure
