"""Monster spawning for a round: tier, cycle, position, enemy class and difficulty scaling (docs/game-design.md §3)."""

from dataclasses import replace

from rpg.domain.definitions import DifficultyDef, GameData
from rpg.domain.entities import MonsterInstance
from rpg.domain.enums import EnemyClass
from rpg.domain.formulas import RoundInfo, pct, round_info, scale_reward, scale_stat, scaling
from rpg.domain.rng import Rng


def spawn_monster(
	data: GameData, rng: Rng, round_number: int, difficulty: DifficultyDef
) -> tuple[MonsterInstance, RoundInfo]:
	balance = data.balance
	info = round_info(round_number, balance, data.tier_count)
	if info.is_boss:
		creature = data.boss_of_tier(info.tier)
		enemy_class = EnemyClass.BOSS
	else:
		creature = rng.pick(data.monsters_in_tier(info.tier))
		enemy_class = EnemyClass.ELITE if rng.chance(balance.elite_chance_pct) else EnemyClass.NORMAL
	row = balance.enemy_class(enemy_class)

	factors = scaling(info, balance, difficulty)

	def stat(value: int, product: int) -> int:
		return max(1, pct(scale_stat(value, product), row.stat_pct))

	def reward(value: int, product: int) -> int:
		return pct(scale_reward(value, product), row.reward_pct)

	hp = stat(creature.hp, factors.hp_pct_product)
	attacks = tuple(
		replace(
			attack,
			min=stat(attack.min, factors.damage_pct_product),
			max=stat(attack.max, factors.damage_pct_product),
		)
		for attack in creature.attacks
	)
	monster = MonsterInstance(
		creature_id=creature.id,
		is_boss=creature.is_boss,
		enemy_class=enemy_class.value,
		hp=hp,
		max_hp=hp,
		xp=reward(creature.xp, factors.reward_xp_pct_product),
		gold_min=reward(creature.gold_min, factors.reward_gold_pct_product),
		gold_max=reward(creature.gold_max, factors.reward_gold_pct_product),
		attacks=attacks,
	)
	return monster, info
