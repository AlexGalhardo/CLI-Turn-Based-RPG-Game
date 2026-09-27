"""Item factory: base item + rarity + affixes (docs/game-design.md §8)."""

from rpg.domain.definitions import DifficultyDef, GameData, ItemDef, VocationDef
from rpg.domain.entities import AffixRoll, ItemInstance
from rpg.domain.enums import Slot, Stat
from rpg.domain.formulas import pct
from rpg.domain.rng import Rng


def can_use(item: ItemDef, vocation: VocationDef) -> bool:
	if item.slot is Slot.WEAPON:
		return item.type in vocation.weapon_types
	if item.slot is Slot.SHIELD:
		return item.type in vocation.shield_types
	return True


def rarity_weights(data: GameData, table: str, difficulty: DifficultyDef) -> list[int]:
	weights = data.balance.rarity_weights[table]
	result: list[int] = []
	for rarity in data.balance.rarities:
		weight = weights.get(rarity.id, 0)
		if rarity.id != "common":
			weight = pct(weight, difficulty.non_common_weight_pct)
		result.append(weight)
	return result


def generate_item(
	data: GameData,
	rng: Rng,
	*,
	vocation: VocationDef,
	tier: int,
	table: str,
	difficulty: DifficultyDef,
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
	rarity = data.balance.rarities[rng.weighted(rarity_weights(data, table, difficulty))]
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
