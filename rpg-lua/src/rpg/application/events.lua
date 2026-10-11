--- Engine events (docs/cross-language-parity.md §3). Flat tables compared across implementations as JSON objects.

local events = {}

--- `event("xp_gained", { amount = 10, total = 30 })` → `{ type = "xp_gained", amount = 10, total = 30 }`.
function events.event(event_type, fields)
	local result = fields or {}
	result.type = event_type
	return result
end

function events.error(code)
	return events.event("error", { code = code })
end

events.ErrorCode = {
	NOT_ENOUGH_MANA = "not_enough_mana",
	NOT_ENOUGH_GOLD = "not_enough_gold",
	NO_POTION = "no_potion",
	UNKNOWN_SPELL = "unknown_spell",
	UNKNOWN_POTION = "unknown_potion",
	POTION_LOCKED = "potion_locked",
	INVALID_PHASE = "invalid_phase",
	INVALID_QUANTITY = "invalid_quantity",
	BAG_FULL = "bag_full",
	CANNOT_EQUIP = "cannot_equip",
	INVALID_ITEM = "invalid_item",
	LEVEL_TOO_LOW = "level_too_low",
	UNKNOWN_COMMAND = "unknown_command",
}

--- Appends every event of `more` to `list` (Python's `list.extend`).
function events.extend(list, more)
	for _, item in ipairs(more) do
		list[#list + 1] = item
	end
	return list
end

return events
