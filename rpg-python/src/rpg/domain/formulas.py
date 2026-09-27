"""Pure integer formulas from docs/game-design.md. No randomness, no state."""

from dataclasses import dataclass

from rpg.domain.definitions import Balance, DifficultyDef, SpellLevelDef


def pct(value: int, percent: int) -> int:
	"""floor(value * percent / 100) for non-negative operands — the only rounding rule of the engine."""
	if value < 0 or percent < 0:
		raise ValueError("pct operands must be non-negative")
	return value * percent // 100


def clamp(value: int, minimum: int, maximum: int) -> int:
	return max(minimum, min(maximum, value))


def xp_for_level(level: int) -> int:
	"""Total experience needed to reach `level` (Tibia formula)."""
	if level <= 1:
		return 0
	return 50 * (level**3 - 6 * level**2 + 17 * level - 12) // 3


def mana_for_magic_level(magic_level: int, balance: Balance) -> int:
	"""Total mana spent (since level 1) needed to advance from `magic_level` to the next one."""
	step = balance.magic_level_base
	total = step
	for _ in range(1, magic_level):
		step = pct(step, balance.magic_level_growth_pct)
		total += step
	return total


def spell_level_for_uses(uses: int, levels: tuple[SpellLevelDef, ...]) -> SpellLevelDef:
	current = levels[0]
	for level in levels:
		if uses >= level.uses:
			current = level
	return current


def armor_mitigation(damage: int, armor: int) -> int:
	return damage * 100 // (100 + armor)


@dataclass(frozen=True, slots=True)
class RoundInfo:
	round: int
	tier: int
	cycle: int
	position: int
	is_boss: bool


def round_info(round_number: int, balance: Balance, tier_count: int) -> RoundInfo:
	index = round_number - 1
	per_tier = balance.rounds_per_tier
	position = index % per_tier
	return RoundInfo(
		round=round_number,
		tier=(index // per_tier) % tier_count,
		cycle=index // (per_tier * tier_count),
		position=position,
		is_boss=position == per_tier - 1,
	)


@dataclass(frozen=True, slots=True)
class Scaling:
	hp_pct_product: int
	damage_pct_product: int
	reward_xp_pct_product: int
	reward_gold_pct_product: int


def scaling(info: RoundInfo, balance: Balance, difficulty: DifficultyDef) -> Scaling:
	cycle_pct = 100 + info.cycle * balance.cycle_stat_pct
	reward_pct = 100 + info.cycle * balance.cycle_reward_pct
	position_pct = 100 if info.is_boss else 100 + info.position * balance.position_pct
	return Scaling(
		hp_pct_product=difficulty.hp_pct * cycle_pct * position_pct,
		damage_pct_product=difficulty.damage_pct * cycle_pct * position_pct,
		reward_xp_pct_product=difficulty.xp_pct * reward_pct,
		reward_gold_pct_product=difficulty.gold_pct * reward_pct,
	)


def scale_stat(value: int, pct_product: int) -> int:
	"""Applies three chained percentages in one floor: value * a * b * c / 1_000_000."""
	return value * pct_product // 1_000_000


def scale_reward(value: int, pct_product: int) -> int:
	"""Applies two chained percentages in one floor: value * a * b / 10_000."""
	return value * pct_product // 10_000
