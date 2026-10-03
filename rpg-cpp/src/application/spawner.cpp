#include "application/spawner.hpp"

#include <algorithm>
#include <string_view>

#include "domain/formulas.hpp"

namespace rpg::application {

std::pair<domain::MonsterInstance, domain::RoundInfo> spawn_monster(
    const domain::GameData& data, domain::Rng& rng, std::int64_t round, const domain::DifficultyDef& difficulty) {
	const domain::Balance& balance = data.balance;
	const domain::RoundInfo info = domain::round_info(round, balance, data.tier_count());
	const domain::MonsterDef* creature = nullptr;
	std::string_view enemy_class = domain::enemy_class::boss;
	if (info.is_boss) {
		creature = &data.boss_of_tier(info.tier);
	} else {
		creature = rng.pick(data.monsters_in_tier(info.tier));
		enemy_class = rng.chance(balance.elite_chance_pct) ? domain::enemy_class::elite : domain::enemy_class::normal;
	}
	const domain::EnemyClassDef& row = balance.enemy_class(enemy_class);

	const domain::Scaling factors = domain::scaling(info, balance, difficulty);
	const auto stat = [&](std::int64_t value, std::int64_t product) {
		return std::max<std::int64_t>(1, domain::pct(domain::scale_stat(value, product), row.stat_pct));
	};
	const auto reward = [&](std::int64_t value, std::int64_t product) {
		return domain::pct(domain::scale_reward(value, product), row.reward_pct);
	};
	const std::int64_t hp = stat(creature->hp, factors.hp_pct_product);

	std::vector<domain::MonsterAttack> attacks = creature->attacks;
	for (domain::MonsterAttack& attack : attacks) {
		attack.min = stat(attack.min, factors.damage_pct_product);
		attack.max = stat(attack.max, factors.damage_pct_product);
	}

	domain::MonsterInstance monster{
	    .creature_id = creature->id,
	    .is_boss = creature->is_boss,
	    .enemy_class = std::string(enemy_class),
	    .hp = hp,
	    .max_hp = hp,
	    .xp = reward(creature->xp, factors.reward_xp_pct_product),
	    .gold_min = reward(creature->gold_min, factors.reward_gold_pct_product),
	    .gold_max = reward(creature->gold_max, factors.reward_gold_pct_product),
	    .attacks = std::move(attacks),
	    .statuses = {},
	    .stun_cooldown = 0,
	    .boss_actions = 0,
	};
	return {std::move(monster), info};
}

} // namespace rpg::application
