--- Pure integer formulas from docs/game-design.md. No randomness, no state.
--- `//` is Lua's floor division on integers; `/` and `^` would produce floats and are never used here.

local base = require("rpg.domain.base")

local formulas = {}

--- floor(value * percent / 100) for non-negative operands: the only rounding rule of the engine.
function formulas.pct(value, percent)
	if value < 0 or percent < 0 then
		base.raise("ValueError", "pct operands must be non-negative")
	end
	return value * percent // 100
end

function formulas.clamp(value, minimum, maximum)
	return math.max(minimum, math.min(maximum, value))
end

--- Total experience needed to reach `level` (Tibia formula).
function formulas.xp_for_level(level)
	if level <= 1 then
		return 0
	end
	return 50 * (level * level * level - 6 * level * level + 17 * level - 12) // 3
end

--- Total mana spent (since level 1) needed to advance from `magic_level` to the next one.
function formulas.mana_for_magic_level(magic_level, balance)
	local step = balance.magic_level_base
	local total = step
	for _ = 1, magic_level - 1 do
		step = formulas.pct(step, balance.magic_level_growth_pct)
		total = total + step
	end
	return total
end

function formulas.spell_level_for_uses(uses, levels)
	local current = levels[1]
	for _, level in ipairs(levels) do
		if uses >= level.uses then
			current = level
		end
	end
	return current
end

function formulas.armor_mitigation(damage, armor)
	return damage * 100 // (100 + armor)
end

--- RoundInfo: `round`, `tier`, `cycle`, `position`, `is_boss`.
function formulas.round_info(round_number, balance, tier_count)
	local index = round_number - 1
	local per_tier = balance.rounds_per_tier
	local position = index % per_tier
	return {
		round = round_number,
		tier = (index // per_tier) % tier_count,
		cycle = index // (per_tier * tier_count),
		position = position,
		is_boss = position == per_tier - 1,
	}
end

--- Scaling: the percentage products applied to a monster of that round and difficulty.
function formulas.scaling(info, balance, difficulty)
	local cycle_pct = 100 + info.cycle * balance.cycle_stat_pct
	local reward_pct = 100 + info.cycle * balance.cycle_reward_pct
	local position_pct = info.is_boss and 100 or (100 + info.position * balance.position_pct)
	return {
		hp_pct_product = difficulty.hp_pct * cycle_pct * position_pct,
		damage_pct_product = difficulty.damage_pct * cycle_pct * position_pct,
		reward_xp_pct_product = difficulty.xp_pct * reward_pct,
		reward_gold_pct_product = difficulty.gold_pct * reward_pct,
	}
end

--- Applies three chained percentages in one floor: value * a * b * c / 1_000_000.
function formulas.scale_stat(value, pct_product)
	return value * pct_product // 1000000
end

--- Applies two chained percentages in one floor: value * a * b / 10_000.
function formulas.scale_reward(value, pct_product)
	return value * pct_product // 10000
end

return formulas
