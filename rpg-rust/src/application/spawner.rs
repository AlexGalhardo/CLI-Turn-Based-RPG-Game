//! Monster spawning for a round: tier, cycle, position, enemy class and difficulty scaling (docs/game-design.md §3).

use crate::domain::definitions::{DifficultyDef, GameData, MonsterAttack};
use crate::domain::entities::MonsterInstance;
use crate::domain::enums::EnemyClass;
use crate::domain::formulas::{RoundInfo, pct, round_info, scale_reward, scale_stat, scaling};
use crate::domain::rng::Rng;

pub fn spawn_monster(
	data: &GameData,
	rng: &mut Rng,
	round_number: i64,
	difficulty: &DifficultyDef,
) -> (MonsterInstance, RoundInfo) {
	let balance = &data.balance;
	let info = round_info(round_number, balance, data.tier_count());
	let (creature, enemy_class) = if info.is_boss {
		(data.boss_of_tier(info.tier), EnemyClass::Boss)
	} else {
		let creature = *rng.pick(&data.monsters_in_tier(info.tier));
		(creature, if rng.chance(balance.elite_chance_pct) { EnemyClass::Elite } else { EnemyClass::Normal })
	};
	let row = balance.enemy_class(enemy_class);

	let factors = scaling(&info, balance, difficulty);
	let stat = |value: i64, product: i64| 1.max(pct(scale_stat(value, product), row.stat_pct));
	let reward = |value: i64, product: i64| pct(scale_reward(value, product), row.reward_pct);

	let hp = stat(creature.hp, factors.hp_pct_product);
	let attacks = creature
		.attacks
		.iter()
		.map(|attack| MonsterAttack {
			min: stat(attack.min, factors.damage_pct_product),
			max: stat(attack.max, factors.damage_pct_product),
			..attack.clone()
		})
		.collect();
	let monster = MonsterInstance {
		creature_id: creature.id.clone(),
		is_boss: creature.is_boss,
		enemy_class,
		hp,
		max_hp: hp,
		xp: reward(creature.xp, factors.reward_xp_pct_product),
		gold_min: reward(creature.gold.min, factors.reward_gold_pct_product),
		gold_max: reward(creature.gold.max, factors.reward_gold_pct_product),
		attacks,
		statuses: Vec::new(),
		stun_cooldown: 0,
		boss_actions: 0,
	};
	(monster, info)
}
