--- Hand-written ANSI renderer of the UI controller (layout specified in docs/tui.md).
---
--- `App` is pure: `handle_key`, `tick` and `auto_battle_tick` change it and `render` turns it into one string, so the
--- screen is testable without a terminal (`render(false)` drops the colours). The impure terminal loop is
--- `app.run_tui` at the end of the file, on top of `tui/terminal.lua`.

local base = require("rpg.domain.base")
local art = require("rpg.infrastructure.art")
local repositories = require("rpg.infrastructure.repositories")
local controller_module = require("rpg.presentation.controller")
local render = require("rpg.presentation.render")
local terminal = require("rpg.presentation.tui.terminal")
local VERSION = require("rpg.version")

local app = {}

app.ANIMATION_MS = 500

local LOG_LINES = 5
local TWO_COLUMN_THRESHOLD = 4
local COLUMN_WIDTH = 44
local ART_WIDTH = 32
local TOP_HEIGHT = 8
local PLAYER_HEIGHT = 5
local LOG_HEIGHT = 7
local MIN_MENU_HEIGHT = 10
local ACCENT = "#ffa62b"
local KEY_COLOR = "#5fd7ff"

-- The controller and render.lua speak in colour names (elements, rarities, semantic styles, plain colours); this is
-- the one place that knows their RGB values.
local COLORS = {
	physical = "#ffffff",
	fire = "#ff5f5f",
	ice = "#5fd7ff",
	energy = "#d75fff",
	earth = "#5fd75f",
	holy = "#ffd75f",
	death = "#8a8a8a",
	common = "#ffffff",
	rare = "#1e90ff",
	legendary = "#ffaf00",
	mythic = "#af87ff",
	warning = "#ffd75f",
	gain = "#5fd75f",
	loss = "#ff5f5f",
	dim = "#808080",
	green = "#5fd75f",
	yellow = "#ffd75f",
	red = "#ff5f5f",
	blue = "#5f87ff",
}

local function color_hex(name)
	return name and COLORS[name] or nil
end

-- ── styled text ─────────────────────────────────────────────────────────────
-- A screen is a list of lines, a line is a list of spans, a span is a text with one style:
-- `{ text = "…", color = "#rrggbb" or nil, bold = …, italic = …, underline = … }`.

local BOLD = { bold = true }
local ITALIC = { italic = true }
local TITLE_STYLE = { bold = true, underline = true }

local function add(line, text, color, style)
	style = style or {}
	line[#line + 1] =
		{ text = text, color = color, bold = style.bold, italic = style.italic, underline = style.underline }
	return line
end

local function width_of(text)
	return utf8.len(text) or #text
end

--- The first `count` characters of `text` (all of it when it is shorter).
local function prefix(text, count)
	if count <= 0 then
		return ""
	end
	local stop = utf8.offset(text, count + 1)
	return stop ~= nil and text:sub(1, stop - 1) or text
end

--- Appends `source` to `target`, truncated to `width` visible characters and padded with spaces.
local function fit(target, source, width)
	local used = 0
	for _, span in ipairs(source or {}) do
		if used >= width then
			break
		end
		local text = prefix(span.text, width - used)
		add(target, text, span.color, span)
		used = used + width_of(text)
	end
	add(target, string.rep(" ", width - used))
end

--- A bordered box of `height` rows: `rows` are truncated or padded to fill it. `title_row` (optional) replaces the
--- top border.
local function panel(screen, width, height, rows, title_row)
	if title_row ~= nil then
		screen[#screen + 1] = add({}, title_row, ACCENT)
	else
		screen[#screen + 1] = add({}, "╭" .. string.rep("─", width - 2) .. "╮", ACCENT)
	end
	for index = 1, height - 2 do
		local row = add({}, "│ ", ACCENT)
		fit(row, rows[index], width - 4)
		screen[#screen + 1] = add(row, " │", ACCENT)
	end
	screen[#screen + 1] = add({}, "╰" .. string.rep("─", width - 2) .. "╯", ACCENT)
end

-- ── update ──────────────────────────────────────────────────────────────────

local App = base.class("App")
app.App = App

--- With `animate` off (--no-anim, tests) only the first art frame is shown and an auto-battle plays instantly.
function App.new(controller, animate, art_library)
	return setmetatable({
		controller = controller,
		animate = animate,
		tick_count = 0,
		-- One-shot monster animations ("hurt", "attack") still to be shown, one per animation tick.
		cues = {},
		width = render.MIN_COLUMNS,
		height = render.MIN_ROWS,
		_art = art_library or art.ArtLibrary.new(controller.services.shared_dir),
	}, App)
end

function App:_take_cues()
	self.cues = self.animate and base.copy_list(self.controller.animation_cues) or {}
	self.controller.animation_cues = {}
end

--- Forwards a key to the controller. Returns false when the player asked to leave.
function App:handle_key(key)
	local controller = self.controller
	if key == "ctrl+c" then
		return false
	end
	controller:press(key)
	if controller.exit_requested then
		return false
	end
	-- Without animation an auto-battle plays instantly; otherwise the terminal loop paces it.
	if controller:auto_battle_active() and not self.animate then
		controller:run_auto_battle()
	end
	self:_take_cues()
	return true
end

--- Animation timer (500 ms): advances the idle loop and consumes one one-shot cue.
function App:tick()
	self.tick_count = self.tick_count + 1
	table.remove(self.cues, 1)
end

--- Auto-battle timer: plays one turn. Returns whether the fight goes on.
function App:auto_battle_tick()
	local running = self.controller:auto_battle_step()
	self:_take_cues()
	return running
end

-- ── rendering ───────────────────────────────────────────────────────────────

local function add_bar(line, color, current, maximum)
	return add(line, render.bar(current, maximum), color)
end

--- The top box: monster art on the left, monster (or game title) information on the right.
function App:_render_top(screen)
	local controller = self.controller
	local info = {}
	for index = 1, TOP_HEIGHT - 2 do
		info[index] = {}
	end
	local frame, art_color, art_style

	local monster = controller:monster_view()
	if monster == nil then
		frame = art.frame_for(self._art:load_file("families", "dragon"), "idle", self.tick_count)
		art_style, art_color = BOLD, "#5fd75f"
		add(info[1], controller:t("app.title"), nil, BOLD)
		add(info[2], controller:t("app.subtitle"), nil, ITALIC)
		add(info[4], "v" .. VERSION .. " · Lua")
	else
		local creature = controller.services.data:creature(monster.creature_id)
		local animation = self.cues[1] or "idle"
		frame = art.frame_for(self._art:for_creature(creature), animation, self.tick_count)
		local element_hex = color_hex(monster.element) or "#ffffff"
		local hurt = animation == "hurt"
		art_style, art_color = hurt and BOLD or nil, hurt and "#ff5f5f" or element_hex

		if monster.is_boss then
			add(info[1], controller:t("hud.boss"), "#d75fff", BOLD)
			add(info[1], " ")
		elseif monster.enemy_class == "elite" then
			add(info[1], controller:t("hud.elite"), "#ffd75f", BOLD)
			add(info[1], " ")
		end
		add(info[1], monster.name:upper(), nil, BOLD)
		add(info[2], "HP ", nil, BOLD)
		add_bar(info[2], color_hex(render.hp_color(monster.hp, monster.max_hp)), monster.hp, monster.max_hp)
		add(info[2], string.format("  %d/%d", monster.hp, monster.max_hp))
		add(info[3], monster.details, element_hex)
	end

	local rows = {}
	for index = 1, TOP_HEIGHT - 2 do
		local row = {}
		fit(row, frame[index] ~= nil and add({}, frame[index], art_color, art_style) or nil, ART_WIDTH)
		for _, span in ipairs(info[index]) do
			row[#row + 1] = span
		end
		rows[index] = row
	end

	-- The header sits inside the top border: "╭─ Round 7 · Tier 1 ───╮".
	local header = controller:header()
	local title = "╭─ "
		.. header
		.. " "
		.. string.rep("─", math.max(0, self.width - width_of(header) - 5))
		.. "╮"
	panel(screen, self.width, TOP_HEIGHT, rows, title)
end

function App:_render_player(screen)
	local rows = {}
	local player = self.controller:player_view()
	if player ~= nil then
		rows[1] = add({}, player.summary)
		add(rows[1], "   " .. player.gold, "#ffd75f")
		if player.statuses ~= "" then
			add(rows[1], "   " .. player.statuses, "#ff5f5f")
		end
		rows[2] = add({}, "HP ", nil, BOLD)
		add_bar(rows[2], color_hex(render.hp_color(player.hp, player.max_hp)), player.hp, player.max_hp)
		add(rows[2], string.format("  %d/%d", player.hp, player.max_hp), nil, BOLD)
		rows[3] = add({}, "MP ", nil, BOLD)
		add_bar(rows[3], color_hex("blue"), player.mp, player.max_mp)
		add(rows[3], string.format("  %d/%d   %s", player.mp, player.max_mp, player.xp))
	end
	panel(screen, self.width, PLAYER_HEIGHT, rows)
end

function App:_render_log(screen)
	local log = self.controller.log
	local rows = {}
	for index = math.max(1, #log - LOG_LINES + 1), #log do
		rows[#rows + 1] = add({}, log[index])
	end
	panel(screen, self.width, LOG_HEIGHT, rows)
end

--- One option: "[1] label  detail". In two columns the label is cut and the cell padded to COLUMN_WIDTH.
local function add_option(row, option, two_columns, last_in_row)
	add(row, "[" .. option.key:upper() .. "] ", KEY_COLOR, BOLD)
	local detail = option.detail ~= "" and ("  " .. option.detail) or ""
	local detail_width = width_of(detail)
	local label = option.label
	if two_columns then
		label = prefix(label, math.max(0, COLUMN_WIDTH - 1 - detail_width))
	end
	add(row, label, color_hex(option.color))
	add(row, detail, color_hex(option.detail_color))
	local used = width_of(label) + detail_width
	if two_columns and not last_in_row and used < COLUMN_WIDTH then
		add(row, string.rep(" ", COLUMN_WIDTH - used))
	end
end

--- The bottom box: title, body lines, options, then the input prompt and the message of the last key press.
function App:_render_menu(screen, height)
	local controller = self.controller
	local room = height - 2
	local options = controller:options()
	local body = controller:body_lines()
	local colors = controller:body_colors()
	local prompt = controller:input_prompt()
	local has_message = controller.message ~= ""

	local columns = #options > TWO_COLUMN_THRESHOLD and 2 or 1
	local option_rows = (#options + columns - 1) // columns
	local tail = (#options > 0 and 1 + option_rows or 0) + (prompt ~= nil and 2 or 0) + (has_message and 2 or 0)

	local rows = { add({}, controller:title(), nil, TITLE_STYLE) }

	-- Long bodies (the equipment screen on a small terminal) are cut so the options always stay visible.
	local body_room = math.max(0, room - 1 - tail)
	local shown = #body
	if #body > body_room then
		shown = math.max(0, body_room - 1)
	end
	for index = 1, shown do
		rows[#rows + 1] = add({}, body[index], color_hex(colors[index]))
	end
	if shown < #body then
		rows[#rows + 1] = add({}, "…")
	end

	if #options > 0 then
		rows[#rows + 1] = {}
		local row = {}
		for index, option in ipairs(options) do
			local last_in_row = columns == 1 or index % columns == 0 or index == #options
			add_option(row, option, columns == 2, last_in_row)
			if last_in_row then
				rows[#rows + 1] = row
				row = {}
			end
		end
	end
	if prompt ~= nil then
		rows[#rows + 1] = {}
		rows[#rows + 1] = add({}, prompt, nil, BOLD)
	end
	if has_message then
		rows[#rows + 1] = {}
		rows[#rows + 1] = add({}, controller.message, "#ff5f5f", BOLD)
	end
	panel(screen, self.width, height, rows)
end

function App:_build_screen()
	local controller = self.controller
	local screen = {}
	if self.width < render.MIN_COLUMNS or self.height < render.MIN_ROWS then
		local message = controller:t("app.resize", { columns = render.MIN_COLUMNS, rows = render.MIN_ROWS })
		screen[1] = add({}, message, "#ffd75f", BOLD)
		return screen
	end
	local used = TOP_HEIGHT + PLAYER_HEIGHT
	self:_render_top(screen)
	self:_render_player(screen)
	if not controller_module.PAGED_VIEWS[controller.view] then
		self:_render_log(screen)
		used = used + LOG_HEIGHT
	end
	self:_render_menu(screen, math.max(MIN_MENU_HEIGHT, self.height - used))
	return screen
end

local function span_text(span, ansi)
	local styled = ansi and span.text ~= "" and (span.color or span.bold or span.italic or span.underline)
	if not styled then
		return span.text
	end
	-- SGR: 1 bold, 3 italic, 4 underline, 38;2;r;g;b a 24-bit foreground colour.
	local codes = { "0" }
	if span.bold then
		codes[#codes + 1] = "1"
	end
	if span.italic then
		codes[#codes + 1] = "3"
	end
	if span.underline then
		codes[#codes + 1] = "4"
	end
	if span.color then
		local red, green, blue = span.color:match("^#(%x%x)(%x%x)(%x%x)$")
		codes[#codes + 1] = string.format("38;2;%d;%d;%d", tonumber(red, 16), tonumber(green, 16), tonumber(blue, 16))
	end
	return "\27[" .. table.concat(codes, ";") .. "m" .. span.text .. "\27[0m"
end

--- The whole screen, rows separated by "\n"; with `ansi` the text carries colour escapes.
function App:render(ansi)
	local lines = {}
	for index, line in ipairs(self:_build_screen()) do
		local parts = {}
		for position, span in ipairs(line) do
			parts[position] = span_text(span, ansi)
		end
		lines[index] = table.concat(parts)
	end
	return table.concat(lines, "\n")
end

-- ── the terminal loop (the only impure part) ────────────────────────────────

local function draw(self)
	self.width, self.height = terminal.size()
	-- Home the cursor, clear to the end of every row while writing, then clear whatever is below the frame.
	terminal.write("\27[H" .. self:render(true):gsub("\n", "\27[K\r\n") .. "\27[K\27[J")
end

--- One thread, two timers: wait for a key until the next timer is due, then run whatever is due and redraw.
local function interactive_loop(self)
	local controller = self.controller
	local next_animation = terminal.now_ms() + app.ANIMATION_MS
	local next_auto_battle = nil
	draw(self)
	while true do
		local now = terminal.now_ms()
		local wake = self.animate and next_animation or nil
		if next_auto_battle ~= nil and (wake == nil or next_auto_battle < wake) then
			wake = next_auto_battle
		end
		local timeout = wake ~= nil and math.max(0, wake - now) or -1

		local key = terminal.read_key(timeout)
		if key == false or (key ~= nil and not self:handle_key(key)) then
			return
		end
		now = terminal.now_ms()
		if controller:auto_battle_active() and next_auto_battle == nil then
			next_auto_battle = now + controller:auto_battle_interval_ms()
		end
		if self.animate and now >= next_animation then
			self:tick()
			next_animation = now + app.ANIMATION_MS
		end
		if next_auto_battle ~= nil and now >= next_auto_battle then
			local running = self:auto_battle_tick()
			next_auto_battle = running and (now + controller:auto_battle_interval_ms()) or nil
		end
		draw(self)
	end
end

--- Without an interactive terminal (a pipe, a file): print the screen as plain text and read whole lines, each one a
--- sequence of keys followed by Enter.
local function line_loop(self, input, output)
	self.animate = false
	while true do
		output:write(self:render(false), "\n")
		output:flush()
		local line = input:read("l")
		if line == nil then
			return
		end
		line = line:gsub("\r$", "")
		for _, code in utf8.codes(line, true) do
			if not self:handle_key(utf8.char(code)) then
				return
			end
		end
		if not self:handle_key("enter") then
			return
		end
	end
end
app.line_loop = line_loop

--- `options`: data, shared_dir, data_dir, seed, lang, animate, version. Runs the interactive game until the player
--- quits and returns the process exit code.
function app.run_tui(options)
	local services = {
		data = options.data,
		shared_dir = options.shared_dir,
		settings = repositories.SettingsRepository.new(options.data_dir),
		repositories = repositories.file_repositories(options.data_dir),
		clock = repositories.SystemClock.new(),
		version = VERSION,
	}
	local controller =
		controller_module.Controller.new(services, { seed = options.seed, locale_override = options.lang })
	local self = App.new(controller, options.animate)
	if terminal.start() then
		-- The terminal must be restored even when the game fails, or the shell is left in raw mode.
		local finished, problem = xpcall(interactive_loop, debug.traceback, self)
		terminal.stop()
		if not finished then
			error(problem, 0)
		end
	else
		line_loop(self, io.stdin, io.stdout)
	end
	controller:save_session()
	return 0
end

return app
