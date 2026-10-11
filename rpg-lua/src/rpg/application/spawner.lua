--- Monster spawning for a round: tier, cycle, position, enemy class and difficulty scaling (docs/game-design.md §3).

local definitions = require("rpg.domain.definitions")
local entities = require("rpg.domain.entities")
local enums = require("rpg.domain.enums")
local formulas = require("rpg.domain.formulas")

local EnemyClass = enums.EnemyClass
local pct = formulas.pct

local spawner = {}

--- Returns the spawned `MonsterInstance` and the `RoundInfo` of the round.
function spawner.spawn_monster(data, rng, round_number, difficulty)
	local balance = data.balance
	local info = formulas.round_info(round_number, balance, data.tier_count)
	local creature, enemy_class
	if info.is_boss then
		creature = data:boss_of_tier(info.tier)
		enemy_class = EnemyClass.BOSS
	else
		creature = rng:pick(data:monsters_in_tier(info.tier))
		enemy_class = rng:chance(balance.elite_chance_pct) and EnemyClass.ELITE or EnemyClass.NORMAL
	end
	local row = balance:enemy_class(enemy_class)

	local factors = formulas.scaling(info, balance, difficulty)

	local function stat(value, product)
		return math.max(1, pct(formulas.scale_stat(value, product), row.stat_pct))
	end

	local function reward(value, product)
		return pct(formulas.scale_reward(value, product), row.reward_pct)
	end

	local hp = stat(creature.hp, factors.hp_pct_product)
	local attacks = {}
	for index, attack in ipairs(creature.attacks) do
		attacks[index] = definitions.replace(attack, {
			min = stat(attack.min, factors.damage_pct_product),
			max = stat(attack.max, factors.damage_pct_product),
		})
	end
	local monster = entities.MonsterInstance.new({
		creature_id = creature.id,
		is_boss = creature.is_boss,
		enemy_class = enemy_class,
		hp = hp,
		max_hp = hp,
		xp = reward(creature.xp, factors.reward_xp_pct_product),
		gold_min = reward(creature.gold_min, factors.reward_gold_pct_product),
		gold_max = reward(creature.gold_max, factors.reward_gold_pct_product),
		attacks = attacks,
	})
	return monster, info
end

return spawner
