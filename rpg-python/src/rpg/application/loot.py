"""Item factory: base item + rarity + affixes (docs/game-design.md §8)."""

from collections.abc import Mapping

from rpg.domain.definitions import GameData, ItemDef, RarityDef, VocationDef
from rpg.domain.entities import AffixRoll, ItemInstance
from rpg.domain.enums import Slot, Stat
from rpg.domain.rng import Rng


def can_use(item: ItemDef, vocation: VocationDef) -> bool:
	if item.slot is Slot.WEAPON:
		return item.type in vocation.weapon_types
	if item.slot is Slot.SHIELD:
		return item.type in vocation.shield_types
	return True


def roll_rarity(data: GameData, rng: Rng, weights: Mapping[str, int]) -> RarityDef:
	"""Weighted roll in the order of `balance.rarities`; zero weights are skipped and a single option is not rolled."""
	options = [(rarity, weights.get(rarity.id, 0)) for rarity in data.balance.rarities]
	options = [(rarity, weight) for rarity, weight in options if weight > 0]
	if not options:
		raise ValueError("rarity table without a positive weight")
	if len(options) == 1:
		return options[0][0]
	return options[rng.weighted([weight for _, weight in options])][0]


def generate_item(
	data: GameData,
	rng: Rng,
	*,
	vocation: VocationDef,
	tier: int,
	weights: Mapping[str, int],
	uid: int,
) -> ItemInstance | None:
	"""Returns None (consuming no randomness) when no item fits the vocation and tier."""
	lowest_tier = max(0, tier - 1)
	candidates = sorted(
		(item for item in data.items if lowest_tier <= item.tier <= tier and can_use(item, vocation)),
		key=lambda item: item.id,
	)
	if not candidates:
		return None
	base = rng.pick(candidates)
	rarity = roll_rarity(data, rng, weights)
	affix_count = rng.roll(rarity.affix_min, rarity.affix_max)

	rolls: list[AffixRoll] = []
	used_stats: set[Stat] = set()
	for _ in range(affix_count):
		pool = sorted(
			(affix for affix in data.affixes if base.slot in affix.slots and affix.stat not in used_stats),
			key=lambda affix: affix.id,
		)
		if not pool:
			break
		affix = rng.pick(pool)
		used_stats.add(affix.stat)
		rolls.append(AffixRoll(affix.stat, rng.roll(affix.min, affix.max) + tier * affix.per_tier))
	return ItemInstance(uid=uid, item_id=base.id, rarity=rarity.id, tier=tier, affixes=tuple(rolls))
