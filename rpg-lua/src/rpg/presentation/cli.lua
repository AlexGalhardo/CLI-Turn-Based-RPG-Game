--- Command-line flags, identical in every implementation (docs/tui.md).
--- The messages and the exit behaviour follow Python's argparse, which the reference uses.

local base = require("rpg.domain.base")
local i18n = require("rpg.infrastructure.i18n")
local VERSION = require("rpg.version")

local cli = {}

cli.HELP_TEXT = table.concat({
	"usage: rpg [-h] [--version] [--seed SEED] [--lang {en,pt-BR}] [--no-anim]",
	"           [--data-dir DATA_DIR] [--simulate N] [--vocation VOCATION]",
	"           [--difficulty DIFFICULTY]",
	"",
	"Endless turn-based RPG for the terminal.",
	"",
	"options:",
	"  -h, --help            show this help message and exit",
	"  --version             show program's version number and exit",
	"  --seed SEED           deterministic run",
	"  --lang {en,pt-BR}     override the saved language",
	"  --no-anim             disable animations",
	"  --data-dir DATA_DIR   saves/profile location",
	"  --simulate N          run N headless bot games and print a report",
	"  --vocation VOCATION   (simulator) restrict to one vocation",
	"  --difficulty DIFFICULTY",
	"                        (simulator) restrict to one difficulty",
}, "\n")

--- The usage lines alone: what argparse prints before an error.
cli.USAGE_TEXT = cli.HELP_TEXT:match("^(.-)\n\n")

local VALUE_FLAGS = {
	["--seed"] = "seed",
	["--simulate"] = "simulate",
	["--lang"] = "lang",
	["--data-dir"] = "data_dir",
	["--vocation"] = "vocation",
	["--difficulty"] = "difficulty",
}

local function usage_error(message)
	base.raise("UsageError", message)
end

--- The whole text must be an integer: "12abc" and "" are rejected.
local function parse_integer(text)
	if not text:match("^[+-]?%d+$") then
		return nil
	end
	return math.tointeger(tonumber(text))
end

local function parse_seed(value)
	local seed = parse_integer(value)
	if seed == nil then
		usage_error(string.format("argument --seed: invalid _non_negative value: '%s'", value))
	elseif seed < 0 then
		usage_error("argument --seed: must be >= 0")
	end
	return seed
end

local function parse_runs(value)
	local runs = parse_integer(value)
	if runs == nil then
		usage_error(string.format("argument --simulate: invalid _positive value: '%s'", value))
	elseif runs <= 0 then
		usage_error("argument --simulate: must be > 0")
	end
	return runs
end

local function parse_lang(value)
	if not base.contains(i18n.SUPPORTED_LOCALES, value) then
		usage_error(string.format("argument --lang: invalid choice: '%s' (choose from 'en', 'pt-BR')", value))
	end
	return value
end

local PARSERS = { ["--seed"] = parse_seed, ["--simulate"] = parse_runs, ["--lang"] = parse_lang }

--- CliOptions: seed, lang, no_anim, data_dir, simulate, vocation, difficulty (nil when the flag is absent), plus
--- `exit` (with `output`) for `--help` and `--version`. Raises a `UsageError` for invalid flags.
function cli.parse_args(args)
	local options = { no_anim = false }
	local show_help, show_version = false, false
	local index = 1
	while index <= #args do
		local argument = args[index]
		-- `--flag=value` and `--flag value` are both accepted.
		local flag, inline_value = argument:match("^(%-%-[^=]+)=(.*)$")
		flag = flag or argument
		if flag == "-h" or flag == "--help" then
			show_help = true
		elseif flag == "--version" then
			show_version = true
		elseif flag == "--no-anim" then
			options.no_anim = true
		elseif VALUE_FLAGS[flag] == nil then
			usage_error("unrecognized arguments: " .. argument)
		else
			local value = inline_value
			if value == nil then
				index = index + 1
				value = args[index]
				if value == nil then
					usage_error(string.format("argument %s: expected one argument", flag))
				end
			end
			local parse = PARSERS[flag]
			options[VALUE_FLAGS[flag]] = parse ~= nil and parse(value) or value
		end
		index = index + 1
	end
	-- argparse acts on --help/--version as soon as it meets them, so they win over any other flag.
	if show_help then
		options.exit, options.output = true, cli.HELP_TEXT
	elseif show_version then
		options.exit, options.output = true, string.format("rpg %s (lua)", VERSION)
	end
	return options
end

return cli
