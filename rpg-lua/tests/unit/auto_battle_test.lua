--- Auto-battle policy decisions (docs/game-design.md §13).

local auto_battle = require("rpg.application.auto_battle")
local commands = require("rpg.application.commands")
local GameEngine = require("rpg.application.engine")
local run_state = require("rpg.application.run_state")
local character = require("rpg.domain.character")
local fixture = require("support.engine_fixture")
local helpers = require("support.helpers")
local T = require("support.test")

local GROUP = "unit/auto_battle"

local AutoBattlePolicy = auto_battle.AutoBattlePolicy
local MODES = auto_battle.AutoBattleMode.values
local MELEE, SPELLS, BALANCED = "melee", "spells", "balanced"
local OFFENSE_TURN = 1

local function battle(data, vocation, round_number)
	local config = run_state.RunConfig.new("Auto", vocation or "warrior", "normal")
	local engine = GameEngine.new_run(data, config, 11)
	engine.state.round = round_number or 0
	engine:step(commands.NextFight())
	return engine
end

local function choose(data, engine, mode, turn)
	engine.state.turn = turn
	return AutoBattlePolicy.new(data, mode):choose(engine.state)
end

local function support_turn(data, mode)
	return data.balance.auto_battle:mode(mode).support_every - 1
end

T.test(GROUP, "offensive actions per mode", function()
	local data = helpers.data()
	local engine = battle(data)
	engine.state.player.mp = 10000
	T.eq(choose(data, engine, MELEE, OFFENSE_TURN + 1), commands.Attack())
	T.eq(choose(data, engine, SPELLS, OFFENSE_TURN), commands.Cast("annihilation"))
	T.eq(choose(data, engine, BALANCED, 2), commands.Cast("annihilation"))
	engine.state.player.mp = data:spell("brutal_strike").mana
	T.eq(choose(data, engine, SPELLS, OFFENSE_TURN), commands.Cast("brutal_strike"))
	engine.state.player.mp = 0
	T.eq(choose(data, engine, SPELLS, OFFENSE_TURN), commands.Attack())
end)

T.test(GROUP, "support turn cadence per mode", function()
	local data = helpers.data()
	local turns = {}
	for index, mode in ipairs(MODES) do
		turns[index] = support_turn(data, mode)
	end
	T.eq(MODES, { MELEE, SPELLS, BALANCED })
	T.eq(turns, { 4, 4, 1 })
end)

T.test(GROUP, "support turn heals below half hp", function()
	local data = helpers.data()
	for _, mode in ipairs(MODES) do
		local engine = battle(data)
		local player = engine.state.player
		local max_hp = character.build_sheet(player, data).max_hp
		player.hp = max_hp * 40 // 100
		player.mp = 10000
		T.eq(choose(data, engine, mode, support_turn(data, mode)), commands.Cast("wound_cleansing"), mode)
		player.mp = 0
		T.eq(choose(data, engine, mode, support_turn(data, mode)), commands.UsePotion("health_potion"), mode)
		fixture.clear(player.potions)
		T.eq(choose(data, engine, mode, support_turn(data, mode)), commands.Attack(), mode)
	end
end)

T.test(GROUP, "heal waits for the support turn above the emergency line", function()
	local data = helpers.data()
	local engine = battle(data)
	local player = engine.state.player
	player.hp = character.build_sheet(player, data).max_hp * 40 // 100
	T.eq(choose(data, engine, MELEE, OFFENSE_TURN), commands.Attack())
end)

T.test(GROUP, "emergency heal on any turn", function()
	local data = helpers.data()
	local engine = battle(data)
	local player = engine.state.player
	player.hp = character.build_sheet(player, data).max_hp * 20 // 100
	player.mp = 10000
	T.eq(choose(data, engine, MELEE, OFFENSE_TURN), commands.Cast("wound_cleansing"))
end)

T.test(GROUP, "best potion is the strongest owned", function()
	local data = helpers.data()
	local engine = battle(data)
	local player = engine.state.player
	player.hp = 1
	player.mp = 0
	player.potions.strong_health_potion = 1
	T.eq(choose(data, engine, MELEE, OFFENSE_TURN), commands.UsePotion("strong_health_potion"))
end)

T.test(GROUP, "support turn drinks mana when low", function()
	local data = helpers.data()
	local engine = battle(data, "mage")
	engine.state.player.mp = 0
	local turn = support_turn(data, SPELLS)
	T.eq(choose(data, engine, SPELLS, turn), commands.UsePotion("mana_potion"))
	fixture.clear(engine.state.player.potions)
	T.eq(choose(data, engine, SPELLS, turn), commands.Attack())
end)

T.test(GROUP, "support turn defends against a telegraphed charge", function()
	local data = helpers.data()
	local engine = battle(data, "warrior", 9)
	local boss = engine.state.monster
	T.ok(boss ~= nil)
	T.ok(boss.is_boss)
	local every = data.balance.boss_telegraph_every
	boss.boss_actions = every
	local turn = support_turn(data, MELEE)
	T.eq(choose(data, engine, MELEE, turn), commands.Defend())
	T.eq(choose(data, engine, MELEE, turn + 1), commands.Attack())
	boss.boss_actions = 0
	T.eq(choose(data, engine, MELEE, turn), commands.Attack())
end)

T.test(GROUP, "policy finishes fights with valid commands", function()
	local data = helpers.data()
	for _, mode in ipairs(MODES) do
		local engine = battle(data, "archer")
		local policy = AutoBattlePolicy.new(data, mode)
		for _ = 1, 500 do
			if engine.state.phase ~= "battle" then
				break
			end
			local events = engine:step(policy:choose(engine.state))
			T.no(fixture.has_type(events, "error"), mode)
		end
		T.ne(engine.state.phase, "battle", mode)
	end
end)

T.test(GROUP, "policy is deterministic", function()
	local data = helpers.data()
	local engine = battle(data, "mage")
	local rng_state = engine:rng_state()
	local function decisions()
		local result = {}
		for _, mode in ipairs(MODES) do
			for turn = 0, 5 do
				result[#result + 1] = choose(data, engine, mode, turn)
			end
		end
		return result
	end
	local first = decisions()
	local second = decisions()
	T.eq(#first, 18)
	T.eq(first, second)
	T.eq(engine:rng_state(), rng_state)
end)
