--- Test runner: `lua tests/run.lua [filter] [--list]` from `rpg-lua/`.
--- The filter is any substring of "level/file/test name" (`golden/`, `unit/rng`, …).

local root = (arg[0]:gsub("[/\\]?tests[/\\]run%.lua$", ""))
if root == "" then
	root = "."
end
package.path = root .. "/src/?.lua;" .. root .. "/tests/?.lua;" .. package.path

local filesystem = require("rpg.infrastructure.filesystem")
local helpers = require("support.helpers")
local T = require("support.test")

helpers.root = root

local filter, list
for _, argument in ipairs(arg) do
	if argument == "--list" then
		list = true
	else
		filter = argument
	end
end

for _, level in ipairs({ "unit", "integration", "golden", "e2e" }) do
	for _, name in ipairs(filesystem.list_dir(root .. "/tests/" .. level)) do
		local module = name:match("^(.*_test)%.lua$")
		if module ~= nil then
			require(level .. "." .. module)
		end
	end
end

if list then
	for _, case in ipairs(T.tests) do
		print(case.name)
	end
	os.exit(0)
end

local failures = T.run(filter)
helpers.cleanup()
os.exit(failures == 0 and 0 or 1)
