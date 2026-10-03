"""Player commands. Each maps 1:1 to a JSON object used by golden files (`{"type": "cast", "spellId": "…"}`)."""

from dataclasses import dataclass

from rpg.domain.enums import Slot
from rpg.domain.json_types import JsonObject, JsonValue, json_int, json_obj, json_str


@dataclass(frozen=True, slots=True)
class Attack:
	pass


@dataclass(frozen=True, slots=True)
class Cast:
	spell_id: str


@dataclass(frozen=True, slots=True)
class UsePotion:
	potion_id: str


@dataclass(frozen=True, slots=True)
class Defend:
	pass


@dataclass(frozen=True, slots=True)
class NextFight:
	pass


@dataclass(frozen=True, slots=True)
class BuyPotion:
	potion_id: str
	quantity: int


@dataclass(frozen=True, slots=True)
class SellItem:
	uid: int


@dataclass(frozen=True, slots=True)
class Equip:
	uid: int


@dataclass(frozen=True, slots=True)
class Unequip:
	slot: Slot


@dataclass(frozen=True, slots=True)
class BuyStockItem:
	index: int


@dataclass(frozen=True, slots=True)
class EndRun:
	"""Victory phase: close the won run (history + Hall of Fame)."""


@dataclass(frozen=True, slots=True)
class ContinueRun:
	"""Victory phase: keep playing endlessly after beating the final boss."""


type Command = (
	Attack
	| Cast
	| UsePotion
	| Defend
	| NextFight
	| BuyPotion
	| SellItem
	| Equip
	| Unequip
	| BuyStockItem
	| EndRun
	| ContinueRun
)


def command_to_dict(command: Command) -> JsonObject:
	match command:
		case Attack():
			return {"type": "attack"}
		case Cast(spell_id):
			return {"type": "cast", "spellId": spell_id}
		case UsePotion(potion_id):
			return {"type": "potion", "potionId": potion_id}
		case Defend():
			return {"type": "defend"}
		case NextFight():
			return {"type": "next_fight"}
		case BuyPotion(potion_id, quantity):
			return {"type": "buy_potion", "potionId": potion_id, "quantity": quantity}
		case SellItem(uid):
			return {"type": "sell_item", "uid": uid}
		case Equip(uid):
			return {"type": "equip", "uid": uid}
		case Unequip(slot):
			return {"type": "unequip", "slot": slot.value}
		case BuyStockItem(index):
			return {"type": "buy_stock_item", "index": index}
		case EndRun():
			return {"type": "end_run"}
		case ContinueRun():
			return {"type": "continue_run"}


def command_from_dict(raw: JsonValue) -> Command:
	data = json_obj(raw)
	match json_str(data["type"]):
		case "attack":
			return Attack()
		case "cast":
			return Cast(json_str(data["spellId"]))
		case "potion":
			return UsePotion(json_str(data["potionId"]))
		case "defend":
			return Defend()
		case "next_fight":
			return NextFight()
		case "buy_potion":
			return BuyPotion(json_str(data["potionId"]), json_int(data["quantity"]))
		case "sell_item":
			return SellItem(json_int(data["uid"]))
		case "equip":
			return Equip(json_int(data["uid"]))
		case "unequip":
			return Unequip(Slot(json_str(data["slot"])))
		case "buy_stock_item":
			return BuyStockItem(json_int(data["index"]))
		case "end_run":
			return EndRun()
		case "continue_run":
			return ContinueRun()
		case other:
			raise ValueError(f"unknown command type: {other}")
