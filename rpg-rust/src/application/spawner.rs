//! Monster spawning for a round: tier, cycle, position and difficulty scaling (docs/game-design.md §3).

use crate::domain::definitions::{DifficultyDef, GameData, MonsterAttack};
use crate::domain::entities::MonsterInstance;
use crate::domain::formulas::{RoundInfo, round_info, scale_reward, scale_stat, scaling};
use crate::domain::rng::Rng;

pub fn spawn_monster(
	data: &GameData,
	rng: &mut Rng,
	round_number: i64,
	difficulty: &DifficultyDef,
) -> (MonsterInstance, RoundInfo) {
	let info = round_info(round_number, &data.balance, data.tier_count());
	let creature =
		if info.is_boss { data.boss_of_tier(info.tier) } else { *rng.pick(&data.monsters_in_tier(info.tier)) };

	let factors = scaling(&info, &data.balance, difficulty);
	let hp = 1.max(scale_stat(creature.hp, factors.hp_pct_product));
	let attacks = creature
		.attacks
		.iter()
		.map(|attack| MonsterAttack {
			min: 1.max(scale_stat(attack.min, factors.damage_pct_product)),
			max: 1.max(scale_stat(attack.max, factors.damage_pct_product)),
			..attack.clone()
		})
		.collect();
	let monster = MonsterInstance {
		creature_id: creature.id.clone(),
		is_boss: creature.is_boss,
		hp,
		max_hp: hp,
		xp: scale_reward(creature.xp, factors.reward_xp_pct_product),
		gold_min: scale_reward(creature.gold.min, factors.reward_gold_pct_product),
		gold_max: scale_reward(creature.gold.max, factors.reward_gold_pct_product),
		attacks,
		statuses: Vec::new(),
		stun_cooldown: 0,
		boss_actions: 0,
	};
	(monster, info)
}
