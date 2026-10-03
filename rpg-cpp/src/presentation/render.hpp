#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rpg::presentation {

// Layout constants from docs/tui.md.
inline constexpr std::int64_t bar_width = 25;
inline constexpr int min_columns = 100;
inline constexpr int min_rows = 30;

// The colour of an element, as hex so every renderer matches ("" when unknown).
std::string_view element_color(std::string_view element);

// The colour of an item rarity ("" when unknown).
std::string_view rarity_color(std::string_view rarity);

// `█` filled / `░` empty cells. A living creature always shows at least one filled cell.
std::string bar(std::int64_t current, std::int64_t maximum, std::int64_t width);

// Green above 50%, yellow above 25%, red otherwise.
std::string_view hp_color(std::int64_t current, std::int64_t maximum);

// The key of a list entry: 1-9 then a-z.
std::string list_key(std::size_t index);

// The inverse of list_key (std::nullopt when the key is not a list key).
std::optional<std::size_t> list_index(std::string_view key);

// UTF-8 helpers for text input (a name may contain accented letters).
std::size_t utf8_length(std::string_view text);
void utf8_pop_back(std::string& text);

} // namespace rpg::presentation
