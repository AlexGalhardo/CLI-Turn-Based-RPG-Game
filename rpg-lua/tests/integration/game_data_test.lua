local base = require("rpg.domain.base")
local enums = require("rpg.domain.enums")
local json = require("rpg.domain.json_types")
local data_loader = require("rpg.infrastructure.data_loader")
local filesystem = require("rpg.infrastructure.filesystem")
local paths = require("rpg.infrastructure.paths")
local helpers = require("support.helpers")
local T = require("support.test")

local GROUP = "integration/game_data"

--- The ids of a list of definitions, in list order.
local function ids(list)
	return base.map(list, function(definition)
		return definition.id
	end)
end

--- A set written as a sorted list, so two sets compare with `T.eq`.
local function set_of(list)
	local sorted = base.copy_list(list)
	table.sort(sorted)
	return sorted
end

local function creatures(data)
	local all = base.copy_list(data.monsters)
	for _, boss in ipairs(data.bosses) do
		all[#all + 1] = boss
	end
	return all
end

T.test(GROUP, "content requirements", function()
	local data = helpers.data()
	T.ok(#data.monsters >= 100)
	T.eq(data.tier_count, 10)
	T.eq(set_of(ids(data.vocations)), { "archer", "mage", "warrior" })
	for tier = 0, data.tier_count - 1 do
		T.ok(#data:monsters_in_tier(tier) >= 9, "tier " .. tier)
		T.ok(data:boss_of_tier(tier).is_boss)
	end
end)

T.test(GROUP, "cross references are valid", function()
	local data = helpers.data()
	for _, vocation in ipairs(data.vocations) do
		data:item(vocation.starter_weapon)
		for _, spell_id in ipairs(vocation.spells) do
			data:spell(spell_id)
		end
	end
	local seen = {}
	for _, creature in ipairs(creatures(data)) do
		T.ok(base.contains(data.families, creature.family), creature.id)
		for _, attack in ipairs(creature.attacks) do
			T.ok(attack.min <= attack.max, creature.id)
			if attack.status ~= nil then
				data:status(attack.status.status)
			end
		end
		if creature.is_boss then
			T.ok(creature.charge_attack ~= nil, creature.id)
			creature:attack(creature.charge_attack)
		end
		T.no(seen[creature.id], "duplicated id " .. creature.id)
		seen[creature.id] = true
	end
	for _, spell in ipairs(data.spells) do
		if spell.level3_bonus.status ~= nil then
			T.ok(enums.is_member(enums.StatusKind, data:status(spell.level3_bonus.status).kind), spell.id)
		end
	end
	T.ok(#data.balance.starting_potions > 0)
	for _, starting in ipairs(data.balance.starting_potions) do
		data:potion(starting[1])
	end
end)

T.test(GROUP, "balance m8 tables", function()
	local data = helpers.data()
	local balance = data.balance
	T.eq(ids(balance.rarities), { "common", "rare", "legendary", "mythic" })
	local stat_pcts, effect_pcts = {}, {}
	for index, rarity in ipairs(balance.rarities) do
		stat_pcts[index] = rarity.stat_pct
	end
	for index, level in ipairs(balance.spell_levels) do
		effect_pcts[index] = level.effect_pct
	end
	T.eq(stat_pcts, { 100, 150, 200, 300 })
	T.eq(effect_pcts, { 100, 150, 200 })
	T.eq(base.sorted_keys(balance.rarity_weights), { "merchant" })
	T.eq(#balance.enemy_classes, 3)
	for _, enemy_class in ipairs(balance.enemy_classes) do
		for rarity_id in pairs(enemy_class.rarity_weights) do
			T.ok(base.contains(ids(balance.rarities), rarity_id), enemy_class.id .. " drops " .. rarity_id)
		end
	end
	T.ok(balance:enemy_class("elite").stat_pct > balance:enemy_class("normal").stat_pct)
	T.eq(base.sorted_keys(balance.item_score_weights), set_of(enums.Stat.values))
	T.eq(data:boss_of_tier(data.tier_count - 1).id, "ferumbras")
	T.eq(balance.final_round, balance.rounds_per_tier * data.tier_count)
end)

T.test(GROUP, "lookup errors", function()
	local data = helpers.data()
	T.raises("UnknownIdError", function()
		data:spell("avada_kedavra")
	end)
	T.raises("UnknownIdError", function()
		data.balance:difficulty("nightmare")
	end)
	T.raises("UnknownIdError", function()
		data.balance:rarity("epic")
	end)
	T.raises("UnknownIdError", function()
		data.balance:enemy_class("champion")
	end)
	T.raises("UnknownIdError", function()
		data.balance.auto_battle:mode("berserk")
	end)
	T.raises("UnknownIdError", function()
		data:creature("rat"):attack("laser")
	end)
end)

T.test(GROUP, "invalid data raises data error", function()
	local source = filesystem.join(helpers.shared_dir(), "data")
	local copy = filesystem.join(helpers.temp_dir(), "shared")
	local target = filesystem.join(copy, "data")
	filesystem.make_dirs(target)
	for _, name in ipairs(filesystem.list_dir(source)) do
		if name:match("%.json$") then
			filesystem.write_file(filesystem.join(target, name), filesystem.read_file(filesystem.join(source, name)))
		end
	end
	-- The untouched copy loads, so the errors below come from the edits alone.
	T.eq(#data_loader.load_game_data(copy).monsters, #helpers.data().monsters)

	local vocations = filesystem.join(target, "vocations.json")
	local document = helpers.read_json(vocations)
	json.remove(document.vocations[1], "startHp")
	filesystem.write_file(vocations, json.encode(document))
	T.raises("DataError", function()
		data_loader.load_game_data(copy)
	end, "startHp")
	filesystem.write_file(vocations, "{ not json")
	T.raises("DataError", function()
		data_loader.load_game_data(copy)
	end, "vocations.json")
end)

-- Standard Lua cannot change the environment of its own process (the reference uses `monkeypatch.setenv`), so the
-- precedence of the variables is tested through the pure `resolve_data_dir_from` and against the real environment.
T.test(GROUP, "paths", function()
	local directory = helpers.temp_dir()
	local override = os.getenv(paths.SHARED_DIR_ENV)
	if override ~= nil and override ~= "" then
		T.eq(paths.find_shared_dir(), override)
		T.eq(paths.find_shared_dir(directory), override)
	else
		T.raises("FileNotFoundError", function()
			paths.find_shared_dir(directory)
		end, paths.SHARED_DIR_ENV)
	end
	T.eq(paths.resolve_data_dir("custom"), "custom")
	local home = filesystem.home_dir()
	local from_env = filesystem.join(directory, "x")
	T.eq(paths.resolve_data_dir_from("custom", from_env, home), "custom")
	T.eq(paths.resolve_data_dir_from(nil, from_env, home), from_env)
	T.eq(paths.resolve_data_dir_from("", "", home), filesystem.join(home, ".cli-turn-based-rpg"))
	T.eq(paths.resolve_data_dir_from(nil, nil, home):match("[^/\\]+$"), ".cli-turn-based-rpg")
	T.eq(paths.resolve_data_dir(), paths.resolve_data_dir_from(nil, os.getenv(paths.DATA_DIR_ENV), home))
end)
