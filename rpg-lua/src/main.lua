--- Entry point. `lua src/main.lua [flags]` during development; the `rpg-lua` binary runs this same file from the
--- sources compiled into it.

local source = debug.getinfo(1, "S").source
if source:sub(1, 1) == "@" then
	-- Run from a file: make `require("rpg.…")` find the modules next to it.
	local directory = source:sub(2):match("^(.*)[/\\][^/\\]*$") or "."
	package.path = directory .. "/?.lua;" .. package.path
end

local main_run = require("rpg.main_run")

os.exit(main_run.run(arg or {}), true)
