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

// Semantic colours used by the equipment screen (docs/tui.md): empty slots, score/stat gains and losses.
inline constexpr std::string_view style_warning = "warning";
inline constexpr std::string_view style_gain = "gain";
inline constexpr std::string_view style_loss = "loss";
inline constexpr std::string_view style_dim = "dim";

// The colour of a semantic style ("" when unknown).
std::string_view style_color(std::string_view style);

// "+N" for a gain, "-N" for a loss, "0" otherwise.
std::string format_delta(std::int64_t delta);

// style_gain, style_loss or "" (no colour) for a zero delta.
std::string_view delta_style(std::int64_t delta);

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
// The first `count` code points of a UTF-8 string.
std::string utf8_prefix(std::string_view text, std::size_t count);

} // namespace rpg::presentation
