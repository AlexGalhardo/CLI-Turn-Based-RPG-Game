"""Merchant phase: potions, bag, equipment and rotating stock (docs/game-design.md §10)."""

from rpg.application.auto_equip import auto_equip
from rpg.application.commands import BuyPotion, BuyStockItem, Equip, SellItem, Unequip
from rpg.application.events import ErrorCode, Event, error, event
from rpg.application.loot import can_use, generate_item
from rpg.application.run_state import RunState
from rpg.domain.character import build_sheet, item_value, required_level
from rpg.domain.definitions import GameData
from rpg.domain.entities import ItemInstance
from rpg.domain.enums import Slot
from rpg.domain.formulas import pct, round_info
from rpg.domain.rng import Rng

MAX_POTIONS_PER_PURCHASE = 99

type MerchantCommand = BuyPotion | SellItem | Equip | Unequip | BuyStockItem


def stock_price(item: ItemInstance, data: GameData) -> int:
	return pct(item_value(item, data), data.balance.merchant_markup_pct)


def available_potions(state: RunState, data: GameData) -> list[str]:
	next_round = state.round + 1
	return [potion.id for potion in data.potions if potion.unlock_round <= next_round]


class Merchant:
	def __init__(self, data: GameData, rng: Rng, state: RunState) -> None:
		self._data = data
		self._rng = rng
		self._state = state

	def enter(self) -> list[Event]:
		"""Generates the rotating stock for the tier of the next round."""
		state = self._state
		vocation = self._data.vocation(state.player.vocation_id)
		tier = round_info(state.round + 1, self._data.balance, self._data.tier_count).tier
		state.merchant_stock = []
		for _ in range(self._data.balance.merchant_stock_size):
			item = generate_item(
				self._data,
				self._rng,
				vocation=vocation,
				tier=tier,
				weights=self._data.balance.rarity_weights["merchant"],
				uid=state.next_item_uid,
			)
			if item is not None:
				state.take_item_uid()
				state.merchant_stock.append(item)
		return [event("merchant_entered", round=state.round)]

	def handle(self, command: MerchantCommand) -> list[Event]:
		match command:
			case BuyPotion(potion_id, quantity):
				return self._buy_potion(potion_id, quantity)
			case SellItem(uid):
				return self._sell(uid)
			case Equip(uid):
				return self._equip(uid)
			case Unequip(slot):
				return self._unequip(slot)
			case BuyStockItem(index):
				return self._buy_stock(index)

	def _buy_potion(self, potion_id: str, quantity: int) -> list[Event]:
		player = self._state.player
		if potion_id not in {potion.id for potion in self._data.potions}:
			return [error(ErrorCode.UNKNOWN_POTION)]
		if potion_id not in available_potions(self._state, self._data):
			return [error(ErrorCode.POTION_LOCKED)]
		if not 1 <= quantity <= MAX_POTIONS_PER_PURCHASE:
			return [error(ErrorCode.INVALID_QUANTITY)]
		cost = self._data.potion(potion_id).price * quantity
		if player.gold < cost:
			return [error(ErrorCode.NOT_ENOUGH_GOLD)]
		player.gold -= cost
		player.potions[potion_id] = player.potion_count(potion_id) + quantity
		return [event("potion_bought", potionId=potion_id, quantity=quantity, gold=cost)]

	def _find_in_bag(self, uid: int) -> ItemInstance | None:
		return next((item for item in self._state.player.bag if item.uid == uid), None)

	def _sell(self, uid: int) -> list[Event]:
		item = self._find_in_bag(uid)
		if item is None:
			return [error(ErrorCode.INVALID_ITEM)]
		value = item_value(item, self._data)
		self._state.player.bag.remove(item)
		self._state.player.gold += value
		return [event("item_sold", uid=uid, itemId=item.item_id, gold=value)]

	def _equip(self, uid: int) -> list[Event]:
		player = self._state.player
		item = self._find_in_bag(uid)
		if item is None:
			return [error(ErrorCode.INVALID_ITEM)]
		definition = self._data.item(item.item_id)
		if not can_use(definition, self._data.vocation(player.vocation_id)):
			return [error(ErrorCode.CANNOT_EQUIP)]
		if required_level(item, self._data) > player.level:
			return [error(ErrorCode.LEVEL_TOO_LOW)]
		events: list[Event] = []
		player.bag.remove(item)
		previous = player.equipment.pop(definition.slot, None)
		if previous is not None:
			player.bag.append(previous)
			events.append(
				event("item_unequipped", uid=previous.uid, itemId=previous.item_id, slot=definition.slot.value)
			)
		player.equipment[definition.slot] = item
		events.append(event("item_equipped", uid=item.uid, itemId=item.item_id, slot=definition.slot.value))
		self._clamp_resources()
		return events

	def _unequip(self, slot: Slot) -> list[Event]:
		player = self._state.player
		item = player.equipment.get(slot)
		if item is None:
			return [error(ErrorCode.INVALID_ITEM)]
		if len(player.bag) >= self._data.balance.bag_capacity:
			return [error(ErrorCode.BAG_FULL)]
		del player.equipment[slot]
		player.bag.append(item)
		self._clamp_resources()
		return [event("item_unequipped", uid=item.uid, itemId=item.item_id, slot=slot.value)]

	def _buy_stock(self, index: int) -> list[Event]:
		state = self._state
		if not 0 <= index < len(state.merchant_stock):
			return [error(ErrorCode.INVALID_ITEM)]
		if len(state.player.bag) >= self._data.balance.bag_capacity:
			return [error(ErrorCode.BAG_FULL)]
		item = state.merchant_stock[index]
		price = stock_price(item, self._data)
		if state.player.gold < price:
			return [error(ErrorCode.NOT_ENOUGH_GOLD)]
		state.player.gold -= price
		state.merchant_stock.pop(index)
		state.player.bag.append(item)
		events = [event("item_bought", uid=item.uid, itemId=item.item_id, gold=price)]
		if state.config.auto_equip:
			events.extend(auto_equip(state, self._data))
		return events

	def _clamp_resources(self) -> None:
		player = self._state.player
		sheet = build_sheet(player, self._data)
		player.hp = min(player.hp, sheet.max_hp)
		player.mp = min(player.mp, sheet.max_mp)
