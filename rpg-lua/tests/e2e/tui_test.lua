--- End-to-end tests: the real App (controller + renderer) driven by key presses and read back as plain text.
--- No terminal is involved: no animation unless a test asks for it, fixed seed, temporary data directory.

local GreedyBot = require("rpg.application.bot")
local loot = require("rpg.application.loot")
local Merchant = require("rpg.application.merchant")
local base = require("rpg.domain.base")
local enums = require("rpg.domain.enums")
local filesystem = require("rpg.infrastructure.filesystem")
local repositories = require("rpg.infrastructure.repositories")
local controller_module = require("rpg.presentation.controller")
local render = require("rpg.presentation.render")
local app_module = require("rpg.presentation.tui.app")
local VERSION = require("rpg.version")
local fixture = require("support.controller_fixture")
local helpers = require("support.helpers")
local T = require("support.test")

local GROUP = "e2e/tui"

local View, Phase = controller_module.View, enums.Phase
local list_key = render.list_key

--- `options`: lang (see `fixture.new_controller`), animate, dir.
local function open_game(options)
	options = options or {}
	local controller, dir = fixture.new_controller({ seed = 42, lang = options.lang, dir = options.dir })
	return { controller = controller, dir = dir, app = app_module.App.new(controller, options.animate or false) }
end

local function press(game, key)
	return game.app:handle_key(key)
end

--- Presses every character of `keys` as its own key.
local function type_keys(game, keys)
	for character in keys:gmatch(utf8.charpattern) do
		press(game, character)
	end
end

local function screen_text(game)
	return game.app:render(false)
end

local function state_of(game)
	return game.controller.session:state()
end

local function rows_of(text)
	local rows = {}
	for row in (text .. "\n"):gmatch("(.-)\n") do
		rows[#rows + 1] = row
	end
	return rows
end

--- The screen always fills the terminal: MIN_ROWS rows of exactly MIN_COLUMNS characters.
local function check_full_screen(game)
	local rows = rows_of(screen_text(game))
	local label = "view " .. game.controller.view
	T.eq(#rows, render.MIN_ROWS, label)
	for index, row in ipairs(rows) do
		T.eq(utf8.len(row), render.MIN_COLUMNS, string.format("%s, row %d %q", label, index, row))
	end
end

local function file_exists(game, name)
	return filesystem.exists(filesystem.join(game.dir, name))
end

local function history_count(game)
	local count = 0
	for _, name in ipairs(filesystem.list_dir(filesystem.join(game.dir, "history"))) do
		count = count + (name:match("%.json$") and 1 or 0)
	end
	return count
end

T.test(GROUP, "first launch language then new run flow", function()
	local game = open_game({ lang = false })
	T.eq(game.controller.view, View.LANGUAGE)
	press(game, "2")
	T.eq(game.controller.view, View.TITLE)
	T.contains(screen_text(game), "Nova jornada")
	press(game, "2")
	T.eq(game.controller.view, View.DIFFICULTY)
	press(game, "3")
	type_keys(game, "Ana")
	press(game, "enter")
	T.eq(game.controller.view, View.VOCATION)
	press(game, "3")
	T.eq(game.controller.view, View.AUTO_EQUIP)
	press(game, "2")
	T.eq(game.controller.view, View.MERCHANT)
	local text = screen_text(game)
	T.contains(text, "Ana")
	T.contains(text, "Mago")

	local settings = helpers.read_json(filesystem.join(game.dir, "settings.json"))
	T.eq(settings.locale, "pt-BR")
	T.ok(file_exists(game, "save.json"))
end)

T.test(GROUP, "language switch redraws the screen", function()
	local game = open_game()
	T.contains(screen_text(game), "New run")
	type_keys(game, "61")
	T.eq(game.controller.view, View.LANGUAGE)
	press(game, "2")
	T.eq(game.controller.view, View.SETTINGS)
	press(game, "0")
	local text = screen_text(game)
	T.contains(text, "Nova jornada")
	T.contains(text, "Sair")
	T.lacks(text, "New run")
	check_full_screen(game)
end)

T.test(GROUP, "screen is 100 by 30 and reports small terminals", function()
	local game = open_game()
	local text = screen_text(game)
	local rows = rows_of(text)
	T.eq(#rows, render.MIN_ROWS)
	T.eq(utf8.len(rows[1]), render.MIN_COLUMNS)
	T.contains(text, "v" .. VERSION .. " · Lua")

	local resize = game.controller:t("app.resize", { columns = render.MIN_COLUMNS, rows = render.MIN_ROWS })
	game.app.width = 80
	T.contains(screen_text(game), "100")
	T.eq(screen_text(game), resize)
	T.lacks(screen_text(game), "╭")
	game.app.width, game.app.height = render.MIN_COLUMNS, render.MIN_ROWS - 1
	T.eq(screen_text(game), resize)
	game.app.height = render.MIN_ROWS
	T.contains(screen_text(game), "╭")
end)

T.test(GROUP, "every rendered row is 100 columns wide", function()
	local game = open_game()
	check_full_screen(game)
	-- Title → hall of fame, bestiary (paged), achievements, settings.
	for _, keys in ipairs({ "3", "04", "n", "05", "06" }) do
		type_keys(game, keys)
		check_full_screen(game)
	end
	type_keys(game, "02")
	check_full_screen(game)
	press(game, "2")
	press(game, "enter")
	check_full_screen(game) -- the name prompt with its validation message
	type_keys(game, "Abcdefghijklmnop")
	check_full_screen(game)
	press(game, "enter")
	check_full_screen(game)
	press(game, "3")
	check_full_screen(game)
	press(game, "2")
	T.eq(game.controller.view, View.MERCHANT)
	-- Merchant → potions, quantity, sell, equipment, slot, stock, character.
	for _, keys in ipairs({ "", "1", "1", "\27", "02", "03", "1", "004", "05" }) do
		for character in keys:gmatch(".") do
			press(game, character == "\27" and "escape" or character)
		end
		check_full_screen(game)
	end
	type_keys(game, "00")
	T.eq(game.controller.view, View.BATTLE)
	local monster = state_of(game).monster
	monster.hp, monster.max_hp = 1000000, 1000000
	-- Battle → attack, spells, potions, auto-battle menu.
	for _, keys in ipairs({ "", "1", "2", "03", "05" }) do
		type_keys(game, keys)
		check_full_screen(game)
	end
end)

T.test(GROUP, "ansi output has escapes and plain output has none", function()
	local game = open_game()
	type_keys(game, "22")
	type_keys(game, "Ansi")
	press(game, "enter")
	type_keys(game, "120")
	T.eq(game.controller.view, View.BATTLE)
	local plain = game.app:render(false)
	local ansi = game.app:render(true)
	T.lacks(plain, "\27")
	T.contains(ansi, "\27[")
	T.contains(ansi, "\27[0m")
	-- A 24-bit foreground colour (the accent of the borders).
	T.contains(ansi, "38;2;255;166;43")
	-- Stripping the escapes gives back the plain screen.
	T.eq((ansi:gsub("\27%[[%d;]*m", "")), plain)
end)

T.test(GROUP, "battle merchant save quit and continue", function()
	local game = open_game()
	type_keys(game, "22")
	type_keys(game, "Bo")
	press(game, "enter")
	type_keys(game, "12")
	T.eq(game.controller.view, View.MERCHANT)
	press(game, "1")
	T.eq(game.controller.view, View.BUY_POTIONS)
	press(game, "1")
	T.eq(game.controller.view, View.QUANTITY)
	press(game, "1")
	press(game, "enter")
	T.ok(game.controller.session ~= nil)
	T.eq(state_of(game).player:potion_count("health_potion"), 6)
	type_keys(game, "05")
	T.contains(screen_text(game), "Equipment")
	type_keys(game, "00")
	T.eq(game.controller.view, View.BATTLE)
	T.contains(screen_text(game), "HP")
	local monster = state_of(game).monster
	T.ok(monster ~= nil)
	monster.hp, monster.max_hp = 1000000, 1000000
	press(game, "1")
	press(game, "2")
	T.eq(game.controller.view, View.SPELLS)
	press(game, list_key(0))
	press(game, "3")
	T.eq(game.controller.view, View.POTIONS)
	press(game, "escape")
	T.eq(game.controller.view, View.BATTLE)
	press(game, "q")
	T.eq(game.controller.view, View.TITLE)
	T.contains(screen_text(game), "Continue")
	press(game, "1")
	T.eq(game.controller.view, View.MERCHANT)
	T.ok(game.controller.session ~= nil)
	T.eq(game.controller.session.info.sessions, 2)
end)

--- Position of the first element of `list` equal to `value`, counting from 0 (a list key index).
local function position_of(list, value)
	local index = base.index_of(list, value)
	T.ok(index ~= nil, "missing " .. tostring(value))
	return index - 1
end

--- The keys a player presses to issue `command` from the merchant or battle screen.
local function keys_for(command, controller)
	local data = controller.services.data
	local state = controller.session:state()
	local player = state.player
	local kind = command.type
	if kind == "attack" then
		return { "1" }
	elseif kind == "defend" then
		return { "4" }
	elseif kind == "cast" then
		return { "2", list_key(position_of(data:vocation(player.vocation_id).spells, command.spell_id)) }
	elseif kind == "potion" then
		-- The potion menu lists the owned potions in data order.
		local owned = {}
		for _, potion in ipairs(data.potions) do
			if player:potion_count(potion.id) > 0 then
				owned[#owned + 1] = potion.id
			end
		end
		return { "3", list_key(position_of(owned, command.potion_id)) }
	elseif kind == "next_fight" then
		return { "0" }
	elseif kind == "buy_potion" then
		local index = position_of(Merchant.available_potions(state, data), command.potion_id)
		local keys = { "1", list_key(index) }
		for digit in tostring(command.quantity):gmatch(".") do
			keys[#keys + 1] = digit
		end
		keys[#keys + 1] = "enter"
		keys[#keys + 1] = "0"
		return keys
	elseif kind == "sell_item" then
		local uids = base.map(player.bag, function(item)
			return item.uid
		end)
		return { "2", list_key(position_of(uids, command.uid)), "0" }
	elseif kind == "equip" then
		-- The equipment menu lists the usable bag items first, in bag order.
		local vocation = data:vocation(player.vocation_id)
		local usable = {}
		for _, item in ipairs(player.bag) do
			if loot.can_use(data:item(item.item_id), vocation) then
				usable[#usable + 1] = item.uid
			end
		end
		return { "3", list_key(position_of(usable, command.uid)), "1", "0" }
	elseif kind == "buy_stock_item" then
		return { "4", list_key(command.index), "0" }
	elseif kind == "end_run" then
		return { "1" }
	end
	error("unexpected command " .. tostring(kind))
end

-- Plays a whole run through the UI keys, choosing what the bot would do, until death.
T.test(GROUP, "full run until game over", function()
	local game = open_game()
	local bot = GreedyBot.new(helpers.data())
	type_keys(game, "23")
	type_keys(game, "Hero")
	press(game, "enter")
	type_keys(game, "12")
	local controller = game.controller
	local jumped = false
	for _ = 1, 5000 do
		local session = controller.session
		T.ok(session ~= nil)
		local state = session:state()
		if state.phase == Phase.GAME_OVER then
			break
		end
		if not jumped and state.phase == Phase.MERCHANT and next(state.stats.kills) ~= nil then
			-- After the first kill, skip ahead so the run ends quickly (each fight costs many key presses).
			state.round = 95
			jumped = true
		end
		for _, key in ipairs(keys_for(bot:choose(state), controller)) do
			press(game, key)
		end
	end
	T.ok(jumped)
	T.eq(controller.view, View.GAME_OVER)
	T.contains(screen_text(game), state_of(game).won and "RUN COMPLETE" or "GAME OVER")
	check_full_screen(game)
	press(game, "2")
	press(game, "3")
	T.contains(screen_text(game), "Hero")
	type_keys(game, "05")
	T.contains(screen_text(game), "[x] First Blood")
	type_keys(game, "04")
	T.eq(controller.view, View.BESTIARY)
	type_keys(game, "np0")
	T.eq(press(game, "0"), false)
	T.no(file_exists(game, "save.json"))
	T.eq(history_count(game), 1)
end)

T.test(GROUP, "game over screen", function()
	local game = open_game()
	type_keys(game, "22")
	type_keys(game, "Doom")
	press(game, "enter")
	type_keys(game, "120")
	T.eq(game.controller.view, View.BATTLE)
	local state = state_of(game)
	local creature = game.controller.services.data:creature(state.monster.creature_id)
	state.monster.hp, state.monster.max_hp = 1000000, 1000000
	for _ = 1, 200 do
		if state.phase ~= Phase.BATTLE then
			break
		end
		state.player.hp = 1
		press(game, "4")
	end
	T.eq(game.controller.view, View.GAME_OVER)
	local text = screen_text(game)
	T.contains(text, "GAME OVER")
	T.contains(text, "Doom")
	T.contains(text, creature.name)
	T.contains(text, game.controller:t("gameover.new_run"))
	check_full_screen(game)
	T.no(file_exists(game, "save.json"))
	T.eq(history_count(game), 1)
	press(game, "1")
	T.eq(game.controller.view, View.DIFFICULTY)
end)

-- Every fight is played by the auto-battle (instant without animation) until the run ends.
T.test(GROUP, "full run with auto battle", function()
	local game = open_game()
	type_keys(game, "21")
	type_keys(game, "Auto")
	press(game, "enter")
	type_keys(game, "21")
	local controller = game.controller
	T.ok(controller.session ~= nil)
	T.eq(state_of(game).config.auto_equip, true)
	local modes = { "1", "2", "3" }
	for fight = 0, 4999 do
		local phase = state_of(game).phase
		if phase == Phase.GAME_OVER then
			break
		end
		if phase == Phase.VICTORY then
			T.contains(screen_text(game), "VICTORY")
			press(game, "1")
		else
			type_keys(game, "05")
			press(game, modes[fight % #modes + 1])
			T.no(controller:auto_battle_active())
		end
	end
	T.eq(controller.view, View.GAME_OVER)
	T.ok(state_of(game).round >= 1)
	T.eq(history_count(game), 1)
end)

-- With animation on, the renderer's timer drives the fight: the test sends the timer ticks itself.
T.test(GROUP, "auto battle is paced by a timer", function()
	local dir = helpers.temp_dir()
	repositories.SettingsRepository.new(dir):save(repositories.Settings({ locale = "en", battle_speed = 2 }))
	local game = open_game({ lang = false, animate = true, dir = dir })
	type_keys(game, "21")
	type_keys(game, "Tim")
	press(game, "enter")
	type_keys(game, "120")
	local controller = game.controller
	T.eq(controller.view, View.BATTLE)
	local state = state_of(game)
	T.ok(state.monster ~= nil)
	state.monster.hp, state.monster.max_hp = 1000000, 1000000
	state.player.hp = 1000000
	type_keys(game, "51")
	-- With animation on, the key press only starts the auto-battle: nothing is played until a tick.
	T.ok(controller:auto_battle_active())
	T.eq(controller:auto_battle_interval_ms(), controller_module.AUTO_BATTLE_BASE_MS // 2)
	local turn = state.turn
	for tick = 1, 3 do
		T.eq(game.app:auto_battle_tick(), true)
		T.eq(state.turn, turn + tick)
	end
	-- Keys are ignored while the fight plays itself.
	press(game, "4")
	T.ok(controller:auto_battle_active())
	T.eq(state.turn, turn + 3)
	state.monster.hp = 1
	for _ = 1, 20 do
		if not controller:auto_battle_active() then
			break
		end
		game.app:auto_battle_tick()
	end
	T.no(controller:auto_battle_active())
	T.ne(controller.view, View.BATTLE)
	T.eq(game.app:auto_battle_tick(), false)
end)

T.test(GROUP, "auto battle is instant without animation", function()
	local game = open_game()
	type_keys(game, "21")
	type_keys(game, "Now")
	press(game, "enter")
	type_keys(game, "120")
	T.eq(game.controller.view, View.BATTLE)
	type_keys(game, "51")
	-- The whole fight was played inside the key press.
	T.no(game.controller:auto_battle_active())
	T.ne(state_of(game).phase, Phase.BATTLE)
	T.ne(game.controller.view, View.BATTLE)
	T.eq(game.app.cues, {})
end)

T.test(GROUP, "animation cues are shown one per tick", function()
	local game = open_game({ animate = true })
	type_keys(game, "22")
	type_keys(game, "Cue")
	press(game, "enter")
	type_keys(game, "120")
	local state = state_of(game)
	T.ok(state.monster ~= nil)
	state.monster.hp, state.monster.max_hp = 1000000, 1000000
	press(game, "4")
	-- Defending makes the monster act: its attack is queued as a one-shot animation unless it healed or missed.
	local cues = #game.app.cues
	T.eq(game.controller.animation_cues, {})
	local tick = game.app.tick_count
	game.app:tick()
	T.eq(game.app.tick_count, tick + 1)
	T.eq(#game.app.cues, cues > 0 and cues - 1 or 0)
	T.contains(screen_text(game), "HP")
end)

--- A line reader over `lines` and a writer that keeps everything, standing in for stdin and stdout.
local function fake_streams(lines)
	local input = { position = 0 }
	function input:read(format)
		T.eq(format, "l")
		self.position = self.position + 1
		return lines[self.position]
	end
	local output = { chunks = {}, flushes = 0 }
	function output:write(...)
		for _, chunk in ipairs({ ... }) do
			self.chunks[#self.chunks + 1] = chunk
		end
		return self
	end
	function output:flush()
		self.flushes = self.flushes + 1
	end
	function output:text()
		return table.concat(self.chunks)
	end
	return input, output
end

T.test(GROUP, "line loop reads keys line by line until the input ends", function()
	local game = open_game({ animate = true })
	-- Each line is a sequence of keys followed by Enter; "\r" comes from Windows line endings.
	local input, output = fake_streams({ "22", "Liz\r", "12" })
	app_module.line_loop(game.app, input, output)
	T.eq(game.app.animate, false)
	T.eq(game.controller.view, View.MERCHANT)
	T.eq(state_of(game).player.name, "Liz")
	-- One screen before each read, the last one before the read that hits the end of the input.
	T.eq(input.position, 4)
	T.eq(output.flushes, 4)
	local text = output:text()
	T.lacks(text, "\27")
	T.contains(text, "New run")
	T.contains(text, game.controller:t("new_run.name_invalid"))
	T.contains(text, game.controller:t("merchant.title_start"))
	T.eq(text:sub(-#screen_text(game) - 1), screen_text(game) .. "\n")
end)

T.test(GROUP, "line loop stops when the player quits", function()
	local game = open_game()
	local input, output = fake_streams({ "6", "00", "2" })
	app_module.line_loop(game.app, input, output)
	T.ok(game.controller.exit_requested)
	-- The third line is never read: the second "0" of the second line quits from the title.
	T.eq(input.position, 2)
	T.eq(output.flushes, 2)
	T.contains(output:text(), game.controller:t("settings.title"))
end)
