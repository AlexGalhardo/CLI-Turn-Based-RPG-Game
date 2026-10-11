--- Replays every shared/golden scenario; every other implementation runs the same files.

local commands = require("rpg.application.commands")
local GameEngine = require("rpg.application.engine")
local run_state = require("rpg.application.run_state")
local json = require("rpg.domain.json_types")
local filesystem = require("rpg.infrastructure.filesystem")
local golden = require("rpg.tools.golden")
local helpers = require("support.helpers")
local T = require("support.test")

local GROUP = "golden/golden_files"

local function golden_dir()
	return filesystem.join(helpers.shared_dir(), "golden")
end

local function scenario_files()
	local files = {}
	for _, name in ipairs(filesystem.list_dir(golden_dir())) do
		if name:match("%.json$") and name ~= "prng.json" then
			files[#files + 1] = name
		end
	end
	return files
end

local function load(name)
	return json.json_obj(helpers.read_json(filesystem.join(golden_dir(), name)))
end

--- Path of the first difference between two JSON values, or nil when they are equal.
local function difference(actual, expected, path)
	if type(actual) == "table" and type(expected) == "table" and actual ~= json.null and expected ~= json.null then
		for key, value in pairs(expected) do
			local found = difference(actual[key], value, path .. "/" .. tostring(key))
			if found ~= nil then
				return found
			end
		end
		for key in pairs(actual) do
			if expected[key] == nil then
				return path .. "/" .. tostring(key) .. ": unexpected " .. T.show(actual[key])
			end
		end
		return nil
	end
	if actual ~= expected then
		return string.format("%s: expected %s, got %s", path, T.show(expected), T.show(actual))
	end
	return nil
end

local function check(actual, expected, what)
	local found = difference(actual, expected, what)
	T.ok(found == nil, found)
end

T.test(GROUP, "golden files exist", function()
	T.ok(#scenario_files() >= 11)
end)

T.test(GROUP, "replay matches golden", function()
	local data = helpers.data()
	for _, name in ipairs(scenario_files()) do
		local document = load(name)
		local config = run_state.RunConfig.from_dict(document.config)
		local engine, first = GameEngine.new_run(data, config, json.json_int(document.seed))
		local expected_events = json.json_list(document.events)
		check(first, expected_events[1], name .. " run creation")
		for index, raw_command in ipairs(json.json_list(document.commands)) do
			local events = engine:step(commands.command_from_dict(raw_command))
			check(events, expected_events[index + 1], string.format("%s command #%d", name, index))
		end
		check(golden.final_state(engine), document.finalState, name .. " finalState")
		check(json.decode(json.encode(engine.state:to_dict())), document.finalRun, name .. " finalRun")
		check(run_state.RunState.from_dict(document.finalRun):to_dict(), document.finalRun, name .. " round trip")
	end
end)

T.test(GROUP, "recording the scenarios with the bot gives the golden files", function()
	local data = helpers.data()
	for _, scenario in ipairs(golden.scenarios(data)) do
		local recorded = json.decode(json.encode(golden.record(data, scenario)))
		check(recorded, load(scenario.name .. ".json"), scenario.name)
	end
end)

T.test(GROUP, "a tampered golden file is detected", function()
	local document = load("mage-spells.json")
	local events = json.json_list(document.events)
	local last = events[#events]
	last[#last + 1] = { type = "xp_gained", amount = 1, total = 1 }
	local data = helpers.data()
	local engine = GameEngine.new_run(data, run_state.RunConfig.from_dict(document.config), document.seed)
	local produced
	for _, raw_command in ipairs(json.json_list(document.commands)) do
		produced = engine:step(commands.command_from_dict(raw_command))
	end
	T.no(json.equal(produced, last))
end)
