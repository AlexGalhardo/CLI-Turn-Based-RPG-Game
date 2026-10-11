local commands = require("rpg.application.commands")
local loot = require("rpg.application.loot")
local Merchant = require("rpg.application.merchant")
local character = require("rpg.domain.character")
local entities = require("rpg.domain.entities")
local Rng = require("rpg.domain.rng")
local fixture = require("support.engine_fixture")
local helpers = require("support.helpers")
local T = require("support.test")

local GROUP = "unit/merchant_and_loot"

local AffixRoll = entities.AffixRoll
local item = fixture.item

local function error_events(code)
	return { { type = "error", code = code } }
end

local function affix(id, stat, minimum, maximum, per_tier, slots)
	return { id = id, stat = stat, min = minimum, max = maximum, per_tier = per_tier, slots = slots }
end

local function loot_data(data)
	return helpers.with_test_items(data):with({
		affixes = {
			affix("of_power", "attack", 1, 3, 2, { "weapon" }),
			affix("of_the_bear", "maxHp", 5, 10, 5, { "weapon", "helmet" }),
			affix("of_speed", "dodge", 1, 2, 0, { "weapon", "ring" }),
			affix("of_speed_2", "dodge", 1, 2, 0, { "weapon" }),
		},
	})
end

--- Appends `count` common test rings with uids `first_uid`, `first_uid + 1`, … to the bag.
local function fill_bag(player, first_uid, count)
	for offset = 0, count - 1 do
		player.bag[#player.bag + 1] = item(first_uid + offset, "test_ring", "common", 0)
	end
end

T.test(GROUP, "buy potion rules", function()
	local engine = helpers.new_engine()
	local player = engine.state.player
	local gold = player.gold
	T.eq(
		engine:step(commands.BuyPotion("health_potion", 2)),
		{ { type = "potion_bought", potionId = "health_potion", quantity = 2, gold = 100 } }
	)
	T.eq(player.gold, gold - 100)
	T.eq(engine:step(commands.BuyPotion("health_potion", 1)), error_events("not_enough_gold"))
	T.eq(engine:step(commands.BuyPotion("health_potion", 0)), error_events("invalid_quantity"))
	T.eq(engine:step(commands.BuyPotion("great_health_potion", 1)), error_events("potion_locked"))
	T.eq(engine:step(commands.BuyPotion("elixir", 1)), error_events("unknown_potion"))
	T.eq(engine.state.stats.potions_bought.health_potion, 2)
	T.eq(engine.state.stats.gold_spent, 100)
end)

T.test(GROUP, "available potions unlock by round", function()
	local data = helpers.data()
	local engine = helpers.new_engine()
	T.eq(Merchant.available_potions(engine.state, data), { "health_potion", "mana_potion" })
	engine.state.round = 80
	T.eq(#Merchant.available_potions(engine.state, data), #data.potions)
end)

T.test(GROUP, "equip swap sell and unequip", function()
	local game_data = helpers.with_test_items(helpers.data())
	local engine = helpers.new_engine({ data = game_data })
	local player = engine.state.player
	local axe = item(50, "test_axe", "common", 0)
	local rod = item(51, "test_rod", "rare", 0)
	player.bag[#player.bag + 1] = axe
	player.bag[#player.bag + 1] = rod
	local starter_uid = player.equipment.weapon.uid

	T.eq(engine:step(commands.Equip(51)), error_events("cannot_equip"))
	T.eq(engine:step(commands.Equip(50)), {
		{ type = "item_unequipped", uid = starter_uid, itemId = "sword", slot = "weapon" },
		{ type = "item_equipped", uid = 50, itemId = "test_axe", slot = "weapon" },
	})
	T.eq(character.build_sheet(player, game_data).melee_min, 8 + 20)

	local gold = player.gold
	T.eq(
		engine:step(commands.SellItem(51)),
		{ { type = "item_sold", uid = 51, itemId = "test_rod", gold = character.item_value(rod, game_data) } }
	)
	T.eq(player.gold, gold + 250)
	T.eq(engine:step(commands.SellItem(51)), error_events("invalid_item"))
	T.eq(
		engine:step(commands.Unequip("weapon")),
		{ { type = "item_unequipped", uid = 50, itemId = "test_axe", slot = "weapon" } }
	)
	T.eq(engine:step(commands.Unequip("weapon")), error_events("invalid_item"))
	T.eq(engine:step(commands.Equip(999)), error_events("invalid_item"))
end)

T.test(GROUP, "unequip with full bag and hp clamp", function()
	local game_data = helpers.with_test_items(helpers.data())
	local engine = helpers.new_engine({ data = game_data })
	local player = engine.state.player
	player.equipment.helmet = item(70, "test_helmet", "common", 0)
	player.hp = character.build_sheet(player, game_data).max_hp
	fill_bag(player, 100, game_data.balance.bag_capacity)
	T.eq(engine:step(commands.Unequip("helmet")), error_events("bag_full"))
	table.remove(player.bag)
	engine:step(commands.Unequip("helmet"))
	T.eq(player.hp, character.build_sheet(player, game_data).max_hp)
end)

T.test(GROUP, "merchant stock purchase", function()
	local game_data = loot_data(helpers.data())
	local engine = helpers.new_engine({ data = game_data })
	local state = engine.state
	T.eq(#state.merchant_stock, game_data.balance.merchant_stock_size)
	local stock_item = state.merchant_stock[1]
	local price = Merchant.stock_price(stock_item, game_data)
	state.player.gold = price
	T.eq(
		engine:step(commands.BuyStockItem(0)),
		{ { type = "item_bought", uid = stock_item.uid, itemId = stock_item.item_id, gold = price } }
	)
	T.ok(fixture.has(state.player.bag, stock_item))
	T.eq(engine:step(commands.BuyStockItem(0)), error_events("not_enough_gold"))
	T.eq(engine:step(commands.BuyStockItem(9)), error_events("invalid_item"))
	fill_bag(state.player, 200, 30)
	T.eq(engine:step(commands.BuyStockItem(0)), error_events("bag_full"))
	engine:step(commands.NextFight())
	T.eq(state.merchant_stock, {})
end)

T.test(GROUP, "generate item is deterministic and unique affixes", function()
	local game_data = loot_data(helpers.data())
	local vocation = game_data:vocation("warrior")
	local weights = game_data.balance:enemy_class("boss").rarity_weights
	local function batch()
		local result = {}
		for uid = 0, 19 do
			result[#result + 1] = loot.generate_item(
				game_data,
				Rng.new(5),
				{ vocation = vocation, tier = 0, weights = weights, uid = uid }
			)
		end
		return result
	end
	local first_batch = batch()
	T.eq(#first_batch, 20)
	T.eq(first_batch, batch())
	local rng = Rng.new(11)
	for uid = 0, 199 do
		local generated =
			loot.generate_item(game_data, rng, { vocation = vocation, tier = 1, weights = weights, uid = uid })
		T.ok(generated ~= nil)
		T.ok(generated.rarity == "legendary" or generated.rarity == "mythic", generated.rarity)
		local seen = {}
		for _, roll in ipairs(generated.affixes) do
			T.no(seen[roll.stat], "repeated affix stat " .. roll.stat)
			seen[roll.stat] = true
		end
		T.ok(loot.can_use(game_data:item(generated.item_id), vocation))
	end
end)

T.test(GROUP, "generate item without candidates consumes nothing", function()
	local data = helpers.data()
	local rng = Rng.new(3)
	local empty = data:with({ items = {} })
	local weights = data.balance:enemy_class("normal").rarity_weights
	local generated =
		loot.generate_item(empty, rng, { vocation = data:vocation("mage"), tier = 9, weights = weights, uid = 1 })
	T.eq(generated, nil)
	T.eq(rng.state, 3)
end)

T.test(GROUP, "roll rarity skips zero weights and single options", function()
	local data = helpers.data()
	local rng = Rng.new(1)
	T.eq(loot.roll_rarity(data, rng, { rare = 5 }).id, "rare")
	T.eq(loot.roll_rarity(data, rng, { common = 0, mythic = 3 }).id, "mythic")
	T.eq(rng.state, 1)
	local rolled = {}
	for _ = 1, 40 do
		rolled[loot.roll_rarity(data, rng, { common = 1, legendary = 1 }).id] = true
	end
	T.eq(rolled, { common = true, legendary = true })
	T.ne(rng.state, 1)
	T.raises("ValueError", function()
		loot.roll_rarity(data, rng, { common = 0 })
	end, "positive")
end)

T.test(GROUP, "rarities scale base stats and affix counts", function()
	local game_data = helpers.with_test_items(helpers.data())
	local cases = { { "common", 20, 0 }, { "rare", 30, 1 }, { "legendary", 40, 2 }, { "mythic", 60, 2 } }
	for _, case in ipairs(cases) do
		local rarity, attack, affixes = case[1], case[2], case[3]
		local stats, order = character.item_stats(item(1, "test_axe", rarity, 0), game_data)
		T.eq(stats, { attack = attack }, rarity)
		T.eq(order, { "attack" }, rarity)
		local definition = game_data.balance:rarity(rarity)
		T.eq({ definition.affix_min, definition.affix_max }, { affixes, affixes }, rarity)
	end
end)

T.test(GROUP, "item stats apply rarity and affixes", function()
	local game_data = helpers.with_test_items(helpers.data())
	local helmet = item(1, "test_helmet", "legendary", 0, { AffixRoll.new("maxHp", 7) })
	local stats, order = character.item_stats(helmet, game_data)
	T.eq(stats, { armor = 20, maxHp = 107 })
	T.eq(order, { "armor", "maxHp" })
	T.eq(character.item_value(helmet, game_data), 600)
end)

T.test(GROUP, "item score weights final stats", function()
	local game_data = helpers.with_test_items(helpers.data())
	local weights = game_data.balance.item_score_weights
	local common = item(1, "test_helmet", "common", 0)
	T.eq(character.item_score(common, game_data), 10 * weights.armor + 50 * weights.maxHp)
	local scores = {}
	for index, rarity in ipairs({ "common", "rare", "legendary" }) do
		scores[index] = character.item_score(item(1, "test_helmet", rarity, 0), game_data)
	end
	-- Strictly increasing: sorted and with three distinct values.
	T.ok(scores[1] < scores[2] and scores[2] < scores[3])
	local with_affix = item(1, "test_helmet", "common", 0, { AffixRoll.new("dodge", 2) })
	T.eq(character.item_score(with_affix, game_data), character.item_score(common, game_data) + 2 * weights.dodge)
end)

T.test(GROUP, "required level grows with the item tier", function()
	local data = helpers.data()
	local per_tier = data.balance.item_level_per_tier
	T.eq(character.required_level(item(1, "sword", "common", 0), data), 1)
	T.eq(character.required_level(item(1, "sword", "common", 3), data), 1 + 3 * per_tier)
end)

T.test(GROUP, "equip rejects items above the player level", function()
	local game_data = helpers.with_test_items(helpers.data())
	local engine = helpers.new_engine({ data = game_data })
	local player = engine.state.player
	local axe = item(60, "test_axe", "common", 5)
	player.bag[#player.bag + 1] = axe
	local rng_state = engine:rng_state()
	T.eq(engine:step(commands.Equip(60)), error_events("level_too_low"))
	T.ok(fixture.has(player.bag, axe))
	T.eq(engine:rng_state(), rng_state)
	player.level = character.required_level(axe, game_data)
	local events = engine:step(commands.Equip(60))
	T.eq(events[#events], { type = "item_equipped", uid = 60, itemId = "test_axe", slot = "weapon" })
end)

T.test(GROUP, "selling an equipped uid is rejected", function()
	local engine = helpers.new_engine()
	local player = engine.state.player
	local weapon = player.equipment.weapon
	local gold = player.gold
	T.eq(engine:step(commands.SellItem(weapon.uid)), error_events("invalid_item"))
	T.eq(player.equipment.weapon, weapon)
	T.eq(player.gold, gold)
end)
