"""Derived character stats: vocation base + equipment (docs/game-design.md §4)."""

from collections import Counter
from collections.abc import Mapping
from dataclasses import dataclass

from rpg.domain.definitions import GameData
from rpg.domain.entities import ItemInstance, Player
from rpg.domain.enums import PROTECTION_BY_ELEMENT, Element, Slot, Stat
from rpg.domain.formulas import pct


def item_stats(item: ItemInstance, data: GameData) -> dict[Stat, int]:
	definition = data.item(item.item_id)
	rarity = data.balance.rarity(item.rarity)
	stats: Counter[Stat] = Counter({stat: pct(value, rarity.stat_pct) for stat, value in definition.stats.items()})
	for affix in item.affixes:
		stats[affix.stat] += affix.value
	return dict(stats)


def item_value(item: ItemInstance, data: GameData) -> int:
	return pct(data.item(item.item_id).value, data.balance.rarity(item.rarity).value_pct)


@dataclass(frozen=True, slots=True)
class CharacterSheet:
	max_hp: int
	max_mp: int
	hp_regen: int
	mp_regen: int
	melee_min: int
	melee_max: int
	weapon_element: Element
	armor: int
	crit_chance: int
	crit_damage: int
	spell_power: int
	physical_damage: int
	dodge: int
	parry: int
	life_leech: int
	mana_leech: int
	protections: Mapping[Element, int]

	def protection(self, element: Element) -> int:
		return self.protections.get(element, 0)


def build_sheet(player: Player, data: GameData) -> CharacterSheet:
	vocation = data.vocation(player.vocation_id)
	caps = data.balance.caps
	totals: Counter[Stat] = Counter()
	for item in player.equipment.values():
		totals.update(item_stats(item, data))

	weapon = player.equipment.get(Slot.WEAPON)
	weapon_element = Element.PHYSICAL
	if weapon is not None:
		weapon_element = data.item(weapon.item_id).element or Element.PHYSICAL

	level_bonus = (player.level - 1) * vocation.melee_per_level
	attack = totals[Stat.ATTACK]
	return CharacterSheet(
		max_hp=vocation.start_hp + (player.level - 1) * vocation.hp_per_level + totals[Stat.MAX_HP],
		max_mp=vocation.start_mp + (player.level - 1) * vocation.mp_per_level + totals[Stat.MAX_MP],
		hp_regen=vocation.hp_regen + totals[Stat.HP_REGEN],
		mp_regen=vocation.mp_regen + totals[Stat.MP_REGEN],
		melee_min=vocation.melee_min + level_bonus + attack,
		melee_max=vocation.melee_max + level_bonus + attack,
		weapon_element=weapon_element,
		armor=totals[Stat.ARMOR],
		crit_chance=min(totals[Stat.CRIT_CHANCE], caps.crit_chance),
		crit_damage=totals[Stat.CRIT_DAMAGE],
		spell_power=totals[Stat.SPELL_POWER],
		physical_damage=totals[Stat.PHYSICAL_DAMAGE],
		dodge=min(totals[Stat.DODGE], caps.dodge),
		parry=min(totals[Stat.PARRY], caps.parry),
		life_leech=min(totals[Stat.LIFE_LEECH], caps.leech),
		mana_leech=min(totals[Stat.MANA_LEECH], caps.leech),
		protections={element: min(totals[stat], caps.protection) for element, stat in PROTECTION_BY_ELEMENT.items()},
	)
