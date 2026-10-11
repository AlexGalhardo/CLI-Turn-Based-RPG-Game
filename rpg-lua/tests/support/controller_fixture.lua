--- Builders shared by the controller and TUI tests: a controller on its own temporary data directory, and the key
--- presses that start a run (the `make_controller` / `start_run` helpers of the reference tests).

local repositories = require("rpg.infrastructure.repositories")
local controller_module = require("rpg.presentation.controller")
local helpers = require("support.helpers")

local fixture = {}

--- `options`: data, dir (reuse a data directory), seed (7, as in the reference unit tests), lang ("en"; `false`
--- keeps the saved language, which shows the language screen on a first launch), seed_source.
--- Returns the controller and its data directory.
function fixture.new_controller(options)
	options = options or {}
	local dir = options.dir or helpers.temp_dir()
	local services = {
		data = options.data or helpers.data(),
		shared_dir = helpers.shared_dir(),
		settings = repositories.SettingsRepository.new(dir),
		repositories = repositories.file_repositories(dir),
		clock = helpers.fake_clock(),
		version = "test",
	}
	local lang = options.lang
	if lang == nil then
		lang = "en"
	end
	local controller = controller_module.Controller.new(services, {
		seed = options.seed or 7,
		locale_override = lang or nil,
		seed_source = options.seed_source,
	})
	return controller, dir
end

--- Presses every character of `text` as its own key.
function fixture.type_text(controller, text)
	for character in text:gmatch(utf8.charpattern) do
		controller:press(character)
	end
end

--- `options`: name ("Zed"), vocation_key ("1"), auto_equip_key ("2").
function fixture.start_run(controller, options)
	options = options or {}
	controller:press("2")
	controller:press("2")
	fixture.type_text(controller, options.name or "Zed")
	controller:press("enter")
	controller:press(options.vocation_key or "1")
	controller:press(options.auto_equip_key or "2")
end

function fixture.option_keys(controller)
	local keys = {}
	for index, option in ipairs(controller:options()) do
		keys[index] = option.key
	end
	return keys
end

function fixture.option_labels(controller)
	local labels = {}
	for index, option in ipairs(controller:options()) do
		labels[index] = option.label
	end
	return labels
end

--- Whether any string of `list` satisfies `predicate`.
function fixture.any(list, predicate)
	for _, value in ipairs(list) do
		if predicate(value) then
			return true
		end
	end
	return false
end

function fixture.starts_with(text, prefix)
	return text:sub(1, #prefix) == prefix
end

function fixture.ends_with(text, suffix)
	return text:sub(-#suffix) == suffix
end

function fixture.has(text, part)
	return text:find(part, 1, true) ~= nil
end

return fixture
