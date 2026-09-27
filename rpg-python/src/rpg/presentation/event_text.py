"""Turns engine events into translated sentences. Shared by every presentation (text UI and TUI)."""

from rpg.application.events import Event, EventValue
from rpg.application.run_state import RunState
from rpg.domain.definitions import GameData, UnknownIdError
from rpg.infrastructure.i18n import Translator

# Event fields that hold ids: they are replaced by display names before formatting.
_NAME_FIELDS = {"spellId": "spell", "potionId": "potion", "monsterId": "monster", "itemId": "item"}


class EventFormatter:
	def __init__(self, data: GameData, translator: Translator) -> None:
		self._data = data
		self._t = translator

	def format(self, event: Event, state: RunState) -> str:
		params: dict[str, object] = dict(event)
		for field, name in _NAME_FIELDS.items():
			if field in event:
				params[name] = self._display_name(field, event[field])
		for field in ("element", "status", "rarity", "resource"):
			if field in event:
				params[field] = self._t.t(f"{field}.{event[field]}")
		if "monster" not in params and state.monster is not None:
			params["monster"] = self._data.creature(state.monster.creature_id).name
		if "uid" in event and "item" not in params:
			params["item"] = self._item_name_by_uid(event["uid"], state)
		return self._t.t(self._key(event), **params)

	def _key(self, event: Event) -> str:
		event_type = str(event["type"])
		if event_type == "error":
			return f"error.{event['code']}"
		variant = ""
		if event.get("crit") is True:
			variant = "_crit"
		elif event.get("charged") is True:
			variant = "_charged"
		elif event_type == "round_started" and event.get("isBoss") is True:
			variant = "_boss"
		elif "target" in event:
			variant = f"_{event['target']}"
		return f"event.{event_type}{variant}"

	def _display_name(self, field: str, value: EventValue) -> str:
		identifier = str(value)
		try:
			match field:
				case "spellId":
					return self._data.spell(identifier).name
				case "potionId":
					return self._data.potion(identifier).name
				case "monsterId":
					return self._data.creature(identifier).name
				case _:
					return self._data.item(identifier).name
		except UnknownIdError:
			return identifier

	def _item_name_by_uid(self, uid: EventValue, state: RunState) -> str:
		player = state.player
		for item in [*player.bag, *player.equipment.values(), *state.merchant_stock]:
			if item.uid == uid:
				return self._data.item(item.item_id).name
		return f"#{uid}"
