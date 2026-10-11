--- Tests of the compiled executable. They run only when `RPG_BINARY` points at it:
---
---   RPG_BINARY="$PWD/build/debug/rpg-lua.exe" lua tests/run.lua e2e/binary

local json = require("rpg.domain.json_types")
local filesystem = require("rpg.infrastructure.filesystem")
local i18n = require("rpg.infrastructure.i18n")
local main_run = require("rpg.main_run")
local cli = require("rpg.presentation.cli")
local VERSION = require("rpg.version")
local helpers = require("support.helpers")
local T = require("support.test")

local GROUP = "e2e/binary"

local BINARY = os.getenv("RPG_BINARY")
if BINARY == nil or BINARY == "" then
	return
end

local WINDOWS = filesystem.WINDOWS

local function quote(text)
	if WINDOWS then
		return '"' .. text .. '"'
	end
	return "'" .. text:gsub("'", "'\\''") .. "'"
end

--- A path as the platform shell wants it, quoted.
local function quote_path(path)
	return quote(WINDOWS and path:gsub("/", "\\") or path)
end

--- Runs the binary with `RPG_DATA_DIR` set to a fresh temp directory and stderr merged into stdout.
--- `stdin_path` (optional) is redirected to its standard input. Returns the output, the exit code and the directory.
local function run_binary(arguments, stdin_path)
	local data_dir = helpers.temp_dir()
	local parts = {}
	-- `io.popen` runs cmd.exe on Windows, where the variable is set with a command of its own.
	if WINDOWS then
		parts[1] = string.format('set "RPG_DATA_DIR=%s" &&', data_dir)
	else
		parts[1] = "RPG_DATA_DIR=" .. quote(data_dir)
	end
	parts[#parts + 1] = quote_path(BINARY)
	for _, argument in ipairs(arguments) do
		parts[#parts + 1] = quote(argument)
	end
	if stdin_path ~= nil then
		parts[#parts + 1] = "< " .. quote_path(stdin_path)
	end
	parts[#parts + 1] = "2>&1"
	local pipe = io.popen(table.concat(parts, " "), "r")
	T.ok(pipe ~= nil, "cannot run " .. BINARY)
	local output = pipe:read("a"):gsub("\r\n", "\n")
	local _, _, code = pipe:close()
	return output, code, data_dir
end

--- A stream that collects what is written to it.
local function capture()
	local stream = { parts = {} }
	function stream:write(...)
		for _, part in ipairs({ ... }) do
			self.parts[#self.parts + 1] = tostring(part)
		end
		return self
	end
	function stream:text()
		return table.concat(self.parts)
	end
	return stream
end

T.test(GROUP, "version", function()
	local output, code = run_binary({ "--version" })
	T.eq(output, string.format("rpg %s (lua)\n", VERSION))
	T.eq(code, 0)
end)

T.test(GROUP, "help prints the usage", function()
	local output, code = run_binary({ "--help" })
	T.eq(output, cli.HELP_TEXT .. "\n")
	T.contains(output, "usage: rpg")
	T.contains(output, "--simulate N")
	T.eq(code, 0)
end)

T.test(GROUP, "invalid flag exits with code 2", function()
	local output, code, data_dir = run_binary({ "--bogus" })
	T.eq(code, 2)
	T.contains(output, "usage: rpg")
	T.contains(output, "rpg: error: unrecognized arguments: --bogus")
	T.no(filesystem.exists(filesystem.join(data_dir, "save.json")))
	for _, arguments in ipairs({ { "--seed", "-1" }, { "--simulate", "0" }, { "--lang", "fr" } }) do
		output, code = run_binary(arguments)
		T.eq(code, 2, table.concat(arguments, " "))
		T.contains(output, "rpg: error:")
	end
end)

T.test(GROUP, "simulate prints the same report as the in-process run", function()
	local arguments = { "--simulate", "1", "--vocation", "mage", "--difficulty", "easy", "--seed", "5" }
	local out, err = capture(), capture()
	T.eq(main_run.run(arguments, out, err), 0)
	T.eq(err:text(), "")
	local output, code = run_binary(arguments)
	T.eq(code, 0)
	T.contains(output, "mage")
	T.eq(output, out:text())
end)

T.test(GROUP, "simulate rejects an unknown vocation", function()
	local output, code = run_binary({ "--simulate", "1", "--vocation", "knight" })
	T.eq(code, 2)
	T.contains(output, "invalid run config")
end)

T.test(GROUP, "piped game creates a save", function()
	local input_dir = helpers.temp_dir()
	local data_dir = filesystem.join(input_dir, "data")
	local input = filesystem.join(input_dir, "input.txt")
	-- Language, new run, difficulty, name, vocation, next fight, then an attack; the input ends mid-battle.
	filesystem.write_file(input, table.concat({ "1", "2", "2", "Alex", "1", "2", "0", "1" }, "\n") .. "\n")
	local output, code = run_binary({ "--seed", "42", "--no-anim", "--data-dir", data_dir }, input)
	T.eq(code, 0, output:sub(-400))
	T.contains(output, i18n.Translator.new(helpers.shared_dir()):t("app.title"))
	T.contains(output, "Seed 42")
	-- Plain text without a terminal: no escape sequences.
	T.lacks(output, "\27[")

	local save_path = filesystem.join(data_dir, "save.json")
	T.ok(filesystem.exists(save_path), "save.json was not written")
	local save = json.decode(filesystem.read_file(save_path))
	T.eq(save.schemaVersion, 2)
	T.eq(save.implementation, "lua")
	T.eq(save.gameVersion, VERSION)
	T.eq(save.run.seed, 42)
	T.eq(save.run.config.name, "Alex")
	-- A quit in the middle of a fight keeps the last merchant snapshot.
	T.eq(save.run.phase, "merchant")
	T.ok(filesystem.exists(filesystem.join(data_dir, "settings.json")))
end)
