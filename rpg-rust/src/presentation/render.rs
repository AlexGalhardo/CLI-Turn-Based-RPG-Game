//! Framework-independent rendering helpers (bars, colours, list keys) — specified in docs/tui.md.

use crate::domain::enums::Element;

pub const BAR_WIDTH: usize = 25;
pub const MIN_COLUMNS: u16 = 100;
pub const MIN_ROWS: u16 = 30;
pub const LIST_KEYS: &str = "123456789abcdefghijklmnopqrstuvwxyz";

/// Colour names of the reference (Rich names); the TUI maps them to terminal colours.
pub fn element_color(element: Element) -> &'static str {
	match element {
		Element::Physical => "white",
		Element::Fire => "red",
		Element::Ice => "cyan",
		Element::Energy => "magenta",
		Element::Earth => "green",
		Element::Holy => "yellow",
		Element::Death => "bright_black",
	}
}

pub fn rarity_color(rarity: &str) -> Option<&'static str> {
	match rarity {
		"common" => Some("white"),
		"rare" => Some("dodger_blue1"),
		"legendary" => Some("orange1"),
		"mythic" => Some("medium_purple1"),
		_ => None,
	}
}

// Semantic colours used by the equipment screen (docs/tui.md): empty slots, score/stat gains and losses.
pub const STYLE_WARNING: &str = "warning";
pub const STYLE_GAIN: &str = "gain";
pub const STYLE_LOSS: &str = "loss";
pub const STYLE_DIM: &str = "dim";

pub fn style_color(style: &str) -> Option<&'static str> {
	match style {
		STYLE_WARNING => Some("yellow"),
		STYLE_GAIN => Some("green"),
		STYLE_LOSS => Some("red"),
		STYLE_DIM => Some("bright_black"),
		_ => None,
	}
}

pub fn format_delta(delta: i64) -> String {
	if delta > 0 { format!("+{delta}") } else { delta.to_string() }
}

pub fn delta_style(delta: i64) -> Option<&'static str> {
	match delta.signum() {
		1 => Some(STYLE_GAIN),
		-1 => Some(STYLE_LOSS),
		_ => None,
	}
}

/// `█` filled / `░` empty. A living creature always shows at least one filled cell.
pub fn bar(current: i64, maximum: i64, width: usize) -> String {
	if maximum <= 0 {
		return "░".repeat(width);
	}
	let mut filled = (width as i64 * current.clamp(0, maximum) / maximum) as usize;
	if current > 0 {
		filled = filled.max(1);
	}
	"█".repeat(filled) + &"░".repeat(width - filled)
}

pub fn hp_color(current: i64, maximum: i64) -> &'static str {
	if maximum > 0 && current * 100 > maximum * 50 {
		return "green";
	}
	if maximum > 0 && current * 100 > maximum * 25 {
		return "yellow";
	}
	"red"
}

pub fn list_key(index: usize) -> String {
	LIST_KEYS[index..=index].to_owned()
}

pub fn list_index(key: &str) -> Option<usize> {
	if key.chars().count() != 1 {
		return None;
	}
	LIST_KEYS.find(key)
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn render_helpers() {
		assert_eq!(bar(0, 100, 10), "░".repeat(10));
		assert_eq!(bar(1, 100, 10), "█".to_owned() + &"░".repeat(9));
		assert_eq!(bar(100, 100, 10), "█".repeat(10));
		assert_eq!(bar(5, 0, 4), "░".repeat(4));
		assert_eq!(hp_color(60, 100), "green");
		assert_eq!(hp_color(30, 100), "yellow");
		assert_eq!(hp_color(10, 100), "red");
		assert_eq!(list_key(0), "1");
		assert_eq!(list_key(9), "a");
		assert_eq!(list_index("a"), Some(9));
		assert_eq!(list_index("!"), None);
		assert_eq!(list_index("ab"), None);
		assert_eq!(element_color(Element::Death), "bright_black");
		assert_eq!(rarity_color("legendary"), Some("orange1"));
		assert_eq!(rarity_color("fire"), None);
		assert_eq!(rarity_color("mythic"), Some("medium_purple1"));
		assert_eq!(style_color(STYLE_WARNING), Some("yellow"));
		assert_eq!(style_color(STYLE_DIM), Some("bright_black"));
		assert_eq!(style_color("fire"), None);
		assert_eq!(format_delta(5), "+5");
		assert_eq!(format_delta(-3), "-3");
		assert_eq!(format_delta(0), "0");
		assert_eq!(delta_style(2), Some(STYLE_GAIN));
		assert_eq!(delta_style(-2), Some(STYLE_LOSS));
		assert_eq!(delta_style(0), None);
	}
}
