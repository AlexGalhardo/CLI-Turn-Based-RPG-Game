"""Framework-independent rendering helpers (bars, colours, list keys) — specified in docs/tui.md."""

from rpg.domain.enums import Element

BAR_WIDTH = 25
MIN_COLUMNS = 100
MIN_ROWS = 30
LIST_KEYS = "123456789abcdefghijklmnopqrstuvwxyz"

ELEMENT_COLORS: dict[str, str] = {
	Element.PHYSICAL: "white",
	Element.FIRE: "red",
	Element.ICE: "cyan",
	Element.ENERGY: "magenta",
	Element.EARTH: "green",
	Element.HOLY: "yellow",
	Element.DEATH: "bright_black",
}

RARITY_COLORS: dict[str, str] = {
	"common": "white",
	"rare": "dodger_blue1",
	"epic": "medium_purple1",
	"legendary": "orange1",
}


def bar(current: int, maximum: int, width: int = BAR_WIDTH) -> str:
	"""`█` filled / `░` empty. A living creature always shows at least one filled cell."""
	if maximum <= 0:
		return "░" * width
	filled = width * max(0, min(current, maximum)) // maximum
	if current > 0:
		filled = max(1, filled)
	return "█" * filled + "░" * (width - filled)


def hp_color(current: int, maximum: int) -> str:
	if maximum > 0 and current * 100 > maximum * 50:
		return "green"
	if maximum > 0 and current * 100 > maximum * 25:
		return "yellow"
	return "red"


def list_key(index: int) -> str:
	return LIST_KEYS[index]


def list_index(key: str) -> int | None:
	position = LIST_KEYS.find(key)
	return None if position < 0 or len(key) != 1 else position
