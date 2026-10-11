--- Derived character stats: vocation base + equipment (docs/game-design.md §4 and §8).

local enums = require("rpg.domain.enums")
local formulas = require("rpg.domain.formulas")

local pct = formulas.pct
local Stat, Slot, Element = enums.Stat, enums.Slot, enums.Element

local character = {}

--- Final stats of an item. Returns the stat → value map and the stats in the reference order
--- (definition stats first, then the affixes that add a new stat).
function character.item_stats(item, data)
	local definition = data:item(item.item_id)
	local rarity = data.balance:rarity(item.rarity)
	local stats, order = {}, {}
	for _, stat in ipairs(definition.stat_order) do
		stats[stat] = pct(definition.stats[stat], rarity.stat_pct)
		order[#order + 1] = stat
	end
	for _, affix in ipairs(item.affixes) do
		if stats[affix.stat] == nil then
			order[#order + 1] = affix.stat
		end
		stats[affix.stat] = (stats[affix.stat] or 0) + affix.value
	end
	return stats, order
end

function character.item_value(item, data)
	return pct(data:item(item.item_id).value, data.balance:rarity(item.rarity).value_pct)
end

--- Sum of the item's final stats weighted by `balance.itemScoreWeights` (like Diablo's item power).
function character.item_score(item, data)
	local weights = data.balance.item_score_weights
	local score = 0
	for stat, value in pairs(character.item_stats(item, data)) do
		score = score + value * (weights[stat] or 0)
	end
	return score
end

--- Uses the instance tier: the round tier the item was generated for (docs/game-design.md §8).
function character.required_level(item, data)
	return 1 + item.tier * data.balance.item_level_per_tier
end

function character.equipment_score(player, data)
	local score = 0
	for _, item in pairs(player.equipment) do
		score = score + character.item_score(item, data)
	end
	return score
end

local CharacterSheet = {}
CharacterSheet.__index = CharacterSheet

function CharacterSheet:protection(element)
	return self.protections[element] or 0
end

--- CharacterSheet: max_hp, max_mp, hp_regen, mp_regen, melee_min, melee_max, weapon_element, armor, crit_chance,
--- crit_damage, spell_power, physical_damage, dodge, parry, life_leech, mana_leech, protections.
function character.build_sheet(player, data)
	local vocation = data:vocation(player.vocation_id)
	local caps = data.balance.caps
	local totals = {}
	for _, item in pairs(player.equipment) do
		for stat, value in pairs(character.item_stats(item, data)) do
			totals[stat] = (totals[stat] or 0) + value
		end
	end
	local function total(stat)
		return totals[stat] or 0
	end

	local weapon = player.equipment[Slot.WEAPON]
	local weapon_element = Element.PHYSICAL
	if weapon ~= nil then
		weapon_element = data:item(weapon.item_id).element or Element.PHYSICAL
	end

	local level_bonus = (player.level - 1) * vocation.melee_per_level
	local attack = total(Stat.ATTACK)
	local protections = {}
	for element, stat in pairs(enums.PROTECTION_BY_ELEMENT) do
		protections[element] = math.min(total(stat), caps.protection)
	end
	return setmetatable({
		max_hp = vocation.start_hp + (player.level - 1) * vocation.hp_per_level + total(Stat.MAX_HP),
		max_mp = vocation.start_mp + (player.level - 1) * vocation.mp_per_level + total(Stat.MAX_MP),
		hp_regen = vocation.hp_regen + total(Stat.HP_REGEN),
		mp_regen = vocation.mp_regen + total(Stat.MP_REGEN),
		melee_min = vocation.melee_min + level_bonus + attack,
		melee_max = vocation.melee_max + level_bonus + attack,
		weapon_element = weapon_element,
		armor = total(Stat.ARMOR),
		crit_chance = math.min(total(Stat.CRIT_CHANCE), caps.crit_chance),
		crit_damage = total(Stat.CRIT_DAMAGE),
		spell_power = total(Stat.SPELL_POWER),
		physical_damage = total(Stat.PHYSICAL_DAMAGE),
		dodge = math.min(total(Stat.DODGE), caps.dodge),
		parry = math.min(total(Stat.PARRY), caps.parry),
		life_leech = math.min(total(Stat.LIFE_LEECH), caps.leech),
		mana_leech = math.min(total(Stat.MANA_LEECH), caps.leech),
		protections = protections,
	}, CharacterSheet)
end

return character
