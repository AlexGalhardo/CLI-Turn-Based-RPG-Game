#include "application/spawner.h"

#include <stdlib.h>
#include <string.h>

static int64_t scaled_stat(int64_t value, int64_t product, const EnemyClassDef *row) {
	return max_i64(1, pct(scale_stat(value, product), row->stat_pct));
}

static int64_t scaled_reward(int64_t value, int64_t product, const EnemyClassDef *row) {
	return pct(scale_reward(value, product), row->reward_pct);
}

RoundInfo spawn_monster(
    const GameData *data, Rng *rng, int64_t round_number, const DifficultyDef *difficulty, MonsterInstance *out) {
	const Balance *balance = &data->balance;
	RoundInfo info = round_info(round_number, balance, data_tier_count(data));
	const MonsterDef *creature;
	EnemyClass enemy_class;
	if (info.is_boss) {
		creature = data_boss_of_tier(data, info.tier);
		enemy_class = ENEMY_BOSS;
	} else {
		const MonsterDef **tier_monsters = xmalloc((size_t)data->monster_count * sizeof(*tier_monsters));
		int count = data_monsters_in_tier(data, info.tier, tier_monsters);
		creature = tier_monsters[rng_pick(rng, (size_t)count)];
		free(tier_monsters);
		enemy_class = rng_chance(rng, balance->elite_chance_pct) ? ENEMY_ELITE : ENEMY_NORMAL;
	}
	const EnemyClassDef *row = balance_enemy_class(balance, enemy_class);
	Scaling factors = scaling(info, balance, difficulty);

	memset(out, 0, sizeof(*out));
	id_set(out->creature_id, creature->id);
	out->is_boss = creature->is_boss;
	out->enemy_class = enemy_class;
	out->hp = scaled_stat(creature->hp, factors.hp_pct_product, row);
	out->max_hp = out->hp;
	out->xp = scaled_reward(creature->xp, factors.reward_xp_pct_product, row);
	out->gold_min = scaled_reward(creature->gold_min, factors.reward_gold_pct_product, row);
	out->gold_max = scaled_reward(creature->gold_max, factors.reward_gold_pct_product, row);
	out->attack_count = creature->attack_count;
	for (int i = 0; i < creature->attack_count; i++) {
		out->attacks[i] = creature->attacks[i];
		out->attacks[i].min = scaled_stat(creature->attacks[i].min, factors.damage_pct_product, row);
		out->attacks[i].max = scaled_stat(creature->attacks[i].max, factors.damage_pct_product, row);
	}
	return info;
}
