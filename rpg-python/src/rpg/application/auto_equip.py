"""Auto-equip with auto-sell (docs/game-design.md §8.1). Consumes no randomness."""

from rpg.application.events import Event, event
from rpg.application.loot import can_use
from rpg.application.run_state import RunState
from rpg.domain.character import build_sheet, item_score, item_value, required_level
from rpg.domain.definitions import GameData
from rpg.domain.entities import ItemInstance
from rpg.domain.enums import EQUIPMENT_SLOT_ORDER, Slot


def best_bag_item(state: RunState, data: GameData, slot: Slot) -> ItemInstance | None:
	"""Highest-score bag item the player can wear in `slot` now; ties go to the lowest uid."""
	player = state.player
	vocation = data.vocation(player.vocation_id)
	candidates = [
		item
		for item in player.bag
		if data.item(item.item_id).slot is slot
		and can_use(data.item(item.item_id), vocation)
		and required_level(item, data) <= player.level
	]
	if not candidates:
		return None
	return min(candidates, key=lambda item: (-item_score(item, data), item.uid))


def auto_equip(state: RunState, data: GameData) -> list[Event]:
	player = state.player
	events: list[Event] = []
	for slot in EQUIPMENT_SLOT_ORDER:
		best = best_bag_item(state, data, slot)
		if best is None:
			continue
		score = item_score(best, data)
		current = player.equipment.get(slot)
		if current is not None and score <= item_score(current, data):
			continue
		player.bag.remove(best)
		player.equipment[slot] = best
		events.append(event("item_auto_equipped", uid=best.uid, itemId=best.item_id, slot=slot.value, score=score))
		if current is not None:
			gold = item_value(current, data)
			player.gold += gold
			events.append(event("item_auto_sold", uid=current.uid, itemId=current.item_id, gold=gold))
	if events:
		sheet = build_sheet(player, data)
		player.hp = min(player.hp, sheet.max_hp)
		player.mp = min(player.mp, sheet.max_mp)
	return events
