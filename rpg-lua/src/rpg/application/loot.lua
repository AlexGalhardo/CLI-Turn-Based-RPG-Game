--- Item factory: base item + rarity + affixes (docs/game-design.md §8).

local base = require("rpg.domain.base")
local entities = require("rpg.domain.entities")
local enums = require("rpg.domain.enums")

local Slot = enums.Slot

local loot = {}

function loot.can_use(item, vocation)
	if item.slot == Slot.WEAPON then
		return base.contains(vocation.weapon_types, item.type)
	end
	if item.slot == Slot.SHIELD then
		return base.contains(vocation.shield_types, item.type)
	end
	return true
end

--- Weighted roll in the order of `balance.rarities`; zero weights are skipped and a single option is not rolled.
function loot.roll_rarity(data, rng, weights)
	local options, option_weights = {}, {}
	for _, rarity in ipairs(data.balance.rarities) do
		local weight = weights[rarity.id] or 0
		if weight > 0 then
			options[#options + 1] = rarity
			option_weights[#option_weights + 1] = weight
		end
	end
	if #options == 0 then
		base.raise("ValueError", "rarity table without a positive weight")
	end
	if #options == 1 then
		return options[1]
	end
	return options[rng:weighted(option_weights)]
end

--- `options`: vocation, tier, weights, uid. Returns nil (consuming no randomness) when no item fits the vocation
--- and tier.
function loot.generate_item(data, rng, options)
	local vocation, tier = options.vocation, options.tier
	local lowest_tier = math.max(0, tier - 1)
	local candidates = base.filter(data.items_by_id, function(item)
		return lowest_tier <= item.tier and item.tier <= tier and loot.can_use(item, vocation)
	end)
	if #candidates == 0 then
		return nil
	end
	local chosen = rng:pick(candidates)
	local rarity = loot.roll_rarity(data, rng, options.weights)
	local affix_count = rng:roll(rarity.affix_min, rarity.affix_max)

	local rolls = {}
	local used_stats = {}
	for _ = 1, affix_count do
		local pool = base.filter(data.affixes_by_id, function(affix)
			return base.contains(affix.slots, chosen.slot) and not used_stats[affix.stat]
		end)
		if #pool == 0 then
			break
		end
		local affix = rng:pick(pool)
		used_stats[affix.stat] = true
		rolls[#rolls + 1] = entities.AffixRoll.new(affix.stat, rng:roll(affix.min, affix.max) + tier * affix.per_tier)
	end
	return entities.ItemInstance.new({
		uid = options.uid,
		item_id = chosen.id,
		rarity = rarity.id,
		tier = tier,
		affixes = rolls,
	})
end

return loot
