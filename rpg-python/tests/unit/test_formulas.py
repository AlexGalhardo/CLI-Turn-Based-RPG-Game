import pytest

from rpg.domain.definitions import GameData
from rpg.domain.formulas import (
	armor_mitigation,
	clamp,
	mana_for_magic_level,
	pct,
	round_info,
	scale_reward,
	scale_stat,
	scaling,
	spell_level_for_uses,
	xp_for_level,
)


def test_pct_floors() -> None:
	assert pct(99, 50) == 49
	assert pct(0, 150) == 0
	assert pct(7, 100) == 7


def test_pct_rejects_negative() -> None:
	with pytest.raises(ValueError, match="non-negative"):
		pct(-1, 10)


def test_clamp() -> None:
	assert clamp(5, 0, 3) == 3
	assert clamp(-1, 0, 3) == 0
	assert clamp(2, 0, 3) == 2


@pytest.mark.parametrize(
	("level", "xp"), [(1, 0), (2, 100), (3, 200), (4, 400), (5, 800), (10, 9300), (20, 98800), (100, 15694800)]
)
def test_xp_for_level_matches_tibia_table(level: int, xp: int) -> None:
	assert xp_for_level(level) == xp


def test_mana_for_magic_level_is_cumulative(data: GameData) -> None:
	base = data.balance.magic_level_base
	growth = data.balance.magic_level_growth_pct
	assert mana_for_magic_level(1, data.balance) == base
	assert mana_for_magic_level(2, data.balance) == base + base * growth // 100
	assert mana_for_magic_level(5, data.balance) > mana_for_magic_level(4, data.balance)


def test_spell_level_for_uses(data: GameData) -> None:
	levels = data.balance.spell_levels
	assert spell_level_for_uses(0, levels).level == 1
	assert spell_level_for_uses(19, levels).level == 1
	assert spell_level_for_uses(20, levels).level == 2
	assert spell_level_for_uses(49, levels).level == 2
	assert spell_level_for_uses(50, levels).level == 3
	assert spell_level_for_uses(5000, levels).level == 3


def test_armor_mitigation() -> None:
	assert armor_mitigation(100, 0) == 100
	assert armor_mitigation(100, 100) == 50
	assert armor_mitigation(10, 3) == 9


@pytest.mark.parametrize(
	("round_number", "expected"),
	[
		(1, (0, 0, 0, False)),
		(9, (0, 0, 8, False)),
		(10, (0, 0, 9, True)),
		(11, (1, 0, 0, False)),
		(100, (9, 0, 9, True)),
		(101, (0, 1, 0, False)),
		(250, (4, 2, 9, True)),
	],
)
def test_round_info(data: GameData, round_number: int, expected: tuple[int, int, int, bool]) -> None:
	info = round_info(round_number, data.balance, data.tier_count)
	assert (info.tier, info.cycle, info.position, info.is_boss) == expected


def test_scaling_combines_difficulty_cycle_and_position(data: GameData) -> None:
	balance = data.balance
	hard = balance.difficulty("hard")
	info = round_info(103, balance, data.tier_count)
	factors = scaling(info, balance, hard)
	cycle_pct = 100 + balance.cycle_stat_pct
	position_pct = 100 + 2 * balance.position_pct
	assert factors.hp_pct_product == hard.hp_pct * cycle_pct * position_pct
	assert scale_stat(1000, factors.hp_pct_product) == 1000 * hard.hp_pct * cycle_pct * position_pct // 1_000_000
	assert (
		scale_reward(100, factors.reward_xp_pct_product)
		== 100 * hard.xp_pct * (100 + balance.cycle_reward_pct) // 10_000
	)


def test_boss_ignores_position_scaling(data: GameData) -> None:
	normal = data.balance.difficulty("normal")
	info = round_info(10, data.balance, data.tier_count)
	assert scaling(info, data.balance, normal).hp_pct_product == normal.hp_pct * 100 * 100
