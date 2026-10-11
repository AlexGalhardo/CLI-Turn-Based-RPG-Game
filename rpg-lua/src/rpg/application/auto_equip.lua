--- Auto-equip with auto-sell (docs/game-design.md §8.1). Consumes no randomness.

local events_module = require("rpg.application.events")
local loot = require("rpg.application.loot")
local base = require("rpg.domain.base")
local character = require("rpg.domain.character")
local enums = require("rpg.domain.enums")

local event = events_module.event

local auto_equip = {}

--- Highest-score bag item the player can wear in `slot` now; ties go to the lowest uid.
function auto_equip.best_bag_item(state, data, slot)
	local player = state.player
	local vocation = data:vocation(player.vocation_id)
	local best, best_score
	for _, item in ipairs(player.bag) do
		local definition = data:item(item.item_id)
		if
			definition.slot == slot
			and loot.can_use(definition, vocation)
			and character.required_level(item, data) <= player.level
		then
			local score = character.item_score(item, data)
			if best == nil or score > best_score or (score == best_score and item.uid < best.uid) then
				best, best_score = item, score
			end
		end
	end
	return best
end

function auto_equip.auto_equip(state, data)
	local player = state.player
	local events = {}
	for _, slot in ipairs(enums.EQUIPMENT_SLOT_ORDER) do
		local best = auto_equip.best_bag_item(state, data, slot)
		if best ~= nil then
			local score = character.item_score(best, data)
			local current = player.equipment[slot]
			if current == nil or score > character.item_score(current, data) then
				base.remove_value(player.bag, best)
				player.equipment[slot] = best
				events[#events + 1] =
					event("item_auto_equipped", { uid = best.uid, itemId = best.item_id, slot = slot, score = score })
				if current ~= nil then
					local gold = character.item_value(current, data)
					player.gold = player.gold + gold
					events[#events + 1] =
						event("item_auto_sold", { uid = current.uid, itemId = current.item_id, gold = gold })
				end
			end
		end
	end
	if #events > 0 then
		local sheet = character.build_sheet(player, data)
		player.hp = math.min(player.hp, sheet.max_hp)
		player.mp = math.min(player.mp, sheet.max_mp)
	end
	return events
end

return auto_equip
