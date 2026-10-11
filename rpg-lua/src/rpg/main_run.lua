--- `run(args, out, err)`: flags → simulator or TUI. Returns the process exit code and writes to the given streams,
--- so the whole command line is testable (like `run()` in the Go and C ports).

local simulator = require("rpg.application.simulator")
local base = require("rpg.domain.base")
local data_loader = require("rpg.infrastructure.data_loader")
local paths = require("rpg.infrastructure.paths")
local cli = require("rpg.presentation.cli")
local simulator_report = require("rpg.presentation.simulator_report")

local main_run = {}

local function run_simulator(data, options, out, err)
	local vocations = options.vocation and { options.vocation }
		or base.map(data.vocations, function(vocation)
			return vocation.id
		end)
	local difficulties = options.difficulty and { options.difficulty }
		or base.map(data.balance.difficulties, function(difficulty)
			return difficulty.id
		end)
	-- `options.seed or 1` in the reference: a missing seed and seed 0 both start at 1.
	local base_seed = (options.seed ~= nil and options.seed ~= 0) and options.seed or 1
	local simulated, summaries = base.try({ "ValueError" }, function()
		local result = {}
		for _, vocation in ipairs(vocations) do
			for _, difficulty in ipairs(difficulties) do
				result[#result + 1] = simulator.simulate(data, vocation, difficulty, options.simulate, base_seed)
			end
		end
		return result
	end)
	if not simulated then
		err:write("error: ", summaries.message, "\n")
		return 2
	end
	out:write(simulator_report.render_report(summaries, data), "\n")
	return 0
end

function main_run.run(args, out, err)
	out, err = out or io.stdout, err or io.stderr
	local parsed, options = base.try({ "UsageError" }, cli.parse_args, args)
	if not parsed then
		-- Like argparse: the usage lines, then the error, exit code 2.
		err:write(cli.USAGE_TEXT, "\nrpg: error: ", options.message, "\n")
		return 2
	end
	if options.exit then
		out:write(options.output, "\n")
		return 0
	end

	local loaded, data, shared_dir = base.try({ "DataError", "FileNotFoundError" }, function()
		local directory = paths.find_shared_dir()
		return data_loader.load_game_data(directory), directory
	end)
	if not loaded then
		err:write("rpg: ", data.message, "\n")
		return 1
	end
	if options.simulate ~= nil then
		return run_simulator(data, options, out, err)
	end
	local no_anim_env = os.getenv("RPG_NO_ANIM")
	-- The renderer is only loaded when the game is played, like the reference does with its TUI framework.
	local app = require("rpg.presentation.tui.app")
	return app.run_tui({
		data = data,
		shared_dir = shared_dir,
		data_dir = paths.resolve_data_dir(options.data_dir),
		seed = options.seed,
		lang = options.lang,
		animate = not options.no_anim and (no_anim_env == nil or no_anim_env == ""),
	})
end

return main_run
