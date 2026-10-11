--- M8 screens of the UI controller: settings, auto-equip step, auto-battle, victory and the equipment screen.

local profile_module = require("rpg.application.profile")
local character = require("rpg.domain.character")
local entities = require("rpg.domain.entities")
local enums = require("rpg.domain.enums")
local controller_module = require("rpg.presentation.controller")
local render = require("rpg.presentation.render")
local fixture = require("support.controller_fixture")
local helpers = require("support.helpers")
local T = require("support.test")

local GROUP = "unit/controller_arpg"

local View = controller_module.View
local Phase, Slot, Stat = enums.Phase, enums.Slot, enums.Stat
local ItemInstance, AffixRoll = entities.ItemInstance, entities.AffixRoll
local item_score = character.item_score
local any, has, starts_with, ends_with = fixture.any, fixture.has, fixture.starts_with, fixture.ends_with

local function item(uid, item_id, rarity, tier, affixes)
	return ItemInstance.new({ uid = uid, item_id = item_id, rarity = rarity, tier = tier, affixes = affixes })
end

T.test(GROUP, "settings toggle and persist", function()
	local controller = fixture.new_controller()
	controller:press("6")
	local labels = fixture.option_labels(controller)
	T.eq(labels[2], "Auto-equip on new runs: Off")
	T.eq(labels[3], "Auto-battle speed: 1x")
	controller:press("2")
	controller:press("3")
	T.eq(controller.settings, { auto_equip = true, battle_speed = 2 })
	T.eq(controller:auto_battle_interval_ms(), controller_module.AUTO_BATTLE_BASE_MS // 2)
	T.eq(controller.services.settings:load(), { auto_equip = true, battle_speed = 2 })
	controller:press("3")
	T.eq(controller.settings.battle_speed, 1)
	controller:press("1")
	controller:press("1")
	T.eq(controller.view, View.SETTINGS)
	T.eq(controller.services.settings:load(), { locale = "en", auto_equip = true, battle_speed = 1 })
end)

T.test(GROUP, "new run auto equip step marks the default", function()
	local first, dir = fixture.new_controller()
	first.services.settings:save({ locale = "en", auto_equip = true, battle_speed = 1 })
	local controller = fixture.new_controller({ dir = dir })
	fixture.start_run(controller, { auto_equip_key = "0" })
	T.eq(controller.view, View.VOCATION)
	controller:press("1")
	T.eq(controller.view, View.AUTO_EQUIP)
	T.eq(controller:title(), controller:t("new_run.auto_equip"))
	local labels = fixture.option_labels(controller)
	T.ok(ends_with(labels[1], "(default)"))
	T.no(ends_with(labels[2], "(default)"))
	controller:press("1")
	T.eq(controller.view, View.MERCHANT)
	T.ok(controller.session ~= nil)
	T.eq(controller.session:state().config.auto_equip, true)
end)

local function battle_controller(vocation_key)
	local controller = fixture.new_controller()
	fixture.start_run(controller, { vocation_key = vocation_key or "1" })
	controller:press("0")
	T.eq(controller.view, View.BATTLE)
	return controller
end

T.test(GROUP, "auto battle menu and instant run", function()
	local controller = battle_controller()
	controller:press("5")
	T.eq(controller.view, View.AUTO_BATTLE)
	T.eq(fixture.option_keys(controller), { "1", "2", "3", "0" })
	controller:press("0")
	T.eq(controller.view, View.BATTLE)
	controller:press("5")
	controller:press("3")
	T.ok(controller:auto_battle_active())
	T.eq(
		controller.log[#controller.log],
		controller:t("auto_battle.started", { mode = controller:t("auto_battle.balanced") })
	)
	local session = controller.session
	T.ok(session ~= nil)
	local turn = session:state().turn
	controller:press("1")
	T.eq(session:state().turn, turn)
	controller:run_auto_battle()
	T.no(controller:auto_battle_active())
	T.ne(session:state().phase, Phase.BATTLE)
	T.ok(controller.view == View.MERCHANT or controller.view == View.GAME_OVER, controller.view)
	T.eq(controller:auto_battle_step(), false)
end)

T.test(GROUP, "auto battle steps one turn at a time", function()
	local controller = battle_controller()
	local session = controller.session
	T.ok(session ~= nil)
	local monster = session:state().monster
	T.ok(monster ~= nil)
	monster.hp, monster.max_hp = 1000000, 1000000
	controller:press("5")
	controller:press("1")
	local turn = session:state().turn
	session:state().player.hp = 1000000
	T.eq(controller:auto_battle_step(), true)
	T.eq(session:state().turn, turn + 1)
end)

local function victory_controller()
	local data = helpers.data()
	local controller = fixture.new_controller({ data = helpers.calm(data) })
	fixture.start_run(controller)
	local session = controller.session
	T.ok(session ~= nil)
	session:state().round = data.balance.final_round - 1
	controller:press("0")
	for _ = 1, 50 do
		local monster = session:state().monster
		if monster == nil then
			break
		end
		monster.hp = 1
		controller:press("1")
	end
	T.eq(controller.view, View.VICTORY)
	return controller
end

T.test(GROUP, "victory screen end run", function()
	local controller = victory_controller()
	T.eq(controller:title(), controller:t("victory.title"))
	T.contains(controller:body_lines()[1], "Ferumbras")
	controller:press("9")
	T.eq(controller.view, View.VICTORY)
	controller:press("1")
	T.eq(controller.view, View.GAME_OVER)
	T.eq(controller:title(), controller:t("gameover.title_won"))
	T.contains(controller:body_lines()[1], "won the run")
	controller:press("2")
	controller:press("3")
	T.contains(controller:body_lines()[1], "WON")
end)

T.test(GROUP, "victory screen continue", function()
	local controller = victory_controller()
	controller:press("2")
	T.eq(controller.view, View.MERCHANT)
	T.ok(controller.session ~= nil)
	T.eq(controller.session:state().won, true)
end)

T.test(GROUP, "continue saved victory returns to the victory screen", function()
	local controller = victory_controller()
	controller.session = nil
	controller.view = View.TITLE
	controller:press("1")
	T.eq(controller.view, View.VICTORY)
end)

local function equipment_controller()
	local controller = fixture.new_controller({ data = helpers.with_test_items(helpers.data()) })
	fixture.start_run(controller)
	controller:press("3")
	T.eq(controller.view, View.EQUIPMENT)
	return controller
end

T.test(GROUP, "equipment screen lists every slot and the bag", function()
	local controller = equipment_controller()
	local game_data = controller.services.data
	local session = controller.session
	T.ok(session ~= nil)
	local player = session:state().player
	for _, bag_item in ipairs({
		item(900, "test_axe", "rare", 0),
		item(901, "test_rod", "common", 0),
		item(902, "test_helmet", "legendary", 9),
	}) do
		player.bag[#player.bag + 1] = bag_item
	end
	local lines = controller:body_lines()
	local colors = controller:body_colors()
	local starter = player.equipment[Slot.WEAPON]
	T.eq(lines[1], "EQUIPPED · total score " .. item_score(starter, game_data))
	T.ok(starts_with(lines[2], "Weapon: Sword [Common] · Lv 1"), lines[2])
	T.eq(lines[3], "Shield: - empty -")
	T.eq(colors[3], render.STYLE_WARNING)
	local empty = 0
	for _, line in ipairs(lines) do
		empty = empty + (has(line, "- empty -") and 1 or 0)
	end
	T.eq(empty, 7)
	T.eq(lines[#lines], "BAG (usable)")
	local options = controller:options()
	T.eq(fixture.option_keys(controller), { "1", "2", "3", "0" })
	local axe, helmet, slot = options[1], options[2], options[3]
	local delta = item_score(item(900, "test_axe", "rare", 0), game_data) - item_score(starter, game_data)
	T.eq(axe.detail, render.format_delta(delta))
	T.eq(axe.detail_color, render.STYLE_GAIN)
	T.eq(axe.color, "rare")
	T.eq(helmet.color, render.STYLE_DIM)
	T.ok(ends_with(helmet.label, "requires Lv 37"), helmet.label)
	T.eq(slot.label, "Weapon: Sword [Common]")
end)

T.test(GROUP, "comparison shows stat and score deltas", function()
	local controller = equipment_controller()
	local session = controller.session
	T.ok(session ~= nil)
	local player = session:state().player
	player.equipment[Slot.HELMET] = item(800, "test_helmet", "common", 0, { AffixRoll.new(Stat.DODGE, 3) })
	player.bag[#player.bag + 1] = item(801, "test_helmet", "rare", 0, { AffixRoll.new(Stat.CRIT_CHANCE, 2) })
	controller:press("1")
	T.eq(controller.view, View.COMPARE)
	T.eq(controller:title(), "Helmet: Test Helmet → Test Helmet")
	local lines = controller:body_lines()
	local line_colors = controller:body_colors()
	T.eq(#line_colors, #lines)
	local colors = {}
	for index, line in ipairs(lines) do
		colors[line] = line_colors[index]
	end
	T.eq(colors["Armor: 10 → 15 (+5)"], render.STYLE_GAIN)
	T.eq(colors["Max HP: 50 → 75 (+25)"], render.STYLE_GAIN)
	T.eq(colors["Critical chance: 0 → 2 (+2)"], render.STYLE_GAIN)
	T.eq(colors["Dodge: 3 → 0 (-3)"], render.STYLE_LOSS)
	T.eq(colors["Affixes gained: +2 Critical chance"], render.STYLE_GAIN)
	T.eq(colors["Affixes lost: +3 Dodge"], render.STYLE_LOSS)
	T.ok(any(lines, function(line)
		return starts_with(line, "Score: ")
	end))
	T.eq(fixture.option_keys(controller), { "1", "0" })
	controller:press("1")
	T.eq(controller.view, View.EQUIPMENT)
	T.eq(player.equipment[Slot.HELMET].uid, 801)
end)

local function list_key_of(controller, prefix)
	for _, option in ipairs(controller:options()) do
		if starts_with(option.label, prefix) then
			return option.key
		end
	end
	error("no option starts with " .. prefix)
end

T.test(GROUP, "comparison warns about the required level", function()
	local controller = equipment_controller()
	local session = controller.session
	T.ok(session ~= nil)
	local bag = session:state().player.bag
	bag[#bag + 1] = item(810, "test_axe", "common", 3)
	controller:press("1")
	local colors, lines = controller:body_colors(), controller:body_lines()
	T.eq(colors[#colors], render.STYLE_LOSS)
	T.eq(lines[#lines], "Requires level 13 (you are level 1).")
	controller:press("1")
	T.eq(controller.message, controller:t("error.level_too_low"))
	T.eq(controller.view, View.EQUIPMENT)
	controller:press("0")
	controller:press("3")
	controller:press(list_key_of(controller, "Weapon"))
	T.eq(controller.view, View.EQUIPPED_SLOT)
	T.eq(controller:title(), "Weapon")
	T.eq(controller:body_lines()[2], "Attack: 6")
	controller:press("1")
	T.eq(controller.view, View.EQUIPMENT)
	T.eq(session:state().player.equipment[Slot.WEAPON], nil)
	controller:press("0")
end)

T.test(GROUP, "compare and slot views survive missing items", function()
	local controller = equipment_controller()
	local session = controller.session
	T.ok(session ~= nil)
	local lines = controller:body_lines()
	T.eq(lines[#lines], controller:t("equipment.bag_empty"))
	controller.view = View.COMPARE
	T.eq(controller:body_lines(), {})
	T.eq(controller:title(), controller:t("merchant.equipment"))
	controller.view = View.EQUIPPED_SLOT
	controller:press("0")
	controller.view = View.EQUIPPED_SLOT
	local equipment = session:state().player.equipment
	for slot in pairs(equipment) do
		equipment[slot] = nil
	end
	T.eq(controller:body_lines(), { controller:t("equipment.empty") })
end)

T.test(GROUP, "monster view and hall of fame markers", function()
	local elite_data = helpers.with_balance(helpers.data(), { elite_chance_pct = 100 })
	local controller = fixture.new_controller({ data = elite_data })
	local repository = controller.services.repositories.profile
	local profile = repository:load()
	profile.hall_of_fame[#profile.hall_of_fame + 1] = profile_module.HallOfFameEntry.new({
		run_id = "r",
		name = "Ana",
		vocation = "mage",
		difficulty = "hard",
		round = 100,
		level = 50,
		ended_at = "2026-01-01T00:00:00Z",
		won = true,
	})
	repository:save(profile)
	controller:press("3")
	T.contains(controller:body_lines()[1], "WON")
	controller:press("0")
	fixture.start_run(controller)
	controller:press("0")
	local monster = controller:monster_view()
	T.ok(monster ~= nil)
	T.eq(monster.enemy_class, "elite")
	T.ok(any(controller.log, function(line)
		return has(line, "ELITE")
	end))
end)
