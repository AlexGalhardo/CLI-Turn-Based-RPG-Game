"""Monster spawning for a round: tier, cycle, position and difficulty scaling (docs/game-design.md §3)."""

from dataclasses import replace

from rpg.domain.definitions import DifficultyDef, GameData
from rpg.domain.entities import MonsterInstance
from rpg.domain.formulas import RoundInfo, round_info, scale_reward, scale_stat, scaling
from rpg.domain.rng import Rng


def spawn_monster(
	data: GameData, rng: Rng, round_number: int, difficulty: DifficultyDef
) -> tuple[MonsterInstance, RoundInfo]:
	info = round_info(round_number, data.balance, data.tier_count)
	creature = data.boss_of_tier(info.tier) if info.is_boss else rng.pick(data.monsters_in_tier(info.tier))

	factors = scaling(info, data.balance, difficulty)
	hp = max(1, scale_stat(creature.hp, factors.hp_pct_product))
	attacks = tuple(
		replace(
			attack,
			min=max(1, scale_stat(attack.min, factors.damage_pct_product)),
			max=max(1, scale_stat(attack.max, factors.damage_pct_product)),
		)
		for attack in creature.attacks
	)
	monster = MonsterInstance(
		creature_id=creature.id,
		is_boss=creature.is_boss,
		hp=hp,
		max_hp=hp,
		xp=scale_reward(creature.xp, factors.reward_xp_pct_product),
		gold_min=scale_reward(creature.gold_min, factors.reward_gold_pct_product),
		gold_max=scale_reward(creature.gold_max, factors.reward_gold_pct_product),
		attacks=attacks,
	)
	return monster, info
