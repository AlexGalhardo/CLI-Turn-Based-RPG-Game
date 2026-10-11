--- A deterministic heuristic player used by the simulator and by the end-to-end parity tests.
---
--- The bot only reads the run state and returns one command at a time, so it can drive any engine implementation.
--- Its decisions are part of the golden "bot full run" files: changing them requires regenerating those files.

local commands = require("rpg.application.commands")
local loot = require("rpg.application.loot")
local Merchant = require("rpg.application.merchant")
local base = require("rpg.domain.base")
local character = require("rpg.domain.character")
local enums = require("rpg.domain.enums")
local formulas = require("rpg.domain.formulas")

local Phase, Resource, SpellKind = enums.Phase, enums.Resource, enums.SpellKind

local HEAL_THRESHOLD_PCT = 45
local MANA_POTION_THRESHOLD_PCT = 25
local MAX_POTION_STOCK = 20

--- Python's `max(options, key=lambda o: (value(o), o.id))`: highest value, ties go to the highest id.
local function best_by(options, value)
	local best, best_value
	for _, option in ipairs(options) do
		local current = value(option)
		if best == nil or current > best_value or (current == best_value and option.id > best.id) then
			best, best_value = option, current
		end
	end
	return best
end

local function by_max(option)
	return option.max
end

local GreedyBot = base.class("GreedyBot")

function GreedyBot.new(data)
	return setmetatable({ _data = data }, GreedyBot)
end

function GreedyBot:choose(state)
	if state.phase == Phase.BATTLE then
		return self:_battle(state)
	end
	if state.phase == Phase.VICTORY then
		return commands.EndRun()
	end
	return self:_merchant(state)
end

-- ── battle ──────────────────────────────────────────────────────────────────

function GreedyBot:_battle(state)
	local player = state.player
	local monster = state.monster
	if monster == nil then
		base.raise("RuntimeError", "battle without a monster")
	end
	local sheet = character.build_sheet(player, self._data)

	if self:_charge_incoming(monster) then
		return commands.Defend()
	end
	if player.hp * 100 < sheet.max_hp * HEAL_THRESHOLD_PCT then
		local heal = self:_heal(state)
		if heal ~= nil then
			return heal
		end
	end
	if player.mp * 100 < sheet.max_mp * MANA_POTION_THRESHOLD_PCT then
		local potion = self:_best_owned_potion(state, Resource.MP)
		if potion ~= nil then
			return commands.UsePotion(potion.id)
		end
	end
	local spell = self:_best_attack_spell(state, monster)
	if spell ~= nil then
		return commands.Cast(spell.id)
	end
	return commands.Attack()
end

function GreedyBot:_charge_incoming(monster)
	if not monster.is_boss then
		return false
	end
	local every = self._data.balance.boss_telegraph_every
	return monster.boss_actions % (every + 1) == every
end

function GreedyBot:_cost(state, spell)
	local uses = state.player.spell_uses[spell.id] or 0
	return formulas.pct(spell.mana, formulas.spell_level_for_uses(uses, self._data.balance.spell_levels).mana_pct)
end

function GreedyBot:_spells(state, kind)
	local result = {}
	for _, spell_id in ipairs(self._data:vocation(state.player.vocation_id).spells) do
		local spell = self._data:spell(spell_id)
		if spell.kind == kind and self:_cost(state, spell) <= state.player.mp then
			result[#result + 1] = spell
		end
	end
	return result
end

function GreedyBot:_heal(state)
	local spells = self:_spells(state, SpellKind.HEAL)
	if #spells > 0 then
		return commands.Cast(best_by(spells, by_max).id)
	end
	local potion = self:_best_owned_potion(state, Resource.HP)
	return potion ~= nil and commands.UsePotion(potion.id) or nil
end

function GreedyBot:_best_owned_potion(state, resource)
	local owned = base.filter(self._data.potions, function(potion)
		return potion.resource == resource and state.player:potion_count(potion.id) > 0
	end)
	return best_by(owned, by_max)
end

function GreedyBot:_best_attack_spell(state, monster)
	local creature = self._data:creature(monster.creature_id)
	local candidates = base.filter(self:_spells(state, SpellKind.ATTACK), function(spell)
		return creature:resistance(spell.element) > 0
	end)
	return best_by(candidates, function(spell)
		return (spell.min + spell.max) * creature:resistance(spell.element)
	end)
end

-- ── merchant ────────────────────────────────────────────────────────────────

function GreedyBot:_merchant(state)
	local player = state.player
	local data = self._data
	local vocation = data:vocation(player.vocation_id)
	local by_uid = base.sorted(player.bag, function(a, b)
		return a.uid < b.uid
	end)
	for _, item in ipairs(by_uid) do
		local definition = data:item(item.item_id)
		if loot.can_use(definition, vocation) and character.required_level(item, data) <= player.level then
			local current = player.equipment[definition.slot]
			if current == nil or character.item_score(item, data) > character.item_score(current, data) then
				return commands.Equip(item.uid)
			end
		end
	end
	if #by_uid > 0 then
		return commands.SellItem(by_uid[1].uid)
	end
	return self:_potion_purchase(state, Resource.HP)
		or self:_potion_purchase(state, Resource.MP)
		or commands.NextFight()
end

function GreedyBot:_potion_purchase(state, resource)
	local unlocked = Merchant.available_potions(state, self._data)
	local options = base.filter(self._data.potions, function(potion)
		return potion.resource == resource and base.contains(unlocked, potion.id)
	end)
	if #options == 0 then
		return nil
	end
	local best = best_by(options, by_max)
	local owned = 0
	for _, potion in ipairs(options) do
		owned = owned + state.player:potion_count(potion.id)
	end
	local target = math.min(MAX_POTION_STOCK, 5 + state.round // 5)
	local budget = resource == Resource.MP and state.player.gold // 2 or state.player.gold
	local quantity = math.min(target - owned, budget // best.price)
	if quantity <= 0 then
		return nil
	end
	return commands.BuyPotion(best.id, quantity)
end

return GreedyBot
