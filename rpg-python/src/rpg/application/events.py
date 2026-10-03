"""Engine events (docs/cross-language-parity.md §3). Flat JSON objects compared across implementations."""

type EventValue = int | str | bool
type Event = dict[str, EventValue]


def event(event_type: str, **fields: EventValue) -> Event:
	return {"type": event_type, **fields}


def error(code: str) -> Event:
	return event("error", code=code)


class ErrorCode:
	NOT_ENOUGH_MANA = "not_enough_mana"
	NOT_ENOUGH_GOLD = "not_enough_gold"
	NO_POTION = "no_potion"
	UNKNOWN_SPELL = "unknown_spell"
	UNKNOWN_POTION = "unknown_potion"
	POTION_LOCKED = "potion_locked"
	INVALID_PHASE = "invalid_phase"
	INVALID_QUANTITY = "invalid_quantity"
	BAG_FULL = "bag_full"
	CANNOT_EQUIP = "cannot_equip"
	INVALID_ITEM = "invalid_item"
	LEVEL_TOO_LOW = "level_too_low"
	UNKNOWN_COMMAND = "unknown_command"
