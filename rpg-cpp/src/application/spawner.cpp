#include "application/spawner.hpp"

#include <algorithm>

namespace rpg::application {

std::pair<domain::MonsterInstance, domain::RoundInfo> spawn_monster(
    const domain::GameData& data, domain::Rng& rng, std::int64_t round, const domain::DifficultyDef& difficulty) {
	const domain::RoundInfo info = domain::round_info(round, data.balance, data.tier_count());
	const domain::MonsterDef& creature =
	    info.is_boss ? data.boss_of_tier(info.tier) : *rng.pick(data.monsters_in_tier(info.tier));

	const domain::Scaling factors = domain::scaling(info, data.balance, difficulty);
	const std::int64_t hp = std::max<std::int64_t>(1, domain::scale_stat(creature.hp, factors.hp_pct_product));

	std::vector<domain::MonsterAttack> attacks = creature.attacks;
	for (domain::MonsterAttack& attack : attacks) {
		attack.min = std::max<std::int64_t>(1, domain::scale_stat(attack.min, factors.damage_pct_product));
		attack.max = std::max<std::int64_t>(1, domain::scale_stat(attack.max, factors.damage_pct_product));
	}

	domain::MonsterInstance monster{
	    .creature_id = creature.id,
	    .is_boss = creature.is_boss,
	    .hp = hp,
	    .max_hp = hp,
	    .xp = domain::scale_reward(creature.xp, factors.reward_xp_pct_product),
	    .gold_min = domain::scale_reward(creature.gold_min, factors.reward_gold_pct_product),
	    .gold_max = domain::scale_reward(creature.gold_max, factors.reward_gold_pct_product),
	    .attacks = std::move(attacks),
	    .statuses = {},
	    .stun_cooldown = 0,
	    .boss_actions = 0,
	};
	return {std::move(monster), info};
}

} // namespace rpg::application
