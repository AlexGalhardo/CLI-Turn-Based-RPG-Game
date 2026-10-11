--- Headless balance simulator: the bot plays many runs and we aggregate how often it wins and how far it gets.

local GreedyBot = require("rpg.application.bot")
local GameEngine = require("rpg.application.engine")
local run_state = require("rpg.application.run_state")
local base = require("rpg.domain.base")
local enums = require("rpg.domain.enums")

local MAX_STEPS_PER_RUN = 200000

local simulator = {}

--- RunResult: round, level, death_cause, won.
function simulator.play_one(data, config, seed)
	local engine = GameEngine.new_run(data, config, seed)
	local bot = GreedyBot.new(data)
	local steps = 0
	while engine.state.phase ~= enums.Phase.GAME_OVER do
		if steps == MAX_STEPS_PER_RUN then
			base.raise("RuntimeError", string.format("run did not finish (seed %d)", seed))
		end
		engine:step(bot:choose(engine.state))
		steps = steps + 1
	end
	local state = engine.state
	return { round = state.round, level = state.player.level, death_cause = state.death_cause or "", won = state.won }
end

local function percentile(sorted_values, percent)
	return sorted_values[math.min(#sorted_values - 1, #sorted_values * percent // 100) + 1]
end

--- SimulationSummary: vocation, difficulty, runs, wins, win_rate_pct, min_round, p10_round, median_round,
--- p90_round, max_round, mean_level, top_killers ({ { creature_id, deaths }, … }, at most three).
function simulator.simulate(data, vocation, difficulty, runs, base_seed)
	if runs <= 0 then
		base.raise("ValueError", "runs must be positive")
	end
	base_seed = base_seed or 1
	local rounds, killers = {}, {}
	local wins, levels = 0, 0
	for index = 0, runs - 1 do
		local result = simulator.play_one(data, run_state.RunConfig.new("Bot", vocation, difficulty), base_seed + index)
		rounds[#rounds + 1] = result.round
		levels = levels + result.level
		if result.won then
			wins = wins + 1
		end
		if result.death_cause ~= "" then
			killers[result.death_cause] = (killers[result.death_cause] or 0) + 1
		end
	end
	table.sort(rounds)
	local ranking = {}
	for _, creature_id in ipairs(base.sorted_keys(killers)) do
		ranking[#ranking + 1] = { creature_id, killers[creature_id] }
	end
	ranking = base.sorted(ranking, function(a, b)
		return a[2] > b[2]
	end)
	return {
		vocation = vocation,
		difficulty = difficulty,
		runs = runs,
		wins = wins,
		win_rate_pct = wins * 100 // runs,
		min_round = rounds[1],
		p10_round = percentile(rounds, 10),
		median_round = percentile(rounds, 50),
		p90_round = percentile(rounds, 90),
		max_round = rounds[#rounds],
		mean_level = levels // runs,
		top_killers = table.move(ranking, 1, math.min(3, #ranking), 1, {}),
	}
end

return simulator
