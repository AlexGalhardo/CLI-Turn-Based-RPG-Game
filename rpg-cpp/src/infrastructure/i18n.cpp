#include "infrastructure/i18n.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace rpg::infrastructure {

namespace {

std::map<std::string, std::string, std::less<>> load_locale(const assets::SharedFs& shared, std::string_view locale) {
	const std::string path = "i18n/" + std::string(locale) + ".json";
	const auto found = shared.find(path);
	if (found == shared.end()) {
		throw std::invalid_argument("missing locale file: " + path);
	}
	std::map<std::string, std::string, std::less<>> messages;
	const nlohmann::json document = nlohmann::json::parse(found->second);
	for (const auto& [key, value] : document.items()) {
		if (value.is_string()) {
			messages.emplace(key, value.get<std::string>());
		}
	}
	return messages;
}

// The `\w` of the reference's `\{(\w+)\}` placeholder pattern (keys are ASCII).
bool is_word(char character) { return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_'; }

} // namespace

bool is_supported_locale(std::string_view locale) { return std::ranges::contains(supported_locales, locale); }

Translator::Translator(const assets::SharedFs& shared, std::string_view locale) : locale_(locale) {
	if (!is_supported_locale(locale)) {
		throw std::invalid_argument("unsupported locale: " + std::string(locale));
	}
	fallback_ = load_locale(shared, default_locale);
	messages_ = locale == default_locale ? fallback_ : load_locale(shared, locale);
}

bool Translator::has(std::string_view key) const { return messages_.contains(key) || fallback_.contains(key); }

std::string Translator::t(std::string_view key, const Params& params) const {
	std::string_view template_text = key;
	if (const auto found = messages_.find(key); found != messages_.end() && !found->second.empty()) {
		template_text = found->second;
	} else if (const auto fallback = fallback_.find(key); fallback != fallback_.end() && !fallback->second.empty()) {
		template_text = fallback->second;
	}

	// A hand-written scan instead of std::regex: placeholders are simple and this keeps rendering cheap.
	std::string result;
	result.reserve(template_text.size());
	std::size_t index = 0;
	while (index < template_text.size()) {
		const char current = template_text[index];
		if (current == '{') {
			std::size_t end = index + 1;
			while (end < template_text.size() && is_word(template_text[end])) {
				++end;
			}
			if (end < template_text.size() && template_text[end] == '}' && end > index + 1) {
				const std::string_view name = template_text.substr(index + 1, end - index - 1);
				if (const auto param = params.find(name); param != params.end()) {
					result += param->second.text();
				} else {
					result += template_text.substr(index, end - index + 1);
				}
				index = end + 1;
				continue;
			}
		}
		result += current;
		++index;
	}
	return result;
}

} // namespace rpg::infrastructure
