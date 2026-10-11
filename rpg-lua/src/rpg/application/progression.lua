--- Experience, levels, magic levels and spell levels (docs/game-design.md §4 and §9).

local events_module = require("rpg.application.events")
local base = require("rpg.domain.base")
local character = require("rpg.domain.character")
local formulas = require("rpg.domain.formulas")

local event = events_module.event

local Progression = base.class("Progression")

function Progression.new(data)
	return setmetatable({ _data = data }, Progression)
end

function Progression:gain_experience(player, amount)
	player.xp = player.xp + amount
	local events = { event("xp_gained", { amount = amount, total = player.xp }) }
	local vocation = self._data:vocation(player.vocation_id)
	while player.xp >= formulas.xp_for_level(player.level + 1) do
		player.level = player.level + 1
		local sheet = character.build_sheet(player, self._data)
		player.hp = math.min(sheet.max_hp, player.hp + vocation.hp_per_level)
		player.mp = math.min(sheet.max_mp, player.mp + vocation.mp_per_level)
		events[#events + 1] = event("level_up", { level = player.level, maxHp = sheet.max_hp, maxMp = sheet.max_mp })
	end
	return events
end

function Progression:after_cast(player, spell, mana_cost)
	local levels = self._data.balance.spell_levels
	local events = {}
	local uses_before = player.spell_uses[spell.id] or 0
	player.spell_uses[spell.id] = uses_before + 1
	local before = formulas.spell_level_for_uses(uses_before, levels)
	local after = formulas.spell_level_for_uses(uses_before + 1, levels)
	if after.level ~= before.level then
		events[#events + 1] = event("spell_level_up", { spellId = spell.id, level = after.level })
	end

	player.mana_spent = player.mana_spent + mana_cost
	while player.mana_spent >= formulas.mana_for_magic_level(player.magic_level, self._data.balance) do
		player.magic_level = player.magic_level + 1
		events[#events + 1] = event("magic_level_up", { magicLevel = player.magic_level })
	end
	return events
end

return Progression
