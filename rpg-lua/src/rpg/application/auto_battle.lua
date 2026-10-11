--- Auto-battle policy (docs/game-design.md §13): picks the player's battle commands from the run state only.
---
--- It lives in the application layer, not in the engine: the commands it returns are ordinary commands, so a fight
--- played by the policy replays like any other. Every command it returns is valid (affordable spells, owned potions).

local commands = require("rpg.application.commands")
local base = require("rpg.domain.base")
local character = require("rpg.domain.character")
local enums = require("rpg.domain.enums")
local formulas = require("rpg.domain.formulas")

local Resource, SpellKind = enums.Resource, enums.SpellKind

local OFFENSE_ATTACK = "attack"

local auto_battle = {}

auto_battle.AutoBattleMode = { MELEE = "melee", SPELLS = "spells", BALANCED = "balanced" }
auto_battle.AutoBattleMode.values = { "melee", "spells", "balanced" }

--- Highest `max`; ties go to the lowest id.
local function strongest(options)
	local best
	for _, option in ipairs(options) do
		if best == nil or option.max > best.max or (option.max == best.max and option.id < best.id) then
			best = option
		end
	end
	return best
end

local AutoBattlePolicy = base.class("AutoBattlePolicy")
auto_battle.AutoBattlePolicy = AutoBattlePolicy

function AutoBattlePolicy.new(data, mode)
	return setmetatable({
		_data = data,
		_mode = data.balance.auto_battle:mode(mode),
		_config = data.balance.auto_battle,
	}, AutoBattlePolicy)
end

function AutoBattlePolicy:choose(state)
	local player = state.player
	local sheet = character.build_sheet(player, self._data)
	local config = self._config

	if player.hp * 100 < sheet.max_hp * config.emergency_heal_below_pct then
		local heal = self:_heal(state)
		if heal ~= nil then
			return heal
		end
	end
	local every = self._mode.support_every
	if state.turn % every == every - 1 then
		if player.hp * 100 < sheet.max_hp * config.heal_below_pct then
			local heal = self:_heal(state)
			if heal ~= nil then
				return heal
			end
		end
		if player.mp * 100 < sheet.max_mp * config.mana_below_pct then
			local potion = self:_best_potion(state, Resource.MP)
			if potion ~= nil then
				return commands.UsePotion(potion.id)
			end
		end
		if self:_telegraph_pending(state) then
			return commands.Defend()
		end
	end
	return self:_offense(state)
end

function AutoBattlePolicy:_offense(state)
	if self._mode.offense == OFFENSE_ATTACK then
		return commands.Attack()
	end
	local spell = strongest(self:_affordable(state, SpellKind.ATTACK))
	return spell == nil and commands.Attack() or commands.Cast(spell.id)
end

function AutoBattlePolicy:_heal(state)
	local spell = strongest(self:_affordable(state, SpellKind.HEAL))
	if spell ~= nil then
		return commands.Cast(spell.id)
	end
	local potion = self:_best_potion(state, Resource.HP)
	return potion ~= nil and commands.UsePotion(potion.id) or nil
end

function AutoBattlePolicy:_affordable(state, kind)
	local player = state.player
	local levels = self._data.balance.spell_levels
	local result = {}
	for _, spell_id in ipairs(self._data:vocation(player.vocation_id).spells) do
		local spell = self._data:spell(spell_id)
		local level = formulas.spell_level_for_uses(player.spell_uses[spell.id] or 0, levels)
		if spell.kind == kind and formulas.pct(spell.mana, level.mana_pct) <= player.mp then
			result[#result + 1] = spell
		end
	end
	return result
end

function AutoBattlePolicy:_best_potion(state, resource)
	return strongest(base.filter(self._data.potions, function(potion)
		return potion.resource == resource and state.player:potion_count(potion.id) > 0
	end))
end

--- The boss announced its charged attack: its next action is the charge.
function AutoBattlePolicy:_telegraph_pending(state)
	local monster = state.monster
	if monster == nil or not monster.is_boss then
		return false
	end
	local every = self._data.balance.boss_telegraph_every
	return monster.boss_actions % (every + 1) == every
end

return auto_battle
