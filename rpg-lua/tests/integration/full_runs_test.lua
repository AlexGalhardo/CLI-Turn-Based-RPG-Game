--- Whole runs driven by the bot: the engine must always terminate, be deterministic and survive save/restore.

local GreedyBot = require("rpg.application.bot")
local GameEngine = require("rpg.application.engine")
local run_state = require("rpg.application.run_state")
local json = require("rpg.domain.json_types")
local helpers = require("support.helpers")
local T = require("support.test")

local GROUP = "integration/full_runs"

local MAX_STEPS = 50000

local RunConfig, RunState = run_state.RunConfig, run_state.RunState

local function play_to_death(engine, bot)
	local log = {}
	for _ = 1, MAX_STEPS do
		if engine.state.phase == "game_over" then
			return log
		end
		local events = engine:step(bot:choose(engine.state))
		for _, event in ipairs(events) do
			T.ne(event.type, "error", T.show(event))
		end
		log[#log + 1] = events
	end
	error("run did not finish")
end

local function total_kills(state)
	local total = 0
	for _, kills in pairs(state.stats.kills) do
		total = total + kills
	end
	return total
end

for _, difficulty in ipairs({ "easy", "normal", "hard" }) do
	for _, vocation in ipairs({ "warrior", "archer", "mage" }) do
		T.test(GROUP, string.format("bot plays until the run ends [%s-%s]", vocation, difficulty), function()
			local data = helpers.data()
			local engine = GameEngine.new_run(data, RunConfig.new("Bot", vocation, difficulty), 1234)
			local log = play_to_death(engine, GreedyBot.new(data))
			local state = engine.state
			T.eq(state.phase, "game_over")
			T.ok(state.round >= 1)
			T.ok(state.stats.damage_dealt > 0)
			local last = log[#log]
			if state.won then
				local before = log[#log - 1]
				T.eq(state.round, data.balance.final_round)
				T.eq(state.death_cause, nil)
				T.eq(before[#before], { type = "run_won", round = state.round })
				T.eq(last, { { type = "run_ended", won = true } })
				T.eq(total_kills(state), state.round)
			else
				T.ok(state.death_cause ~= nil and state.death_cause ~= "")
				T.eq(last[#last].type, "player_died")
				T.eq(total_kills(state), state.round - 1)
			end
		end)
	end
end

-- The provisional balance must keep the victory reachable (the balance gate tunes the rates later).
T.test(GROUP, "some bot runs are won", function()
	local data = helpers.data()
	local won = 0
	for seed = 2002, 2005 do
		local engine = GameEngine.new_run(data, RunConfig.new("Bot", "archer", "easy"), seed)
		play_to_death(engine, GreedyBot.new(data))
		if engine.state.won then
			won = won + 1
		end
	end
	T.ok(won > 0)
end)

T.test(GROUP, "same seed same events", function()
	local data = helpers.data()
	local logs = {}
	for attempt = 1, 2 do
		local engine, first = GameEngine.new_run(data, RunConfig.new("Bot", "archer", "normal"), 777)
		local log = play_to_death(engine, GreedyBot.new(data))
		table.insert(log, 1, first)
		logs[attempt] = log
	end
	T.ok(#logs[1] > 1)
	T.eq(logs[1], logs[2])
end)

T.test(GROUP, "different seeds diverge", function()
	local data = helpers.data()
	local finals, distinct = {}, 0
	for seed = 1, 3 do
		local engine = GameEngine.new_run(data, RunConfig.new("Bot", "warrior", "normal"), seed)
		play_to_death(engine, GreedyBot.new(data))
		local final = json.encode(engine.state.stats:to_dict())
		if not finals[final] then
			finals[final] = true
			distinct = distinct + 1
		end
	end
	T.ok(distinct > 1)
end)

T.test(GROUP, "restore mid run continues identically", function()
	local data = helpers.data()
	local bot = GreedyBot.new(data)
	local reference = GameEngine.new_run(data, RunConfig.new("Bot", "mage", "hard"), 99)
	local reference_log = play_to_death(reference, bot)

	local engine = GameEngine.new_run(data, RunConfig.new("Bot", "mage", "hard"), 99)
	local log, restores = {}, 0
	while engine.state.phase ~= "game_over" do
		T.ok(#log < MAX_STEPS, "run did not finish")
		if engine.state.phase == "merchant" and engine.state.round % 3 == 0 then
			local snapshot = json.decode(json.encode(engine.state:to_dict()))
			engine = GameEngine.restore(data, RunState.from_dict(snapshot), engine:rng_state())
			restores = restores + 1
		end
		log[#log + 1] = engine:step(bot:choose(engine.state))
	end
	T.ok(restores > 0)
	T.eq(#log, #reference_log)
	T.eq(log, reference_log)
end)

T.test(GROUP, "new run rejects invalid config", function()
	local data = helpers.data()
	T.raises("ValueError", function()
		GameEngine.new_run(data, RunConfig.new("X", "knight", "normal"), 1)
	end, "invalid run config")
	T.raises("ValueError", function()
		GameEngine.new_run(data, RunConfig.new("X", "mage", "nightmare"), 1)
	end, "invalid run config")
end)
