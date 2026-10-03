"""Deterministic run counters, derived only from engine events (docs/game-design.md §11)."""

from collections import Counter
from dataclasses import dataclass, field

from rpg.application.events import Event
from rpg.domain.json_types import JsonObject, JsonValue, json_int, json_list, json_obj, json_str


def _counter_from(raw: JsonValue) -> Counter[str]:
	return Counter({key: json_int(value) for key, value in json_obj(raw).items()})


def _counter_to(counter: Counter[str]) -> JsonObject:
	return {key: counter[key] for key in sorted(counter)}


def _int_field(event: Event, name: str) -> int:
	value = event[name]
	if isinstance(value, bool) or not isinstance(value, int):
		raise TypeError(f"event field {name} must be int")
	return value


def _str_field(event: Event, name: str) -> str:
	value = event[name]
	if not isinstance(value, str):
		raise TypeError(f"event field {name} must be str")
	return value


@dataclass(slots=True)
class DroppedItem:
	item_id: str
	rarity: str
	round: int


@dataclass(slots=True)
class RunStatistics:
	damage_dealt: int = 0
	damage_taken: int = 0
	healing_done: int = 0
	highest_hit: int = 0
	normal_attacks: int = 0
	crits: int = 0
	dodges: int = 0
	parries: int = 0
	defends: int = 0
	gold_looted: int = 0
	gold_spent: int = 0
	gold_earned: int = 0
	items_sold: int = 0
	items_auto_equipped: int = 0
	bosses_killed: int = 0
	elites_killed: int = 0
	spells_cast: Counter[str] = field(default_factory=Counter)
	potions_used: Counter[str] = field(default_factory=Counter)
	potions_bought: Counter[str] = field(default_factory=Counter)
	potions_dropped: Counter[str] = field(default_factory=Counter)
	items_dropped: Counter[str] = field(default_factory=Counter)
	kills: Counter[str] = field(default_factory=Counter)
	statuses_applied: Counter[str] = field(default_factory=Counter)
	dropped_items: list[DroppedItem] = field(default_factory=list)

	def record(self, events: list[Event], current_round: int) -> None:
		for event in events:
			if not self._record_combat(event):
				self._record_loot(event, current_round)

	def _record_combat(self, event: Event) -> bool:
		"""Battle counters; returns False when the event is not a battle event."""
		match event["type"]:
			case "player_attacked":
				self.normal_attacks += 1
				self._dealt(event)
			case "spell_cast":
				self.spells_cast[_str_field(event, "spellId")] += 1
				self._dealt(event)
			case "spell_healed":
				self.spells_cast[_str_field(event, "spellId")] += 1
				self.healing_done += _int_field(event, "amount")
			case "potion_used":
				self.potions_used[_str_field(event, "potionId")] += 1
				if event["resource"] == "hp":
					self.healing_done += _int_field(event, "amount")
			case "player_defended":
				self.defends += 1
			case "monster_attacked":
				self.damage_taken += _int_field(event, "damage")
			case "attack_dodged":
				self.dodges += 1
			case "attack_parried":
				self.parries += 1
				self.damage_dealt += _int_field(event, "reflected")
			case "monster_parried":
				self.damage_taken += _int_field(event, "reflected")
			case "status_ticked":
				if event["target"] == "player":
					self.damage_taken += _int_field(event, "damage")
				else:
					self.damage_dealt += _int_field(event, "damage")
			case "status_applied":
				if event["target"] == "monster":
					self.statuses_applied[_str_field(event, "status")] += 1
			case "monster_killed":
				self.kills[_str_field(event, "monsterId")] += 1
				if event["isBoss"] is True:
					self.bosses_killed += 1
				if event["enemyClass"] == "elite":
					self.elites_killed += 1
			case _:
				return False
		return True

	def _record_loot(self, event: Event, current_round: int) -> None:
		match event["type"]:
			case "gold_looted":
				self.gold_looted += _int_field(event, "amount")
			case "item_dropped":
				rarity = _str_field(event, "rarity")
				self.items_dropped[rarity] += 1
				self.dropped_items.append(DroppedItem(_str_field(event, "itemId"), rarity, current_round))
			case "potion_bought":
				self.potions_bought[_str_field(event, "potionId")] += _int_field(event, "quantity")
				self.gold_spent += _int_field(event, "gold")
			case "item_bought":
				self.gold_spent += _int_field(event, "gold")
			case "potion_dropped":
				self.potions_dropped[_str_field(event, "potionId")] += 1
			case "item_auto_equipped":
				self.items_auto_equipped += 1
			case "item_sold" | "item_auto_sold":
				self.items_sold += 1
				self.gold_earned += _int_field(event, "gold")
			case _:
				pass

	def _dealt(self, event: Event) -> None:
		damage = _int_field(event, "damage")
		self.damage_dealt += damage
		self.highest_hit = max(self.highest_hit, damage)
		if event["crit"] is True:
			self.crits += 1

	def to_dict(self) -> JsonObject:
		return {
			"damageDealt": self.damage_dealt,
			"damageTaken": self.damage_taken,
			"healingDone": self.healing_done,
			"highestHit": self.highest_hit,
			"normalAttacks": self.normal_attacks,
			"crits": self.crits,
			"dodges": self.dodges,
			"parries": self.parries,
			"defends": self.defends,
			"goldLooted": self.gold_looted,
			"goldSpent": self.gold_spent,
			"goldEarned": self.gold_earned,
			"itemsSold": self.items_sold,
			"itemsAutoEquipped": self.items_auto_equipped,
			"bossesKilled": self.bosses_killed,
			"elitesKilled": self.elites_killed,
			"spellsCast": _counter_to(self.spells_cast),
			"potionsUsed": _counter_to(self.potions_used),
			"potionsBought": _counter_to(self.potions_bought),
			"potionsDropped": _counter_to(self.potions_dropped),
			"itemsDropped": _counter_to(self.items_dropped),
			"kills": _counter_to(self.kills),
			"statusesApplied": _counter_to(self.statuses_applied),
			"droppedItems": [
				{"itemId": item.item_id, "rarity": item.rarity, "round": item.round} for item in self.dropped_items
			],
		}

	@staticmethod
	def from_dict(raw: JsonValue) -> RunStatistics:
		data = json_obj(raw)
		return RunStatistics(
			damage_dealt=json_int(data["damageDealt"]),
			damage_taken=json_int(data["damageTaken"]),
			healing_done=json_int(data["healingDone"]),
			highest_hit=json_int(data["highestHit"]),
			normal_attacks=json_int(data["normalAttacks"]),
			crits=json_int(data["crits"]),
			dodges=json_int(data["dodges"]),
			parries=json_int(data["parries"]),
			defends=json_int(data["defends"]),
			gold_looted=json_int(data["goldLooted"]),
			gold_spent=json_int(data["goldSpent"]),
			gold_earned=json_int(data["goldEarned"]),
			items_sold=json_int(data["itemsSold"]),
			items_auto_equipped=json_int(data["itemsAutoEquipped"]),
			bosses_killed=json_int(data["bossesKilled"]),
			elites_killed=json_int(data["elitesKilled"]),
			spells_cast=_counter_from(data["spellsCast"]),
			potions_used=_counter_from(data["potionsUsed"]),
			potions_bought=_counter_from(data["potionsBought"]),
			potions_dropped=_counter_from(data["potionsDropped"]),
			items_dropped=_counter_from(data["itemsDropped"]),
			kills=_counter_from(data["kills"]),
			statuses_applied=_counter_from(data["statusesApplied"]),
			dropped_items=[
				DroppedItem(json_str(i["itemId"]), json_str(i["rarity"]), json_int(i["round"]))
				for i in (json_obj(entry) for entry in json_list(data["droppedItems"]))
			],
		)
