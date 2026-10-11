local formulas = require("rpg.domain.formulas")
local helpers = require("support.helpers")
local T = require("support.test")

local GROUP = "unit/formulas"

T.test(GROUP, "pct floors", function()
	T.eq(formulas.pct(99, 50), 49)
	T.eq(formulas.pct(0, 150), 0)
	T.eq(formulas.pct(7, 100), 7)
end)

T.test(GROUP, "pct rejects negative", function()
	T.raises("ValueError", function()
		formulas.pct(-1, 10)
	end, "non-negative")
end)

T.test(GROUP, "clamp", function()
	T.eq(formulas.clamp(5, 0, 3), 3)
	T.eq(formulas.clamp(-1, 0, 3), 0)
	T.eq(formulas.clamp(2, 0, 3), 2)
end)

T.test(GROUP, "xp for level matches tibia table", function()
	local table_rows = {
		{ 1, 0 },
		{ 2, 100 },
		{ 3, 200 },
		{ 4, 400 },
		{ 5, 800 },
		{ 10, 9300 },
		{ 20, 98800 },
		{ 100, 15694800 },
	}
	for _, row in ipairs(table_rows) do
		T.eq(formulas.xp_for_level(row[1]), row[2], "level " .. row[1])
	end
end)

T.test(GROUP, "mana for magic level is cumulative", function()
	local balance = helpers.data().balance
	local base = balance.magic_level_base
	local growth = balance.magic_level_growth_pct
	T.eq(formulas.mana_for_magic_level(1, balance), base)
	T.eq(formulas.mana_for_magic_level(2, balance), base + base * growth // 100)
	T.ok(formulas.mana_for_magic_level(5, balance) > formulas.mana_for_magic_level(4, balance))
end)

T.test(GROUP, "spell level for uses", function()
	local levels = helpers.data().balance.spell_levels
	T.eq(formulas.spell_level_for_uses(0, levels).level, 1)
	T.eq(formulas.spell_level_for_uses(19, levels).level, 1)
	T.eq(formulas.spell_level_for_uses(20, levels).level, 2)
	T.eq(formulas.spell_level_for_uses(49, levels).level, 2)
	T.eq(formulas.spell_level_for_uses(50, levels).level, 3)
	T.eq(formulas.spell_level_for_uses(5000, levels).level, 3)
end)

T.test(GROUP, "armor mitigation", function()
	T.eq(formulas.armor_mitigation(100, 0), 100)
	T.eq(formulas.armor_mitigation(100, 100), 50)
	T.eq(formulas.armor_mitigation(10, 3), 9)
end)

T.test(GROUP, "round info", function()
	local data = helpers.data()
	local cases = {
		{ 1, { 0, 0, 0, false } },
		{ 9, { 0, 0, 8, false } },
		{ 10, { 0, 0, 9, true } },
		{ 11, { 1, 0, 0, false } },
		{ 100, { 9, 0, 9, true } },
		{ 101, { 0, 1, 0, false } },
		{ 250, { 4, 2, 9, true } },
	}
	for _, case in ipairs(cases) do
		local info = formulas.round_info(case[1], data.balance, data.tier_count)
		T.eq({ info.tier, info.cycle, info.position, info.is_boss }, case[2], "round " .. case[1])
	end
end)

T.test(GROUP, "scaling combines difficulty cycle and position", function()
	local data = helpers.data()
	local balance = data.balance
	local hard = balance:difficulty("hard")
	local info = formulas.round_info(103, balance, data.tier_count)
	local factors = formulas.scaling(info, balance, hard)
	local cycle_pct = 100 + balance.cycle_stat_pct
	local position_pct = 100 + 2 * balance.position_pct
	T.eq(factors.hp_pct_product, hard.hp_pct * cycle_pct * position_pct)
	T.eq(formulas.scale_stat(1000, factors.hp_pct_product), 1000 * hard.hp_pct * cycle_pct * position_pct // 1000000)
	T.eq(
		formulas.scale_reward(100, factors.reward_xp_pct_product),
		100 * hard.xp_pct * (100 + balance.cycle_reward_pct) // 10000
	)
end)

T.test(GROUP, "boss ignores position scaling", function()
	local data = helpers.data()
	local normal = data.balance:difficulty("normal")
	local info = formulas.round_info(10, data.balance, data.tier_count)
	T.eq(formulas.scaling(info, data.balance, normal).hp_pct_product, normal.hp_pct * 100 * 100)
end)
