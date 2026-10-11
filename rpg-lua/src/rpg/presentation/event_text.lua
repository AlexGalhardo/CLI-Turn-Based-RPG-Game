--- Turns engine events into translated sentences. Shared by every presentation (text UI and TUI).

local base = require("rpg.domain.base")

-- Event fields that hold ids: they are replaced by display names before formatting.
local NAME_FIELDS = {
	{ "spellId", "spell" },
	{ "potionId", "potion" },
	{ "monsterId", "monster" },
	{ "itemId", "item" },
}
local TRANSLATED_FIELDS = { "element", "status", "rarity", "resource" }

local EventFormatter = base.class("EventFormatter")

function EventFormatter.new(data, translator)
	return setmetatable({ _data = data, _t = translator }, EventFormatter)
end

function EventFormatter:format(event, state)
	local params = {}
	for key, value in pairs(event) do
		params[key] = value
	end
	for _, field in ipairs(NAME_FIELDS) do
		if event[field[1]] ~= nil then
			params[field[2]] = self:_display_name(field[1], event[field[1]])
		end
	end
	for _, field in ipairs(TRANSLATED_FIELDS) do
		if event[field] ~= nil then
			params[field] = self._t:t(field .. "." .. tostring(event[field]))
		end
	end
	if params.monster == nil and state.monster ~= nil then
		params.monster = self._data:creature(state.monster.creature_id).name
	end
	if event.uid ~= nil and params.item == nil then
		params.item = self:_item_name_by_uid(event.uid, state)
	end
	return self._t:t(self:_key(event), params)
end

function EventFormatter:_key(event)
	local event_type = tostring(event.type)
	if event_type == "error" then
		return "error." .. tostring(event.code)
	end
	local variant = ""
	if event.crit == true then
		variant = "_crit"
	elseif event.charged == true then
		variant = "_charged"
	elseif event_type == "round_started" and event.isBoss == true then
		variant = "_boss"
	elseif event_type == "round_started" and event.enemyClass == "elite" then
		variant = "_elite"
	elseif event.target ~= nil then
		variant = "_" .. tostring(event.target)
	end
	return "event." .. event_type .. variant
end

function EventFormatter:_display_name(field, value)
	local identifier = tostring(value)
	local data = self._data
	local found, definition = base.try({ "UnknownIdError" }, function()
		if field == "spellId" then
			return data:spell(identifier)
		elseif field == "potionId" then
			return data:potion(identifier)
		elseif field == "monsterId" then
			return data:creature(identifier)
		end
		return data:item(identifier)
	end)
	return found and definition.name or identifier
end

function EventFormatter:_item_name_by_uid(uid, state)
	local player = state.player
	local function name_in(items)
		for _, item in pairs(items) do
			if item.uid == uid then
				return self._data:item(item.item_id).name
			end
		end
		return nil
	end
	return name_in(player.bag) or name_in(player.equipment) or name_in(state.merchant_stock) or ("#" .. tostring(uid))
end

return EventFormatter
