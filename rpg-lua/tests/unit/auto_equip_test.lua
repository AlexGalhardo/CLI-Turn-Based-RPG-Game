--- Auto-equip with auto-sell (docs/game-design.md §8.1).

local auto_equip = require("rpg.application.auto_equip").auto_equip
local commands = require("rpg.application.commands")
local GameEngine = require("rpg.application.engine")
local run_state = require("rpg.application.run_state")
local character = require("rpg.domain.character")
local definitions = require("rpg.domain.definitions")
local fixture = require("support.engine_fixture")
local helpers = require("support.helpers")
local T = require("support.test")

local GROUP = "unit/auto_equip"

local function new_engine(data, auto)
	if auto == nil then
		auto = true
	end
	local engine = GameEngine.new_run(data, run_state.RunConfig.new("Auto", "warrior", "normal", auto), 42)
	return engine
end

local function uids(items)
	local result = {}
	for index, item in ipairs(items) do
		result[index] = item.uid
	end
	return result
end

T.test(GROUP, "better item is equipped and the old one sold", function()
	local game_data = helpers.with_test_items(helpers.data())
	local engine = new_engine(game_data)
	local state = engine.state
	local starter = state.player.equipment.weapon
	local axe = fixture.item(50, "test_axe", "common", 0)
	state.player.bag[#state.player.bag + 1] = axe
	local gold = state.player.gold
	local rng_state = engine:rng_state()
	local events = auto_equip(state, game_data)
	T.eq(events, {
		{
			type = "item_auto_equipped",
			uid = 50,
			itemId = "test_axe",
			slot = "weapon",
			score = character.item_score(axe, game_data),
		},
		{
			type = "item_auto_sold",
			uid = starter.uid,
			itemId = "sword",
			gold = character.item_value(starter, game_data),
		},
	})
	T.eq(state.player.equipment.weapon, axe)
	T.eq(state.player.gold, gold + character.item_value(starter, game_data))
	T.eq(state.player.bag, {})
	T.eq(engine:rng_state(), rng_state)
end)

T.test(GROUP, "empty slots are filled without selling", function()
	local game_data = helpers.with_test_items(helpers.data())
	local state = new_engine(game_data).state
	state.player.bag[#state.player.bag + 1] = fixture.item(60, "test_helmet", "common", 0)
	local events = auto_equip(state, game_data)
	T.eq(fixture.types(events), { "item_auto_equipped" })
	T.eq(state.player.equipment.helmet.uid, 60)
end)

T.test(GROUP, "ties go to the lowest uid and worse items stay", function()
	local game_data = helpers.with_test_items(helpers.data())
	local state = new_engine(game_data).state
	local bag = state.player.bag
	bag[#bag + 1] = fixture.item(72, "test_helmet", "common", 0)
	bag[#bag + 1] = fixture.item(71, "test_helmet", "common", 0)
	bag[#bag + 1] = fixture.item(73, "test_rod", "common", 0)
	auto_equip(state, game_data)
	T.eq(state.player.equipment.helmet.uid, 71)
	T.eq(uids(state.player.bag), { 72, 73 })
	T.eq(auto_equip(state, game_data), {})
end)

T.test(GROUP, "items above the player level are skipped", function()
	local game_data = helpers.with_test_items(helpers.data())
	local state = new_engine(game_data).state
	state.player.bag[#state.player.bag + 1] = fixture.item(80, "test_axe", "mythic", 9)
	T.eq(auto_equip(state, game_data), {})
	state.player.level = 1 + 9 * game_data.balance.item_level_per_tier
	T.eq(auto_equip(state, game_data)[1].uid, 80)
end)

T.test(GROUP, "victory triggers auto equip only when enabled", function()
	local game_data = helpers.with_enemy_class(
		helpers.calm(helpers.with_test_items(helpers.data())),
		"normal",
		{ drop_chance_pct = 100 }
	)
	local weak_sword = definitions.replace(game_data:item("sword"), { stats = {}, stat_order = {} })
	local items = {}
	for index, item in ipairs(game_data.items) do
		items[index] = item.id == "sword" and weak_sword or item
	end
	game_data = game_data:with({ items = items })
	for _, enabled in ipairs({ true, false }) do
		local engine = new_engine(game_data, enabled)
		local monster = fixture.fight(engine)
		monster.hp = 1
		local events = engine:step(commands.Attack())
		T.eq(engine.state.phase, "merchant")
		T.eq(fixture.has_type(events, "item_auto_equipped"), enabled)
		T.eq(engine.state.stats.items_auto_equipped, enabled and 1 or 0)
	end
end)

T.test(GROUP, "buying a stock item triggers auto equip", function()
	local game_data = helpers.with_test_items(helpers.data())
	local engine = new_engine(game_data)
	local state = engine.state
	local axe = fixture.item(90, "test_axe", "common", 0)
	state.merchant_stock = { axe }
	state.player.gold = 10000
	local events = engine:step(commands.BuyStockItem(0))
	T.eq(fixture.types(events), { "item_bought", "item_auto_equipped", "item_auto_sold" })
	T.eq(state.player.equipment.weapon, axe)
	local manual = new_engine(game_data, false)
	manual.state.merchant_stock = { axe }
	manual.state.player.gold = 10000
	T.eq(fixture.types(manual:step(commands.BuyStockItem(0))), { "item_bought" })
end)
