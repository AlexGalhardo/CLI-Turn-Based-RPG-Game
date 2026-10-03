"""Immutable game definitions loaded from shared/data (see docs/data-format.md)."""

from collections.abc import Mapping
from dataclasses import dataclass, field
from functools import cached_property
from typing import Protocol

from rpg.domain.enums import Element, Resource, Slot, SpellKind, Stat, StatusKind


class UnknownIdError(KeyError):
	"""Raised when a definition id is not present in the loaded data."""


@dataclass(frozen=True, slots=True)
class StatusOnHit:
	status: str
	chance: int
	damage_pct: int


@dataclass(frozen=True, slots=True)
class MonsterAttack:
	id: str
	element: Element
	min: int
	max: int
	weight: int
	status: StatusOnHit | None = None


@dataclass(frozen=True, slots=True)
class MonsterDef:
	id: str
	name: str
	tier: int
	family: str
	hp: int
	xp: int
	gold_min: int
	gold_max: int
	attacks: tuple[MonsterAttack, ...]
	resistances: Mapping[Element, int]
	is_boss: bool = False
	charge_attack: str | None = None

	def resistance(self, element: Element) -> int:
		return self.resistances.get(element, 100)

	def attack(self, attack_id: str) -> MonsterAttack:
		for attack in self.attacks:
			if attack.id == attack_id:
				return attack
		raise UnknownIdError(attack_id)


@dataclass(frozen=True, slots=True)
class Level3Bonus:
	status: str | None = None
	chance: int = 0
	cleanse: bool = False


@dataclass(frozen=True, slots=True)
class SpellDef:
	id: str
	name: str
	words: str
	kind: SpellKind
	element: Element
	mana: int
	min: int
	max: int
	per_level: int
	per_magic_level: int
	level3_bonus: Level3Bonus


@dataclass(frozen=True, slots=True)
class VocationDef:
	id: str
	name: str
	start_hp: int
	start_mp: int
	hp_per_level: int
	mp_per_level: int
	hp_regen: int
	mp_regen: int
	melee_min: int
	melee_max: int
	melee_per_level: int
	weapon_types: tuple[str, ...]
	shield_types: tuple[str, ...]
	starter_weapon: str
	spells: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class PotionDef:
	id: str
	name: str
	resource: Resource
	min: int
	max: int
	price: int
	unlock_round: int


@dataclass(frozen=True, slots=True)
class StatusDef:
	id: str
	kind: StatusKind
	element: Element
	turns: int


@dataclass(frozen=True, slots=True)
class ItemDef:
	id: str
	name: str
	slot: Slot
	type: str
	tier: int
	element: Element | None
	stats: Mapping[Stat, int]
	value: int


@dataclass(frozen=True, slots=True)
class AffixDef:
	id: str
	stat: Stat
	min: int
	max: int
	per_tier: int
	slots: tuple[Slot, ...]


@dataclass(frozen=True, slots=True)
class AchievementDef:
	id: str
	type: str
	value: int


@dataclass(frozen=True, slots=True)
class DifficultyDef:
	id: str
	hp_pct: int
	damage_pct: int
	gold_pct: int
	xp_pct: int


@dataclass(frozen=True, slots=True)
class RarityDef:
	id: str
	stat_pct: int
	value_pct: int
	affix_min: int
	affix_max: int


@dataclass(frozen=True, slots=True)
class EnemyClassDef:
	"""A row of `balance.enemyClasses`: multipliers, combat chances and drop table (docs/game-design.md §3)."""

	id: str
	stat_pct: int
	reward_pct: int
	dodge: int
	parry: int
	crit: int
	heal: int
	drop_chance_pct: int
	drops: int
	potion_drop_pct: int
	rarity_weights: Mapping[str, int]


@dataclass(frozen=True, slots=True)
class AutoBattleModeDef:
	id: str
	offense: str
	support_every: int


@dataclass(frozen=True, slots=True)
class AutoBattleDef:
	heal_below_pct: int
	mana_below_pct: int
	emergency_heal_below_pct: int
	modes: tuple[AutoBattleModeDef, ...]

	def mode(self, mode_id: str) -> AutoBattleModeDef:
		for mode in self.modes:
			if mode.id == mode_id:
				return mode
		raise UnknownIdError(mode_id)


@dataclass(frozen=True, slots=True)
class SpellLevelDef:
	level: int
	uses: int
	effect_pct: int
	mana_pct: int


@dataclass(frozen=True, slots=True)
class Caps:
	crit_chance: int
	dodge: int
	parry: int
	leech: int
	protection: int


@dataclass(frozen=True, slots=True)
class Balance:
	rounds_per_tier: int
	cycle_stat_pct: int
	cycle_reward_pct: int
	position_pct: int
	final_round: int
	elite_chance_pct: int
	difficulties: tuple[DifficultyDef, ...]
	enemy_classes: tuple[EnemyClassDef, ...]
	crit_multiplier_pct: int
	defend_damage_pct: int
	parry_reflect_pct: int
	monster_heal_pct: int
	boss_telegraph_every: int
	boss_charge_damage_pct: int
	caps: Caps
	magic_level_base: int
	magic_level_growth_pct: int
	spell_levels: tuple[SpellLevelDef, ...]
	starting_gold: int
	starting_potions: tuple[tuple[str, int], ...]
	bag_capacity: int
	item_level_per_tier: int
	item_score_weights: Mapping[Stat, int]
	rarities: tuple[RarityDef, ...]
	rarity_weights: Mapping[str, Mapping[str, int]]
	merchant_stock_size: int
	merchant_markup_pct: int
	spell_status_damage_pct: int
	auto_battle: AutoBattleDef

	def difficulty(self, difficulty_id: str) -> DifficultyDef:
		for difficulty in self.difficulties:
			if difficulty.id == difficulty_id:
				return difficulty
		raise UnknownIdError(difficulty_id)

	def enemy_class(self, class_id: str) -> EnemyClassDef:
		for enemy_class in self.enemy_classes:
			if enemy_class.id == class_id:
				return enemy_class
		raise UnknownIdError(class_id)

	def rarity(self, rarity_id: str) -> RarityDef:
		for rarity in self.rarities:
			if rarity.id == rarity_id:
				return rarity
		raise UnknownIdError(rarity_id)


class _HasId(Protocol):
	@property
	def id(self) -> str: ...


def _index[T: _HasId](items: tuple[T, ...]) -> dict[str, T]:
	return {item.id: item for item in items}


@dataclass(frozen=True)
class GameData:
	balance: Balance
	vocations: tuple[VocationDef, ...]
	spells: tuple[SpellDef, ...]
	monsters: tuple[MonsterDef, ...]
	bosses: tuple[MonsterDef, ...]
	potions: tuple[PotionDef, ...]
	statuses: tuple[StatusDef, ...]
	items: tuple[ItemDef, ...]
	affixes: tuple[AffixDef, ...] = ()
	achievements: tuple[AchievementDef, ...] = ()
	families: tuple[str, ...] = field(default=())

	@cached_property
	def _vocations(self) -> dict[str, VocationDef]:
		return _index(self.vocations)

	@cached_property
	def _spells(self) -> dict[str, SpellDef]:
		return _index(self.spells)

	@cached_property
	def _creatures(self) -> dict[str, MonsterDef]:
		return _index(self.monsters + self.bosses)

	@cached_property
	def _potions(self) -> dict[str, PotionDef]:
		return _index(self.potions)

	@cached_property
	def _statuses(self) -> dict[str, StatusDef]:
		return _index(self.statuses)

	@cached_property
	def _items(self) -> dict[str, ItemDef]:
		return _index(self.items)

	@cached_property
	def tier_count(self) -> int:
		return len(self.bosses)

	@cached_property
	def _monsters_by_tier(self) -> dict[int, tuple[MonsterDef, ...]]:
		by_tier: dict[int, list[MonsterDef]] = {}
		for monster in self.monsters:
			by_tier.setdefault(monster.tier, []).append(monster)
		return {tier: tuple(sorted(group, key=lambda m: m.id)) for tier, group in by_tier.items()}

	@cached_property
	def _bosses_by_tier(self) -> dict[int, MonsterDef]:
		return {boss.tier: boss for boss in self.bosses}

	def vocation(self, vocation_id: str) -> VocationDef:
		return _lookup(self._vocations, vocation_id)

	def spell(self, spell_id: str) -> SpellDef:
		return _lookup(self._spells, spell_id)

	def creature(self, creature_id: str) -> MonsterDef:
		return _lookup(self._creatures, creature_id)

	def potion(self, potion_id: str) -> PotionDef:
		return _lookup(self._potions, potion_id)

	def status(self, status_id: str) -> StatusDef:
		return _lookup(self._statuses, status_id)

	def item(self, item_id: str) -> ItemDef:
		return _lookup(self._items, item_id)

	def monsters_in_tier(self, tier: int) -> tuple[MonsterDef, ...]:
		return self._monsters_by_tier.get(tier, ())

	def boss_of_tier(self, tier: int) -> MonsterDef:
		return self._bosses_by_tier[tier]


def _lookup[T](index: Mapping[str, T], key: str) -> T:
	try:
		return index[key]
	except KeyError:
		raise UnknownIdError(key) from None
