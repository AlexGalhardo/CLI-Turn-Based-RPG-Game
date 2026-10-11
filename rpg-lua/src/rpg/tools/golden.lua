--- Records the golden scenarios with this implementation (docs/cross-language-parity.md §4).
---
--- Only the Python reference writes shared/golden; here the recorder is what the golden test compares against the
--- committed files, which also proves that the bot issues the same commands.

local GreedyBot = require("rpg.application.bot")
local commands = require("rpg.application.commands")
local GameEngine = require("rpg.application.engine")
local run_state = require("rpg.application.run_state")
local base = require("rpg.domain.base")
local enums = require("rpg.domain.enums")
local json = require("rpg.domain.json_types")

local Phase = enums.Phase
local RunConfig = run_state.RunConfig

local MAX_BOT_STEPS = 20000

local golden = {}

function golden.final_state(engine)
	local state = engine.state
	local player = state.player
	return json.object({
		{ "phase", state.phase },
		{ "round", state.round },
		{ "turn", state.turn },
		{ "level", player.level },
		{ "xp", player.xp },
		{ "magicLevel", player.magic_level },
		{ "hp", player.hp },
		{ "mp", player.mp },
		{ "gold", player.gold },
		{ "rngState", engine:rng_state() },
		{ "nextItemUid", state.next_item_uid },
		{ "stats", state.stats:to_dict() },
	})
end

--- A scenario's `commands` is a function `(engine, index) -> command or nil` (index counts from 0).
function golden.record(data, scenario)
	local engine, first = GameEngine.new_run(data, scenario.config, scenario.seed)
	local recorded = json.array({})
	local events = json.array({ json.array(first) })
	local finished = false
	for index = 0, MAX_BOT_STEPS - 1 do
		local command = scenario.commands(engine, index)
		if command == nil then
			finished = true
			break
		end
		recorded[#recorded + 1] = commands.command_to_dict(command)
		events[#events + 1] = json.array(engine:step(command))
	end
	if not finished then
		base.raise("RuntimeError", "scenario " .. scenario.name .. " did not finish")
	end
	return json.object({
		{ "name", scenario.name },
		{ "seed", scenario.seed },
		{ "config", scenario.config:to_dict() },
		{ "commands", recorded },
		{ "events", events },
		{ "finalState", golden.final_state(engine) },
		{ "finalRun", engine.state:to_dict() },
	})
end

local function scripted(list)
	return function(_, index)
		return list[index + 1]
	end
end

local function bot_until_death(data)
	local bot = GreedyBot.new(data)
	return function(engine)
		if engine.state.phase == Phase.GAME_OVER then
			return nil
		end
		return bot:choose(engine.state)
	end
end

--- Plays `list` in battle, starting a new fight (`next_fight`) whenever the previous one is over.
local function battle_script(list)
	local queue = base.copy_list(list)
	return function(engine)
		local phase = engine.state.phase
		if #queue == 0 or phase == Phase.GAME_OVER then
			return nil
		end
		if phase == Phase.MERCHANT then
			return commands.NextFight()
		end
		return table.remove(queue, 1)
	end
end

--- The bot wins the run; in the victory phase it probes invalid commands, continues and plays a few more rounds.
local function bot_victory_then_continue(data, extra_rounds)
	local bot = GreedyBot.new(data)
	local at_victory = { commands.Attack(), commands.NextFight(), commands.ContinueRun() }
	return function(engine)
		local state = engine.state
		if state.phase == Phase.GAME_OVER then
			return nil
		end
		if state.phase == Phase.VICTORY then
			return table.remove(at_victory, 1)
		end
		if state.won and state.phase == Phase.MERCHANT and state.round >= data.balance.final_round + extra_rounds then
			return nil
		end
		return bot:choose(state)
	end
end

function golden.scenarios(data)
	local c = commands
	local result = {
		{
			name = "merchant-and-errors",
			seed = 7,
			config = RunConfig.new("Alex", "warrior", "normal"),
			commands = scripted({
				c.BuyPotion("health_potion", 1),
				c.BuyPotion("great_health_potion", 1),
				c.BuyPotion("health_potion", 0),
				c.BuyPotion("mana_potion", 99),
				c.BuyStockItem(0),
				c.SellItem(12345),
				c.SellItem(1),
				c.Equip(12345),
				c.Unequip("ring"),
				c.Unequip("weapon"),
				c.Equip(1),
				c.Attack(),
				c.EndRun(),
				c.ContinueRun(),
				c.NextFight(),
				c.EndRun(),
				c.BuyPotion("health_potion", 1),
				c.Cast("flame_strike"),
				c.Defend(),
				c.Cast("brutal_strike"),
				c.UsePotion("mana_potion"),
				c.Attack(),
				c.Attack(),
				c.Attack(),
			}),
		},
		{
			name = "mage-spells",
			seed = 2026,
			config = RunConfig.new("Mia", "mage", "easy"),
			commands = battle_script({
				c.Cast("flame_strike"),
				c.Cast("energy_strike"),
				c.Cast("intense_healing"),
				c.UsePotion("health_potion"),
				c.Cast("eternal_winter"),
				c.Attack(),
				c.Attack(),
				c.Attack(),
				c.Cast("intense_healing"),
				c.Defend(),
				c.Cast("flame_strike"),
			}),
		},
	}
	for _, vocation in ipairs({ "warrior", "archer", "mage" }) do
		for _, difficulty in ipairs({ "easy", "normal", "hard" }) do
			result[#result + 1] = {
				name = string.format("bot-full-run-%s-%s", vocation, difficulty),
				seed = 1000 + #result,
				config = RunConfig.new("Bot", vocation, difficulty),
				commands = bot_until_death(data),
			}
		end
	end
	-- Seed chosen so the bot beats the final boss: covers elites, auto-equip, the victory phase and continue_run.
	result[#result + 1] = {
		name = "bot-victory-continue-archer-easy",
		seed = 2002,
		config = RunConfig.new("Victor", "archer", "easy", true),
		commands = bot_victory_then_continue(data, 3),
	}
	return result
end

return golden
