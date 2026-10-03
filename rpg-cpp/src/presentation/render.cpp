#include "presentation/render.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace rpg::presentation {

namespace {

constexpr std::string_view list_keys = "123456789abcdefghijklmnopqrstuvwxyz";

constexpr std::array<std::pair<std::string_view, std::string_view>, 7> element_colors{{
    {"physical", "#ffffff"},
    {"fire", "#ff5f5f"},
    {"ice", "#5fd7ff"},
    {"energy", "#d75fff"},
    {"earth", "#5fd75f"},
    {"holy", "#ffd75f"},
    {"death", "#8a8a8a"},
}};

constexpr std::array<std::pair<std::string_view, std::string_view>, 4> rarity_colors{{
    {"common", "#ffffff"},
    {"rare", "#1e90ff"},
    {"legendary", "#ffaf00"},
    {"mythic", "#af87ff"},
}};

constexpr std::array<std::pair<std::string_view, std::string_view>, 4> style_colors{{
    {style_warning, "#ffd75f"},
    {style_gain, "#5fd75f"},
    {style_loss, "#ff5f5f"},
    {style_dim, "#8a8a8a"},
}};

template <std::size_t Size>
std::string_view lookup(
    const std::array<std::pair<std::string_view, std::string_view>, Size>& table, std::string_view key) {
	const auto found = std::ranges::find(table, key, &std::pair<std::string_view, std::string_view>::first);
	return found == table.end() ? std::string_view{} : found->second;
}

std::string repeat(std::string_view cell, std::int64_t count) {
	std::string result;
	for (std::int64_t i = 0; i < count; ++i) {
		result += cell;
	}
	return result;
}

} // namespace

std::string_view element_color(std::string_view element) { return lookup(element_colors, element); }

std::string_view rarity_color(std::string_view rarity) { return lookup(rarity_colors, rarity); }

std::string_view style_color(std::string_view style) { return lookup(style_colors, style); }

std::string format_delta(std::int64_t delta) { return delta > 0 ? "+" + std::to_string(delta) : std::to_string(delta); }

std::string_view delta_style(std::int64_t delta) {
	if (delta > 0) {
		return style_gain;
	}
	if (delta < 0) {
		return style_loss;
	}
	return {};
}

std::string bar(std::int64_t current, std::int64_t maximum, std::int64_t width) {
	if (maximum <= 0) {
		return repeat("░", width);
	}
	std::int64_t filled = width * std::max<std::int64_t>(0, std::min(current, maximum)) / maximum;
	if (current > 0) {
		filled = std::max<std::int64_t>(1, filled);
	}
	return repeat("█", filled) + repeat("░", width - filled);
}

std::string_view hp_color(std::int64_t current, std::int64_t maximum) {
	if (maximum > 0 && current * 100 > maximum * 50) {
		return "#5fd75f";
	}
	if (maximum > 0 && current * 100 > maximum * 25) {
		return "#ffd75f";
	}
	return "#ff5f5f";
}

std::string list_key(std::size_t index) { return std::string(1, list_keys.at(index)); }

std::optional<std::size_t> list_index(std::string_view key) {
	if (key.size() != 1) {
		return std::nullopt;
	}
	const std::size_t position = list_keys.find(key.front());
	return position == std::string_view::npos ? std::nullopt : std::optional{position};
}

std::size_t utf8_length(std::string_view text) {
	// Counts every byte that is not a continuation byte (10xxxxxx).
	return static_cast<std::size_t>(
	    std::ranges::count_if(text, [](char byte) { return (static_cast<unsigned char>(byte) & 0xC0U) != 0x80U; }));
}

std::string utf8_prefix(std::string_view text, std::size_t count) {
	std::size_t seen = 0;
	for (std::size_t i = 0; i < text.size(); ++i) {
		if ((static_cast<unsigned char>(text[i]) & 0xC0U) != 0x80U) {
			if (seen == count) {
				return std::string(text.substr(0, i));
			}
			seen += 1;
		}
	}
	return std::string(text);
}

void utf8_pop_back(std::string& text) {
	while (!text.empty()) {
		const auto byte = static_cast<unsigned char>(text.back());
		text.pop_back();
		if ((byte & 0xC0U) != 0x80U) {
			return;
		}
	}
}

} // namespace rpg::presentation
