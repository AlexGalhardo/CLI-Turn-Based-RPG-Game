--- Framework-independent UI state machine: which screen is shown, its options and what each key does.
---
--- The terminal renderer (`tui/app.lua`) only draws this controller. Every port implements the same controller,
--- which is what keeps the interfaces practically identical (docs/tui.md).

local auto_battle = require("rpg.application.auto_battle")
local commands = require("rpg.application.commands")
local GameSession = require("rpg.application.game_session")
local loot = require("rpg.application.loot")
local Merchant = require("rpg.application.merchant")
local profile_module = require("rpg.application.profile")
local run_state = require("rpg.application.run_state")
local base = require("rpg.domain.base")
local character = require("rpg.domain.character")
local enums = require("rpg.domain.enums")
local formulas = require("rpg.domain.formulas")
local i18n = require("rpg.infrastructure.i18n")
local repositories = require("rpg.infrastructure.repositories")
local EventFormatter = require("rpg.presentation.event_text")
local render = require("rpg.presentation.render")

local Phase, SpellKind = enums.Phase, enums.SpellKind
local item_score, item_stats, required_level = character.item_score, character.item_stats, character.required_level
local format_delta, delta_style, list_key = render.format_delta, render.delta_style, render.list_key

local controller = {}

controller.MAX_LOG_LINES = 50
controller.MAX_NAME_LENGTH = 16
controller.MAX_QUANTITY_DIGITS = 2
controller.PAGE_SIZE = 10
-- Auto-battle pace (docs/tui.md): one turn every 600 ms at 1x, 300 ms at 2x; instant with --no-anim.
controller.AUTO_BATTLE_BASE_MS = 600
-- Safety net: a fight that somehow never ends hands control back to the player.
controller.MAX_AUTO_BATTLE_TURNS = 10000

local View = {
	LANGUAGE = "language",
	TITLE = "title",
	SETTINGS = "settings",
	DIFFICULTY = "difficulty",
	NAME = "name",
	VOCATION = "vocation",
	AUTO_EQUIP = "auto_equip",
	MERCHANT = "merchant",
	BUY_POTIONS = "buy_potions",
	QUANTITY = "quantity",
	SELL = "sell",
	EQUIPMENT = "equipment",
	COMPARE = "compare",
	EQUIPPED_SLOT = "equipped_slot",
	STOCK = "stock",
	CHARACTER = "character",
	BATTLE = "battle",
	SPELLS = "spells",
	POTIONS = "potions",
	AUTO_BATTLE = "auto_battle",
	VICTORY = "victory",
	GAME_OVER = "game_over",
	HALL_OF_FAME = "hall_of_fame",
	BESTIARY = "bestiary",
	ACHIEVEMENTS = "achievements",
}
controller.View = View

local BATTLE_VIEWS = { battle = true, spells = true, potions = true, auto_battle = true }
local TEXT_INPUT_VIEWS = { name = true, quantity = true }
local PAGED_VIEWS = { hall_of_fame = true, bestiary = true, achievements = true, character = true }
local STYLED_VIEWS = { equipment = true, compare = true, equipped_slot = true }
controller.PAGED_VIEWS = PAGED_VIEWS

-- The i18n key of each view's title, when it needs no parameter.
local TITLE_KEYS = {
	language = "language.title",
	title = "app.title",
	settings = "settings.title",
	difficulty = "new_run.difficulty",
	name = "new_run.name",
	vocation = "new_run.vocation",
	auto_equip = "new_run.auto_equip",
	buy_potions = "merchant.buy_potions",
	sell = "merchant.sell_items",
	equipment = "merchant.equipment",
	stock = "merchant.stock",
	character = "merchant.character",
	battle = "battle.title",
	spells = "battle.spells",
	potions = "battle.potions",
	auto_battle = "auto_battle.title",
	victory = "victory.title",
	hall_of_fame = "menu.hall_of_fame",
	bestiary = "menu.bestiary",
	achievements = "menu.achievements",
}

--- MenuOption: key, label, color (or nil), detail ("" when there is none), detail_color (or nil).
local function option(key, label, color, detail, detail_color)
	return { key = key, label = label, color = color, detail = detail or "", detail_color = detail_color }
end

--- BodyLine: text and color (or nil).
local function body_line(text, color)
	return { text = text, color = color }
end

function controller.random_seed()
	return math.random(0, 0xFFFFFFFF)
end

local function trim(text)
	return (text:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function length(text)
	return utf8.len(text) or #text
end

--- A single printable character (one code point that is not a control character).
local function is_printable(key)
	return utf8.len(key) == 1 and not key:match("^%c")
end

local Controller = base.class("Controller")
controller.Controller = Controller

--- `services`: data, shared_dir, settings (SettingsRepository), repositories, clock, version.
--- `options`: seed, locale_override, seed_source (a function returning the seed of a new run).
function Controller.new(services, options)
	options = options or {}
	local self = setmetatable({
		services = services,
		_data = services.data,
		_seed = options.seed,
		_seed_source = options.seed_source or controller.random_seed,
	}, Controller)
	local settings = services.settings:load()
	self.settings = settings
	self:_set_locale(options.locale_override or settings.locale or i18n.DEFAULT_LOCALE)
	self.view = (options.locale_override or settings.locale) and View.TITLE or View.LANGUAGE
	self._language_return = View.TITLE
	self.session = nil
	self.log = {}
	self.message = ""
	self.input_buffer = ""
	self.exit_requested = false
	self.animation_cues = {}
	self.page = 0
	self._difficulty = "normal"
	self._name = ""
	self._vocation = ""
	self._potion_id = ""
	self._compare_uid = 0
	self._slot = enums.Slot.WEAPON
	self._auto_battle = nil
	self._auto_mode = auto_battle.AutoBattleMode.MELEE
	self._auto_turns = 0
	return self
end

-- ── i18n ────────────────────────────────────────────────────────────────────

function Controller:_set_locale(locale)
	self.locale = locale
	self._translator = i18n.Translator.new(self.services.shared_dir, locale)
	self._formatter = EventFormatter.new(self._data, self._translator)
end

function Controller:t(key, params)
	return self._translator:t(key, params)
end

function Controller:_log(text)
	self.log[#self.log + 1] = text
	if #self.log > controller.MAX_LOG_LINES then
		table.remove(self.log, 1)
	end
end

-- ── queries used by renderers ───────────────────────────────────────────────

function Controller:title()
	local view = self.view
	if view == View.MERCHANT then
		local round_number = self.session ~= nil and self.session:state().round or 0
		if round_number == 0 then
			return self:t("merchant.title_start")
		end
		return self:t("merchant.title", { round = round_number })
	elseif view == View.QUANTITY then
		return self:t("merchant.quantity", { name = self._data:potion(self._potion_id).name })
	elseif view == View.COMPARE then
		return self:_compare_title()
	elseif view == View.EQUIPPED_SLOT then
		return self:t("slot." .. self._slot)
	elseif view == View.GAME_OVER then
		local won = self.session ~= nil and self.session:state().won
		return self:t(won and "gameover.title_won" or "gameover.title")
	end
	return self:t(TITLE_KEYS[view])
end

function Controller:options()
	local result = {}
	for index, entry in ipairs(self:_menu()) do
		result[index] = entry.option
	end
	return result
end

--- Informative lines shown above the options (paged for long lists).
function Controller:body_lines()
	local lines
	if STYLED_VIEWS[self.view] then
		lines = base.map(self:_styled_body(), function(line)
			return line.text
		end)
	else
		lines = self:_body()
	end
	local page_size = controller.PAGE_SIZE
	if not PAGED_VIEWS[self.view] or #lines <= page_size then
		return lines
	end
	local pages = (#lines + page_size - 1) // page_size
	self.page = math.min(self.page, pages - 1)
	local start = self.page * page_size
	local shown = table.move(lines, start + 1, math.min(start + page_size, #lines), 1, {})
	shown[#shown + 1] = ""
	shown[#shown + 1] = self:t("menu.page", { page = self.page + 1, pages = pages })
	return shown
end

--- Colour of each line of `body_lines()` (semantic styles from render.lua or rarity ids); `false` for no colour,
--- because a Lua list cannot hold `nil`.
function Controller:body_colors()
	if STYLED_VIEWS[self.view] then
		return base.map(self:_styled_body(), function(line)
			return line.color or false
		end)
	end
	return base.map(self:body_lines(), function()
		return false
	end)
end

--- The text field of the name and quantity screens, or nil.
function Controller:input_prompt()
	if TEXT_INPUT_VIEWS[self.view] then
		return "> " .. self.input_buffer .. "_"
	end
	return nil
end

function Controller:header()
	if self.session == nil then
		return self:t("app.subtitle")
	end
	local state = self.session:state()
	local data = self._data
	local tier = formulas.round_info(math.max(1, state.round), data.balance, data.tier_count).tier + 1
	local difficulty = self:t("difficulty." .. state.config.difficulty_id)
	local round_text = self:t("hud.round", { round = state.round, tier = tier, difficulty = difficulty })
	return round_text .. " · " .. self:t("hud.seed", { seed = state.seed })
end

function Controller:_status_list(statuses)
	local parts = {}
	for index, status in ipairs(statuses) do
		parts[index] = string.format("%s(%d)", self:t("status." .. status.status_id), status.turns)
	end
	return table.concat(parts, " ")
end

function Controller:_elements(creature, matches)
	local names = {}
	for _, element in ipairs(enums.Element.values) do
		if matches(creature:resistance(element)) then
			names[#names + 1] = self:t("element." .. element)
		end
	end
	return names
end

--- MonsterView (name, creature_id, hp, max_hp, is_boss, enemy_class, element, details), or nil outside a fight.
function Controller:monster_view()
	local monster = self.session ~= nil and self.session:state().monster or nil
	if monster == nil then
		return nil
	end
	local creature = self._data:creature(monster.creature_id)
	local main
	for _, attack in ipairs(monster.attacks) do
		if main == nil or attack.weight > main.weight or (attack.weight == main.weight and attack.id > main.id) then
			main = attack
		end
	end
	local weak = self:_elements(creature, function(resistance)
		return resistance > 100
	end)
	local parts, seen = {}, {}
	for _, attack in ipairs(monster.attacks) do
		local name = self:t("element." .. attack.element)
		if not seen[name] then
			seen[name] = true
			parts[#parts + 1] = name
		end
	end
	local details = table.concat(parts, " · ")
	if #weak > 0 then
		details = details .. " · " .. self:t("hud.weak", { elements = table.concat(weak, ", ") })
	end
	if #monster.statuses > 0 then
		details = details .. " · " .. self:_status_list(monster.statuses)
	end
	return {
		name = creature.name,
		creature_id = creature.id,
		hp = monster.hp,
		max_hp = monster.max_hp,
		is_boss = monster.is_boss,
		enemy_class = monster.enemy_class,
		element = main.element,
		details = details,
	}
end

--- PlayerView (summary, gold, hp, max_hp, mp, max_mp, xp, statuses), or nil without a run.
function Controller:player_view()
	if self.session == nil then
		return nil
	end
	local player = self.session:state().player
	local sheet = character.build_sheet(player, self._data)
	return {
		summary = self:t("hud.player", {
			name = player.name,
			vocation = self:t("vocation." .. player.vocation_id),
			level = player.level,
			magicLevel = player.magic_level,
		}),
		gold = self:t("hud.gold", { gold = player.gold }),
		hp = player.hp,
		max_hp = sheet.max_hp,
		mp = player.mp,
		max_mp = sheet.max_mp,
		xp = self:t("hud.xp", { xp = player.xp, next = formulas.xp_for_level(player.level + 1) }),
		statuses = self:_status_list(player.statuses),
	}
end

-- ── input ───────────────────────────────────────────────────────────────────

function Controller:press(key)
	if self:auto_battle_active() then
		return
	end
	self.message = ""
	if TEXT_INPUT_VIEWS[self.view] then
		self:_text_input(key)
		return
	end
	if PAGED_VIEWS[self.view] and (key == "n" or key == "p") then
		self.page = math.max(0, self.page + (key == "n" and 1 or -1))
		return
	end
	if key == "escape" then
		key = "0"
	end
	for _, entry in ipairs(self:_menu()) do
		if entry.option.key == key then
			entry.action()
			return
		end
	end
end

-- ── auto-battle (docs/game-design.md §13, docs/tui.md) ──────────────────────

function Controller:auto_battle_active()
	return self._auto_battle ~= nil
end

function Controller:auto_battle_interval_ms()
	return controller.AUTO_BATTLE_BASE_MS // self.settings.battle_speed
end

--- Plays one auto-battle turn. Returns true while the fight goes on (the renderer's timer keeps ticking).
function Controller:auto_battle_step()
	local policy = self._auto_battle
	local session = self.session
	if policy == nil or session == nil or session:state().phase ~= Phase.BATTLE then
		self._auto_battle = nil
		return false
	end
	self._auto_turns = self._auto_turns + 1
	self:_step(policy:choose(session:state()))
	if session:state().phase ~= Phase.BATTLE or self._auto_turns >= controller.MAX_AUTO_BATTLE_TURNS then
		self._auto_battle = nil
	end
	return self:auto_battle_active()
end

--- Instant mode (--no-anim): plays the whole fight at once.
function Controller:run_auto_battle()
	while self:auto_battle_step() do
	end
end

function Controller:_start_auto_battle(mode)
	return function()
		self._auto_mode = mode
		self._auto_turns = 0
		self._auto_battle = auto_battle.AutoBattlePolicy.new(self._data, mode)
		self.view = View.BATTLE
		self:_log(self:t("auto_battle.started", { mode = self:t("auto_battle." .. mode) }))
	end
end

function Controller:_text_input(key)
	if key == "escape" then
		self.input_buffer = ""
		self.view = self.view == View.NAME and View.DIFFICULTY or View.BUY_POTIONS
	elseif key == "backspace" then
		local last = utf8.offset(self.input_buffer, -1)
		self.input_buffer = last ~= nil and self.input_buffer:sub(1, last - 1) or ""
	elseif key == "enter" then
		self:_submit_text()
	elseif self.view == View.NAME and is_printable(key) then
		if length(self.input_buffer) < controller.MAX_NAME_LENGTH then
			self.input_buffer = self.input_buffer .. key
		end
	elseif self.view == View.QUANTITY and key:match("^%d$") and #self.input_buffer < controller.MAX_QUANTITY_DIGITS then
		self.input_buffer = self.input_buffer .. key
	end
end

function Controller:_submit_text()
	local text = trim(self.input_buffer)
	self.input_buffer = ""
	if self.view == View.NAME then
		if length(text) < 1 or length(text) > controller.MAX_NAME_LENGTH then
			self.message = self:t("new_run.name_invalid")
			return
		end
		self._name = text
		self.view = View.VOCATION
		return
	end
	self.view = View.BUY_POTIONS
	local quantity = tonumber(text)
	if quantity ~= nil and quantity > 0 then
		self:_step(commands.BuyPotion(self._potion_id, quantity))
	end
end

-- ── menus ───────────────────────────────────────────────────────────────────

local function entry(menu_option, action)
	return { option = menu_option, action = action }
end

local function with_back(entries, back)
	entries[#entries + 1] = back
	return entries
end

--- The options of the current view with what each one does: `{ { option = MenuOption, action = function }, … }`.
function Controller:_menu()
	local view = self.view
	if view == View.LANGUAGE then
		local menu = {}
		for index, locale in ipairs(i18n.SUPPORTED_LOCALES) do
			menu[index] = entry(option(tostring(index), self:t("language." .. locale)), self:_choose_language(locale))
		end
		return menu
	elseif view == View.TITLE then
		return self:_title_menu()
	elseif view == View.SETTINGS then
		return self:_settings_menu()
	elseif view == View.DIFFICULTY then
		local menu = {}
		for index, difficulty in ipairs(self._data.balance.difficulties) do
			local key = "difficulty." .. difficulty.id
			local label = self:t(key) .. " — " .. self:t(key .. ".description")
			menu[index] = entry(option(tostring(index), label), self:_choose_difficulty(difficulty.id))
		end
		return with_back(menu, self:_back(View.TITLE))
	elseif view == View.VOCATION then
		local menu = {}
		for index, vocation in ipairs(self._data.vocations) do
			local key = "vocation." .. vocation.id
			local label = self:t(key) .. " — " .. self:t(key .. ".description")
			menu[index] = entry(option(tostring(index), label), self:_choose_vocation(vocation.id))
		end
		return with_back(menu, self:_back(View.DIFFICULTY))
	elseif view == View.AUTO_EQUIP then
		return self:_auto_equip_menu()
	elseif view == View.MERCHANT then
		return self:_merchant_menu()
	elseif view == View.BUY_POTIONS then
		return with_back(self:_potion_shop(), self:_back(View.MERCHANT))
	elseif view == View.SELL then
		return with_back(self:_sell_menu(), self:_back(View.MERCHANT))
	elseif view == View.EQUIPMENT then
		return with_back(self:_equipment_menu(), self:_back(View.MERCHANT))
	elseif view == View.COMPARE then
		return {
			entry(option("1", self:t("equipment.equip")), function()
				self:_equip_compared()
			end),
			self:_back(View.EQUIPMENT),
		}
	elseif view == View.EQUIPPED_SLOT then
		return {
			entry(option("1", self:t("equipment.unequip")), function()
				self:_unequip_slot()
			end),
			self:_back(View.EQUIPMENT),
		}
	elseif view == View.STOCK then
		return with_back(self:_stock_menu(), self:_back(View.MERCHANT))
	elseif view == View.CHARACTER then
		return { self:_back(View.MERCHANT) }
	elseif view == View.BATTLE then
		return {
			entry(option("1", self:t("battle.attack")), self:_command(commands.Attack())),
			entry(option("2", self:t("battle.spells")), self:_go(View.SPELLS)),
			entry(option("3", self:t("battle.potions")), self:_go(View.POTIONS)),
			entry(option("4", self:t("battle.defend")), self:_command(commands.Defend())),
			entry(option("5", self:t("battle.auto")), self:_go(View.AUTO_BATTLE)),
			entry(option("q", self:t("battle.save_quit")), function()
				self:_save_and_quit()
			end),
		}
	elseif view == View.AUTO_BATTLE then
		local menu = {}
		for index, mode in ipairs(auto_battle.AutoBattleMode.values) do
			menu[index] = entry(option(tostring(index), self:t("auto_battle." .. mode)), self:_start_auto_battle(mode))
		end
		return with_back(menu, self:_back(View.BATTLE))
	elseif view == View.VICTORY then
		return {
			entry(option("1", self:t("victory.end_run")), self:_command(commands.EndRun())),
			entry(option("2", self:t("victory.continue")), self:_command(commands.ContinueRun())),
		}
	elseif view == View.SPELLS then
		return with_back(self:_spell_menu(), self:_back(View.BATTLE))
	elseif view == View.POTIONS then
		return with_back(self:_battle_potions(), self:_back(View.BATTLE))
	elseif view == View.GAME_OVER then
		return {
			entry(option("1", self:t("gameover.new_run")), function()
				self:_new_run()
			end),
			entry(option("2", self:t("gameover.title_screen")), self:_go(View.TITLE)),
		}
	elseif view == View.HALL_OF_FAME or view == View.BESTIARY or view == View.ACHIEVEMENTS then
		return { self:_back(View.TITLE) }
	end
	return {}
end

function Controller:_back(target)
	return entry(option("0", self:t("menu.back")), self:_go(target))
end

function Controller:_go(target)
	return function()
		self.view = target
		self.page = 0
	end
end

function Controller:_title_menu()
	local menu = {}
	if self.services.repositories.saves:load() ~= nil then
		menu[#menu + 1] = entry(option("1", self:t("menu.continue")), function()
			self:_continue()
		end)
	end
	menu[#menu + 1] = entry(option("2", self:t("menu.new_run")), function()
		self:_new_run()
	end)
	menu[#menu + 1] = entry(option("3", self:t("menu.hall_of_fame")), self:_go(View.HALL_OF_FAME))
	menu[#menu + 1] = entry(option("4", self:t("menu.bestiary")), self:_go(View.BESTIARY))
	menu[#menu + 1] = entry(option("5", self:t("menu.achievements")), self:_go(View.ACHIEVEMENTS))
	menu[#menu + 1] = entry(option("6", self:t("menu.settings")), self:_go(View.SETTINGS))
	menu[#menu + 1] = entry(option("0", self:t("menu.quit")), function()
		self.exit_requested = true
	end)
	return menu
end

function Controller:_on_off(enabled)
	return self:t(enabled and "settings.on" or "settings.off")
end

--- The current settings with some fields replaced.
function Controller:_settings_with(overrides)
	local settings = repositories.Settings(self.settings)
	for key, value in pairs(overrides) do
		settings[key] = value
	end
	return settings
end

function Controller:_settings_menu()
	local settings = self.settings
	return {
		entry(option("1", self:t("settings.language", { language = self:t("language." .. self.locale) })), function()
			self._language_return = View.SETTINGS
			self.view = View.LANGUAGE
		end),
		entry(option("2", self:t("settings.auto_equip", { state = self:_on_off(settings.auto_equip) })), function()
			self:_save_settings(self:_settings_with({ auto_equip = not self.settings.auto_equip }))
		end),
		entry(option("3", self:t("settings.battle_speed", { speed = settings.battle_speed })), function()
			self:_cycle_battle_speed()
		end),
		self:_back(View.TITLE),
	}
end

function Controller:_save_settings(settings)
	self.settings = settings
	self.services.settings:save(settings)
end

function Controller:_cycle_battle_speed()
	local speeds = repositories.BATTLE_SPEEDS
	local index = base.index_of(speeds, self.settings.battle_speed) or 1
	self:_save_settings(self:_settings_with({ battle_speed = speeds[index % #speeds + 1] }))
end

function Controller:_auto_equip_menu()
	local default = self:t("new_run.default")
	local menu = {}
	for index, enabled in ipairs({ true, false }) do
		local label = self:t(enabled and "new_run.auto_equip_on" or "new_run.auto_equip_off")
		if enabled == self.settings.auto_equip then
			label = label .. " " .. default
		end
		menu[index] = entry(option(tostring(index), label), self:_start_run(enabled))
	end
	return with_back(menu, self:_back(View.VOCATION))
end

function Controller:_merchant_menu()
	return {
		entry(option("1", self:t("merchant.buy_potions")), self:_go(View.BUY_POTIONS)),
		entry(option("2", self:t("merchant.sell_items")), self:_go(View.SELL)),
		entry(option("3", self:t("merchant.equipment")), self:_go(View.EQUIPMENT)),
		entry(option("4", self:t("merchant.stock")), self:_go(View.STOCK)),
		entry(option("5", self:t("merchant.character")), self:_go(View.CHARACTER)),
		entry(option("0", self:t("merchant.next_fight")), self:_command(commands.NextFight())),
		entry(option("q", self:t("battle.save_quit")), function()
			self:_save_and_quit()
		end),
	}
end

--- Label of an item in a list: `template` receives name, rarity, slot and `params`.
function Controller:_item_label(template, item, params)
	local definition = self._data:item(item.item_id)
	params.name = definition.name
	params.rarity = self:t("rarity." .. item.rarity)
	params.slot = self:t("slot." .. definition.slot)
	return self:t(template, params)
end

function Controller:_potion_shop()
	local state = self:_require_session():state()
	local menu = {}
	for index, potion_id in ipairs(Merchant.available_potions(state, self._data)) do
		local potion = self._data:potion(potion_id)
		local label = self:t("merchant.potion_option", {
			name = potion.name,
			price = potion.price,
			count = state.player:potion_count(potion_id),
		})
		menu[index] = entry(option(list_key(index - 1), label), self:_ask_quantity(potion_id))
	end
	return menu
end

function Controller:_sell_menu()
	local player = self:_require_session():state().player
	local menu = {}
	for index, item in ipairs(player.bag) do
		local label = self:_item_label("merchant.sell_option", item, { gold = character.item_value(item, self._data) })
		menu[index] = entry(option(list_key(index - 1), label, item.rarity), self:_command(commands.SellItem(item.uid)))
	end
	return menu
end

function Controller:_usable_bag()
	local player = self:_require_session():state().player
	local vocation = self._data:vocation(player.vocation_id)
	return base.filter(player.bag, function(item)
		return loot.can_use(self._data:item(item.item_id), vocation)
	end)
end

--- Score of `item` minus the score of what is equipped in its slot (0 for an empty slot).
function Controller:_score_delta(item)
	local equipped = self:_require_session():state().player.equipment[self._data:item(item.item_id).slot]
	return item_score(item, self._data) - (equipped ~= nil and item_score(equipped, self._data) or 0)
end

--- Usable bag items first (keys 1..n, as in docs/tui.md), then the equipped slots.
function Controller:_equipment_menu()
	local player = self:_require_session():state().player
	local data = self._data
	local entries = {}
	for _, item in ipairs(self:_usable_bag()) do
		local definition = data:item(item.item_id)
		local level = required_level(item, data)
		local label = self:t("equipment.bag_option", {
			name = definition.name,
			rarity = self:t("rarity." .. item.rarity),
			slot = self:t("slot." .. definition.slot),
			level = level,
			score = item_score(item, data),
		})
		local too_high = level > player.level
		if too_high then
			label = label .. " · " .. self:t("equipment.requires_level", { level = level })
		end
		local delta = self:_score_delta(item)
		local uid = item.uid
		entries[#entries + 1] = entry(
			option("", label, too_high and render.STYLE_DIM or item.rarity, format_delta(delta), delta_style(delta)),
			function()
				self._compare_uid = uid
				self.view = View.COMPARE
			end
		)
	end
	for _, slot in ipairs(enums.EQUIPMENT_SLOT_ORDER) do
		local equipped = player.equipment[slot]
		if equipped ~= nil then
			local label = self:t("equipment.slot_option", {
				slot = self:t("slot." .. slot),
				name = data:item(equipped.item_id).name,
				rarity = self:t("rarity." .. equipped.rarity),
			})
			entries[#entries + 1] = entry(option("", label, equipped.rarity), function()
				self._slot = slot
				self.view = View.EQUIPPED_SLOT
			end)
		end
	end
	for index, menu_entry in ipairs(entries) do
		menu_entry.option.key = list_key(index - 1)
	end
	return entries
end

function Controller:_compared_item()
	for _, item in ipairs(self:_require_session():state().player.bag) do
		if item.uid == self._compare_uid then
			return item
		end
	end
	return nil
end

function Controller:_equip_compared()
	self:_step(commands.Equip(self._compare_uid))
	self.view = View.EQUIPMENT
end

function Controller:_unequip_slot()
	self:_step(commands.Unequip(self._slot))
	self.view = View.EQUIPMENT
end

function Controller:_stock_menu()
	local stock = self:_require_session():state().merchant_stock
	local menu = {}
	for index, item in ipairs(stock) do
		local label = self:_item_label("merchant.stock_option", item, { gold = Merchant.stock_price(item, self._data) })
		menu[index] =
			entry(option(list_key(index - 1), label, item.rarity), self:_command(commands.BuyStockItem(index - 1)))
	end
	return menu
end

function Controller:_spell_menu()
	local player = self:_require_session():state().player
	local levels = self._data.balance.spell_levels
	local menu = {}
	for index, spell_id in ipairs(self._data:vocation(player.vocation_id).spells) do
		local spell = self._data:spell(spell_id)
		local uses = player.spell_uses[spell_id] or 0
		local level = formulas.spell_level_for_uses(uses, levels)
		local label = self:t("battle.spell_option", {
			name = spell.name,
			words = spell.words,
			mana = formulas.pct(spell.mana, level.mana_pct),
			level = level.level,
			uses = uses,
		})
		local color = spell.kind == SpellKind.HEAL and "green" or spell.element
		menu[index] = entry(option(list_key(index - 1), label, color), self:_command(commands.Cast(spell_id)))
	end
	return menu
end

function Controller:_battle_potions()
	local player = self:_require_session():state().player
	local menu = {}
	for _, potion in ipairs(self._data.potions) do
		local count = player:potion_count(potion.id)
		if count > 0 then
			local label = self:t("battle.potion_option", { name = potion.name, count = count })
			menu[#menu + 1] = entry(option(list_key(#menu), label), self:_command(commands.UsePotion(potion.id)))
		end
	end
	return menu
end

-- ── actions ─────────────────────────────────────────────────────────────────

function Controller:_choose_language(locale)
	return function()
		self:_set_locale(locale)
		self:_save_settings(self:_settings_with({ locale = locale }))
		self.view = self._language_return
	end
end

function Controller:_new_run()
	self.session = nil
	self.view = View.DIFFICULTY
end

function Controller:_choose_difficulty(difficulty_id)
	return function()
		self._difficulty = difficulty_id
		self.input_buffer = ""
		self.view = View.NAME
	end
end

function Controller:_choose_vocation(vocation_id)
	return function()
		self._vocation = vocation_id
		self.view = View.AUTO_EQUIP
	end
end

function Controller:_start_run(auto_equip)
	return function()
		local seed = self._seed
		if seed == nil then
			seed = self._seed_source()
		end
		local services = self.services
		local config = run_state.RunConfig.new(self._name, self._vocation, self._difficulty, auto_equip)
		local session, events = GameSession.start(self._data, config, seed, {
			repositories = services.repositories,
			clock = services.clock,
			game_version = services.version,
		})
		self.session = session
		self.log = {}
		self:_record({ events = events, achievements = {} })
		self.view = View.MERCHANT
	end
end

function Controller:_continue()
	local services = self.services
	local session = GameSession.resume(self._data, services.repositories, services.clock, services.version)
	if session == nil then
		return
	end
	local state = session:state()
	self.session = session
	self.log = {}
	self:_log(self:t("menu.welcome_back", { name = state.player.name, round = state.round }))
	self.view = state.phase == Phase.VICTORY and View.VICTORY or View.MERCHANT
end

function Controller:_ask_quantity(potion_id)
	return function()
		self._potion_id = potion_id
		self.input_buffer = ""
		self.view = View.QUANTITY
	end
end

function Controller:_command(command)
	return function()
		self:_step(command)
	end
end

function Controller:_save_and_quit()
	self:save_session()
	self.session = nil
	self.view = View.TITLE
end

--- Persists the play time of the run in progress (called when the player leaves the game).
function Controller:save_session()
	if self.session ~= nil then
		self.session:save_and_quit()
	end
end

function Controller:_require_session()
	if self.session == nil then
		base.raise("RuntimeError", "view " .. self.view .. " needs an active run")
	end
	return self.session
end

function Controller:_step(command)
	local session = self:_require_session()
	self:_record(session:step(command))
	local phase = session:state().phase
	if phase == Phase.BATTLE then
		self.view = View.BATTLE
	elseif phase == Phase.GAME_OVER then
		self.view = View.GAME_OVER
	elseif phase == Phase.VICTORY then
		self.view = View.VICTORY
	elseif BATTLE_VIEWS[self.view] or self.view == View.VICTORY then
		self.view = View.MERCHANT
	end
end

function Controller:_record(result)
	local state = self:_require_session():state()
	local cues = {}
	for _, event in ipairs(result.events) do
		local text = self._formatter:format(event, state)
		if event.type == "error" then
			self.message = text
		else
			self:_log(text)
			if (event.type == "player_attacked" or event.type == "spell_cast") and (event.damage or 0) ~= 0 then
				cues[#cues + 1] = "hurt"
			elseif event.type == "monster_attacked" then
				cues[#cues + 1] = "attack"
			end
		end
	end
	for _, achievement in ipairs(result.achievements) do
		local name = self:t("achievement." .. achievement.id .. ".name")
		self:_log(self:t("achievement.unlocked", { name = name }))
	end
	self.animation_cues = cues
end

-- ── informative bodies ──────────────────────────────────────────────────────

function Controller:_profile()
	if self.session ~= nil then
		return self.session.profile
	end
	return profile_module.ProfileService.new(self._data, self.services.repositories.profile:load())
end

function Controller:_body()
	local view = self.view
	if view == View.CHARACTER then
		return self:_character_sheet()
	elseif view == View.GAME_OVER then
		return self:_game_over_summary()
	elseif view == View.VICTORY then
		return self:_victory_summary()
	elseif view == View.HALL_OF_FAME then
		return self:_hall_of_fame()
	elseif view == View.BESTIARY then
		return self:_bestiary()
	elseif view == View.ACHIEVEMENTS then
		return self:_achievements()
	elseif view == View.MERCHANT then
		local player = self:_require_session():state().player
		return { self:t("merchant.welcome", { name = player.name, gold = player.gold }) }
	elseif view == View.SELL and #self:_require_session():state().player.bag == 0 then
		return { self:t("merchant.empty_bag") }
	elseif view == View.STOCK and #self:_require_session():state().merchant_stock == 0 then
		return { self:t("merchant.empty_stock") }
	elseif view == View.POTIONS then
		for _, count in pairs(self:_require_session():state().player.potions) do
			if count ~= 0 then
				return {}
			end
		end
		return { self:t("battle.no_potions") }
	end
	return {}
end

function Controller:_character_sheet()
	local player = self:_require_session():state().player
	local sheet = character.build_sheet(player, self._data)
	local balance = self._data.balance
	local lines = {
		self:t("character.level", {
			level = player.level,
			xp = player.xp,
			next = formulas.xp_for_level(player.level + 1),
		}),
		self:t("character.magic_level", {
			magicLevel = player.magic_level,
			spent = player.mana_spent,
			next = formulas.mana_for_magic_level(player.magic_level, balance),
		}),
		self:t("character.hp_mp", { hp = player.hp, maxHp = sheet.max_hp, mp = player.mp, maxMp = sheet.max_mp }),
		self:t("character.melee", {
			min = sheet.melee_min,
			max = sheet.melee_max,
			element = self:t("element." .. sheet.weapon_element),
		}),
	}
	local stats = {
		{ "stat.armor", sheet.armor },
		{ "stat.hpRegen", sheet.hp_regen },
		{ "stat.mpRegen", sheet.mp_regen },
		{ "stat.critChance", sheet.crit_chance },
		{ "stat.critDamage", sheet.crit_damage },
		{ "stat.spellPower", sheet.spell_power },
		{ "stat.physicalDamage", sheet.physical_damage },
		{ "stat.dodge", sheet.dodge },
		{ "stat.parry", sheet.parry },
		{ "stat.lifeLeech", sheet.life_leech },
		{ "stat.manaLeech", sheet.mana_leech },
	}
	for _, stat in ipairs(stats) do
		if stat[2] ~= 0 then
			lines[#lines + 1] = self:t("character.stat_line", { stat = self:t(stat[1]), value = stat[2] })
		end
	end
	for _, element in ipairs(enums.Element.values) do
		local value = sheet.protections[element]
		if value ~= 0 then
			lines[#lines + 1] =
				self:t("character.stat_line", { stat = self:t("element." .. element), value = value .. "%" })
		end
	end
	lines[#lines + 1] = ""
	lines[#lines + 1] = self:t("character.equipment")
	for _, slot in ipairs(enums.Slot.values) do
		local item = player.equipment[slot]
		local slot_name = self:t("slot." .. slot)
		if item == nil then
			lines[#lines + 1] = self:t("character.empty_slot", { slot = slot_name })
		else
			lines[#lines + 1] = self:t("character.slot", {
				slot = slot_name,
				item = self._data:item(item.item_id).name,
				rarity = self:t("rarity." .. item.rarity),
			})
		end
	end
	lines[#lines + 1] = self:t("character.bag", { count = #player.bag, capacity = balance.bag_capacity })
	return lines
end

function Controller:_run_stats_line()
	local state = self:_require_session():state()
	local kills = 0
	for _, count in pairs(state.stats.kills) do
		kills = kills + count
	end
	return self:t("gameover.stats", {
		level = state.player.level,
		damage = state.stats.damage_dealt,
		kills = kills,
		elites = state.stats.elites_killed,
		bosses = state.stats.bosses_killed,
	})
end

function Controller:_game_over_summary()
	local state = self:_require_session():state()
	local params = {
		name = state.player.name,
		vocation = self:t("vocation." .. state.player.vocation_id),
		round = state.round,
	}
	local summary
	if state.death_cause ~= nil and state.death_cause ~= "" then
		params.monster = self._data:creature(state.death_cause).name
		summary = self:t("gameover.summary", params)
	elseif state.won then
		summary = self:t("gameover.won_summary", params)
	else
		params.monster = "?"
		summary = self:t("gameover.summary", params)
	end
	return { summary, self:_run_stats_line() }
end

function Controller:_victory_summary()
	local state = self:_require_session():state()
	local data = self._data
	local boss = data:boss_of_tier(formulas.round_info(state.round, data.balance, data.tier_count).tier)
	return {
		self:t("victory.summary", {
			name = state.player.name,
			vocation = self:t("vocation." .. state.player.vocation_id),
			monster = boss.name,
			round = state.round,
		}),
		self:_run_stats_line(),
		"",
		self:t("victory.choice"),
	}
end

-- ── equipment screens (docs/tui.md "Equipment screen") ──────────────────────

function Controller:_styled_body()
	if self.view == View.EQUIPMENT then
		return self:_equipment_body()
	elseif self.view == View.COMPARE then
		return self:_compare_body()
	end
	return self:_slot_body()
end

function Controller:_equipment_body()
	local player = self:_require_session():state().player
	local data = self._data
	local lines =
		{ body_line(self:t("equipment.equipped_header", { score = character.equipment_score(player, data) })) }
	for _, slot in ipairs(enums.EQUIPMENT_SLOT_ORDER) do
		local slot_name = self:t("slot." .. slot)
		local item = player.equipment[slot]
		if item == nil then
			lines[#lines + 1] = body_line(self:t("equipment.slot_empty", { slot = slot_name }), render.STYLE_WARNING)
		else
			local text = self:t("equipment.slot_line", {
				slot = slot_name,
				name = data:item(item.item_id).name,
				rarity = self:t("rarity." .. item.rarity),
				level = required_level(item, data),
				score = item_score(item, data),
			})
			lines[#lines + 1] = body_line(text, item.rarity)
		end
	end
	lines[#lines + 1] = body_line("")
	lines[#lines + 1] = body_line(self:t("equipment.bag_header"))
	if #self:_usable_bag() == 0 then
		lines[#lines + 1] = body_line(self:t("equipment.bag_empty"))
	end
	return lines
end

function Controller:_compare_title()
	local item = self:_compared_item()
	if item == nil then
		return self:t("merchant.equipment")
	end
	local player = self:_require_session():state().player
	local definition = self._data:item(item.item_id)
	local current = player.equipment[definition.slot]
	return self:t("equipment.compare_title", {
		slot = self:t("slot." .. definition.slot),
		current = current == nil and self:t("equipment.empty") or self._data:item(current.item_id).name,
		new = definition.name,
	})
end

function Controller:_affix_list(item)
	if item == nil then
		return ""
	end
	local parts = {}
	for index, affix in ipairs(item.affixes) do
		parts[index] = self:t("equipment.affix", { value = affix.value, stat = self:t("stat." .. affix.stat) })
	end
	return table.concat(parts, ", ")
end

function Controller:_compare_body()
	local item = self:_compared_item()
	if item == nil then
		return {}
	end
	local data = self._data
	local player = self:_require_session():state().player
	local current = player.equipment[data:item(item.item_id).slot]
	local new_stats = item_stats(item, data)
	local old_stats = current ~= nil and item_stats(current, data) or {}
	local lines = {}
	for _, stat in ipairs(enums.Stat.values) do
		if new_stats[stat] ~= nil or old_stats[stat] ~= nil then
			local old, new = old_stats[stat] or 0, new_stats[stat] or 0
			local text = self:t("equipment.stat_delta", {
				stat = self:t("stat." .. stat),
				current = old,
				new = new,
				delta = format_delta(new - old),
			})
			lines[#lines + 1] = body_line(text, delta_style(new - old))
		end
	end
	local gained, lost = self:_affix_list(item), self:_affix_list(current)
	if gained ~= "" then
		lines[#lines + 1] = body_line(self:t("equipment.affixes_gained", { affixes = gained }), render.STYLE_GAIN)
	end
	if lost ~= "" then
		lines[#lines + 1] = body_line(self:t("equipment.affixes_lost", { affixes = lost }), render.STYLE_LOSS)
	end
	local old_score = current ~= nil and item_score(current, data) or 0
	local new_score = item_score(item, data)
	local score = self:t("equipment.score_delta", {
		current = old_score,
		new = new_score,
		delta = format_delta(new_score - old_score),
	})
	lines[#lines + 1] = body_line(score, delta_style(new_score - old_score))
	local level = required_level(item, data)
	if level > player.level then
		lines[#lines + 1] =
			body_line(self:t("equipment.level_needed", { level = level, current = player.level }), render.STYLE_LOSS)
	end
	return lines
end

function Controller:_slot_body()
	local data = self._data
	local item = self:_require_session():state().player.equipment[self._slot]
	if item == nil then
		return { body_line(self:t("equipment.empty"), render.STYLE_WARNING) }
	end
	local lines = {
		body_line(
			self:t("equipment.item_title", {
				name = data:item(item.item_id).name,
				rarity = self:t("rarity." .. item.rarity),
				level = required_level(item, data),
				score = item_score(item, data),
			}),
			item.rarity
		),
	}
	local stats = item_stats(item, data)
	for _, stat in ipairs(enums.Stat.values) do
		if stats[stat] ~= nil then
			lines[#lines + 1] =
				body_line(self:t("character.stat_line", { stat = self:t("stat." .. stat), value = stats[stat] }))
		end
	end
	return lines
end

function Controller:_hall_of_fame()
	local hall = self:_profile().profile.hall_of_fame
	if #hall == 0 then
		return { self:t("hall.empty") }
	end
	local lines = {}
	for index, hall_entry in ipairs(hall) do
		lines[index] = self:t(hall_entry.won and "hall.entry_won" or "hall.entry", {
			position = index,
			name = hall_entry.name,
			vocation = self:t("vocation." .. hall_entry.vocation),
			difficulty = self:t("difficulty." .. hall_entry.difficulty),
			round = hall_entry.round,
			level = hall_entry.level,
			date = hall_entry.ended_at:sub(1, 10),
		})
	end
	return lines
end

function Controller:_bestiary()
	local service = self:_profile()
	local creatures = base.copy_list(self._data.monsters)
	for _, boss in ipairs(self._data.bosses) do
		creatures[#creatures + 1] = boss
	end
	creatures = base.sorted(creatures, function(a, b)
		if a.tier ~= b.tier then
			return a.tier < b.tier
		end
		if a.is_boss ~= b.is_boss then
			return b.is_boss
		end
		return a.name < b.name
	end)
	local lines = {}
	for _, creature in ipairs(creatures) do
		local bestiary_entry = service.profile.bestiary[creature.id]
		if bestiary_entry == nil then
			lines[#lines + 1] = self:t("bestiary.unknown", { tier = creature.tier + 1 })
		elseif service:revealed(creature.id) then
			local weak = self:_elements(creature, function(resistance)
				return resistance > 100
			end)
			local strong = self:_elements(creature, function(resistance)
				return resistance < 100
			end)
			lines[#lines + 1] = self:t("bestiary.entry_revealed", {
				name = creature.name,
				tier = creature.tier + 1,
				kills = bestiary_entry.kills,
				weak = #weak > 0 and table.concat(weak, ", ") or "—",
				strong = #strong > 0 and table.concat(strong, ", ") or "—",
			})
		else
			lines[#lines + 1] = self:t("bestiary.entry", {
				name = creature.name,
				tier = creature.tier + 1,
				kills = bestiary_entry.kills,
			})
		end
	end
	return lines
end

function Controller:_achievements()
	local unlocked = self:_profile().profile.achievements
	local lines = {}
	for index, achievement in ipairs(self._data.achievements) do
		local key = "achievement." .. achievement.id
		local params = {
			name = self:t(key .. ".name"),
			description = self:t(key .. ".description", { value = achievement.value }),
		}
		local unlock = unlocked[achievement.id]
		if unlock == nil then
			lines[index] = self:t("achievements.locked", params)
		else
			params.date = unlock.unlocked_at:sub(1, 10)
			lines[index] = self:t("achievements.unlocked", params)
		end
	end
	return lines
end

return controller
