"""Mutable run state. Serialised with camelCase keys: the save format is shared by the six implementations."""

from dataclasses import dataclass, field

from rpg.domain.definitions import MonsterAttack, StatusOnHit
from rpg.domain.enums import Element, Slot, Stat
from rpg.domain.json_types import JsonObject, JsonValue, json_bool, json_int, json_list, json_obj, json_str


@dataclass(slots=True)
class ActiveStatus:
	status_id: str
	turns: int
	per_turn: int

	def to_dict(self) -> JsonObject:
		return {"statusId": self.status_id, "turns": self.turns, "perTurn": self.per_turn}

	@staticmethod
	def from_dict(raw: JsonValue) -> ActiveStatus:
		data = json_obj(raw)
		return ActiveStatus(json_str(data["statusId"]), json_int(data["turns"]), json_int(data["perTurn"]))


@dataclass(frozen=True, slots=True)
class AffixRoll:
	stat: Stat
	value: int

	def to_dict(self) -> JsonObject:
		return {"stat": self.stat.value, "value": self.value}

	@staticmethod
	def from_dict(raw: JsonValue) -> AffixRoll:
		data = json_obj(raw)
		return AffixRoll(Stat(json_str(data["stat"])), json_int(data["value"]))


@dataclass(frozen=True, slots=True)
class ItemInstance:
	uid: int
	item_id: str
	rarity: str
	tier: int
	affixes: tuple[AffixRoll, ...] = ()

	def to_dict(self) -> JsonObject:
		return {
			"uid": self.uid,
			"itemId": self.item_id,
			"rarity": self.rarity,
			"tier": self.tier,
			"affixes": [affix.to_dict() for affix in self.affixes],
		}

	@staticmethod
	def from_dict(raw: JsonValue) -> ItemInstance:
		data = json_obj(raw)
		return ItemInstance(
			uid=json_int(data["uid"]),
			item_id=json_str(data["itemId"]),
			rarity=json_str(data["rarity"]),
			tier=json_int(data["tier"]),
			affixes=tuple(AffixRoll.from_dict(a) for a in json_list(data["affixes"])),
		)


def _statuses_to_json(statuses: list[ActiveStatus]) -> list[JsonValue]:
	return [status.to_dict() for status in statuses]


def _statuses_from_json(raw: JsonValue) -> list[ActiveStatus]:
	return [ActiveStatus.from_dict(s) for s in json_list(raw)]


@dataclass(slots=True)
class Player:
	name: str
	vocation_id: str
	hp: int
	mp: int
	gold: int
	level: int = 1
	xp: int = 0
	magic_level: int = 1
	mana_spent: int = 0
	potions: dict[str, int] = field(default_factory=dict)
	equipment: dict[Slot, ItemInstance] = field(default_factory=dict)
	bag: list[ItemInstance] = field(default_factory=list)
	spell_uses: dict[str, int] = field(default_factory=dict)
	statuses: list[ActiveStatus] = field(default_factory=list)
	stun_cooldown: int = 0
	defending: bool = False

	def potion_count(self, potion_id: str) -> int:
		return self.potions.get(potion_id, 0)

	def to_dict(self) -> JsonObject:
		return {
			"name": self.name,
			"vocationId": self.vocation_id,
			"hp": self.hp,
			"mp": self.mp,
			"gold": self.gold,
			"level": self.level,
			"xp": self.xp,
			"magicLevel": self.magic_level,
			"manaSpent": self.mana_spent,
			"potions": dict(sorted(self.potions.items())),
			"equipment": {slot.value: item.to_dict() for slot, item in sorted(self.equipment.items())},
			"bag": [item.to_dict() for item in self.bag],
			"spellUses": dict(sorted(self.spell_uses.items())),
			"statuses": _statuses_to_json(self.statuses),
			"stunCooldown": self.stun_cooldown,
			"defending": self.defending,
		}

	@staticmethod
	def from_dict(raw: JsonValue) -> Player:
		data = json_obj(raw)
		return Player(
			name=json_str(data["name"]),
			vocation_id=json_str(data["vocationId"]),
			hp=json_int(data["hp"]),
			mp=json_int(data["mp"]),
			gold=json_int(data["gold"]),
			level=json_int(data["level"]),
			xp=json_int(data["xp"]),
			magic_level=json_int(data["magicLevel"]),
			mana_spent=json_int(data["manaSpent"]),
			potions={k: json_int(v) for k, v in json_obj(data["potions"]).items()},
			equipment={Slot(k): ItemInstance.from_dict(v) for k, v in json_obj(data["equipment"]).items()},
			bag=[ItemInstance.from_dict(i) for i in json_list(data["bag"])],
			spell_uses={k: json_int(v) for k, v in json_obj(data["spellUses"]).items()},
			statuses=_statuses_from_json(data["statuses"]),
			stun_cooldown=json_int(data["stunCooldown"]),
			defending=json_bool(data["defending"]),
		)


def _attack_to_dict(attack: MonsterAttack) -> JsonObject:
	result: JsonObject = {
		"id": attack.id,
		"element": attack.element.value,
		"min": attack.min,
		"max": attack.max,
		"weight": attack.weight,
	}
	if attack.status is not None:
		result["status"] = {
			"id": attack.status.status,
			"chance": attack.status.chance,
			"damagePct": attack.status.damage_pct,
		}
	return result


def attack_from_dict(raw: JsonValue) -> MonsterAttack:
	data = json_obj(raw)
	status_raw = data.get("status")
	status = None
	if status_raw is not None:
		status_data = json_obj(status_raw)
		status = StatusOnHit(
			status=json_str(status_data["id"]),
			chance=json_int(status_data["chance"]),
			damage_pct=json_int(status_data["damagePct"]),
		)
	return MonsterAttack(
		id=json_str(data["id"]),
		element=Element(json_str(data["element"])),
		min=json_int(data["min"]),
		max=json_int(data["max"]),
		weight=json_int(data["weight"]),
		status=status,
	)


@dataclass(slots=True)
class MonsterInstance:
	"""A spawned monster: definition id plus stats already scaled for the round and difficulty."""

	creature_id: str
	is_boss: bool
	enemy_class: str
	hp: int
	max_hp: int
	xp: int
	gold_min: int
	gold_max: int
	attacks: tuple[MonsterAttack, ...]
	statuses: list[ActiveStatus] = field(default_factory=list)
	stun_cooldown: int = 0
	boss_actions: int = 0

	def attack(self, attack_id: str) -> MonsterAttack:
		for attack in self.attacks:
			if attack.id == attack_id:
				return attack
		raise KeyError(attack_id)

	def to_dict(self) -> JsonObject:
		return {
			"creatureId": self.creature_id,
			"isBoss": self.is_boss,
			"enemyClass": self.enemy_class,
			"hp": self.hp,
			"maxHp": self.max_hp,
			"xp": self.xp,
			"goldMin": self.gold_min,
			"goldMax": self.gold_max,
			"attacks": [_attack_to_dict(a) for a in self.attacks],
			"statuses": _statuses_to_json(self.statuses),
			"stunCooldown": self.stun_cooldown,
			"bossActions": self.boss_actions,
		}

	@staticmethod
	def from_dict(raw: JsonValue) -> MonsterInstance:
		data = json_obj(raw)
		return MonsterInstance(
			creature_id=json_str(data["creatureId"]),
			is_boss=json_bool(data["isBoss"]),
			enemy_class=json_str(data["enemyClass"]),
			hp=json_int(data["hp"]),
			max_hp=json_int(data["maxHp"]),
			xp=json_int(data["xp"]),
			gold_min=json_int(data["goldMin"]),
			gold_max=json_int(data["goldMax"]),
			attacks=tuple(attack_from_dict(a) for a in json_list(data["attacks"])),
			statuses=_statuses_from_json(data["statuses"]),
			stun_cooldown=json_int(data["stunCooldown"]),
			boss_actions=json_int(data["bossActions"]),
		)
